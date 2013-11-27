#include "vu.h"

static void VNOP(int vd, int vs, int vt, int e)
{
    const int WB_inhibit = vd = vs = vt = e = 1;

    if (WB_inhibit)
        return; /* message("VNOP", WB_inhibit); */
    return;
}
