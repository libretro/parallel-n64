#include "vu.h"
#include "divrom.h"

INLINE static void do_rsql(int data)
{
    unsigned int addr;
    int fetch;
    int shift = 32;

    if (data < 0)
        data = -data - (data < -32768); /* -(x) if >=; ~(x) if < */
    do
    {
        --shift;
        if (data & (1 << shift))
            goto FOUND_MSB;
    } while (shift); /* while (shift > 0) or ((shift ^ 31) < 32) */
    shift = 31 - 16*DPH; /* if (data == 0) shift = DPH ? 16 ^ 31 : 0 ^ 31; */
FOUND_MSB:
    DPH = 0;
    shift ^= 31; /* Right-to-left shift direction conversion. */
    addr = (data << shift) >> 22;
    addr &= 0x000001FE;
    addr |= 0x00000200 | (shift & 1);
    fetch = div_ROM[addr];
    shift ^= 31; /* Flipped shift direction back to right-. */
    shift >>= 1;
    DivOut = (0x40000000 | (fetch << 14)) >> shift;
    if (DivIn < 0)
        DivOut = ~DivOut;
    else if (DivIn == 0) /* corner case:  overflow via division by zero */
        DivOut = 0x7FFFFFFF;
    else if (DivIn == -32768) /* corner case:  signed underflow barrier */
        DivOut = 0xFFFF0000;
    return;
}

static void VRSQL(void)
{
    const int vd = (inst.W >> 6) & 31;
    const int de = inst.R.rd & 07;
    const int vt = inst.R.rt;

    DivIn &= -DPH;
    DivIn |= (unsigned short)VR[vt][inst.R.rs & 07];
    do_rsql(DivIn);
    SHUFFLE_VECTOR(VACC_L, VR[vt], inst.R.rs & 0xF);
    VR[vd][de] = (short)DivOut;
    return;
}
