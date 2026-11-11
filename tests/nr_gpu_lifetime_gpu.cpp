// GPU test for dlssnr/DlssNr_GpuLifetime.cpp, the tracker DLSS-NR's parked features and textures wait on
// (shaders/dlssnr/DlssNr_Dx12.cpp ParkNr, issue #271), no game: a WARP device, and a queue held back by a fence the CPU
// signals when the test says so, so "the GPU is still running it" is certain, not a race.
//   - a retire waits for a submitted list until the GPU has run it AND the list was reset (or destroyed),
//   - a retire made while the list was still being recorded (a rebuild in the middle of a frame) waits for that list too,
//   - a retire owned by an expired owner is dropped unrun, even while a recording it waits on never completes,
//   - a list destroyed instead of reset closes its recording,
//   - with allocator tracking: an allocator reset closes a recording (also before a submit, after a replay),
//     untracked recordings and bindings from before a tracking break (hooks off, tracking unwanted) do not.
//
//   vcvars64, then from the repo root:
//   cl /std:c++20 /EHsc /O2 /I tests\shim\gpu_lifetime tests\nr_gpu_lifetime_gpu.cpp OptiScaler\dlssnr\DlssNr_GpuLifetime.cpp ^
//     OptiScaler\dlssnr\DlssNr_AllocatorEpochs.cpp d3d12.lib dxgi.lib ole32.lib

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <memory>

#include "../OptiScaler/dlssnr/DlssNr_GpuLifetime.h"
#include "../OptiScaler/dlssnr/DlssNr_AllocatorEpochs.h"

using Microsoft::WRL::ComPtr;

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

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Fence> gate; // the queue waits on it before every list: the CPU decides when the GPU may go on
    ComPtr<ID3D12Fence> idle;
    UINT64 gateValue = 0, idleValue = 0;

    bool Init()
    {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> warp;

        return SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
               SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
               SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
               SUCCEEDED(device->CreateCommandQueue(&kQueue, IID_PPV_ARGS(&queue))) &&
               SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idle)));
    }

    ComPtr<ID3D12GraphicsCommandList> NewList()
    {
        ComPtr<ID3D12GraphicsCommandList> list;
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
        return list;
    }

    // Closes and submits `list` behind the gate, and tells the tracker as the submit hook does.
    void Submit(DlssNr::GpuLifetime& tracker, ID3D12GraphicsCommandList* list)
    {
        list->Close();
        queue->Wait(gate.Get(), ++gateValue);
        ID3D12CommandList* lists[] = { list };
        queue->ExecuteCommandLists(1, lists);
        tracker.Submitted(queue.Get(), 1, lists);
    }

    // Lets the GPU run everything submitted so far and waits until it has.
    void Release()
    {
        gate->Signal(gateValue);
        queue->Signal(idle.Get(), ++idleValue);
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        idle->SetEventOnCompletion(idleValue, event);
        WaitForSingleObject(event, 10000);
        CloseHandle(event);
    }

    static constexpr D3D12_COMMAND_QUEUE_DESC kQueue { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE,
                                                       0 };
};

