#pragma once

// A swapchain the game makes on another window while the frame generation swapchain lives (a launcher, splash or
// helper window). Before, it became a second frame generation swapchain (the first one leaked) and took over the State
// pointers frame generation reads its queue and back buffer format from: freezes and crashes. Now it gets a plain
// swapchain and leaves the State alone.
// A game that moves to a new window keeps today's behaviour: the new window may take frame generation when the old one
// is gone or hidden, or when the new one is larger (the game's real window after a small splash one).
// Pure rule (no OptiScaler state), host-tested in tests/fg_window_rule_smoke.cpp.
#include <cstdint>

namespace FgWindow
{
struct Window
{
    const void* handle = nullptr; // HWND
    bool shown = false;           // exists and is visible
    int64_t area = 0;             // client area in pixels
};

constexpr bool IsOtherWindow(bool fgSwapchainLive, const Window& fg, const Window& requested)
{
    if (!fgSwapchainLive || fg.handle == nullptr || requested.handle == nullptr || requested.handle == fg.handle)
        return false;

    // Frame generation's window is gone or hidden: the game moved on
    if (!fg.shown)
        return false;

    // Larger than frame generation's window: the game's real window after a smaller one
    return requested.area <= fg.area;
}
} // namespace FgWindow
