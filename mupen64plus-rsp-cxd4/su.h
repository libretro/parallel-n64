/******************************************************************************\
* Project:  MSP Emulation Table for Scalar Unit Operations                     *
* Authors:  Iconoclast                                                         *
* Release:  2013.12.10                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/
#ifndef _SU_H
#define _SU_H

/*
 * RSP virtual registers (of scalar unit)
 * The most important are the 32 general-purpose scalar registers.
 * We have the convenience of using a 32-bit machine (Win32) to emulate
 * another 32-bit machine (MIPS/N64), so the most natural way to accurately
 * emulate the scalar GPRs is to use the standard `int` type.  Situations
 * specifically requiring sign-extension or lack thereof are forcibly
 * applied as defined in the MIPS quick reference card and user manuals.
 * Remember that these are not the same "GPRs" as in the MIPS ISA and totally
 * abandon their designated purposes on the master CPU host (the VR4300),
 * hence most of the MIPS names "k0, k1, t0, t1, v0, v1 ..." no longer apply.
 */
static int SR[32];

#include "rsp.h"

NOINLINE static void res_S(void)
{
    export_SP_memory();
    trace_RSP_registers();
    message("RESERVED\nSee SP_STATE.TXT.", 3);
    return;
}

#ifdef EMULATE_STATIC_PC
#define BASE_OFF    0x000
#else
#define BASE_OFF    0x004
#endif

#define SLOT_OFF    (BASE_OFF + 0x000)
#define LINK_OFF    (BASE_OFF + 0x004)
void set_PC(int address)
{
    temp_PC = 0x04001000 + (address & 0xFFC);
#ifndef EMULATE_STATIC_PC
    stage = 1;
#endif
    return;
}

#if (0)
#define MASK_SA(sa) (sa & 31)
/* Force masking in software. */
#else
#define MASK_SA(sa) (sa)
/* Let hardware architecture do the mask for us. */
#endif

#if (0)
#define ENDIAN   0
#else
#define ENDIAN  ~0
#endif
#define BES(address)    ((address) ^ ((ENDIAN) & 03))
#define HES(address)    ((address) ^ ((ENDIAN) & 02))
#define MES(address)    ((address) ^ ((ENDIAN) & 01))
#define WES(address)    ((address) ^ ((ENDIAN) & 00))
#define SR_B(s, i)      (*(byte *)(((byte *)(SR + s)) + BES(i)))
#define SR_S(s, i)      (*(short *)(((byte *)(SR + s)) + HES(i)))
#define SE(x, b)        (-(x & (1 << b)) | (x & ~(~0 << b)))
#define ZE(x, b)        (+(x & (1 << b)) | (x & ~(~0 << b)))

static union {
    unsigned char B[4];
    signed char SB[4];
    unsigned short H[2];
    signed short SH[2];
    unsigned W:  32;
} SR_temp;

extern void ULW(int rd, uint32_t addr);
extern void USW(int rs, uint32_t addr);

/*
 * All other behaviors defined below this point in the file are specific to
 * the SGI N64 extension to the MIPS R4000 and are not entirely implemented.
 */

/*** Scalar, Coprocessor Operations (system control) ***/
extern RCPREG* CR[16];
extern void SP_DMA_READ(void);
extern void SP_DMA_WRITE(void);

static void MFC0(int rt, int rd)
{
    SR[rt] = *(CR[rd]);
    SR[0] = 0x00000000;
    if (rd == 0x7) /* SP_SEMAPHORE_REG */
    {
        if (CFG_MEND_SEMAPHORE_LOCK == 0)
            return;
        *RSP.SP_SEMAPHORE_REG = 0x00000001;
        *RSP.SP_STATUS_REG |= 0x00000001; /* temporary bit to break CPU */
        return;
    }
    if (rd == 0x4) /* SP_STATUS_REG */
    {
        if (CFG_WAIT_FOR_CPU_HOST == 0)
            return;
        ++MFC0_count[rt];
        if (MFC0_count[rt] > 07)
            *RSP.SP_STATUS_REG |= 0x00000001; /* Let OS restart the task. */
    }
    return;
}

