/*
 * z64
 *
 * Copyright (C) 2007  ziggy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
**/

#ifndef _RSP_H_
#define _RSP_H_

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "z64.h"
#include <math.h>       // sqrt
#include <stdlib.h>
#include <stdio.h>
#include <string.h>    // memset

#define INLINE inline

extern void log(m64p_msg_level level, const char *msg, ...);

/* defined in systems/n64.c */
#define rdram ((UINT32*)z64_rspinfo.RDRAM)
//extern UINT32 *rdram;
#define rsp_imem ((UINT32*)z64_rspinfo.IMEM)
//extern UINT32 *rsp_imem;
#define rsp_dmem ((UINT32*)z64_rspinfo.DMEM)
//extern UINT32 *rsp_dmem;
//extern void dp_full_sync(void);

#define vi_origin (*(UINT32*)z64_rspinfo.VI_ORIGIN_REG)
//extern UINT32 vi_origin;
#define vi_width (*(UINT32*)z64_rspinfo.VI_WIDTH_REG)
//extern UINT32 vi_width;
#define vi_control (*(UINT32*)z64_rspinfo.VI_STATUS_REG)
//extern UINT32 vi_control;

#define dp_start (*(UINT32*)z64_rspinfo.DPC_START_REG)
//extern UINT32 dp_start;
#define dp_end (*(UINT32*)z64_rspinfo.DPC_END_REG)
//extern UINT32 dp_end;
#define dp_current (*(UINT32*)z64_rspinfo.DPC_CURRENT_REG)
//extern UINT32 dp_current;
#define dp_status (*(UINT32*)z64_rspinfo.DPC_STATUS_REG)
//extern UINT32 dp_status;

#define sp_pc (*(UINT32*)z64_rspinfo.SP_PC_REG)

typedef union
{
    UINT64 d[2];
    UINT32 l[4];
    INT16 s[8];
    UINT8 b[16];
} VECTOR_REG;

typedef union
{
    INT64 q;
    INT32 l[2];
    INT16 w[4];
} ACCUMULATOR_REG;

typedef struct
{
    // vectors first , need to be memory aligned for sse
    VECTOR_REG v[32];
    ACCUMULATOR_REG accum[8];

    //UINT32 pc;
    UINT32 r[32];
    UINT16 flag[4];

    INT32 square_root_res;
    INT32 square_root_high;
    INT32 reciprocal_res;
    INT32 reciprocal_high;

    UINT32 ppc;
    UINT32 nextpc;

    UINT32 step_count;

    int inval_gen;

    RSP_INFO ext;
} RSP_REGS;

#define z64_rspinfo (rsp.ext)

int rsp_execute(int cycles);
void rsp_reset(void);
void rsp_init(RSP_INFO info);
offs_t rsp_dasm_one(char *buffer, offs_t pc, UINT32 op);

extern UINT32 sp_read_reg(UINT32 reg);
extern void sp_write_reg(UINT32 reg, UINT32 data);
// extern READ32_HANDLER( n64_dp_reg_r );
// extern WRITE32_HANDLER( n64_dp_reg_w );

#define RSREG           ((op >> 21) & 0x1f)
#define RTREG           ((op >> 16) & 0x1f)
#define RDREG           ((op >> 11) & 0x1f)
#define SHIFT           ((op >> 6) & 0x1f)

#define RSVAL           (rsp.r[RSREG])
#define RTVAL           (rsp.r[RTREG])
#define RDVAL           (rsp.r[RDREG])

#define _RSREG(op)      ((op >> 21) & 0x1f)
#define _RTREG(op)      ((op >> 16) & 0x1f)
#define _RDREG(op)      ((op >> 11) & 0x1f)
#define _SHIFT(op)      ((op >> 6) & 0x1f)

#define _RSVAL(op)      (rsp.r[_RSREG(op)])
#define _RTVAL(op)      (rsp.r[_RTREG(op)])
#define _RDVAL(op)      (rsp.r[_RDREG(op)])

#define SIMM16          ((INT32)(INT16)(op))
#define UIMM16          ((UINT16)(op))
#define UIMM26          (op & 0x03ffffff)

