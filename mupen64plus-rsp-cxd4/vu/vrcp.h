#include "vu.h"
#include "divrom.h"

INLINE static void do_rcp(int data)
{
    unsigned int addr;
    int fetch;
    int shift = 32;

    DPH = 0;
    if (data < 0)
        data = -data;
    do
    {
        --shift;
        if (data & (1 << shift))
            goto FOUND_MSB;
    } while (shift); /* while (shift > 0) or ((shift ^ 31) < 32) */
    shift = 16 ^ 31; /* No bits found in (data == 0x00000000), so shift = 16. */
FOUND_MSB:
    shift ^= 31; /* Right-to-left shift direction conversion. */
    addr = (data << shift) >> 22;
    fetch = div_ROM[addr &= 0x000001FF];
    shift ^= 31; /* Flipped shift direction back to right-. */
    DivOut = (0x40000000 | (fetch << 14)) >> shift;
    if (DivIn < 0)
        DivOut = ~DivOut;
    else if (DivIn == 0) /* corner case:  overflow via division by zero */
        DivOut = 0x7FFFFFFF;
    else if (DivIn == -32768) /* corner case:  signed underflow barrier */
        DivOut = 0xFFFF0000;
    return;
}

static void VRCP(void)
{
    const int vd = (inst.W >> 6) & 31;
    const int de = inst.R.rd & 07;
    const int vt = inst.R.rt;

    DivIn = (int)VR[vt][inst.R.rs & 07];
    do_rcp(DivIn);
    SHUFFLE_VECTOR(VACC_L, VR[vt], inst.R.rs & 0xF);
    VR[vd][de] = (short)DivOut;
    return;
}