static void MT_DMA_CACHE(int rt)
{
    *RSP.SP_MEM_ADDR_REG = SR[rt] & 0xFFFFFFF8; /* & 0x00001FF8 */
    return; /* Reserved upper bits are ignored during DMA R/W. */
}
static void MT_DMA_DRAM(int rt)
{
    *RSP.SP_DRAM_ADDR_REG = SR[rt] & 0xFFFFFFF8; /* & 0x00FFFFF8 */
    return; /* Let the reserved bits get sent, but the pointer is 24-bit. */
}
static void MT_DMA_READ_LENGTH(int rt)
{
    *RSP.SP_RD_LEN_REG = SR[rt] | 07;
    SP_DMA_READ();
    return;
}
static void MT_DMA_WRITE_LENGTH(int rt)
{
    *RSP.SP_WR_LEN_REG = SR[rt] | 07;
    SP_DMA_WRITE();
    return;
}
static void MT_SP_STATUS(int rt)
{
    if (SR[rt] & 0xFE000040)
        message("MTC0\nSP_STATUS", 2);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) <<  0);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000002) <<  0);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) <<  1);
    *RSP.MI_INTR_REG &= ~((SR[rt] & 0x00000008) >> 3); /* SP_CLR_INTR */
    *RSP.MI_INTR_REG |=  ((SR[rt] & 0x00000010) >> 4); /* SP_SET_INTR */
    *RSP.SP_STATUS_REG |= (SR[rt] & 0x00000010) >> 4; /* int set halt */
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000020) <<  5);
 /* *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000040) <<  5); */
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000080) <<  6);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000100) <<  6);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000200) <<  7);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000400) <<  7);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000800) <<  8);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00001000) <<  8);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00002000) <<  9);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00004000) <<  9);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00008000) << 10);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00010000) << 10);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00020000) << 11);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00040000) << 11);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00080000) << 12);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00100000) << 12);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00200000) << 13);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00400000) << 13);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00800000) << 14);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x01000000) << 14);
    return;
}
static void MT_SP_RESERVED(int rt)
{
    const uint32_t source = SR[rt] & 0x00000000; /* forced (zilmar, dox) */

    *RSP.SP_SEMAPHORE_REG = source;
    return;
}
static void MT_CMD_START(int rt)
{
    const uint32_t source = SR[rt] & 0xFFFFFFF8; /* Funnelcube demo */

    if (*RSP.DPC_BUFBUSY_REG) /* lock hazards not implemented */
        message("MTC0\nCMD_START", 0);
    *RSP.DPC_END_REG = *RSP.DPC_CURRENT_REG = *RSP.DPC_START_REG = source;
    return;
}
static void MT_CMD_END(int rt)
{
    if (*RSP.DPC_BUFBUSY_REG)
        message("MTC0\nCMD_END", 0); /* This is just CA-related. */
    *RSP.DPC_END_REG = SR[rt] & 0xFFFFFFF8;
    if (RSP.ProcessRdpList == NULL) /* zilmar GFX #1.2 */
        return;
    RSP.ProcessRdpList();
    return;
}
static void MT_CMD_STATUS(int rt)
{
    if (SR[rt] & 0xFFFFFD80) /* unsupported or reserved bits */
        message("MTC0\nCMD_STATUS", 2);
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) << 0);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000002) << 0);
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) << 1);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000008) << 1);
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000010) << 2);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000020) << 2);
/* Some NUS-CIC-6105 SP tasks try to clear some zeroed DPC registers. */
    *RSP.DPC_TMEM_REG     &= !(SR[rt] & 0x00000040) * -1;
 /* *RSP.DPC_PIPEBUSY_REG &= !(SR[rt] & 0x00000080) * -1; */
 /* *RSP.DPC_BUFBUSY_REG  &= !(SR[rt] & 0x00000100) * -1; */
    *RSP.DPC_CLOCK_REG    &= !(SR[rt] & 0x00000200) * -1;
    return;
}
static void MT_CMD_CLOCK(int rt)
{
    message("MTC0\nCMD_CLOCK", 1); /* read-only?? */
    *RSP.DPC_CLOCK_REG = SR[rt];
    return; /* Appendix says this is RW; elsewhere it says R. */
}
static void MT_READ_ONLY(int rt)
{
    char text[64];

    sprintf(text, "MTC0\nInvalid write attempt.\nSR[%i] = 0x%08X", rt, SR[rt]);
    message(text, 2);
    return;
}

