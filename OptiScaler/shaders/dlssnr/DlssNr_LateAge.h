#pragma once
#include <cstdint>

// Age of a Finished Picture capture, counted so that frames OptiScaler's frame generation makes up do not age it.
// The present count (State::frameCount) goes up for every shown frame, so one game frame is several presents old.
namespace DlssNrLateAge
{
// Presents a capture stays fresh for: the shown frames of its own game frame plus the next game frame's first.
inline uint64_t PresentWindow(uint32_t presentsPerGameFrame)
{
    return presentsPerGameFrame <= 1 ? 1 : 2ull * presentsPerGameFrame - 1;
}

inline bool Stale(uint64_t epoch, uint64_t submissionEpoch, uint32_t presentsPerGameFrame)
{
    return epoch < submissionEpoch || epoch - submissionEpoch > PresentWindow(presentsPerGameFrame);
}

// The model drops its history when nothing was applied yet or more than one game frame went by between two applied
// pictures. Capture serials go up by one per game frame.
inline bool NeedsReset(uint64_t lastAppliedSerial, uint64_t appliedSerial)
{
    return lastAppliedSerial == 0 || appliedSerial > lastAppliedSerial + 2;
}
} // namespace DlssNrLateAge
