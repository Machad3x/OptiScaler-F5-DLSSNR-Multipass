#pragma once

#include <cstdint>

// A game can keep more than one DLSS feature alive and evaluate them in turn: Neverness to Everness makes a second one
// for the 3D character preview on its Character, Gift and Party pages, next to the world's, and calls both every frame.
// NR has one model and one history, so the two pictures took turns in it: each got the other's history, and before SR
// the model was rebuilt on every call (their colour formats differ), which froze the game.
//
// This decides, per game DLSS feature, what NR does with it. The main one keeps NR's ordinary state, as in a game with
// one feature. Any other one stands aside (passes untouched), or with SecondaryFeatures = Own model gets a view slot of
// its own (DlssNr_Dx12.cpp: a model, its surfaces and history, like a screen of a multi-monitor layout), or with Share
// runs through the main state as before. Nothing here touches D3D12. Host-testable (tests/nr_contexts_smoke.cpp).
namespace DlssNr::Contexts
{
// [DlssNr] SecondaryFeatures
enum class Mode : uint32_t
{
    StandAside = 0,
    OwnModel = 1,
    Share = 2,
};

constexpr unsigned kOwnSlots = 3; // features with a model of their own besides the main one
// Presented frames without a call from the main feature before another feature may take main over: a game whose first
// feature lingers unused (a menu scene) must not keep its gameplay feature from NR.
constexpr unsigned long long kMainIdleEpochs = 120;

enum class Kind
{
    Main,
    Own, // slot: 0 .. kOwnSlots - 1
    StandAside,
};

struct Role
{
    Kind kind = Kind::Main;
    unsigned slot = 0;
    bool first = false;    // the first call this feature made in this role (to say it once)
    bool tookMain = false; // this feature took main over from an idle one: the state is of another picture
};

// What releasing a game feature leaves to the caller.
struct Released
{
    bool own = false; // its own slot (slot) is to be parked
    unsigned slot = 0;
};

class Map
{
  public:
    // handle 0: the caller does not know the game's feature (the native path): main.
    Role Acquire(uint32_t handle, Mode mode, unsigned long long epoch)
    {
        Role role;

        if (handle == 0 || mode == Mode::Share)
            return role;

        const bool mainIdle = epoch > mainEpoch && epoch - mainEpoch > kMainIdleEpochs;
        if (!haveMain || handle == main || mainIdle)
        {
            const bool newMain = !haveMain || handle != main;
            role.tookMain = haveMain && handle != main;
            role.first = newMain;

            // A feature that had a slot of its own and now takes main gives the slot back
            if (newMain)
                for (unsigned i = 0; i < kOwnSlots; ++i)
                    if (ownUsed[i] && own[i] == handle)
                        ownUsed[i] = false;

            main = handle;
            haveMain = true;
            mainEpoch = epoch;
            return role;
        }

        if (mode == Mode::OwnModel)
        {
            for (unsigned i = 0; i < kOwnSlots; ++i)
                if (ownUsed[i] && own[i] == handle)
                {
                    role.kind = Kind::Own;
                    role.slot = i;
                    return role;
                }

            for (unsigned i = 0; i < kOwnSlots; ++i)
                if (!ownUsed[i])
                {
                    ownUsed[i] = true;
                    own[i] = handle;
                    role.kind = Kind::Own;
                    role.slot = i;
                    role.first = true;
                    return role;
                }
        }

        role.kind = Kind::StandAside;
        role.first = SeenAside(handle);
        return role;
    }

    // The main feature's state is kept: games make their feature again on a resolution change, and NR keeps its model
    // through that as it always did. The next feature to call takes main.
    Released Release(uint32_t handle)
    {
        Released r;

        if (haveMain && handle == main)
            haveMain = false;

        for (unsigned i = 0; i < kOwnSlots; ++i)
            if (ownUsed[i] && own[i] == handle)
            {
                ownUsed[i] = false;
                r.own = true;
                r.slot = i;
            }

        for (unsigned i = 0; i < kAsideRemembered; ++i)
            if (aside[i] == handle)
                aside[i] = 0;

        return r;
    }

    // For the menu: features seen standing aside or with their own model, still alive.
    unsigned OwnCount() const
    {
        unsigned n = 0;
        for (unsigned i = 0; i < kOwnSlots; ++i)
            n += ownUsed[i] ? 1 : 0;
        return n;
    }

    unsigned AsideCount() const
    {
        unsigned n = 0;
        for (unsigned i = 0; i < kAsideRemembered; ++i)
            n += aside[i] != 0 ? 1 : 0;
        return n;
    }

    bool OwnHandle(unsigned slot, uint32_t& handle) const
    {
        if (slot >= kOwnSlots || !ownUsed[slot])
            return false;
        handle = own[slot];
        return true;
    }

    void Reset() { *this = Map {}; }

  private:
    static constexpr unsigned kAsideRemembered = 8;

    // True the first time this feature stands aside (remembered until it is released)
    bool SeenAside(uint32_t handle)
    {
        for (unsigned i = 0; i < kAsideRemembered; ++i)
            if (aside[i] == handle)
                return false;
        for (unsigned i = 0; i < kAsideRemembered; ++i)
            if (aside[i] == 0)
            {
                aside[i] = handle;
                return true;
            }
        return false; // more than remembered: said already for enough of them
    }

    uint32_t main = 0;
    bool haveMain = false;
    unsigned long long mainEpoch = 0;
    uint32_t own[kOwnSlots] = {};
    bool ownUsed[kOwnSlots] = {};
    uint32_t aside[kAsideRemembered] = {};
};
} // namespace DlssNr::Contexts