static void (*MTC0[16])(int) = {
MT_DMA_CACHE       ,MT_DMA_DRAM        ,MT_DMA_READ_LENGTH ,MT_DMA_WRITE_LENGTH,
MT_SP_STATUS       ,MT_READ_ONLY       ,MT_READ_ONLY       ,MT_SP_RESERVED,
MT_CMD_START       ,MT_CMD_END         ,MT_READ_ONLY       ,MT_CMD_STATUS,
MT_CMD_CLOCK       ,MT_READ_ONLY       ,MT_READ_ONLY       ,MT_READ_ONLY
}; 
void SP_DMA_READ(void)
{
    register unsigned int length;
    register unsigned int count;
    register unsigned int skip;

    length = (*RSP.SP_RD_LEN_REG & 0x00000FFF) >>  0;
    count  = (*RSP.SP_RD_LEN_REG & 0x000FF000) >> 12;
    skip   = (*RSP.SP_RD_LEN_REG & 0xFFF00000) >> 20;
    /* length |= 07; // already corrected by mtc0 */
    ++length;
    ++count;
    skip += length;
    do
    { /* `count` always starts > 0, so we begin with `do` instead of `while`. */
        unsigned int offC, offD; /* SP cache and dynamic DMA pointers */
        register unsigned int i = 0;

        --count;
        do
        {
            offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8;
            offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
            memcpy(RSP.DMEM + offC, RSP.RDRAM + offD, 8);
            i += 0x008;
        } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}
void SP_DMA_WRITE(void)
{
    register unsigned int length;
    register unsigned int count;
    register unsigned int skip;

    length = (*RSP.SP_WR_LEN_REG & 0x00000FFF) >>  0;
    count  = (*RSP.SP_WR_LEN_REG & 0x000FF000) >> 12;
    skip   = (*RSP.SP_WR_LEN_REG & 0xFFF00000) >> 20;
    /* length |= 07; // already corrected by mtc0 */
    ++length;
    ++count;
    skip += length;
    do
    { /* `count` always starts > 0, so we begin with `do` instead of `while`. */
        unsigned int offC, offD; /* SP cache and dynamic DMA pointers */
        register unsigned int i = 0;

        --count;
        do
        {
            offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8;
            offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
            memcpy(RSP.RDRAM + offD, RSP.DMEM + offC, 8);
            i += 0x000008;
        } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}

/*** Scalar, Coprocessor Operations (vector unit) ***/

extern ALIGNED short VR[32][8];

/*
 * Since RSP vectors are stored 100% accurately as big-endian arrays for the
 * proper vector operation math to be done, LWC2 and SWC2 emulation code will
 * have to look a little different.  zilmar's method is to distort the endian
 * using an array of unions, permitting hacked byte- and halfword-precision.
 */

/*
 * Universal byte-access macro for 16*8 halfword vectors.
 * Use this macro if you are not sure whether the element is odd or even.
 */
#define VR_B(vt,element)    (*(byte *)((byte *)(VR[vt]) + MES(element)))

/*
 * Optimized byte-access macros for the vector registers.
 * Use these ONLY if you know the element is even (or odd in the second).
 */
#define VR_A(vt,element)    (*(byte *)((byte *)(VR[vt]) + element + MES(0x0)))
#define VR_U(vt,element)    (*(byte *)((byte *)(VR[vt]) + element - MES(0x0)))

/*
 * Optimized halfword-access macro for indexing eight-element vectors.
 * Use this ONLY if you know the element is even, not odd.
 *
 * If the four-bit element is odd, then there is no solution in one hit.
 */
#define VR_S(vt,element)    (*(short *)((byte *)(VR[vt]) + element))

extern unsigned short get_VCO(void);
extern unsigned short get_VCC(void);
extern unsigned char get_VCE(void);
extern void set_VCO(unsigned short VCO);
extern void set_VCC(unsigned short VCC);
extern void set_VCE(unsigned char VCE);
extern short vce[8];

unsigned short rwR_VCE(void)
{ /* never saw a game try to read VCE out to a scalar GPR yet */
    register unsigned short ret_slot;

    ret_slot = 0x00 | (unsigned short)get_VCE();
    return (ret_slot);
}
void rwW_VCE(unsigned short VCE)
{ /* never saw a game try to write VCE using a scalar GPR yet */
    register int i;

    VCE = 0x00 | (VCE & 0xFF);
    for (i = 0; i < 8; i++)
        vce[i] = (VCE >> i) & 1;
    return;
}

static unsigned short (*R_VCF[32])(void) = {
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
/* Hazard reaction barrier:  RD = (UINT16)(inst) >> 11, without &= 3. */
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE
};
static void (*W_VCF[32])(unsigned short) = {
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
/* Hazard reaction barrier:  RD = (UINT16)(inst) >> 11, without &= 3. */
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE
};
static void MFC2(int rt, int vs, int e)
{
    SR_B(rt, 2) = VR_B(vs, e);
    e = (e + 0x1) & 0xF;
    SR_B(rt, 3) = VR_B(vs, e);
    SR[rt] = (signed short)(SR[rt]);
    SR[0] = 0x00000000;
    return;
}
static void MTC2(int rt, int vd, int e)
{
    VR_B(vd, e+0x0) = SR_B(rt, 2);
    VR_B(vd, e+0x1) = SR_B(rt, 3);
    return; /* If element == 0xF, it does not matter; loads do not wrap over. */
}
static void CFC2(int rt, int rd)
{
    SR[rt] = (signed short)R_VCF[rd]();
    SR[0] = 0x00000000;
    return;
}
static void CTC2(int rt, int rd)
{
    W_VCF[rd](SR[rt] & 0x0000FFFF);
    return;
}

/*** Scalar, Coprocessor Operations (vector unit, scalar cache transfers) ***/
INLINE static void LBV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    addr = (SR[base] + 1*offset) & 0x00000FFF;
    VR_B(vt, e) = RSP.DMEM[BES(addr)];
    return;
}
INLINE static void LSV(int vt, int element, int offset, int base)
{
    int correction;
    register uint32_t addr;
    const int e = element;

    if (e & 0x1)
    {
        message("LSV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 2*offset) & 0x00000FFF;
    correction = addr % 0x004;
    if (correction == 0x003)
    {
        message("LSV\nWeird addr.", 3);
        return;
    }
    VR_S(vt, e) = *(short *)(RSP.DMEM + addr - HES(0x000)*(correction - 1));
    return;
}
INLINE static void LLV(int vt, int element, int offset, int base)
{
    int correction;
    register uint32_t addr;
    const int e = element;

    if (e & 0x1)
    {
        message("LLV\nOdd element.", 3);
        return;
    } /* Illegal (but still even) elements are used by Boss Game Studios. */
    addr = (SR[base] + 4*offset) & 0x00000FFF;
    if (addr & 0x00000001)
    {
        message("LLV\nOdd addr.", 3);
        return;
    }
    correction = HES(0x000)*(addr%0x004 - 1);
    VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr - correction);
    addr = (addr + 0x00000002) & 0x00000FFF; /* F3DLX 1.23:  addr%4 is 0x002. */
    VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + correction);
    return;
}
INLINE static void LDV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    if (e & 0x1)
    {
        message("LDV\nOdd element.", 3);
        return;
    } /* Illegal (but still even) elements are used by Boss Game Studios. */
    addr = (SR[base] + 8*offset) & 0x00000FFF;
    switch (addr & 07)
    {
        case 00:
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x006));
            return;
        case 01: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000);
            VR_A(vt, e+0x2) = RSP.DMEM[addr + 0x002 - BES(0x000)];
            VR_U(vt, e+0x3) = RSP.DMEM[addr + 0x003 + BES(0x000)];
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x004);
            VR_A(vt, e+0x6) = RSP.DMEM[addr + 0x006 - BES(0x000)];
            addr += 0x007 + BES(00);
            addr &= 0x00000FFF;
            VR_U(vt, e+0x7) = RSP.DMEM[addr];
            return;
        case 02:
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000 - HES(0x000));
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x002 + HES(0x000));
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x004 - HES(0x000));
            addr += 0x006 + HES(00);
            addr &= 0x00000FFF;
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr);
            return;
        case 03: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_A(vt, e+0x0) = RSP.DMEM[addr + 0x000 - BES(0x000)];
            VR_U(vt, e+0x1) = RSP.DMEM[addr + 0x001 + BES(0x000)];
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x002);
            VR_A(vt, e+0x4) = RSP.DMEM[addr + 0x004 - BES(0x000)];
            addr += 0x005 + BES(00);
            addr &= 0x00000FFF;
            VR_U(vt, e+0x5) = RSP.DMEM[addr];
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + 0x001 - BES(0x000));
            return;
        case 04:
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            addr += 0x004 + WES(00);
            addr &= 0x00000FFF;
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x002));
            return;
        case 05: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000);
            VR_A(vt, e+0x2) = RSP.DMEM[addr + 0x002 - BES(0x000)];
            addr += 0x003;
            addr &= 0x00000FFF;
            VR_U(vt, e+0x3) = RSP.DMEM[addr + BES(0x000)];
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x001);
            VR_A(vt, e+0x6) = RSP.DMEM[addr + BES(0x003)];
            VR_U(vt, e+0x7) = RSP.DMEM[addr + BES(0x004)];
            return;
        case 06:
            VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr - HES(0x000));
            addr += 0x002;
            addr &= 0x00000FFF;
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x004));
            return;
        case 07: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_A(vt, e+0x0) = RSP.DMEM[addr - BES(0x000)];
            addr += 0x001;
            addr &= 0x00000FFF;
            VR_U(vt, e+0x1) = RSP.DMEM[addr + BES(0x000)];
            VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x001);
            VR_A(vt, e+0x4) = RSP.DMEM[addr + BES(0x003)];
            VR_U(vt, e+0x5) = RSP.DMEM[addr + BES(0x004)];
            VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + 0x005);
            return;
    }
}
INLINE static void SBV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    addr = (SR[base] + 1*offset) & 0x00000FFF;
    RSP.DMEM[BES(addr)] = VR_B(vt, e);
    return;
}
INLINE static void SSV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    addr = (SR[base] + 2*offset) & 0x00000FFF;
    RSP.DMEM[BES(addr)] = VR_B(vt, (e + 0x0));
    addr = (addr + 0x00000001) & 0x00000FFF;
    RSP.DMEM[BES(addr)] = VR_B(vt, (e + 0x1) & 0xF);
    return;
}
INLINE static void SLV(int vt, int element, int offset, int base)
{
    int correction;
    register uint32_t addr;
    const int e = element;

    if ((e & 0x1) || e > 0xC) /* must support illegal even elements in F3DEX2 */
    {
        message("SLV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 4*offset) & 0x00000FFF;
    if (addr & 0x00000001)
    {
        message("SLV\nOdd addr.", 3);
        return;
    }
    correction = HES(0x000)*(addr%0x004 - 1);
    *(short *)(RSP.DMEM + addr - correction) = VR_S(vt, e+0x0);
    addr = (addr + 0x00000002) & 0x00000FFF; /* F3DLX 0.95:  "Mario Kart 64" */
    *(short *)(RSP.DMEM + addr + correction) = VR_S(vt, e+0x2);
    return;
}
INLINE static void SDV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    addr = (SR[base] + 8*offset) & 0x00000FFF;
    if (e > 0x8 || (e & 0x1))
    { /* Illegal elements with Boss Game Studios publications. */
        register int i;

        for (i = 0; i < 8; i++)
            RSP.DMEM[BES(addr &= 0x00000FFF)] = VR_B(vt, (e+i)&0xF);
        return;
    }
    switch (addr & 07)
    {
        case 00:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x0);
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x2);
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR_S(vt, e+0x4);
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR_S(vt, e+0x6);
            return;
        case 01: /* "Tetrisphere" audio ucode */
            *(short *)(RSP.DMEM + addr + 0x000) = VR_S(vt, e+0x0);
            RSP.DMEM[addr + 0x002 - BES(0x000)] = VR_A(vt, e+0x2);
            RSP.DMEM[addr + 0x003 + BES(0x000)] = VR_U(vt, e+0x3);
            *(short *)(RSP.DMEM + addr + 0x004) = VR_S(vt, e+0x4);
            RSP.DMEM[addr + 0x006 - BES(0x000)] = VR_A(vt, e+0x6);
            addr += 0x007 + BES(0x000);
            addr &= 0x00000FFF;
            RSP.DMEM[addr] = VR_U(vt, e+0x7);
            return;
        case 02:
            *(short *)(RSP.DMEM + addr + 0x000 - HES(0x000)) = VR_S(vt, e+0x0);
            *(short *)(RSP.DMEM + addr + 0x002 + HES(0x000)) = VR_S(vt, e+0x2);
            *(short *)(RSP.DMEM + addr + 0x004 - HES(0x000)) = VR_S(vt, e+0x4);
            addr += 0x006 + HES(0x000);
            addr &= 0x00000FFF;
            *(short *)(RSP.DMEM + addr) = VR_S(vt, e+0x6);
            return;
        case 03: /* "Tetrisphere" audio ucode */
            RSP.DMEM[addr + 0x000 - BES(0x000)] = VR_A(vt, e+0x0);
            RSP.DMEM[addr + 0x001 + BES(0x000)] = VR_U(vt, e+0x1);
            *(short *)(RSP.DMEM + addr + 0x002) = VR_S(vt, e+0x2);
            RSP.DMEM[addr + 0x004 - BES(0x000)] = VR_A(vt, e+0x4);
            addr += 0x005 + BES(0x000);
            addr &= 0x00000FFF;
            RSP.DMEM[addr] = VR_U(vt, e+0x5);
            *(short *)(RSP.DMEM + addr + 0x001 - BES(0x000)) = VR_S(vt, 0x6);
            return;
        case 04:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x0);
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x2);
            addr = (addr + 0x004) & 0x00000FFF;
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x4);
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x6);
            return;
        case 05: /* "Tetrisphere" audio ucode */
            *(short *)(RSP.DMEM + addr + 0x000) = VR_S(vt, e+0x0);
            RSP.DMEM[addr + 0x002 - BES(0x000)] = VR_A(vt, e+0x2);
            addr = (addr + 0x003) & 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = VR_U(vt, e+0x3);
            *(short *)(RSP.DMEM + addr + 0x001) = VR_S(vt, e+0x4);
            RSP.DMEM[addr + BES(0x003)] = VR_A(vt, e+0x6);
            RSP.DMEM[addr + BES(0x004)] = VR_U(vt, e+0x7);
            return;
        case 06:
            *(short *)(RSP.DMEM + addr - HES(0x000)) = VR_S(vt, e+0x0);
            addr = (addr + 0x002) & 0x00000FFF;
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x2);
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x4);
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR_S(vt, e+0x6);
            return;
        case 07: /* "Tetrisphere" audio ucode */
            RSP.DMEM[addr - BES(0x000)] = VR_A(vt, e+0x0);
            addr = (addr + 0x001) & 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = VR_U(vt, e+0x1);
            *(short *)(RSP.DMEM + addr + 0x001) = VR_S(vt, e+0x2);
            RSP.DMEM[addr + BES(0x003)] = VR_A(vt, e+0x4);
            RSP.DMEM[addr + BES(0x004)] = VR_U(vt, e+0x5);
            *(short *)(RSP.DMEM + addr + 0x005) = VR_S(vt, e+0x6);
            return;
    }
}

