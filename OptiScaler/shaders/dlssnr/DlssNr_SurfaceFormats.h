#pragma once

// Which of NR's surfaces are stale when a format changes (DlssNr_Dx12.cpp, ReleaseSurfacesIfFormatChanged).
//
// Two formats are in play. The model surfaces (output, the ping-pong and clamp scratches) follow the model: the game's
// colour format, or RGBA16F while Compress screen edges packs the picture. The colour surfaces (the kept proxy
// colorCopy, the shrunk copy, the crop, the LUT scratch, the native pair) are made from and into the game's colour, so
// they follow the game's format. With compression on the model format no longer moves when the game's does, and
// checking only that one left the colour surfaces in the old format: the next copy from the game's new colour was an
// invalid call and the device was removed (NBA 2K27, native finished picture RGBA8 -> the game's DLSS after SR,
// RGBA16F).
//
// Our own surfaces are made in the canonical colour format, not the game's exact one: BGRA8 becomes its RGBA8 twin.
// Every shader reads and writes them through views of their own format, so the channel order sorts itself out, and a
// game that swaps its output between RGBA8 and BGRA8 from frame to frame (Watch Dogs: Legion) no longer looks like a
// format change. That swap used to throw away the model feature and its history every time. The few surfaces copied
// straight to or from the game's texture (CopyResource / CopyTextureRegion need the same format family) keep the exact
// format and check it themselves.
//
// A game can flip between the two float formats in the same way: Neverness to Everness hands the pre-SR colour as
// RGBA16F and R11G11B10F on alternate frames during its loading splash, and each flip rebuilt the model (~1 s a
// frame, then a crash). Once a game has shown both, our surfaces are made in RGBA16F for either: R11G11B10F widens to
// it without loss and the shaders read the game's texture through its own view. Only after the first flip, so a game
// that stays on R11G11B10F keeps the surfaces (and memory) it always had.
//
// A game that flips something else the model is built for (a format pair not covered here, two sizes, the placement)
// still makes the model rebuild every frame. RebuildGuard stands NR down while that goes on.

namespace DlssNr
{
// DXGI_FORMAT values, kept as ints so this header needs no Windows headers.
constexpr int kR8G8B8A8Typeless = 27, kR8G8B8A8Unorm = 28, kR8G8B8A8UnormSrgb = 29;
constexpr int kB8G8R8A8Unorm = 87, kB8G8R8A8Typeless = 90, kB8G8R8A8UnormSrgb = 91;
constexpr int kR16G16B16A16Float = 10, kR11G11B10Float = 26;

constexpr bool IsFlippingFloatFormat(int format) { return format == kR16G16B16A16Float || format == kR11G11B10Float; }

// The game moved from one of the two float formats to the other.
constexpr bool IsFloatFlip(int last, int now)
{
    return last != now && IsFlippingFloatFormat(last) && IsFlippingFloatFormat(now);
}

// The format our surfaces are made in for a game colour format. BGRA8 moves to RGBA8 (keeping typeless/UNORM/sRGB),
// and R11G11B10F to RGBA16F once the game has flipped between the two (floatFlipSeen); everything else, RGBA8 itself
// included, stays as it is, so games that never swap see the surfaces they always had.
constexpr int CanonicalColourFormat(int format, bool floatFlipSeen = false)
{
    switch (format)
    {
    case kR11G11B10Float:
        return floatFlipSeen ? kR16G16B16A16Float : kR11G11B10Float;
    case kB8G8R8A8Typeless:
        return kR8G8B8A8Typeless;
    case kB8G8R8A8Unorm:
        return kR8G8B8A8Unorm;
    case kB8G8R8A8UnormSrgb:
        return kR8G8B8A8UnormSrgb;
    default:
        return format;
    }
}

// Two game colour formats our surfaces treat as one: only the channel order differs.
constexpr bool SameColourFamily(int a, int b) { return CanonicalColourFormat(a) == CanonicalColourFormat(b); }

// Formats are DXGI_FORMAT values as ints; 0 (UNKNOWN) means "no such surface yet". The wanted formats are canonical.
constexpr bool SurfacesStale(int haveModel, int wantModel, int haveColour, int wantColour)
{
    return (haveModel != 0 && haveModel != wantModel) || (haveColour != 0 && haveColour != wantColour);
}

// Counts the model rebuilds the game's frames force (format, size, placement) on one view. kLimit of them within
// kWindow calls means the game alternates set-ups, and every call would build the model again: NR stands down (the
// picture passes untouched) until the game asks for one set-up kSettle calls in a row, then builds it once.
struct RebuildGuard
{
    static constexpr unsigned kLimit = 3;
    static constexpr unsigned long long kWindow = 120;
    static constexpr unsigned kSettle = 30;

    unsigned long long calls = 0;
    unsigned long long rebuilds[kLimit] = {}; // call numbers of the latest rebuilds, oldest first
    unsigned count = 0;
    bool standing = false;
    unsigned long long key = 0;
    unsigned settled = 0;

    // key identifies the set-up this call asks for; wouldRebuild: it differs from the one the model was built for.
    // True when NR runs this call.
    bool Allow(unsigned long long setup, bool wouldRebuild)
    {
        ++calls;

        if (standing)
        {
            settled = setup == key ? settled + 1 : 0;
            key = setup;

            if (settled < kSettle)
                return false;

            standing = false;
            count = 0;
            return true;
        }

        if (!wouldRebuild)
            return true;

        if (count == kLimit)
        {
            for (unsigned i = 1; i < kLimit; ++i)
                rebuilds[i - 1] = rebuilds[i];
            --count;
        }
        rebuilds[count++] = calls;

        if (count == kLimit && calls - rebuilds[0] < kWindow)
        {
            standing = true;
            key = setup;
            settled = 0;
            return false;
        }

        return true;
    }
};

// A surface made at the ordinary (uncompressed) size: stale when that size is not the one wanted now. Under Compress
// screen edges the change check sees only the packed size, which can stay put while the ordinary size moves by a
// 16-pixel step (1920x1080, Balanced, scale 0.50 to 0.504: 960x544 -> 976x544, packed 864x496 either way), so the
// surface has to be asked for itself.
constexpr bool SizeStale(unsigned haveW, unsigned haveH, unsigned wantW, unsigned wantH)
{
    return haveW != 0 && (haveW != wantW || haveH != wantH);
}
} // namespace DlssNr
