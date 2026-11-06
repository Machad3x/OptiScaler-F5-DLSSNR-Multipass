// Host check of shaders/dlssnr/DlssNr_LateAge.h: a Finished Picture capture's age window grows with the frames shown
// per game frame, and one missed game frame does not reset the model.
// cl /nologo /std:c++20 /EHsc /W4 tests/nr_late_age_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_LateAge.h"

#include <cstdio>

using namespace DlssNrLateAge;

static int fails = 0;

static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what);
        ++fails;
    }
}

int main()
{
    Check(PresentWindow(0) == 1 && PresentWindow(1) == 1, "x1 window is 1");
    Check(PresentWindow(3) == 5 && PresentWindow(6) == 11, "x3 window 5, x6 window 11");

    Check(!Stale(11, 10, 1), "x1 age 1 fresh");
    Check(Stale(12, 10, 1), "x1 age 2 stale");

    Check(!Stale(13, 10, 3) && !Stale(15, 10, 3), "x3 age 3 and 5 fresh");
    Check(Stale(16, 10, 3), "x3 age 6 stale");

    Check(!Stale(16, 10, 6) && !Stale(21, 10, 6), "x6 age 6 and 11 fresh");
    Check(Stale(22, 10, 6), "x6 age 12 stale");

    Check(Stale(9, 10, 1) && Stale(9, 10, 6), "epoch before submission is stale");

    Check(NeedsReset(0, 5), "first applied picture resets");
    Check(!NeedsReset(5, 6), "next game frame keeps history");
    Check(!NeedsReset(5, 7), "one missed game frame keeps history");
    Check(NeedsReset(5, 8), "two missed game frames reset");

    if (fails)
        return 1;
    printf("PASS: nr_late_age_smoke\n");
    return 0;
}
