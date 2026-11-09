#pragma once

#include <string>
#include <string_view>
#include <new>
#include <stdexcept>
#include <atomic>
#include <windows.h>

namespace loader_boundary
{
inline bool NonExecutable(DWORD flags) noexcept
{
    return (flags & (DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                     LOAD_LIBRARY_AS_IMAGE_RESOURCE)) != 0;
}

// Images the game loaded without running their initialization (DONT_RESOLVE_DLL_REFERENCES): their DllMain never
// ran and their imports are unresolved, so OptiScaler must not bind or hook them (d3d12 binding, module discovery).
// Only the handle is kept, no reference: the game's own unload is not delayed. The FreeLibrary / LdrUnloadDll hooks
// forget a handle when the game frees it. When the list is full, new images are not recorded (behaviour as before
// this list existed). Plain atomics only, so threads without a TLS array can use it.
class KnownNoInitImages
{
    std::atomic<HMODULE> images[64] {};

  public:
    bool Contains(HMODULE module) const noexcept
    {
        if (module == nullptr)
            return false;
        for (const auto& image : images)
            if (image.load(std::memory_order_acquire) == module)
                return true;
        return false;
    }
    void Record(HMODULE module, HMODULE previous) noexcept
    {
        if (module == nullptr || (reinterpret_cast<ULONG_PTR>(module) & 3) != 0 || module == previous ||
            Contains(module))
            return;
        for (auto& image : images)
        {
            HMODULE empty = nullptr;
            if (image.compare_exchange_strong(empty, module, std::memory_order_acq_rel))
                return;
        }
    }
    void Forget(HMODULE module) noexcept
    {
        if (module == nullptr)
            return;
        for (auto& image : images)
        {
            auto expected = module;
            image.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        }
    }
};

// Only name acquisition is recoverable here. Never catch an installer after partial publication.
inline bool CopyName(std::wstring_view view, std::wstring& name) noexcept
{
    try
    {
        name.assign(view);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}
} // namespace loader_boundary
