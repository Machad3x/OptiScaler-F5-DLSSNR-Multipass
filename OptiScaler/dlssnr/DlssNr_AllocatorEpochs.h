#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

// Which command allocator a game command list was last reset with, and how often each allocator has been reset since.
// A recording made into a list becomes unreplayable once that allocator is reset, even while the list object itself
// stays in the game's pool, so GpuLifetime can close it there instead of waiting for the list's own Reset.
//
// Both notifications come from the D3D12 Reset hooks (ResTrack_dx12) on native objects. Everything here is off (one
// relaxed load per Reset) unless the hooks are on AND a user wants it (Superior Scaling, while it has a private DLSS).
// A binding is only trusted inside the "session" it was written in: switching either off ends the session, because
// list resets made meanwhile were not seen and a list may by then record into a different allocator.
namespace DlssNr::AllocatorEpochs
{
// From the hooks, after a successful native Reset.
void ListReset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator);
void AllocatorReset(ID3D12CommandAllocator* allocator);
// The hooks delivering both notifications are on, or are about to come off.
void SetHooked(bool hooked);
// A user has recordings that should close at an allocator reset (reference-free: one user, Superior).
void SetWanted(bool wanted);
bool Hooked();
bool Active();

// What a recording keeps: its allocator's token and that allocator's reset count when the recording began.
class Stamp
{
    Microsoft::WRL::ComPtr<IUnknown> token;
    uint64_t resets = 0;
    friend Stamp Capture(ID3D12GraphicsCommandList* list);

  public:
    bool Bound() const { return token != nullptr; }
    // The allocator has been reset since the recording began: the list cannot be executed again without a Reset.
    // Only counted while Active(); unseen resets keep the recording open (the safe direction).
    bool Expired() const;
};
// `list` is the native identity. Unbound (the ordinary list lifetime applies) when not Active(), when the list was not
// reset in this session, or when a token could not be attached.
Stamp Capture(ID3D12GraphicsCommandList* list);
} // namespace DlssNr::AllocatorEpochs
