#include "pch.h"
#include "DlssNr_AllocatorEpochs.h"

#include <atomic>
#include <objbase.h>
#include <mutex>

namespace DlssNr::AllocatorEpochs
{
namespace
{
using Microsoft::WRL::ComPtr;

std::atomic_bool hooked = false, wanted = false;
// Bumped whenever tracking stops, so no binding from before can be taken for one written after.
std::atomic<uint64_t> session = 1;

// Private-data keys made per process (like GpuLifetime's list watch): another OptiScaler build loaded in the same
// process never reads our tokens as its own.
struct Keys
{
    GUID allocator {}, list {};
    bool valid = SUCCEEDED(CoCreateGuid(&allocator)) && SUCCEEDED(CoCreateGuid(&list));
};
const Keys& GetKeys()
{
    static const Keys keys;
    return keys;
}

// The allocator and every list bound to it each own a reference to this token, never to each other.
struct Token : IUnknown
{
    std::atomic<ULONG> references { 1 };
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown))
            return E_NOINTERFACE;
        *out = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto remaining = --references;
        if (remaining == 0)
            delete this;
        return remaining;
    }
    virtual ~Token() = default;
};
struct AllocatorToken final : Token
{
    std::atomic<uint64_t> resets = 0;
};
struct ListToken final : Token
{
    std::mutex mutex;
    ComPtr<AllocatorToken> allocator;
    uint64_t session = 0;
};

// GetPrivateData of an interface hands back an added reference, which the ComPtr takes over.
template <class T> ComPtr<T> Get(ID3D12Object* object, REFGUID key)
{
    IUnknown* token = nullptr;
    UINT size = sizeof(token);
    ComPtr<T> result;
    if (SUCCEEDED(object->GetPrivateData(key, &size, &token)) && token)
        result.Attach(static_cast<T*>(token));
    return result;
}
template <class T> ComPtr<T> Ensure(ID3D12Object* object, REFGUID key)
{
    auto token = Get<T>(object, key);
    if (!token)
    {
        token.Attach(new T);
        if (FAILED(object->SetPrivateDataInterface(key, token.Get())))
            token.Reset();
    }
    return token;
}

void Stop() { session.fetch_add(1, std::memory_order_acq_rel); }
} // namespace

bool Active()
{
    return hooked.load(std::memory_order_acquire) && wanted.load(std::memory_order_acquire) && GetKeys().valid;
}

void SetHooked(bool on)
{
    if (!on && hooked.exchange(false, std::memory_order_acq_rel))
        Stop();
    else if (on)
        hooked.store(true, std::memory_order_release);
}

void SetWanted(bool on)
{
    if (!on && wanted.exchange(false, std::memory_order_acq_rel))
        Stop();
    else if (on && !wanted.exchange(true, std::memory_order_acq_rel))
        Stop(); // a fresh session: nothing written while it was off is trusted
}

void ListReset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator)
{
    if (!list || !Active())
        return;
    const auto& keys = GetKeys();
    auto binding = Ensure<ListToken>(list, keys.list);
    if (!binding)
        return;
    // Rewrite the binding even when the allocator's token cannot be made, so an unrelated allocator never closes a
    // later recording on this list.
    auto token = allocator ? Ensure<AllocatorToken>(allocator, keys.allocator) : ComPtr<AllocatorToken>();
    std::lock_guard lock(binding->mutex);
    binding->allocator = std::move(token);
    binding->session = session.load(std::memory_order_acquire);
}

void AllocatorReset(ID3D12CommandAllocator* allocator)
{
    if (!allocator || !Active())
        return;
    if (auto token = Get<AllocatorToken>(allocator, GetKeys().allocator))
        token->resets.fetch_add(1, std::memory_order_acq_rel);
}

bool Stamp::Expired() const
{
    return token && static_cast<AllocatorToken*>(token.Get())->resets.load(std::memory_order_acquire) != resets;
}

Stamp Capture(ID3D12GraphicsCommandList* list)
{
    Stamp stamp;
    if (!list || !Active())
        return stamp;
    auto binding = Get<ListToken>(list, GetKeys().list);
    if (!binding)
        return stamp;
    std::lock_guard lock(binding->mutex);
    if (!binding->allocator || binding->session != session.load(std::memory_order_acquire))
        return stamp;
    stamp.resets = binding->allocator->resets.load(std::memory_order_acquire);
    stamp.token = binding->allocator;
    return stamp;
}
} // namespace DlssNr::AllocatorEpochs
