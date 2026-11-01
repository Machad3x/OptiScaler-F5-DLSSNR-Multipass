#include "pch.h"

#include "F5Low.h"

#include <Config.h>
#include <proxies/Ntdll_Proxy.h>

#include <atomic>
#include <filesystem>
#include <vector>

namespace
{
std::atomic<bool> startupNvidia { false };
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
                          std::filesystem::is_regular_file(LibraryPath(), ec));
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

    for (const auto& candidate : candidates)
    {
        // Only Intel's file: a hand-set XeFGPath to xefg_cuda.dll itself must not become its own fallback
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

    // xefg_cuda hands over to Intel's library where it cannot run (and builds on it later): the one OptiScaler's XeFG
    // output uses, so a single copy serves both. It reads the variable when it first needs the library.
    if (const auto intel = IntelLibraryPath(); !intel.empty())
    {
        SetEnvironmentVariableW(kIntelLibraryEnv, intel.c_str());
        LOG_INFO("F5Low: Intel's XeFG library for xefg_cuda: {}", wstring_to_string(intel));
    }
    else
    {
        LOG_INFO("F5Low: no libxess_fg.dll in OptiScaler's folder or XeFGPath; xefg_cuda looks next to itself");
    }

    const auto path = LibraryPath();
    const HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);
    if (module == nullptr)
        LOG_ERROR("F5Low: could not load {} (Win32 {})", wstring_to_string(path), GetLastError());
    else
        LOG_INFO("F5Low: loaded {}", wstring_to_string(path));
    return module;
}

void F5Low::ApplyGenerator()
{
    const auto generator = Config::Instance()->F5LowGenerator.value_or_default();
    const wchar_t* value = GeneratorEnvValue(generator);
    SetEnvironmentVariableW(kGeneratorEnv, value);
    LOG_INFO("F5Low: Frame Generator {} ({}={})", GeneratorLabel(generator), wstring_to_string(kGeneratorEnv),
             wstring_to_string(value));
}
