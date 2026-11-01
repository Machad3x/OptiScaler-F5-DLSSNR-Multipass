// Host check of FgStatusRule.h: the Frame Generation page's status line. No GPU needed.
// cl /std:c++20 /EHsc /W4 /I OptiScaler tests/fg_status_rule_smoke.cpp
#include "../OptiScaler/menu/FgStatusRule.h"

#include <cstdio>

using namespace fgstatus;

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

static Input Running(int interpolated, double shownFps)
{
    Input in;
    in.output = FGOutput::F5Low;
    in.outputLabel = "F5Low";
    in.inputSelected = true;
    in.hasFeature = true;
    in.active = true;
    in.generating = true;
    in.interpolated = interpolated;
    in.shownFps = shownFps;
    return in;
}

int main()
{
    // Off
    {
        const auto s = Describe(Input {});
        CHECK(s.tone == Tone::Neutral && s.text == "Frame generation off");
    }
    {
        auto in = Running(2, 180.0);
        in.inputSelected = false;
        CHECK(Describe(in).tone == Tone::Neutral);
        CHECK(Describe(in).text.find("FG Input") != std::string::npos);
    }
    {
        auto in = Running(2, 180.0);
        in.active = false;
        CHECK(Describe(in).tone == Tone::Neutral);
        CHECK(Describe(in).text.find("Active") != std::string::npos);
    }

    // Waiting
    {
        auto in = Running(2, 180.0);
        in.hasFeature = false;
        CHECK(Describe(in).tone == Tone::Waiting);
        CHECK(Describe(in).text.find("swap chain") != std::string::npos);
    }
    {
        auto in = Running(2, 180.0);
        in.paused = true;
        CHECK(Describe(in).tone == Tone::Waiting);
    }
    {
        auto in = Running(2, 180.0);
        in.generationOff = true;
        CHECK(Describe(in).tone == Tone::Waiting);
        CHECK(Describe(in).text.find("1X") != std::string::npos);
    }
    {
        auto in = Running(2, 180.0);
        in.generating = false;
        CHECK(Describe(in).tone == Tone::Waiting);
    }

    // Generating: multiplier and fps in -> out
    {
        const auto s = Describe(Running(2, 180.0));
        CHECK(s.tone == Tone::Good);
        CHECK(s.text == "F5Low: generating 3X   60 -> 180 fps");
    }
    {
        const auto s = Describe(Running(5, 360.0));
        CHECK(s.text == "F5Low: generating 6X   60 -> 360 fps");
    }
    {
        const auto s = Describe(Running(1, 119.6));
        CHECK(s.text == "F5Low: generating 2X   60 -> 120 fps"); // rounded
    }
    {
        // No frame time yet: no fps
        const auto s = Describe(Running(2, 0.0));
        CHECK(s.text == "F5Low: generating 3X");
    }
    {
        // A count of 0 while generating still reads 2X (one generated frame)
        const auto s = Describe(Running(0, 100.0));
        CHECK(s.text == "F5Low: generating 2X   50 -> 100 fps");
    }

    if (fails == 0)
        printf("fg_status_rule_smoke: PASS\n");
    return fails == 0 ? 0 : 1;
}
