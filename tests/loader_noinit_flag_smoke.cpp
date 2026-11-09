// x64 MSVC: cl /std:c++20 /EHsc /W4 /WX /O2 /I OptiScaler/include tests/loader_noinit_flag_smoke.cpp
//           OptiScaler/library/detours/detours.lib
// hkLdrLoadDll recognises a load without initialization by IMAGE_FILE_EXECUTABLE_IMAGE alone. This real Win32 -> real
// LdrLoadDll check fails if Windows stops passing DONT_RESOLVE_DLL_REFERENCES that way, or sets the bit on normal loads.
#include <windows.h>
#include <winternl.h>
#include <detours/detours.h>
#include <cstdio>
#include <string_view>

using LdrLoadDllFn = NTSTATUS(NTAPI*)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);
static LdrLoadDllFn original = nullptr;
static std::wstring_view watched;
static bool seen = false;
static bool noInitBit = false;

static NTSTATUS NTAPI Hook(PWSTR path, PULONG flags, PUNICODE_STRING name, PHANDLE module)
{
    if (name != nullptr && name->Buffer != nullptr &&
        std::wstring_view(name->Buffer, name->Length / sizeof(wchar_t)).ends_with(watched))
    {
        seen = true;
        noInitBit = flags != nullptr && (*flags & IMAGE_FILE_EXECUTABLE_IMAGE) != 0;
    }
    return original(path, flags, name, module);
}

int main()
{
    original = reinterpret_cast<LdrLoadDllFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrLoadDll"));
    if (original == nullptr || DetourTransactionBegin() != NO_ERROR || DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(Hook)) != NO_ERROR ||
        DetourTransactionCommit() != NO_ERROR)
    {
        printf("FAIL: could not detour LdrLoadDll\n");
        return 2;
    }

    int failures = 0;

    watched = L"msimg32.dll";
    HMODULE module = LoadLibraryExW(L"msimg32.dll", nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (module == nullptr || !seen || !noInitBit)
    {
        printf("FAIL: DONT_RESOLVE load: module=%p seen=%d bit=%d\n", module, seen, noInitBit);
        ++failures;
    }
    if (module != nullptr)
        FreeLibrary(module);

    watched = L"dciman32.dll";
    seen = noInitBit = false;
    module = LoadLibraryW(L"dciman32.dll");
    if (module == nullptr || !seen || noInitBit)
    {
        printf("FAIL: normal load: module=%p seen=%d bit=%d\n", module, seen, noInitBit);
        ++failures;
    }
    if (module != nullptr)
        FreeLibrary(module);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(Hook));
    DetourTransactionCommit();

    if (!failures)
        printf("loader_noinit_flag_smoke: DONT_RESOLVE reaches LdrLoadDll with the no-init bit, a normal load without\n");
    return failures ? 1 : 0;
}
