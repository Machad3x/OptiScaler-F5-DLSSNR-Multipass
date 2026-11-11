#pragma once
#include <algorithm>
#include <cmath>

namespace DlssNr::SuperiorPrivate
{
constexpr double FadeSeconds = .3;
// A reading (still or moving) has to hold this long before CNN switches to it, in both directions.
constexpr double DwellSeconds = .05;
constexpr double FreshSeconds = .5;
constexpr unsigned MotionSamples = 576;
// At least this many moving samples: the camera moves. Fewer than QuietSamples: it is still. In between the last
// reading stands (hysteresis), so a pan hovering at one count does not switch back and forth.
constexpr unsigned MovingSamples = MotionSamples * 3 / 4;
constexpr unsigned QuietSamples = MotionSamples / 2;
// A sample "moves" above 0.1 pixel (1080p height) per frame of this length: about 6 pixels per second, whatever the
// frame rate (the probe scales each frame's vectors by ReferenceFrameMs / its frame time).
constexpr float ReferenceFrameMs = 1000.f / 60.f;
inline float PerSecondScale(float frameMs)
{
    return std::isfinite(frameMs) && frameMs > 0.f ? std::clamp(ReferenceFrameMs / frameMs, .1f, 10.f) : 1.f;
}

struct Fade
{
    float weight = 1.f;
    double previous = -1.;
    bool wasVisible = true;
    float Advance(bool visible, double now)
    {
        const bool starting = !visible && wasVisible && weight == 1.f;
        const double elapsed = previous >= 0. && !starting ? std::max(0., now - previous) : 0.;
        wasVisible = visible;
        previous = now;
        const auto step = static_cast<float>(elapsed / FadeSeconds);
        weight = std::clamp(weight + (visible ? step : -step), 0.f, 1.f);
        return weight < 1e-6f ? (weight = 0.f) : weight;
    }
};

// CNN D (moving) / E (still) selection from the motion probe's readings. `idle` is the preset in use (E).
struct CnnMotion
{
    double observed = -1., since = -1.; // the latest reading's time; when the current reading began
    bool quiet = false, idle = false;
    Fade fade;
    void Observe(unsigned moving, double time)
    {
        if (time <= observed)
            return;
        const bool fresh = observed >= 0. && since >= 0. && time - observed <= FreshSeconds;
        const bool next = moving >= MovingSamples ? false : moving < QuietSamples ? true : fresh && quiet;
        if (!fresh || next != quiet)
            since = time;
        quiet = next;
        observed = time;
    }
    void Invalidate()
    {
        quiet = false;
        since = -1.;
    }
    // True when the preset switched this time (the incoming one must reset its history).
    bool Advance(double now)
    {
        const bool fresh = since >= 0. && now >= observed && now - observed <= FreshSeconds;
        // No fresh reading: back to D, the safe choice in motion. Else switch once the reading has held long enough.
        const bool target = !fresh ? false : quiet != idle && observed - since >= DwellSeconds ? quiet : idle;
        if (fade.Advance(target == idle, now) != 0.f)
            return false;
        idle = target;
        fade = Fade { 1.f, now };
        return true;
    }
};
} // namespace DlssNr::SuperiorPrivate
