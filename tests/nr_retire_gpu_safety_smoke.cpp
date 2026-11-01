// Host check of DlssNr::Retire (shaders/dlssnr/DlssNr_Retire.h): a parked DLSS-NR feature or scratch texture must not be
// freed while GPU work that used it is still in flight, however far behind the GPU runs. Freeing one under the GPU is a
// page fault on the freed resource and a device hang (issue #271: DRED names a freed nv.ngx.dlssnr.resource).
//
// A simulated queue finishes each frame's work `lag` frames after the CPU submitted it. Every frame each NR caller
// counts the parked items down (with its own epoch, as DlssNr_Dx12::Dispatch does), then records the current feature on a
// list and submits it. Every few frames the feature is rebuilt and the old one parked. A free of an item some unfinished
// submit used is counted as unsafe. No GPU and no game needed.
// cl /std:c++20 /EHsc /W4 tests/nr_retire_gpu_safety_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_Retire.h"

#include <cstdio>
#include <deque>
#include <vector>

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

struct Item
{
    int id = 0;
};

// Submits in order; a submit from frame f finishes once the CPU is at frame f + lag.
struct Queue
{
    struct Submit
    {
        int frame;
        int uses;
    };

    std::deque<Submit> inFlight;

    void FinishThrough(int frame)
    {
        while (!inFlight.empty() && inFlight.front().frame <= frame)
            inFlight.pop_front();
    }

    bool InUse(int id) const
    {
        for (const auto& submit : inFlight)
            if (submit.uses == id)
                return true;

        return false;
    }
};

struct Run
{
    int unsafe = 0;   // frees of an item an unfinished submit still used
    int freed = 0;    // all frees
    int leftover = 0; // still parked at the end
};

// `callers` NR calls per frame, each with its own epoch source (the game's upscaler call uses the frame count, native input
// its present epoch: DlssNr_Dx12.cpp EvaluateInternal, DlssNr_Late.inl ApplyNativeInput), so each call is a new epoch.
static Run Simulate(int callers, int lag, int rebuildEvery = 10, int frames = 600)
{
    DlssNr::Retire::List<Item> list;
    Queue queue;
    Run run;
    int feature = 1;

    const auto free = [&](const Item& item)
    {
        ++run.freed;
        if (queue.InUse(item.id))
            ++run.unsafe;
    };

    for (int f = 0; f < frames; ++f)
    {
        queue.FinishThrough(f - lag);

        for (int c = 0; c < callers; ++c)
        {
            const uint64_t epoch = 1'000'000ull * (c + 1) + f;
            list.Tick(epoch, free);
            queue.inFlight.push_back({ f, feature });
        }

        if (f % rebuildEvery == rebuildEvery - 1)
            list.Park({ feature++ });
    }

    run.leftover = (int) list.Size();
    return run;
}

// The smallest lag (in frames) at which `callers` calls per frame free an item under the GPU, or 0 up to `maxLag`.
static int FirstUnsafeLag(int callers, int maxLag)
{
    for (int lag = 1; lag <= maxLag; ++lag)
        if (Simulate(callers, lag).unsafe > 0)
            return lag;

    return 0;
}

int main()
{
    constexpr int kMaxLag = 48;

    printf("NR calls/frame | first GPU lag (frames) that frees under the GPU\n");
    for (int callers = 1; callers <= 4; ++callers)
    {
        const int lag = FirstUnsafeLag(callers, kMaxLag);
        if (lag == 0)
            printf("%14d | none up to %d\n", callers, kMaxLag);
        else
            printf("%14d | %d\n", callers, lag);
    }

    // One NR call per frame, the GPU a few frames behind (frame generation's usual depth): safe, and items do get freed.
    for (int lag = 1; lag <= 4; ++lag)
    {
        const Run run = Simulate(1, lag);
        CHECK(run.unsafe == 0);
        CHECK(run.freed > 0);
    }

    // The rule's promise is "after the GPU is done with it", not "after N epochs": it must hold at any lag and with
    // several callers ticking per frame.
    for (int callers = 1; callers <= 4; ++callers)
        for (int lag = 1; lag <= kMaxLag; ++lag)
        {
            const Run run = Simulate(callers, lag);
            if (run.unsafe != 0)
            {
                printf("FAIL: %d NR calls/frame, GPU %d frames behind: %d of %d frees under the GPU\n", callers, lag,
                       run.unsafe, run.freed);
                ++fails;
                break;
            }
        }

    // Nothing piles up: once the GPU has caught up every parked item but the last few is freed.
    CHECK(Simulate(2, 3).leftover <= DlssNr::Retire::kFrames);

    if (fails == 0)
        printf("retire gpu safety: ok\n");

    return fails == 0 ? 0 : 1;
}
