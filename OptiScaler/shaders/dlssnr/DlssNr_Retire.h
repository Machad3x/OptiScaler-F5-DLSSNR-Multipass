#pragma once

// When DLSS-NR's parked model features and scratch textures are freed (DlssNr_Dx12.cpp: ParkNrFeature, ParkNrResource,
// the tick at the top of DlssNr_Dx12::Dispatch). Kept apart, with no D3D12 or NGX types, so
// tests/nr_retire_gpu_safety_smoke.cpp runs the same rule against a simulated GPU queue.
//
// A parked item is freed kFrames submission epochs after it was parked. An epoch is what the caller hands Dispatch
// (DlssNrFrameInfo::SubmissionEpoch); the first call with a new value counts every parked item down once.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DlssNr::Retire
{

constexpr int kFrames = 32;

template <typename Item> class List
{
  public:
    void Park(const Item& item) { _parked.push_back({ item, kFrames }); }

    // One count down per submission epoch: a call with the epoch of the last one does nothing.
    template <typename FreeFn> void Tick(uint64_t epoch, FreeFn&& free)
    {
        if (epoch == _lastEpoch)
            return;

        _lastEpoch = epoch;

        for (size_t i = 0; i < _parked.size();)
        {
            if (--_parked[i].framesLeft > 0)
            {
                ++i;
                continue;
            }

            free(_parked[i].item);
            _parked.erase(_parked.begin() + i);
        }
    }

    // Everything now, whatever its count (DlssNr::Shutdown).
    template <typename FreeFn> void FreeAll(FreeFn&& free)
    {
        for (auto& parked : _parked)
            free(parked.item);

        _parked.clear();
    }

    size_t Size() const { return _parked.size(); }

  private:
    struct Parked
    {
        Item item;
        int framesLeft;
    };

    std::vector<Parked> _parked;
    uint64_t _lastEpoch = ~0ull;
};

} // namespace DlssNr::Retire
