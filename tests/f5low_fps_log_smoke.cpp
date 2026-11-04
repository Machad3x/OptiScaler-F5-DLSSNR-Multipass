// Host check of native/FpsLog.h: one frame-rate line after each settings change (once it settled), then one now and
// then, with the game's and the shown frame rate. cl /std:c++20 /EHsc /W4 tests/f5low_fps_log_smoke.cpp
#include "../OptiScaler/native/FpsLog.h"

#include <cstdio>
#include <vector>

using namespace native::fpslog;

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

static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

struct Run
{
    Tracker t;
    double ms = 0.0;
    uint64_t shown = 0;
    std::vector<std::string> lines;

    // `seconds` of presents at `fps`, frame generation sending `perPresent` frames each (0: no XeFG)
    void Play(const Settings& s, double fps, double seconds, uint32_t perPresent)
    {
        const int count = (int) (fps * seconds);

        for (int i = 0; i < count; ++i)
        {
            ms += 1000.0 / fps;
            shown += perPresent;
            std::optional<uint64_t> total;
            if (perPresent > 0)
                total = shown;
            if (auto line = t.OnPresent(ms, s, total, ~0ull))
                lines.push_back(*line);
        }
    }
};

int main()
{
    Settings nrOn { .nr = true, .modelScale = 1.5f, .upscalerPass = true, .mode = "NR + frame generation",
                    .fgOutput = "F5Low", .fgMultiplier = 3, .capFps = 75.0f };
    Settings nrOff = nrOn;
    nrOff.nr = false;
    Settings off { .mode = "Off", .fgOutput = "F5Low", .fgMultiplier = 1, .capFps = 75.0f };

    // First settings: no "changed" line; one measurement after settle + 3 s: game 25, shown 75
    {
        Run r;
        r.Play(nrOn, 25.0, 7.0, 3);
        CHECK(r.lines.size() == 1);
        if (r.lines.size() == 1)
        {
            CHECK(Has(r.lines[0], "game 25.0"));
            CHECK(Has(r.lines[0], "shown 75.0"));
            CHECK(Has(r.lines[0], "NR on, model 1.50, upscaler pass on"));
            CHECK(Has(r.lines[0], "frame generation F5Low x3, cap 75 fps"));
        }

        // Nothing changes for 20 s more: no line before 30 s have passed since the last
        r.Play(nrOn, 25.0, 20.0, 3);
        CHECK(r.lines.size() == 1);
        r.Play(nrOn, 25.0, 12.0, 3);
        CHECK(r.lines.size() == 2);

        // A change: one "changed" line at once, then the new setting's own measurement
        r.Play(nrOff, 25.0, 0.5, 3);
        CHECK(r.lines.size() == 3 && Has(r.lines[2], "settings changed to (NR off"));
        r.Play(nrOff, 25.0, 6.0, 3);
        CHECK(r.lines.size() == 4 && Has(r.lines[3], "NR off") && Has(r.lines[3], "game 25.0"));

        // Mode Off, no frame generation: the shown rate is the game's
        r.Play(off, 75.0, 7.0, 0);
        CHECK(r.lines.size() == 6);
        if (r.lines.size() == 6)
        {
            CHECK(Has(r.lines[5], "game 75.0, shown 75.0"));
            CHECK(Has(r.lines[5], "Optical F5Low Off, frame generation F5Low, cap 75 fps"));
        }
    }

    // A change right after another: only the last settings are measured, the settle starts over
    {
        Run r;
        r.Play(nrOn, 60.0, 0.2, 3);
        r.Play(nrOff, 60.0, 0.2, 3);
        r.Play(off, 60.0, 7.0, 0);
        int measured = 0;
        for (const auto& l : r.lines)
            measured += Has(l, "FPS: game") ? 1 : 0;
        CHECK(measured == 1);
        CHECK(Has(r.lines.back(), "Optical F5Low Off"));
    }

    // A step of 5 s, as a player clicks through the menu: each setting gets its own line
    {
        Run r;
        r.Play(nrOn, 60.0, 5.0, 3);
        r.Play(nrOff, 60.0, 5.0, 3);
        r.Play(off, 60.0, 5.0, 0);
        int measured = 0;
        for (const auto& l : r.lines)
            measured += Has(l, "FPS: game") ? 1 : 0;
        CHECK(measured == 3);
    }

    // Frame generation on but XeFG reports nothing: shown is unknown, not the game's rate
    {
        Run r;
        Settings s = nrOn;
        s.fgOutput = "FSR FG";
        r.Play(s, 30.0, 7.0, 0);
        CHECK(r.lines.size() == 1 && Has(r.lines[0], "shown ?"));
    }

    printf(fails == 0 ? "f5low fps log: ok\n" : "f5low fps log: %d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
