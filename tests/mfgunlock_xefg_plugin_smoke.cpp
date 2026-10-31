// Host check of XeFGPlugin.h: which XeFG library the built-in unlock takes from the plugins folder. No GPU needed.
// cl /std:c++20 /EHsc /W4 tests/mfgunlock_xefg_plugin_smoke.cpp
#include "../OptiScaler/framegen/mfgunlock/XeFGPlugin.h"

#include <cstdio>
#include <cwchar>

using namespace MfgUnlock::XeFGPlugin;

static int fails = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
            ++fails;                                                                                                   \
        }                                                                                                              \
    } while (0)

static Input All(MfgUnlockMode mode, bool nvidia)
{
    return { .mode = mode, .xefgPathSet = false, .nvidia = nvidia, .cudaFileFound = true, .intelFileFound = true };
}

static bool Is(const Decision& d, Choice choice, const wchar_t* file)
{
    if (d.choice != choice)
        return false;
    if (file == nullptr)
        return d.file == nullptr;
    return d.file != nullptr && wcscmp(d.file, file) == 0;
}

int main()
{
    // Off (also the default): nothing from plugins, whatever is there
    CHECK(Is(Decide(All(MfgUnlockMode::Off, true)), Choice::None, nullptr));
    CHECK(Is(Decide(All(MfgUnlockMode::Off, false)), Choice::None, nullptr));
    CHECK(Is(Decide(Input {}), Choice::None, nullptr));

    // On or detect, NVIDIA: our xefg_cuda
    CHECK(Is(Decide(All(MfgUnlockMode::On, true)), Choice::XefgCuda, kCudaFile));
    CHECK(Is(Decide(All(MfgUnlockMode::Detect, true)), Choice::XefgCuda, kCudaFile));

    // Other GPUs: Intel's library from plugins, patched
    CHECK(Is(Decide(All(MfgUnlockMode::On, false)), Choice::IntelPatched, kIntelFile));
    CHECK(Is(Decide(All(MfgUnlockMode::Detect, false)), Choice::IntelPatched, kIntelFile));

    // XeFGPath set: the player's path wins
    {
        auto in = All(MfgUnlockMode::On, true);
        in.xefgPathSet = true;
        CHECK(Is(Decide(in), Choice::None, nullptr));
        in.nvidia = false;
        CHECK(Is(Decide(in), Choice::None, nullptr));
    }

    // Missing files
    {
        auto in = All(MfgUnlockMode::On, true);
        in.cudaFileFound = false;
        CHECK(Is(Decide(in), Choice::IntelPatched, kIntelFile)); // NVIDIA without xefg_cuda: Intel, patched
        in.intelFileFound = false;
        CHECK(Is(Decide(in), Choice::None, nullptr));

        auto other = All(MfgUnlockMode::On, false);
        other.intelFileFound = false;
        CHECK(Is(Decide(other), Choice::None, nullptr)); // xefg_cuda alone is of no use off NVIDIA
    }

    // Every decision says why (the log line)
    CHECK(Decide(All(MfgUnlockMode::On, true)).reason[0] != '\0');
    CHECK(Decide(Input {}).reason[0] != '\0');

    if (fails == 0)
        printf("mfgunlock_xefg_plugin_smoke: PASS\n");
    return fails == 0 ? 0 : 1;
}