/*
 * Group II vector loads and stores:
 * PV and UV (As of RCP implementation, XV and ZV are reserved opcodes.)
 */
INLINE static void LPV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0)
    {
        message("LPV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            VR[vt][07] = RSP.DMEM[addr + BES(0x007)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][00] = RSP.DMEM[addr + BES(0x000)] << 8;
            return;
        case 01: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += BES(0x008);
            addr &= 0x00000FFF;
            VR[vt][07] = RSP.DMEM[addr] << 8;
            return;
        case 02: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][06] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x001)] << 8;
            return;
        case 03: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][05] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x002)] << 8;
            return;
        case 04: /* "Resident Evil 2" in-game 3-D, F3DLX 2.08--"WWF No Mercy" */
            VR[vt][00] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][04] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x003)] << 8;
            return;
        case 05: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][03] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x004)] << 8;
            return;
        case 06: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][02] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x005)] << 8;
            return;
        case 07: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][01] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x006)] << 8;
            return;
    }
}
INLINE static void LUV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    int e = element;

    addr = (SR[base] + 8*offset) & 0x00000FFF;
    if (e != 0x0)
    { /* "Mia Hamm Soccer 64" SP exception override (zilmar) */
        addr += -e & 0xF;
        for (b = 0; b < 8; b++)
        {
            VR[vt][b] = RSP.DMEM[BES(addr &= 0x00000FFF)] << 7;
            --e;
            addr -= 16 * (e == 0x0);
            ++addr;
        }
        return;
    }
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            VR[vt][07] = RSP.DMEM[addr + BES(0x007)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][00] = RSP.DMEM[addr + BES(0x000)] << 7;
            return;
        case 01: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += BES(0x008);
            addr &= 0x00000FFF;
            VR[vt][07] = RSP.DMEM[addr] << 7;
            return;
        case 02: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][06] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x001)] << 7;
            return;
        case 03: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][05] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x002)] << 7;
            return;
        case 04: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][04] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x003)] << 7;
            return;
        case 05: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][03] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x004)] << 7;
            return;
        case 06: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][02] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x005)] << 7;
            return;
        case 07: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][01] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x006)] << 7;
            return;
    }
}
INLINE static void SPV(int vt, int element, int offset, int base)
{
    register int b;
    register uint32_t addr;
    const int e = element;

    if (e != 0x0)
    {
        message("SPV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 8);
            return;
        case 01: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][06] >> 8);
            addr += BES(0x008);
            addr &= 0x00000FFF;
            RSP.DMEM[addr] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 02: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][05] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 03: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][04] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 04: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 05: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][02] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 06: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][01] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 07: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][00] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][07] >> 8);
            return;
    }
}
INLINE static void SUV(int vt, int element, int offset, int base)
{
    register int b;
    register uint32_t addr;
    const int e = element;

    if (e != 0x0)
    {
        message("SUV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 7);
            return;
        case 04: /* "Indiana Jones and the Infernal Machine" in-game */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 7);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 7);
            return;
        default: /* Completely legal, just never seen it be done. */
            message("SUV\nWeird addr.", 3);
            return;
    }
}

