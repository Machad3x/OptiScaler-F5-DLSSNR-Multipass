#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace MfgUnlock
{
// True when any family is not off. With every family off nothing is hooked, tracked or patched.
bool AnyFamilyEnabled();

// True for libxess_fg.dll and for any nvngx_dlssg provider path (including the driver store .bin copies).
bool IsProviderModuleName(std::wstring_view name);

// The validated XeFG default-ceiling locator matches the stock `ebx = 3` ceiling.
inline constexpr int kXeFGOriginalDefaultMaxInterpolations = 3;

void ObserveProviderModule(HMODULE module);
bool PrepareXeFG();
void RefreshXeFGStatus();
bool IsXeFGActive();
// True while OptiScaler's XeFG output runs our xefg_cuda.dll with the XeFG unlock on: it offers up to 6x itself,
// so nothing is patched and the maximum it reports is used (capped by [XeFG] MaxInterpolations).
bool IsXeFGCuda();
// The XeFG library the unlock takes from the plugins folder (xefg_cuda.dll on NVIDIA, Intel's libxess_fg.dll
// elsewhere), or empty: unlock off, XeFGPath set, or no file. Decided and logged once, at the first call.
std::wstring XeFGPluginPath();
// The startup nvapi check's answer; DXGI cannot be asked inside DLL_PROCESS_ATTACH, where XeFG is first loaded.
void SetStartupNvidia(bool nvidia);
bool IsTrackedProviderModule(HMODULE module);
bool BeforeModuleUnload(HMODULE module);
void AfterModuleUnload(HMODULE module);
void OnModuleUnloaded(HMODULE module);
} // namespace MfgUnlock
