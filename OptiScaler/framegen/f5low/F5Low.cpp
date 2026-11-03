#include "pch.h"

#include "F5Low.h"

#include <Config.h>
#include <imgui/ImGuiNotify.hpp>
#include <Util.h>
#include <proxies/Ntdll_Proxy.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <set>
#include <vector>

namespace
{
std::atomic<bool> startupNvidia { false };
std::atomic<HMODULE> loadedModule { nullptr }; // the f5low.dll LoadLibraryForOutput loaded
}

void F5Low::SetStartupNvidia(bool nvidia) { startupNvidia.store(nvidia, std::memory_order_release); }

std::wstring F5Low::LibraryPath()
{
    const std::filesystem::path folder(Config::Instance()->PluginPath.value_or(L"plugins"));
    return (folder / kLibraryFile).wstring();
}

F5Low::Availability F5Low::Available()
{
    std::error_code ec;
    return CheckAvailable(startupNvidia.load(std::memory_order_acquire),
                          std::filesystem::is_regular_file(LibraryPath(), ec), !IntelLibraryPath().empty());
}

F5Low::Status F5Low::QueryStatus()
{
    Status status;
    const HMODULE module = loadedModule.load(std::memory_order_acquire);
    if (module == nullptr)
        return status;
    status.state = RunState::Unknown;

    using PFN_f5lowGetStatus = int(__cdecl*)(char*, int);
    const auto getStatus = (PFN_f5lowGetStatus) GetProcAddress(module, "f5lowGetStatus");
    if (getStatus == nullptr)
        return status;

    char reason[256] {};
    status.state = RunStateFromCode(getStatus(reason, (int) sizeof(reason)));
    reason[sizeof(reason) - 1] = 0;
    status.reason = reason;
    return status;
}

void F5Low::LogStatus()
{
    static std::atomic<int> last { -2 };
    const auto status = QueryStatus();
    if (last.exchange((int) status.state) == (int) status.state)
        return;

    LOG_INFO("F5Low: f5low.dll reports: {}{}{}", RunStateLabel(status.state), status.reason.empty() ? "" : " - ",
             status.reason);
}

void F5Low::ReportFailure(const char* what)
{
    static std::atomic<bool> shown { false };
    if (shown.exchange(true))
        return;
    LOG_ERROR("F5Low: {}", what);

    ImGuiToast notification({ ImGuiToastType::Warning, 15000,
                              "F5Low could not start its frame generation, so no frames are generated.\nSwitch FG "
                              "Output to XeFG (or None) and restart the game; the OptiScaler log says why." });
    notification.setTitle("F5Low frame generation is off");
    ImGui::InsertNotification(notification);
}

std::wstring F5Low::IntelLibraryPath()
{
    // Where OptiScaler's XeFG output looks first: XeFGPath (a file, or a folder holding libxess_fg.dll), then
    // OptiScaler's own folder
    std::error_code ec;
    std::vector<std::filesystem::path> candidates;

    if (const auto configured = Config::Instance()->XeFGLibrary.value_or(L""); !configured.empty())
    {
        std::filesystem::path p(configured);
        candidates.push_back(std::filesystem::is_directory(p, ec) ? p / L"libxess_fg.dll" : p);
    }

    candidates.push_back(std::filesystem::path(Config::Instance()->MainDllPath.value_or(L"")) / L"libxess_fg.dll");

    // f5low.dll also looks next to itself, so a copy in the plugins folder counts
    candidates.push_back(std::filesystem::path(LibraryPath()).parent_path() / L"libxess_fg.dll");

    for (const auto& candidate : candidates)
    {
        // Only Intel's file: a hand-set XeFGPath to f5low.dll itself must not become its own fallback
        if (_wcsicmp(candidate.filename().c_str(), L"libxess_fg.dll") == 0 &&
            std::filesystem::is_regular_file(candidate, ec))
            return std::filesystem::absolute(candidate, ec).wstring();
    }

    return {};
}