/*
 * Group III vector loads and stores:
 * HV, FV, and AV (As of RCP implementation, AV opcodes are reserved.)
 */
static void LHV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    if (e != 0x0)
    {
        message("LHV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x0000000E)
    {
        message("LHV\nIllegal addr.", 3);
        return;
    }
    addr ^= MES(00);
    VR[vt][07] = RSP.DMEM[addr + HES(0x00E)] << 7;
    VR[vt][06] = RSP.DMEM[addr + HES(0x00C)] << 7;
    VR[vt][05] = RSP.DMEM[addr + HES(0x00A)] << 7;
    VR[vt][04] = RSP.DMEM[addr + HES(0x008)] << 7;
    VR[vt][03] = RSP.DMEM[addr + HES(0x006)] << 7;
    VR[vt][02] = RSP.DMEM[addr + HES(0x004)] << 7;
    VR[vt][01] = RSP.DMEM[addr + HES(0x002)] << 7;
    VR[vt][00] = RSP.DMEM[addr + HES(0x000)] << 7;
    return;
}
NOINLINE static void LFV(int vt, int element, int offset, int base)
{ /* Dummy implementation only:  Do any games execute this? */
    char debugger[32];

    sprintf(debugger, "%s     $v%i[0x%X], 0x%03X($%i)", "LFV",
        vt, element, offset & 0xFFF, base);
    message(debugger, 3);
    return;
}
static void SHV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    if (e != 0x0)
    {
        message("SHV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x0000000E)
    {
        message("SHV\nIllegal addr.", 3);
        return;
    }
    addr ^= MES(00);
    RSP.DMEM[addr + HES(0x00E)] = (unsigned char)(VR[vt][07] >> 7);
    RSP.DMEM[addr + HES(0x00C)] = (unsigned char)(VR[vt][06] >> 7);
    RSP.DMEM[addr + HES(0x00A)] = (unsigned char)(VR[vt][05] >> 7);
    RSP.DMEM[addr + HES(0x008)] = (unsigned char)(VR[vt][04] >> 7);
    RSP.DMEM[addr + HES(0x006)] = (unsigned char)(VR[vt][03] >> 7);
    RSP.DMEM[addr + HES(0x004)] = (unsigned char)(VR[vt][02] >> 7);
    RSP.DMEM[addr + HES(0x002)] = (unsigned char)(VR[vt][01] >> 7);
    RSP.DMEM[addr + HES(0x000)] = (unsigned char)(VR[vt][00] >> 7);
    return;
}
static void SFV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    addr = (SR[base] + 16*offset) & 0x00000FFF;
    addr &= 0x00000FF3;
    addr ^= BES(00);
    switch (e)
    {
        case 0x0:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][03] >> 7);
            return;
        case 0x8:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][07] >> 7);
            return;
        default:
            message("SFV\nIllegal element.", 3);
            return;
    }
}