int main()
{
    Gpu gpu;

    if (!gpu.Init())
    {
        printf("no WARP device\n");
        return 1;
    }

    // Recorded, submitted, retired: not while the GPU holds it, not after it ran but before the reset, then yes.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());
        gpu.Submit(tracker, list.Get());

        bool ran = false;
        tracker.Retire([&] { ran = true; });

        tracker.Collect();
        CHECK(!ran);

        gpu.Release();
        tracker.Collect();
        CHECK(!ran); // still open: the game has not reset it, and could submit it again

        tracker.ResetRecording(list.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // A finished-picture list can stay in its pool indefinitely after changing modes.
    // It is safe to retire after its sole submission finishes, even while the list object remains alive.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());
        tracker.MarkSingleSubmit(list.Get());

        bool ran = false;
        tracker.Retire([&] { ran = true; });
        tracker.Collect();
        CHECK(tracker.Open(list.Get()) && !ran);

        gpu.Submit(tracker, list.Get());
        tracker.Collect();
        CHECK(!tracker.Open(list.Get()) && !ran);

        gpu.Release();
        tracker.Collect();
        CHECK(ran && list.Get());
    }

    // Allocator tracking (DlssNr_AllocatorEpochs): the hooks are simulated by calling ListReset/AllocatorReset after
    // each successful native Reset, as ResTrack_dx12 does.
    namespace Epochs = DlssNr::AllocatorEpochs;
    Epochs::SetHooked(true);
    Epochs::SetWanted(true);
    CHECK(Epochs::Active());

    // Resets `list` onto `allocator` and tells both the tracker and the epochs, as the hooks do.
    auto resetList = [&](DlssNr::GpuLifetime& tracker, ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* a)
    {
        CHECK(SUCCEEDED(list->Reset(a, nullptr)));
        tracker.ResetRecording(list);
        Epochs::ListReset(list, a);
    };
    auto resetAllocator = [&](ID3D12CommandAllocator* a)
    {
        CHECK(SUCCEEDED(a->Reset()));
        Epochs::AllocatorReset(a);
    };
    auto newAllocator = [&]
    {
        ComPtr<ID3D12CommandAllocator> a;
        CHECK(SUCCEEDED(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a))));
        return a;
    };

    // A game can reset its allocator but leave the closed list in a pool for much longer.
    {
        DlssNr::GpuLifetime tracker;
        auto allocator = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get(), true);
        gpu.Submit(tracker, list.Get());

        bool ran = false;
        tracker.Retire([&] { ran = true; });
        tracker.Collect();
        CHECK(!ran);
        gpu.Release();
        tracker.Collect();
        CHECK(tracker.Open(list.Get()) && !ran);

        resetAllocator(allocator.Get());
        tracker.Collect();
        CHECK(!tracker.Open(list.Get()) && ran);

        // Re-recording on another allocator detaches the earlier association.
        auto another = newAllocator();
        resetList(tracker, list.Get(), another.Get());
        tracker.Record(list.Get(), true);
        CHECK(SUCCEEDED(list->Close()));
        resetAllocator(allocator.Get());
        CHECK(tracker.Open(list.Get()));
        resetAllocator(another.Get());
        CHECK(!tracker.Open(list.Get()));
    }

    // Allocator reset before any submit: nothing can run the recording any more, so its retire runs at once.
    {
        DlssNr::GpuLifetime tracker;
        auto allocator = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get(), true);
        CHECK(SUCCEEDED(list->Close()));
        bool ran = false;
        tracker.Retire([&] { ran = true; });
        resetAllocator(allocator.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // Replayed (submitted twice): the retire waits for the GPU to finish both, also after the allocator reset.
    {
        DlssNr::GpuLifetime tracker;
        auto allocator = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get(), true);
        gpu.Submit(tracker, list.Get());
        gpu.Release();
        bool ran = false;
        tracker.Retire([&] { ran = true; });
        {
            gpu.queue->Wait(gpu.gate.Get(), ++gpu.gateValue);
            ID3D12CommandList* lists[] = { list.Get() };
            gpu.queue->ExecuteCommandLists(1, lists);
            tracker.Submitted(gpu.queue.Get(), 1, lists);
        }
        tracker.Collect();
        CHECK(!ran);
        gpu.Release(); // a real game resets the allocator only after this; the order is what the tracker relies on
        resetAllocator(allocator.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // A recording made without allocator tracking keeps the ordinary list lifetime through an allocator reset.
    {
        DlssNr::GpuLifetime tracker;
        auto allocator = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get());
        gpu.Submit(tracker, list.Get());
        gpu.Release();
        bool ran = false;
        tracker.Retire([&] { ran = true; });
        resetAllocator(allocator.Get());
        tracker.Collect();
        CHECK(tracker.Open(list.Get()) && !ran);
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // Review 2026-11-11 R1-F1: the hooks come off and back on. A list reset while unhooked keeps its old binding, so the
    // binding from before must not be trusted: resetting the OLD allocator must not close a recording the list now
    // makes into another one (that would free resources the GPU has yet to run).
    {
        DlssNr::GpuLifetime tracker;
        auto oldAllocator = newAllocator(), newer = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), oldAllocator.Get()); // bound to oldAllocator
        CHECK(SUCCEEDED(list->Close()));
        Epochs::SetHooked(false);
        CHECK(!Epochs::Active());
        CHECK(SUCCEEDED(list->Reset(newer.Get(), nullptr))); // unseen: the binding still names oldAllocator
        tracker.ResetRecording(list.Get());
        Epochs::SetHooked(true);
        tracker.Record(list.Get(), true);
        gpu.Submit(tracker, list.Get());
        bool ran = false;
        tracker.Retire([&] { ran = true; });
        resetAllocator(oldAllocator.Get());
        tracker.Collect();
        CHECK(tracker.Open(list.Get()) && !ran);
        gpu.Release();
        resetList(tracker, list.Get(), newer.Get()); // the ordinary lifetime closes it
        tracker.Collect();
        CHECK(ran);
    }

    // The same when the user stops wanting the tracking (Superior switched off) and turns it on again.
    {
        DlssNr::GpuLifetime tracker;
        auto oldAllocator = newAllocator(), newer = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), oldAllocator.Get());
        CHECK(SUCCEEDED(list->Close()));
        Epochs::SetWanted(false);
        resetList(tracker, list.Get(), newer.Get()); // not written while off
        Epochs::SetWanted(true);
        tracker.Record(list.Get(), true);
        CHECK(SUCCEEDED(list->Close()));
        resetAllocator(oldAllocator.Get());
        CHECK(tracker.Open(list.Get()));
        resetAllocator(newer.Get()); // newer was never bound in this session either: still the ordinary lifetime
        CHECK(tracker.Open(list.Get()));
        resetList(tracker, list.Get(), newer.Get());
        CHECK(!tracker.Open(list.Get()));
    }

    // AfterRecording: true once the GPU ran the recording and it closed; false for a recording discarded unsubmitted.
    {
        DlssNr::GpuLifetime tracker;
        auto allocator = newAllocator();
        auto list = gpu.NewList();
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get(), true);
        int result = -1;
        CHECK(tracker.AfterRecording(list.Get(), [&](bool submitted) { result = submitted ? 1 : 0; }));
        gpu.Submit(tracker, list.Get());
        tracker.Collect();
        CHECK(result == -1);
        gpu.Release();
        resetAllocator(allocator.Get());
        tracker.Collect();
        CHECK(result == 1);

        resetList(tracker, list.Get(), allocator.Get());
        tracker.Record(list.Get(), true);
        result = -1;
        CHECK(tracker.AfterRecording(list.Get(), [&](bool submitted) { result = submitted ? 1 : 0; }));
        CHECK(SUCCEEDED(list->Close()));
        resetList(tracker, list.Get(), allocator.Get()); // reset without a submit: discarded
        tracker.Collect();
        CHECK(result == 0);

        auto unrecorded = gpu.NewList();
        CHECK(!tracker.AfterRecording(unrecorded.Get(), [](bool) {}));
        unrecorded->Close();
    }
    Epochs::SetWanted(false);
    Epochs::SetHooked(false);

    // Retired in the middle of the recording (before the list is closed or submitted): the list it was parked on counts.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());

        bool ran = false;
        tracker.Retire([&] { ran = true; });

        gpu.Submit(tracker, list.Get());
        tracker.Collect();
        CHECK(!ran);

        gpu.Release();
        tracker.ResetRecording(list.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // Owned by an owner that expired: dropped, never run, though its recording never completes.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());
        gpu.Submit(tracker, list.Get());

        bool ran = false;
        auto owner = std::make_shared<int>(0);
        tracker.Retire([&] { ran = true; }, owner);
        owner.reset();
        tracker.Collect();

        gpu.Release();
        tracker.ResetRecording(list.Get());
        tracker.Collect();
        CHECK(!ran);
    }

    // Owned, owner still alive: runs as a plain retire does.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());
        gpu.Submit(tracker, list.Get());

        bool ran = false;
        auto owner = std::make_shared<int>(0);
        tracker.Retire([&] { ran = true; }, owner);

        gpu.Release();
        tracker.ResetRecording(list.Get());
        tracker.Collect();
        CHECK(ran);
    }

    // A list released instead of reset: its recording closes once it is destroyed.
    {
        DlssNr::GpuLifetime tracker;
        auto list = gpu.NewList();
        tracker.Record(list.Get());
        gpu.Submit(tracker, list.Get());

        bool ran = false;
        tracker.Retire([&] { ran = true; });

        gpu.Release();
        list.Reset();
        tracker.Collect();
        CHECK(ran);
    }

    if (fails == 0)
        printf("gpu lifetime: ok\n");

    return fails == 0 ? 0 : 1;
}
