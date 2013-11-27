#include "vu.h"

static void VNOP(void)
{
    const int WB_inhibit = 1;

    if (WB_inhibit)
        message("VNOP", WB_inhibit);
    return;
}
