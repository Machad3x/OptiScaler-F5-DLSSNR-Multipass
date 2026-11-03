// Host check of the rule that keeps a game's own nvngx_dlssg.dll out while FG Output F5Low runs the DLSS-G generator
// (F5LowRules.h GameDlssgLoadRule). No GPU needed.
// cl /std:c++20 /EHsc /W4 tests/f5low_dlssg_block_smoke.cpp
#include "../OptiScaler/framegen/f5low/F5LowRules.h"

#include <cstdio>

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

using F5Low::Generator;
using F5Low::GameDlssgLoad;

static const wchar_t* kPrivate = L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline";
static const wchar_t* kGamePath = L"C:\\Games\\Game\\bin\\nvngx_dlssg.dll";

// f5lowOutput, generator, keep, fgInputDlssg
static GameDlssgLoad Rule(bool f5low, Generator generator, bool keep, bool inputDlssg, const wchar_t* module,
                          const wchar_t* folder = kPrivate)
{
    return F5Low::GameDlssgLoadRule(f5low, generator, keep, inputDlssg, module, folder);
}

int main()
{
    // The generator is our own: nothing is kept out
    CHECK(Rule(true, Generator::XefgTensor, false, false, kGamePath) == GameDlssgLoad::Allow);

    // DLSS-G generator: the game's copy is blocked, f5low's private copy is not
    CHECK(Rule(true, Generator::Dlssg, false, false, kGamePath) == GameDlssgLoad::Block);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"nvngx_dlssg.dll") == GameDlssgLoad::Block);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"nvngx_dlssg") == GameDlssgLoad::Block);
    CHECK(Rule(true, Generator::Dlssg, false, false,
               L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline\\nvngx_dlssg.dll") == GameDlssgLoad::Allow);

    // A folder that only starts with the same letters is not the private one
    CHECK(Rule(true, Generator::Dlssg, false, false,
               L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline_old\\nvngx_dlssg.dll") == GameDlssgLoad::Block);
    // Nor the folder itself named like the file
    CHECK(Rule(true, Generator::Dlssg, false, false, kPrivate) == GameDlssgLoad::Allow); // not a dlssg file name
    // OptiScaler's own streamline folder holds the source copy: not the private one
    CHECK(Rule(true, Generator::Dlssg, false, false, L"C:\\Games\\Game\\OptiScaler\\streamline\\nvngx_dlssg.dll") ==
          GameDlssgLoad::Block);

    // Other modules are never touched
    CHECK(Rule(true, Generator::Dlssg, false, false, L"C:\\Games\\Game\\sl.dlss_g.dll") == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"nvngx_dlss.dll") == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"C:\\x\\not_nvngx_dlssg.dll") == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"") == GameDlssgLoad::Allow);

    // Another FG Output: never
    CHECK(Rule(false, Generator::Dlssg, false, false, kGamePath) == GameDlssgLoad::Allow);

    // KeepGameDlssg turns the block off
    CHECK(Rule(true, Generator::Dlssg, true, false, kGamePath) == GameDlssgLoad::Allow);

    // FG Input DLSSG needs the game's DLSS-G: allowed, with a warning (also the bare name)
    CHECK(Rule(true, Generator::Dlssg, false, true, kGamePath) == GameDlssgLoad::AllowWarn);
    CHECK(Rule(true, Generator::Dlssg, false, true, L"nvngx_dlssg.dll") == GameDlssgLoad::AllowWarn);
    CHECK(Rule(true, Generator::Dlssg, false, true,
               L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline\\nvngx_dlssg.dll") == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::Dlssg, true, true, kGamePath) == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::XefgTensor, false, true, kGamePath) == GameDlssgLoad::Allow);

    // Case and separators do not matter, for the name and the folder
    CHECK(Rule(true, Generator::Dlssg, false, false, L"C:\\GAMES\\Game\\bin\\NVNGX_DLSSG.DLL") == GameDlssgLoad::Block);
    CHECK(Rule(true, Generator::Dlssg, false, false, L"C:/Games/Game/bin/Nvngx_Dlssg.dll") == GameDlssgLoad::Block);
    CHECK(Rule(true, Generator::Dlssg, false, false,
               L"c:/games/game/optiscaler/PLUGINS/F5LOW_STREAMLINE/NVNGX_DLSSG.dll") == GameDlssgLoad::Allow);
    CHECK(Rule(true, Generator::Dlssg, false, false,
               L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline\\nvngx_dlssg.dll", L"c:/games/game/optiscaler/plugins/f5low_streamline/") ==
          GameDlssgLoad::Allow);

    // Unknown private folder: every copy counts as the game's
    CHECK(Rule(true, Generator::Dlssg, false, false,
               L"C:\\Games\\Game\\OptiScaler\\plugins\\f5low_streamline\\nvngx_dlssg.dll", L"") == GameDlssgLoad::Block);

    if (fails == 0)
        printf("f5low_dlssg_block_smoke: PASS\n");
    return fails == 0 ? 0 : 1;
}
