#include "vu.h"
#include "divrom.h"

static void VRSQ(int vd, int vs, int vt, int e)
{
    vd &= vs &= vt &= e &= 0; /* unused */
    if (vd == vs && vt == e)
        message("VRSQ\nUnimplemented.", 3);
    DPH = 0;
    return;
}