HMODULE F5Low::LoadLibraryForOutput()
{
    const auto availability = Available();
    if (!availability.available)
    {
        LOG_WARN("F5Low: {}", availability.reason);
        return nullptr;
    }

    // f5low.dll hands over to Intel's library where it cannot run (and builds on it later): the one OptiScaler's XeFG
    // output uses, so a single copy serves both. It reads the variable when it first needs the library.
    if (const auto intel = IntelLibraryPath(); !intel.empty())
    {
        SetEnvironmentVariableW(kIntelLibraryEnv, intel.c_str());
        LOG_INFO("F5Low: Intel's XeFG library for f5low.dll: {}", wstring_to_string(intel));
        version_t version {};
        if (Util::GetFileVersion(intel, &version))
            LOG_INFO(
                "F5Low: that libxess_fg.dll is {}.{}.{}.{} (f5low.dll builds its own generator from 1.3.4.95 or "
                "1.3.1.78, in memory in the background, about half a second; otherwise it hands over to Intel's XeFG)",
                version.major, version.minor, version.patch, version.reserved);
    }
    else
    {
        LOG_INFO("F5Low: no libxess_fg.dll in OptiScaler's folder or XeFGPath; f5low.dll looks next to itself");
    }

    const auto path = LibraryPath();
    const HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);
    if (module == nullptr)
        LOG_ERROR("F5Low: could not load {} (Win32 {})", wstring_to_string(path), GetLastError());
    else
    {
        loadedModule.store(module, std::memory_order_release);
        LOG_INFO("F5Low: loaded {}", wstring_to_string(path));
    }
    return module;
}

void F5Low::ApplyGenerator()
{
    const auto generator = Config::Instance()->F5LowGenerator.value_or_default();
    const wchar_t* value = GeneratorEnvValue(generator);
    SetEnvironmentVariableW(kGeneratorEnv, value);
    LOG_INFO("F5Low: Frame Generator {} ({}={})", GeneratorLabel(generator), wstring_to_string(kGeneratorEnv),
             wstring_to_string(value));

    if (generator == Generator::Dlssg)
        if (const auto missing = DlssgFilesMissing(); !missing.empty())
            LOG_WARN("F5Low: {}", missing);
}

std::string F5Low::DlssgFilesMissing()
{
    const std::filesystem::path folder =
        std::filesystem::path(Config::Instance()->MainDllPath.value_or(L"")) / L"streamline";
    std::error_code ec;
    return ::F5Low::DlssgFilesMissing(std::filesystem::is_regular_file(folder / L"sl.interposer.dll", ec),
                                      std::filesystem::is_regular_file(folder / L"nvngx_dlssg.dll", ec));
}

bool F5Low::KeepsGameDlssgOut(const std::wstring& modulePath)
{
    auto* config = Config::Instance();

    // The ini values, not State: the game can load its Streamline before FG Output is applied
    const bool f5lowOutput = config->FGOutput.value_or_default() == FGOutput::F5Low;
    const auto generator = config->F5LowGenerator.value_or_default();
    const bool keep = config->F5LowKeepGameDlssg.value_or_default();
    const bool inputDlssg = config->FGInput.value_or_default() == FGInput::DLSSG;

    // Most loads are some other module: settle that before touching the file system
    if (GameDlssgLoadRule(f5lowOutput, generator, keep, inputDlssg, modulePath, {}) == GameDlssgLoad::Allow)
        return false;

    std::filesystem::path pluginFolder(config->PluginPath.value_or(L""));
    if (pluginFolder.empty())
        pluginFolder = L"plugins";
    if (pluginFolder.is_relative())
        pluginFolder = std::filesystem::path(config->MainDllPath.value_or(L"")) / pluginFolder;
    const std::wstring privateFolder = (pluginFolder / kPrivateStreamlineFolder).lexically_normal().wstring();

    const auto verdict = GameDlssgLoadRule(f5lowOutput, generator, keep, inputDlssg, modulePath, privateFolder);
    if (verdict == GameDlssgLoad::Allow)
        return false;

    static std::mutex mutex;
    static std::set<std::wstring> logged;
    bool first = false;
    {
        std::scoped_lock lock(mutex);
        first = logged.insert(modulePath).second;
    }

    if (first)
    {
        if (verdict == GameDlssgLoad::Block)
            LOG_WARN("F5Low DLSS-G: kept the game's own nvngx_dlssg.dll from loading ({}): two DLSS-G copies fault the "
                     "GPU",
                     wstring_to_string(modulePath));
        else
            LOG_WARN("F5Low DLSS-G: the game's own nvngx_dlssg.dll loads ({}) because FG Input is DLSSG; two DLSS-G "
                     "copies may fault the GPU",
                     wstring_to_string(modulePath));
    }

    return verdict == GameDlssgLoad::Block;
}
