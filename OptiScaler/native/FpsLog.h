#pragma once

// The frame rate after each change of a setting that moves it, for the log: the game's own frames a second and the
// frames frame generation sends to be shown, measured over a few seconds once the change has settled, with the settings
// it was measured under. Pure, so a host test can run it (tests/f5low_fps_log_smoke.cpp). The caller reads the clock,
// the config and XeFG's shown-frame totals (State::xefgShown, framegen/xefg/XeFG_ShownRate.h) and logs the line.

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace native::fpslog
{

// What moves the frame rate. Compared whole: any field changing starts a new measurement.
struct Settings
{
    bool nr = false;           // Enable Neural Rendering
    float modelScale = 1.0f;   // [DlssNr] WorkingScale
    bool upscalerPass = false; // Optical F5Low's "Run the upscaler on the picture"
    const char* mode = "Off";  // the Optical F5Low mode shown on its page
    const char* fgOutput = "none";
    uint32_t fgMultiplier = 1; // 1: no frame generation running
    float capFps = 0.0f;       // FramerateLimit, 0 none

    bool operator==(const Settings& o) const
    {
        return nr == o.nr && modelScale == o.modelScale && upscalerPass == o.upscalerPass &&
               std::string_view(mode) == o.mode && std::string_view(fgOutput) == o.fgOutput &&
               fgMultiplier == o.fgMultiplier && capFps == o.capFps;
    }

    std::string Describe() const
    {
        return std::format("NR {}, model {:.2f}, upscaler pass {}, Optical F5Low {}, frame generation {}{}, cap {}",
                           nr ? "on" : "off", modelScale, upscalerPass ? "on" : "off", mode, fgOutput,
                           fgMultiplier > 1 ? std::format(" x{}", fgMultiplier) : std::string(),
                           capFps > 0.0f ? std::format("{:.0f} fps", capFps) : std::string("none"));
    }
};

class Tracker
{
  public:
    static constexpr double kSettleMs = 1000.0;  // after a change: rebuilds and pauses are not the setting's frame rate
    static constexpr double kMeasureMs = 3000.0; // the first measurement after a change (a step of 5 s is enough)
    static constexpr double kRepeatMs = 30000.0; // then one line this often while nothing changes

    // One game present. `shownTotal`: XeFG's frames sent to be shown, a running total (wraps at `shownMask`), nullopt
    // with no XeFG. A line to log when a measurement closes.
    std::optional<std::string> OnPresent(double nowMs, const Settings& settings, std::optional<uint64_t> shownTotal,
                                         uint64_t shownMask)
    {
        if (!_have || !(settings == _settings))
        {
            const bool first = !_have;
            _have = true;
            _settings = settings;
            _logged = false;
            Restart(nowMs + kSettleMs);
            return first ? std::nullopt
                         : std::optional<std::string>(
                               std::format("FPS: settings changed to ({}); measuring from {:.0f} s after the change",
                                           settings.Describe(), kSettleMs / 1000.0));
        }

        if (nowMs < _start)
            return std::nullopt;

        if (!_started)
        {
            _started = true;
            _start = nowMs;
            _presents = 0;
            _shownStart = shownTotal;
            return std::nullopt;
        }

        ++_presents;
        const double elapsed = nowMs - _start;

        if (elapsed < (_logged ? kRepeatMs : kMeasureMs))
            return std::nullopt;

        const double seconds = elapsed / 1000.0;
        const double game = _presents / seconds;
        std::string shown;

        if (shownTotal.has_value() && _shownStart.has_value() && *shownTotal != *_shownStart)
            shown = std::format("{:.1f}", (double) ((*shownTotal - *_shownStart) & shownMask) / seconds);
        else if (settings.fgMultiplier <= 1)
            shown = std::format("{:.1f}", game);
        else
            shown = "?";

        const std::string line =
            std::format("FPS: game {:.1f}, shown {} over {:.1f} s ({})", game, shown, seconds, settings.Describe());
        _logged = true;
        Restart(nowMs);
        return line;
    }

  private:
    void Restart(double startMs)
    {
        _start = startMs;
        _started = false;
        _presents = 0;
        _shownStart.reset();
    }

    Settings _settings;
    bool _have = false;
    bool _logged = false; // a line went out for these settings: the next one waits kRepeatMs
    bool _started = false;
    double _start = 0.0;
    uint64_t _presents = 0;
    std::optional<uint64_t> _shownStart;
};

} // namespace native::fpslog
