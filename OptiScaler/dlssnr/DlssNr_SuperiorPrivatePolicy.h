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

// A new private DLSS waits for the old ones to retire (one feature at a time), but never longer than RetireWaitFrames:
// a recording that never closes (a list the game keeps for good, no allocator hook) must not keep it off for the
// session. Once the bound ran out, the stuck generations are no longer waited on (GiveUp), so a later change does not
// wait for them again.
constexpr unsigned RetireWaitFrames = 240; // about 2-4 s; a normal retire takes 2-3 frames
struct RetireWait
{
    enum class Step
    {
        Go,    // nothing retiring: create the new one
        Wait,  // keep composing the current edit this frame
        GiveUp // the bound ran out: stop waiting on what is retiring now and create the new one beside it
    };
    unsigned frames = 0;
    Step Next(bool pending)
    {
        if (!pending)
        {
            frames = 0;
            return Step::Go;
        }
        if (frames < RetireWaitFrames)
        {
            ++frames;
            return Step::Wait;
        }
        frames = 0;
        return Step::GiveUp;
    }
};

// Installing the allocator Reset hook while tracking is wanted: at once the first time and whenever the hook came off
// (device hooks released and made again), else at most every HookRetryFrames (an attempt can fail because the list
// Reset hook it needs is not up yet).
constexpr unsigned HookRetryFrames = 600;
struct HookRetry
{
    unsigned wait = 0;
    bool wasHooked = false;
    bool Due(bool hooked)
    {
        if (hooked)
        {
            wasHooked = true;
            return false;
        }
        if (wasHooked)
        {
            wasHooked = false;
            wait = 0;
        }
        if (wait)
        {
            --wait;
            return false;
        }
        wait = HookRetryFrames;
        return true;
    }
};

// NR evaluates without a Superior frame in between (NR off, another transfer, a skipped evaluate): after IdleAfterCalls
// of them Superior collects what it retired on every evaluate, so tracking ends once that is gone. A frame can carry
// several evaluates (before and after SR, views, secondary features), hence the margin.
constexpr unsigned IdleAfterCalls = 16;
struct IdleWatch
{
    unsigned calls = 0;
    void Ran() { calls = 0; }
    bool Due() { return calls >= IdleAfterCalls || ++calls >= IdleAfterCalls; }
};

// What a failed private DLSS creation means. NGX result codes (nvsdk_ngx_defs.h, checked in Pipeline.inl).
constexpr unsigned NgxFail = 0xBAD00000u;
constexpr unsigned NgxFeatureNotSupported = NgxFail | 1, NgxPlatformError = NgxFail | 2,
                   NgxFeatureNotFound = NgxFail | 4, NgxOutOfDate = NgxFail | 12;
enum class CreateVerdict
{
    Ok,
    Generation, // this generation runs without a private DLSS (memory, parameters): a later one tries again
    Session     // no usable DLSS SR here (missing or too old a runtime or driver): not tried again this session
};
inline CreateVerdict ClassifyCreate(unsigned result, bool haveFeature)
{
    if (result == 1u /* NVSDK_NGX_Result_Success */ && haveFeature)
        return CreateVerdict::Ok;
    switch (result)
    {
    case NgxFeatureNotSupported:
    case NgxPlatformError:
    case NgxFeatureNotFound:
    case NgxOutOfDate:
        return CreateVerdict::Session;
    default:
        return CreateVerdict::Generation;
    }
}
} // namespace DlssNr::SuperiorPrivate