/*
 * Group IV vector loads and stores:
 * QV and RV
 */
INLINE static void LQV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element; /* Boss Game Studios illegal elements */

    if (e & 0x1)
    {
        message("LQV\nOdd element.", 3);
        return;
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x00000001)
    {
        message("LQV\nOdd addr.", 3);
        return;
    }
    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2) /* mistake in SGI patent regarding LQV */
    {
        case 0x0/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0xC) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xE) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0x2/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xC) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0x4/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0x6/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0x8/2: /* "Resident Evil 2" cinematics and Boss Game Studios */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0xA/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0xC/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
        case 0xE/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            return;
    }
}
static void LRV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0)
    {
        message("LRV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x00000001)
    {
        message("LRV\nOdd addr.", 3);
        return;
    }
    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2)
    {
        case 0xE/2:
            VR[vt][01] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][02] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x00C));
            return;
        case 0xC/2:
            VR[vt][02] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x00A));
            return;
        case 0xA/2:
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x008));
            return;
        case 0x8/2:
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x006));
            return;
        case 0x6/2:
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x004));
            return;
        case 0x4/2:
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x002));
            return;
        case 0x2/2:
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x000));
            return;
        case 0x0/2:
            return;
    }
}
INLINE static void SQV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (e != 0x0)
    { /* happens with "Mia Hamm Soccer 64" */
        register int i;

        for (i = 0; i < 16 - addr%16; i++)
            RSP.DMEM[BES((addr + i) & 0xFFF)] = VR_B(vt, (e + i) & 0xF);
        return;
    }
    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b)
    {
        case 00:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][07];
            return;
        case 02:
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][06];
            return;
        case 04:
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][05];
            return;
        case 06:
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][04];
            return;
        default:
            message("SQV\nWeird addr.", 3);
            return;
    }
}
static void SRV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0)
    {
        message("SRV\nIllegal element.", 3);
        return;
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x00000001)
    {
        message("SRV\nOdd addr.", 3);
        return;
    }
    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2)
    {
        case 0xE/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][07];
            return;
        case 0xC/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][07];
            return;
        case 0xA/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][07];
            return;
        case 0x8/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][07];
            return;
        case 0x6/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][07];
            return;
        case 0x4/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][07];
            return;
        case 0x2/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][07];
            return;
        case 0x0/2:
            return;
    }
}