#define _SIMM16(op)     ((INT32)(INT16)(op))
#define _UIMM16(op)     ((UINT16)(op))
#define _UIMM26(op)     (op & 0x03ffffff)


/*#define _JUMP(pc) \
if ((GENTRACE("_JUMP %x\n", rsp.nextpc), 1) && rsp_jump(rsp.nextpc)) return 1; \
if (rsp.inval_gen || sp_pc != pc+8) return 0;
*/

#define CARRY_FLAG(x)                   ((rsp.flag[0] & (1 << ((x)))) ? 1 : 0)
#define CLEAR_CARRY_FLAGS()             { rsp.flag[0] &= ~0xff; }
#define SET_CARRY_FLAG(x)               { rsp.flag[0] |= (1 << ((x))); }
#define CLEAR_CARRY_FLAG(x)             { rsp.flag[0] &= ~(1 << ((x))); }

#define COMPARE_FLAG(x)                 ((rsp.flag[1] & (1 << ((x)))) ? 1 : 0)
#define CLEAR_COMPARE_FLAGS()           { rsp.flag[1] &= ~0xff; }
#define SET_COMPARE_FLAG(x)             { rsp.flag[1] |= (1 << ((x))); }
#define CLEAR_COMPARE_FLAG(x)           { rsp.flag[1] &= ~(1 << ((x))); }

#define ZERO_FLAG(x)                    ((rsp.flag[0] & (1 << (8+(x)))) ? 1 : 0)
#define CLEAR_ZERO_FLAGS()              { rsp.flag[0] &= ~0xff00; }
#define SET_ZERO_FLAG(x)                { rsp.flag[0] |= (1 << (8+(x))); }
#define CLEAR_ZERO_FLAG(x)              { rsp.flag[0] &= ~(1 << (8+(x))); }

//#define rsp z64_rsp // to avoid namespace collision with other libs
extern RSP_REGS rsp __attribute__((aligned(16)));


//#define ROPCODE(pc)           cpu_readop32(pc)
#define ROPCODE(pc) program_read_dword_32be(pc | 0x1000)

INLINE UINT8 program_read_byte_32be(UINT32 address)
{
    return ((UINT8*)z64_rspinfo.DMEM)[(address&0x1fff)^3];
}

INLINE UINT16 program_read_word_32be(UINT32 address)
{
    return ((UINT16*)z64_rspinfo.DMEM)[((address&0x1fff)>>1)^1];
}

INLINE UINT32 program_read_dword_32be(UINT32 address)
{
    return ((UINT32*)z64_rspinfo.DMEM)[(address&0x1fff)>>2];
}

INLINE void program_write_byte_32be(UINT32 address, UINT8 data)
{
    ((UINT8*)z64_rspinfo.DMEM)[(address&0x1fff)^3] = data;
}

INLINE void program_write_word_32be(UINT32 address, UINT16 data)
{
    ((UINT16*)z64_rspinfo.DMEM)[((address&0x1fff)>>1)^1] = data;
}

INLINE void program_write_dword_32be(UINT32 address, UINT32 data)
{
    ((UINT32*)z64_rspinfo.DMEM)[(address&0x1fff)>>2] = data;
}

INLINE UINT8 READ8(UINT32 address)
{
    address = 0x04000000 | (address & 0xfff);
    return program_read_byte_32be(address);
}

INLINE UINT16 READ16(UINT32 address)
{
    address = 0x04000000 | (address & 0xfff);

    if (address & 1)
    {
        //osd_die("RSP: READ16: unaligned %08X at %08X\n", address, rsp.ppc);
        return ((program_read_byte_32be(address+0) & 0xff) << 8) | (program_read_byte_32be(address+1) & 0xff);
    }

    return program_read_word_32be(address);
}

INLINE UINT32 READ32(UINT32 address)
{
    address = 0x04000000 | (address & 0xfff);

    if (address & 3)
    {
        //fatalerror("RSP: READ32: unaligned %08X at %08X\n", address, rsp.ppc);
        return ((program_read_byte_32be(address + 0) & 0xff) << 24) |
            ((program_read_byte_32be(address + 1) & 0xff) << 16) |
            ((program_read_byte_32be(address + 2) & 0xff) << 8) |
            ((program_read_byte_32be(address + 3) & 0xff) << 0);
    }

    return program_read_dword_32be(address);
}


