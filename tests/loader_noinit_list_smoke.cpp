// x64 MSVC: cl /std:c++20 /EHsc /W4 /WX /O2 /I OptiScaler/misc tests/loader_noinit_list_smoke.cpp
// The list of images loaded without initialization must never match an unknown module, stay usable when full, and
// keep no reference (a full list or a failed record must not switch off OptiScaler's load handling).
#include "LoaderBoundary.h"
#include <cstdio>

static int failures = 0;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static HMODULE Handle(ULONG_PTR i) { return reinterpret_cast<HMODULE>(0x10000 + i * 0x10000); }

int main()
{
    static loader_boundary::KnownNoInitImages list;

    CHECK(!list.Contains(nullptr));
    CHECK(!list.Contains(Handle(0)));

    list.Record(Handle(0), nullptr);
    CHECK(list.Contains(Handle(0)));
    CHECK(!list.Contains(Handle(1)));

    // Already loaded normally before this load: not a no-init image
    list.Record(Handle(1), Handle(1));
    CHECK(!list.Contains(Handle(1)));

    // Data-file handles carry tag bits and null is no image
    list.Record(reinterpret_cast<HMODULE>(0x10001), nullptr);
    CHECK(!list.Contains(reinterpret_cast<HMODULE>(0x10001)));
    list.Record(nullptr, nullptr);
    CHECK(!list.Contains(nullptr));

    // Freed by the game: forgotten
    list.Forget(Handle(0));
    CHECK(!list.Contains(Handle(0)));
    list.Forget(nullptr);

    // Full: new images are not recorded, and nothing else starts to match
    for (ULONG_PTR i = 10; i != 200; ++i)
        list.Record(Handle(i), nullptr);
    CHECK(list.Contains(Handle(10)));
    CHECK(!list.Contains(Handle(199)));
    CHECK(!list.Contains(nullptr));
    CHECK(!list.Contains(Handle(5)));

    // A freed slot is usable again
    list.Forget(Handle(10));
    list.Record(Handle(199), nullptr);
    CHECK(list.Contains(Handle(199)));

    if (!failures)
        printf("loader_noinit_list_smoke: all checks passed\n");
    return failures ? 1 : 0;
}