/*
 * Group V vector loads and stores
 * TV and SWV (As of RCP implementation, LTWV opcode was undesired.)
 */
INLINE static void LTV(int vt, int element, int offset, int base)
{
    register int i;
    register uint32_t addr;
    const int e = element;

    if (e & 1)
    {
        message("LTV\nIllegal element.", 3);
        return;
    }
    if (vt & 07)
    {
        message("LTV\nUncertain case!", 3);
        return; /* For LTV I am not sure; for STV I have an idea. */
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x0000000F)
    {
        message("LTV\nIllegal addr.", 3);
        return;
    }
    for (i = 0; i < 8; i++) /* SGI screwed LTV up on N64.  See STV instead. */
        VR[vt+i][(-e/2 + i) & 07] = *(short *)(RSP.DMEM + addr + HES(2*i));
    return;
}
NOINLINE static void SWV(int vt, int element, int offset, int base)
{ /* Dummy implementation only:  Do any games execute this? */
    char debugger[32];

    sprintf(debugger, "%s     $v%i[0x%X], 0x%03X($%i)", "SWV",
        vt, element, offset & 0xFFF, base);
    message(debugger, 3);
    return;
}
INLINE static void STV(int vt, int element, int offset, int base)
{
    register int i;
    register uint32_t addr;
    const int e = element;

    if (e & 1)
    {
        message("STV\nIllegal element.", 3);
        return;
    }
    if (vt & 07)
    {
        message("STV\nUncertain case!", 2);
        return; /* vt &= 030; */
    }
    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x0000000F)
    {
        message("STV\nIllegal addr.", 3);
        return;
    }
    for (i = 0; i < 8; i++)
        *(short *)(RSP.DMEM + addr + HES(2*i)) = VR[vt + (e/2 + i)%8][i];
    return;
}