INLINE void WRITE8(UINT32 address, UINT8 data)
{
    address = 0x04000000 | (address & 0xfff);
    program_write_byte_32be(address, data);
}

INLINE void WRITE16(UINT32 address, UINT16 data)
{
    address = 0x04000000 | (address & 0xfff);

    if (address & 1)
    {
        //fatalerror("RSP: WRITE16: unaligned %08X, %04X at %08X\n", address, data, rsp.ppc);
        program_write_byte_32be(address + 0, (data >> 8) & 0xff);
        program_write_byte_32be(address + 1, (data >> 0) & 0xff);
        return;
    }

    program_write_word_32be(address, data);
}

INLINE void WRITE32(UINT32 address, UINT32 data)
{
    address = 0x04000000 | (address & 0xfff);

    if (address & 3)
    {
        //fatalerror("RSP: WRITE32: unaligned %08X, %08X at %08X\n", address, data, rsp.ppc);
        program_write_byte_32be(address + 0, (data >> 24) & 0xff);
        program_write_byte_32be(address + 1, (data >> 16) & 0xff);
        program_write_byte_32be(address + 2, (data >> 8) & 0xff);
        program_write_byte_32be(address + 3, (data >> 0) & 0xff);
        return;
    }

    program_write_dword_32be(address, data);
}

int rsp_jump(int pc);
void rsp_invalidate(int begin, int len);
void rsp_execute_one(UINT32 op);



#define JUMP_ABS(addr)                  { rsp.nextpc = 0x04001000 | (((addr) << 2) & 0xfff); }
#define JUMP_ABS_L(addr,l)              { rsp.nextpc = 0x04001000 | (((addr) << 2) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define JUMP_REL(offset)                { rsp.nextpc = 0x04001000 | ((sp_pc + ((offset) << 2)) & 0xfff); }
#define JUMP_REL_L(offset,l)            { rsp.nextpc = 0x04001000 | ((sp_pc + ((offset) << 2)) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define JUMP_PC(addr)                   { rsp.nextpc = 0x04001000 | ((addr) & 0xfff); }
#define JUMP_PC_L(addr,l)               { rsp.nextpc = 0x04001000 | ((addr) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define LINK(l) rsp.r[l] = sp_pc + 4

#define VDREG                           ((op >> 6) & 0x1f)
#define VS1REG                          ((op >> 11) & 0x1f)
#define VS2REG                          ((op >> 16) & 0x1f)
#define EL                              ((op >> 21) & 0xf)

#define VREG_B(reg, offset)             rsp.v[(reg)].b[((offset)^1)]
#define VREG_S(reg, offset)             rsp.v[(reg)].s[((offset))]
#define VREG_L(reg, offset)             rsp.v[(reg)].l[((offset))]

#define VEC_EL_1(x,z)                   (z) //(vector_elements_1[(x)][(z)])
#define VEC_EL_2(x,z)                   (vector_elements_2[(x)][(z)])

#define ACCUM(x)                        rsp.accum[((x))].q
#define ACCUM_H(x)                      rsp.accum[((x))].w[3]
#define ACCUM_M(x)                      rsp.accum[((x))].w[2]
#define ACCUM_L(x)                      rsp.accum[((x))].w[1]

void unimplemented_opcode(UINT32 op);
void handle_vector_ops(UINT32 op);
UINT32 get_cop0_reg(int reg);
void set_cop0_reg(int reg, UINT32 data);
void handle_lwc2(UINT32 op);
void handle_swc2(UINT32 op);

