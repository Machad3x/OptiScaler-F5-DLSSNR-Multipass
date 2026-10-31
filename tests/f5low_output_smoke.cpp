// Host check of FG Output F5Low: FGOutput.h's UsesXeFGApi and F5LowRules.h. No GPU needed.
// cl /std:c++20 /EHsc /W4 tests/f5low_output_smoke.cpp
#include "../OptiScaler/framegen/FGOutput.h"
#include "../OptiScaler/framegen/f5low/F5LowRules.h"

#include <cstdio>
#include <cstring>
#include <cwchar>

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

// The menu's option list is indexed by the enum: F5Low comes last
static_assert(static_cast<unsigned>(FGOutput::F5Low) == 4);

int main()
{
    // F5Low shares every XeFG path; the others do not
    CHECK(UsesXeFGApi(FGOutput::XeFG));
    CHECK(UsesXeFGApi(FGOutput::F5Low));
    CHECK(!UsesXeFGApi(FGOutput::NoFG));
    CHECK(!UsesXeFGApi(FGOutput::FSRFG));
    CHECK(!UsesXeFGApi(FGOutput::DLSSG));
    CHECK(FgAnswersFullscreenForGame(FGOutput::F5Low));
    CHECK(FgAnswersFullscreenForGame(FGOutput::FSRFG));
    CHECK(!FgAnswersFullscreenForGame(FGOutput::DLSSG));

    // Frame Generator parse / save
    bool valid = false;
    CHECK(!F5Low::ParseGenerator("auto", valid).has_value() && valid);
    CHECK(!F5Low::ParseGenerator("", valid).has_value() && valid);
    CHECK(F5Low::ParseGenerator("xefg_tensor", valid) == F5Low::Generator::XefgTensor && valid);
    CHECK(F5Low::ParseGenerator("XeFG_Tensor", valid) == F5Low::Generator::XefgTensor && valid);
    CHECK(!F5Low::ParseGenerator("dlssg", valid).has_value() && !valid);   // planned, not there yet
    CHECK(!F5Low::ParseGenerator("libxess", valid).has_value() && !valid); // planned, not there yet
    CHECK(!F5Low::ParseGenerator("junk", valid).has_value() && !valid);
    CHECK(strcmp(F5Low::GeneratorText(std::nullopt), "auto") == 0);
    CHECK(strcmp(F5Low::GeneratorText(F5Low::Generator::XefgTensor), "xefg_tensor") == 0);
    {
        // Round trip
        bool ok = false;
        CHECK(F5Low::ParseGenerator(F5Low::GeneratorText(F5Low::Generator::XefgTensor), ok) ==
                  F5Low::Generator::XefgTensor &&
              ok);
    }
    CHECK(strcmp(F5Low::GeneratorLabel(F5Low::Generator::XefgTensor), "XeFG Tensor") == 0);
    CHECK(wcscmp(F5Low::GeneratorEnvValue(F5Low::Generator::XefgTensor), L"own") == 0);

    // Availability
    CHECK(F5Low::CheckAvailable(true, true).available);
    CHECK(!F5Low::CheckAvailable(false, true).available);
    CHECK(!F5Low::CheckAvailable(true, false).available);
    CHECK(!F5Low::CheckAvailable(false, false).available);
    CHECK(strstr(F5Low::CheckAvailable(false, true).reason, "NVIDIA") != nullptr);
    CHECK(strstr(F5Low::CheckAvailable(true, false).reason, "plugins") != nullptr);

    if (fails == 0)
        printf("f5low_output_smoke: PASS\n");
    return fails == 0 ? 0 : 1;
}
