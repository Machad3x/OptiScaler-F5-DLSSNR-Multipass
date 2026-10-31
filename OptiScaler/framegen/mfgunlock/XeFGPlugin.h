#pragma once

#include "Mode.h"

// Which XeFG library the built-in XeFG unlock brings from the plugins folder. Pure rule, host-tested in
// tests/mfgunlock_xefg_plugin_smoke.cpp; MfgUnlock::XeFGPluginPath feeds it the ini, the GPU and the folder.
//
// The release ships three files in plugins: xefg_cuda.dll (our CUDA frame generation, up to 6x on its own),
// xefg_cuda.ini and Intel's libxess_fg.dll (the library xefg_cuda hands over to where it cannot run). The choice of
// GPU is made here and not inside xefg_cuda: when xefg_cuda hands over it loads and calls Intel's library within one
// call, which leaves no moment to patch that copy. So on other GPUs Intel's library is loaded directly and patched.
namespace MfgUnlock::XeFGPlugin
{
inline constexpr wchar_t kCudaFile[] = L"xefg_cuda.dll";
inline constexpr wchar_t kIntelFile[] = L"libxess_fg.dll";

enum class Choice
{
    None,        // OptiScaler loads XeFG as before (XeFGPath, its own folder, the game's)
    XefgCuda,    // plugins\xefg_cuda.dll: runs up to 6x itself, nothing patched
    IntelPatched // plugins\libxess_fg.dll: the unlock patches it as it patches any libxess_fg.dll
};

struct Input
{
    MfgUnlockMode mode = kDefaultMode;
    bool xefgPathSet = false; // [Libraries] XeFGPath has a value (not auto)
    bool nvidia = false;
    bool cudaFileFound = false;
    bool intelFileFound = false;
};

struct Decision
{
    Choice choice = Choice::None;
    const wchar_t* file = nullptr; // in the plugins folder; null with Choice::None
    const char* reason = "";
};

inline Decision Decide(const Input& in)
{
    if (in.mode == MfgUnlockMode::Off)
        return { Choice::None, nullptr, "unlock off" };
    // An explicit path is the player's (testers, installs from before the plugins copy)
    if (in.xefgPathSet)
        return { Choice::None, nullptr, "XeFGPath is set" };
    if (in.nvidia && in.cudaFileFound)
        return { Choice::XefgCuda, kCudaFile, "NVIDIA GPU" };
    if (in.intelFileFound)
        return { Choice::IntelPatched, kIntelFile, in.nvidia ? "no xefg_cuda.dll in plugins" : "not an NVIDIA GPU" };
    return { Choice::None, nullptr, "no XeFG library in plugins" };
}
} // namespace MfgUnlock::XeFGPlugin
