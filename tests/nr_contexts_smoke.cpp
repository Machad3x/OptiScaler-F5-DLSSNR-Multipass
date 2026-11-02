// Host check of shaders/dlssnr/DlssNr_Contexts.h: what NR does with each of the game's DLSS features.
// cl /std:c++17 /EHsc /W4 tests/nr_contexts_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_Contexts.h"

#include <cstdio>

int main()
{
    int fails = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++fails;
        }
    };
    using namespace DlssNr::Contexts;
    constexpr uint32_t world = 1000000, preview = 1000001, third = 1000002, fourth = 1000003, fifth = 1000004;

    {
        // One feature (most games): always main, said once.
        Map m;
        const Role r1 = m.Acquire(world, Mode::StandAside, 1);
        const Role r2 = m.Acquire(world, Mode::StandAside, 2);
        check(r1.kind == Kind::Main && r1.first && !r1.tookMain, "the first feature is main");
        check(r2.kind == Kind::Main && !r2.first, "and stays main");
        check(m.Acquire(0, Mode::StandAside, 3).kind == Kind::Main, "an unknown feature (native path) is main");
    }
    {
        // Neverness to Everness: the character preview next to the world, both every frame.
        Map m;
        m.Acquire(world, Mode::StandAside, 1);
        const Role a = m.Acquire(preview, Mode::StandAside, 2);
        const Role b = m.Acquire(preview, Mode::StandAside, 3);
        check(a.kind == Kind::StandAside && a.first, "stand aside: the preview passes untouched, said once");
        check(b.kind == Kind::StandAside && !b.first, "said once only");
        check(m.Acquire(world, Mode::StandAside, 3).kind == Kind::Main, "the world stays main");
        check(m.AsideCount() == 1, "one feature standing aside");
        m.Release(preview);
        check(m.AsideCount() == 0, "released: forgotten");
        check(m.Acquire(preview, Mode::StandAside, 4).first, "made again: said again");
    }
    {
        // Own model: a slot each, up to kOwnSlots, then stand aside.
        Map m;
        m.Acquire(world, Mode::OwnModel, 1);
        const Role p = m.Acquire(preview, Mode::OwnModel, 1);
        const Role p2 = m.Acquire(preview, Mode::OwnModel, 2);
        check(p.kind == Kind::Own && p.slot == 0 && p.first, "own model: the preview gets slot 0");
        check(p2.kind == Kind::Own && p2.slot == 0 && !p2.first, "and keeps it");
        check(m.Acquire(third, Mode::OwnModel, 2).slot == 1, "a third feature gets slot 1");
        check(m.Acquire(fourth, Mode::OwnModel, 2).slot == 2, "a fourth gets slot 2");
        check(m.Acquire(fifth, Mode::OwnModel, 2).kind == Kind::StandAside, "past the slots: stand aside");
        check(m.OwnCount() == 3, "three own models");
        const Released r = m.Release(third);
        check(r.own && r.slot == 1, "released: its slot is to be parked");
        check(m.Acquire(fifth, Mode::OwnModel, 3).slot == 1, "the freed slot is taken by the next feature");
        uint32_t h = 0;
        check(m.OwnHandle(1, h) && h == fifth, "slot 1 now holds the fifth feature");
    }
    {
        // Share: the old behaviour, every feature through the main state.
        Map m;
        m.Acquire(world, Mode::Share, 1);
        check(m.Acquire(preview, Mode::Share, 2).kind == Kind::Main, "share: everything is main");
    }
    {
        // The main feature released (a resolution change makes it again): the next one to call takes main, and the
        // state is kept (no tookMain, since nothing else had main).
        Map m;
        m.Acquire(world, Mode::StandAside, 1);
        m.Release(world);
        const Role r = m.Acquire(preview, Mode::StandAside, 2);
        check(r.kind == Kind::Main && r.first && !r.tookMain, "main released: the next feature takes it");
        check(m.Acquire(world, Mode::StandAside, 3).kind == Kind::StandAside, "then the old one is secondary");
    }
    {
        // A first feature that lingers unused (a menu scene) does not keep the gameplay feature from NR.
        Map m;
        m.Acquire(world, Mode::StandAside, 1);
        check(m.Acquire(preview, Mode::StandAside, 1 + kMainIdleEpochs).kind == Kind::StandAside,
              "main idle for exactly kMainIdleEpochs: still main");
        const Role r = m.Acquire(preview, Mode::StandAside, 2 + kMainIdleEpochs);
        check(r.kind == Kind::Main && r.tookMain, "main idle longer: the other feature takes main over");
        check(m.Acquire(world, Mode::StandAside, 3 + kMainIdleEpochs).kind == Kind::StandAside,
              "the old main is secondary now");
    }
    {
        // Own model: a slotted feature that takes main over gives its slot back.
        Map m;
        m.Acquire(world, Mode::OwnModel, 1);
        m.Acquire(preview, Mode::OwnModel, 1);
        check(m.OwnCount() == 1, "the preview has its own model");
        const Role r = m.Acquire(preview, Mode::OwnModel, 10 + kMainIdleEpochs);
        check(r.kind == Kind::Main && r.tookMain && m.OwnCount() == 0, "taking main gives the slot back");
    }
    {
        // An epoch going backwards (another clock) is no idle main.
        Map m;
        m.Acquire(world, Mode::StandAside, 500);
        check(m.Acquire(preview, Mode::StandAside, 3).kind == Kind::StandAside, "an earlier epoch: main kept");
    }
    {
        Map m;
        m.Acquire(world, Mode::OwnModel, 1);
        m.Acquire(preview, Mode::OwnModel, 1);
        m.Reset();
        check(m.OwnCount() == 0 && m.Acquire(preview, Mode::OwnModel, 2).kind == Kind::Main, "reset: a fresh map");
    }

    if (fails == 0)
        std::puts("PASS: nr_contexts_smoke");
    return fails != 0;
}
