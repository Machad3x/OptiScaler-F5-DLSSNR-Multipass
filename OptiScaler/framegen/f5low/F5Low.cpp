#include "pch.h"

#include "F5Low.h"

#include <Config.h>
#include <proxies/Ntdll_Proxy.h>

#include <atomic>
#include <filesystem>

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

HMODULE F5Low::LoadLibraryForOutput()
{
    const auto availability = Available();
    if (!availability.available)
    {
        LOG_WARN("F5Low: {}", availability.reason);
        return nullptr;
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
