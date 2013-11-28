#include "vu.h"

static void VMACQ(int vd, int vs, int vt, int e)
{
    vd &= vs &= vt &= e &= 0; /* unused */
    if (vd != vs || vt != e)
        return;
    message("VMACQ\nUnimplemented.", 3); /* untested, any N64 ROMs use this?? */
    return;
}