/*** Modern pseudo-operations (not real instructions, but nice shortcuts) ***/
void ULW(int rd, uint32_t addr)
{ /* "Unaligned Load Word" */
    if (addr & 0x00000001)
    {
        SR_temp.B[03] = RSP.DMEM[BES(addr)];
        addr = (addr + 0x001) & 0xFFF;
        SR_temp.B[02] = RSP.DMEM[BES(addr)];
        addr = (addr + 0x001) & 0xFFF;
        SR_temp.B[01] = RSP.DMEM[BES(addr)];
        addr = (addr + 0x001) & 0xFFF;
        SR_temp.B[00] = RSP.DMEM[BES(addr)];
    }
    else /* addr & 0x00000002 */
    {
        SR_temp.H[01] = *(short *)(RSP.DMEM + addr - HES(0x000));
        addr = (addr + 0x002) & 0xFFF;
        SR_temp.H[00] = *(short *)(RSP.DMEM + addr + HES(0x000));
    }
    SR[rd] = SR_temp.W;
 /* SR[0] = 0x00000000; */
    return;
}
void USW(int rs, uint32_t addr)
{ /* "Unaligned Store Word" */
    SR_temp.W = SR[rs];
    if (addr & 0x00000001)
    {
        RSP.DMEM[BES(addr)] = SR_temp.B[03];
        addr = (addr + 0x001) & 0xFFF;
        RSP.DMEM[BES(addr)] = SR_temp.B[02];
        addr = (addr + 0x001) & 0xFFF;
        RSP.DMEM[BES(addr)] = SR_temp.B[01];
        addr = (addr + 0x001) & 0xFFF;
        RSP.DMEM[BES(addr)] = SR_temp.B[00];
    }
    else /* addr & 0x00000002 */
    {
        *(short *)(RSP.DMEM + addr - HES(0x000)) = SR_temp.H[01];
        addr = (addr + 0x002) & 0xFFF;
        *(short *)(RSP.DMEM + addr + HES(0x000)) = SR_temp.H[00];
    }
    return;
}

#endif