INLINE UINT32 n64_dp_reg_r(UINT32 offset, UINT32 dummy)
{
    switch (offset)
    {
    case 0x00/4:            // DP_START_REG
        return dp_start;

    case 0x04/4:            // DP_END_REG
        return dp_end;

    case 0x08/4:            // DP_CURRENT_REG
        return dp_current;

    case 0x0c/4:            // DP_STATUS_REG
        return dp_status;

    case 0x10/4:            // DP_CLOCK_REG
        return *z64_rspinfo.DPC_CLOCK_REG;

    default:
        log(M64MSG_WARNING, "dp_reg_r: %08X\n", offset);
        break;
    }

    return 0;
}
INLINE void n64_dp_reg_w(UINT32 offset, UINT32 data, UINT32 dummy)
{
    switch (offset)
    {
    case 0x00/4:            // DP_START_REG
        dp_start = data;
        dp_current = dp_start;
        break;

    case 0x04/4:            // DP_END_REG
        dp_end = data;
        //rdp_process_list();
        if (dp_end<dp_start)//three lines for debugging
        {
            log(M64MSG_INFO, "RDP End < RDP Start!");//happens in Stunt Racer with Ville Linde's sp_dma
            break;
        }
        if (dp_end==dp_start)
            break;
        if (z64_rspinfo.ProcessRdpList != NULL) { z64_rspinfo.ProcessRdpList(); }
        break;

    case 0x0c/4:            // DP_STATUS_REG
        if (data & 0x00000001)  dp_status &= ~DP_STATUS_XBUS_DMA;
        if (data & 0x00000002)  dp_status |= DP_STATUS_XBUS_DMA;
        if (data & 0x00000004)  dp_status &= ~DP_STATUS_FREEZE;
        if (data & 0x00000008)  dp_status |= DP_STATUS_FREEZE;
        if (data & 0x00000010)  dp_status &= ~DP_STATUS_FLUSH;
        if (data & 0x00000020)  dp_status |= DP_STATUS_FLUSH;
        break;

    default:
        log(M64MSG_WARNING, "dp_reg_w: %08X, %08X\n", data, offset);
        break;
    }
}

#define INTEL86
#if defined(INTEL86) && defined __GNUC__ && __GNUC__ >= 2
static __inline__ unsigned long long RDTSC(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
// inline volatile uint64_t RDTSC() {
//    register uint64_t TSC asm("eax");
//    asm volatile (".byte 15, 49" : : : "eax", "edx");
//    return TSC;
// }
// #define RDTSC1(n) __asm__ __volatile__("rdtsc" : "=a" (n): )
// #define RDTSC2(n) __asm__ __volatile__ ("rdtsc\nmov %%edx,%%eax" : "=a" (n): )
// inline void RDTSC(uint64_t& a) { uint32_t b, c; RDTSC1(b); RDTSC2(c);
//  a = (((uint64_t)c)<<32) | b; }
#elif defined(INTEL86) && defined WIN32
#define rdtsc __asm __emit 0fh __asm __emit 031h
#define cpuid __asm __emit 0fh __asm __emit 0a2h
inline uint64_t RDTSC() {
    static uint32_t temp;
    __asm {
        push edx
            push eax
            rdtsc
            mov temp, eax
            pop eax
            pop edx
    }
    return temp;
}
#else
#define RDTSC(n) n=0
#endif

#ifdef GENTRACE
#undef GENTRACE
#include <stdarg.h>
inline void GENTRACE(const char * s, ...) {
    va_list ap;
    va_start(ap, s);
    vfprintf(stderr, s, ap);
    va_end(ap);
    int i;
    for (i=0; i<32; i++)
        fprintf(stderr, "r%d=%x ", i, rsp.r[i]);
    fprintf(stderr, "\n");
    for (i=0; i<32; i++)
        fprintf(stderr, "v%d=%x %x %x %x %x %x %x %x ", i,
        (UINT16)rsp.v[i].s[0],
        (UINT16)rsp.v[i].s[1],
        (UINT16)rsp.v[i].s[2],
        (UINT16)rsp.v[i].s[3],
        (UINT16)rsp.v[i].s[4],
        (UINT16)rsp.v[i].s[5],
        (UINT16)rsp.v[i].s[6],
        (UINT16)rsp.v[i].s[7]
    );
    fprintf(stderr, "\n");

    fprintf(stderr, "f0=%x f1=%x f2=%x f3=%x\n", rsp.flag[0],
        rsp.flag[1],rsp.flag[2],rsp.flag[3]);
}
#endif
//#define GENTRACE printf
//#define GENTRACE

#ifdef RSPTIMING
extern uint64_t rsptimings[512];
extern int rspcounts[512];
#endif

#endif // ifndef _RSP_H_
