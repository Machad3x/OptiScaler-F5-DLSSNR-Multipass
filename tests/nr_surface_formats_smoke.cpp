// Host check of shaders/dlssnr/DlssNr_SurfaceFormats.h: when a format change makes NR's surfaces stale.
// cl /std:c++20 /EHsc /W4 tests/nr_surface_formats_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_SurfaceFormats.h"

#include <cstdio>

int main()
{
    constexpr int kRgba8 = 28, kRgba16f = 10;
    int fails = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++fails;
        }
    };
    using DlssNr::SurfacesStale;

    check(!SurfacesStale(0, kRgba8, 0, kRgba8), "nothing made yet: nothing stale");
    check(!SurfacesStale(kRgba8, kRgba8, kRgba8, kRgba8), "same formats: kept");
    check(SurfacesStale(kRgba8, kRgba16f, kRgba8, kRgba8), "model format changed (compression switched on)");
    // The crash: compression on, so the model is RGBA16F before and after; only the game's colour moved.
    check(SurfacesStale(kRgba16f, kRgba16f, kRgba8, kRgba16f), "only the colour format changed, model format fixed");
    check(SurfacesStale(kRgba8, kRgba16f, kRgba8, kRgba16f), "both changed");
    check(!SurfacesStale(kRgba16f, kRgba16f, kRgba16f, kRgba16f), "compression on, nothing changed: kept");
    check(SurfacesStale(kRgba16f, kRgba8, kRgba8, kRgba8), "compression switched off");

    // Watch Dogs: Legion swaps its output RGBA8 <-> BGRA8 (27 <-> 90) between frames: one family, nothing stale.
    using DlssNr::CanonicalColourFormat;
    using DlssNr::SameColourFamily;
    constexpr int kRgba8Typeless = 27, kRgba8Srgb = 29, kBgra8 = 87, kBgra8Typeless = 90, kBgra8Srgb = 91;
    constexpr int kRgb10a2 = 24, kR11g11b10 = 26, kBgrx8 = 88;
    check(CanonicalColourFormat(kBgra8Typeless) == kRgba8Typeless, "BGRA8 typeless -> RGBA8 typeless");
    check(CanonicalColourFormat(kBgra8) == kRgba8, "BGRA8 UNORM -> RGBA8 UNORM");
    check(CanonicalColourFormat(kBgra8Srgb) == kRgba8Srgb, "BGRA8 sRGB -> RGBA8 sRGB");
    const int others[] = { kRgba8Typeless, kRgba8, kRgba8Srgb, kRgba16f, kRgb10a2, kR11g11b10, kBgrx8 };
    for (const int f : others)
        check(CanonicalColourFormat(f) == f, "everything else keeps its format");
    check(SameColourFamily(kRgba8Typeless, kBgra8Typeless), "27 and 90 are one family");
    check(SameColourFamily(kBgra8, kRgba8), "87 and 28 are one family");
    check(!SameColourFamily(kRgba8, kRgba8Srgb), "sRGB is its own family");
    check(!SameColourFamily(kBgra8Typeless, kRgba16f), "BGRA8 and RGBA16F are not");
    check(!SameColourFamily(kRgba8, kRgb10a2), "RGBA8 and RGB10A2 are not");
    const int was = CanonicalColourFormat(kRgba8Typeless), now = CanonicalColourFormat(kBgra8Typeless);
    check(!SurfacesStale(was, now, was, now), "the swap 27 -> 90: kept");
    check(SurfacesStale(was, kRgba16f, was, kRgba16f), "27 -> RGBA16F: rebuilt");

    // Neverness to Everness flips its pre-SR colour RGBA16F <-> R11G11B10F (10 <-> 26) during its loading splash.
    using DlssNr::IsFloatFlip;
    check(IsFloatFlip(kRgba16f, kR11g11b10) && IsFloatFlip(kR11g11b10, kRgba16f), "10 <-> 26 is a float flip");
    check(!IsFloatFlip(kR11g11b10, kR11g11b10), "no change is no flip");
    check(!IsFloatFlip(0, kR11g11b10), "the first frame is no flip");
    check(!IsFloatFlip(kRgba8, kRgba16f) && !IsFloatFlip(kRgb10a2, kR11g11b10), "other pairs are not");
    check(CanonicalColourFormat(kR11g11b10) == kR11g11b10, "before a flip R11G11B10F keeps its surfaces");
    check(CanonicalColourFormat(kR11g11b10, true) == kRgba16f, "after a flip R11G11B10F gets RGBA16F surfaces");
    check(CanonicalColourFormat(kRgba16f, true) == kRgba16f, "after a flip RGBA16F stays");
    check(CanonicalColourFormat(kBgra8, true) == kRgba8 && CanonicalColourFormat(kRgb10a2, true) == kRgb10a2,
          "the flag moves nothing else");
    {
        const int a = CanonicalColourFormat(kRgba16f, true), b = CanonicalColourFormat(kR11g11b10, true);
        check(!SurfacesStale(a, b, a, b) && !SurfacesStale(b, a, b, a), "after the flip, 10 <-> 26 keeps the model");
    }

    // RebuildGuard: three rebuilds within the window stand NR down; it runs again once one set-up held kSettle calls.
    using DlssNr::RebuildGuard;
    {
        RebuildGuard g;
        int ran = 0;
        // An alternating game: every call asks for the other set-up, so each would rebuild.
        for (int i = 0; i < 200; ++i)
            ran += g.Allow(i & 1 ? 2 : 1, true) ? 1 : 0;
        check(ran == (int) RebuildGuard::kLimit - 1, "alternating: two rebuilds run, then NR stands down");
        check(g.standing, "alternating: still standing");
        // The game settles on set-up 1: the first kSettle calls still wait, then it runs (and builds once).
        int waited = 0;
        while (!g.Allow(1, true) && waited < 100)
            ++waited;
        check(waited == (int) RebuildGuard::kSettle, "settled: runs after kSettle steady calls");
        check(!g.standing, "settled: no longer standing");
        check(g.Allow(1, false), "settled: steady frames run");
    }
    {
        RebuildGuard g;
        bool allRan = true;
        // A game that changes resolution now and then (one rebuild every 100 calls) is never stood down.
        for (int i = 0; i < 1000; ++i)
            allRan = g.Allow(i / 100, i % 100 == 0 && i > 0) && allRan;
        check(allRan && !g.standing, "occasional rebuilds: never stood down");
    }
    {
        RebuildGuard g;
        for (int i = 0; i < 500; ++i)
            g.Allow(7, false);
        check(!g.standing, "steady game: never stood down");
    }

    // The reviewer's probe: the ordinary size moved 960x544 -> 976x544 while the packed one did not.
    check(DlssNr::SizeStale(960, 544, 976, 544), "ordinary size moved, packed size did not");
    check(DlssNr::SizeStale(976, 544, 976, 560), "height moved");
    check(!DlssNr::SizeStale(976, 544, 976, 544), "same ordinary size: kept");
    check(!DlssNr::SizeStale(0, 0, 976, 544), "no surface yet");

    if (fails == 0)
        std::puts("PASS: nr_surface_formats_smoke");
    return fails != 0;
}
