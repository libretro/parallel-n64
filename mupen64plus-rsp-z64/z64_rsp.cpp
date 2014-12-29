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

/*
Nintendo/SGI Reality Signal Processor (RSP) emulator

Written by Ville Linde
*/
// #include "z64.h"
#include "rsp.h"
#include "rsp_opinfo.h"
#include <math.h>       // sqrt
#include <assert.h>
#include <string.h>

#define INLINE inline

#define LOG_INSTRUCTION_EXECUTION               0
#define SAVE_DISASM                             0
#define SAVE_DMEM                               0

#define PRINT_VECREG(x)         printf("V%d: %04X|%04X|%04X|%04X|%04X|%04X|%04X|%04X\n", (x), \
    (UINT16)VREG_S((x),0), (UINT16)VREG_S((x),1), \
    (UINT16)VREG_S((x),2), (UINT16)VREG_S((x),3), \
    (UINT16)VREG_S((x),4), (UINT16)VREG_S((x),5), \
    (UINT16)VREG_S((x),6), (UINT16)VREG_S((x),7))


extern offs_t rsp_dasm_one(char *buffer, offs_t pc, UINT32 op);

#if LOG_INSTRUCTION_EXECUTION
static FILE *exec_output;
#endif


// INLINE void sp_set_status(UINT32 status)
// {
//      if (status & 0x1)
//      {
//              cpu_trigger(6789);

//              cpunum_set_input_line(1, INPUT_LINE_HALT, ASSERT_LINE);
//              rsp_sp_status |= SP_STATUS_HALT;
//      }
//      if (status & 0x2)
//      {
//              rsp_sp_status |= SP_STATUS_BROKE;

//              if (rsp_sp_status & SP_STATUS_INTR_BREAK)
//              {
//                      signal_rcp_interrupt(SP_INTERRUPT);
//              }
//      }
// }


#if 0
enum
{
    RSP_PC = 1,
    RSP_R0,
    RSP_R1,
    RSP_R2,
    RSP_R3,
    RSP_R4,
    RSP_R5,
    RSP_R6,
    RSP_R7,
    RSP_R8,
    RSP_R9,
    RSP_R10,
    RSP_R11,
    RSP_R12,
    RSP_R13,
    RSP_R14,
    RSP_R15,
    RSP_R16,
    RSP_R17,
    RSP_R18,
    RSP_R19,
    RSP_R20,
    RSP_R21,
    RSP_R22,
    RSP_R23,
    RSP_R24,
    RSP_R25,
    RSP_R26,
    RSP_R27,
    RSP_R28,
    RSP_R29,
    RSP_R30,
    RSP_R31,
};
#endif


#ifdef RSPTIMING
uint64_t rsptimings[512];
int rspcounts[512];
#endif


#define JUMP_ABS(addr)                  { rsp.nextpc = 0x04001000 | (((addr) << 2) & 0xfff); }
#define JUMP_ABS_L(addr,l)              { rsp.nextpc = 0x04001000 | (((addr) << 2) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define JUMP_REL(offset)                { rsp.nextpc = 0x04001000 | ((sp_pc + ((offset) << 2)) & 0xfff); }
#define JUMP_REL_L(offset,l)            { rsp.nextpc = 0x04001000 | ((sp_pc + ((offset) << 2)) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define JUMP_PC(addr)                   { rsp.nextpc = 0x04001000 | ((addr) & 0xfff); }
#define JUMP_PC_L(addr,l)               { rsp.nextpc = 0x04001000 | ((addr) & 0xfff); rsp.r[l] = sp_pc + 4; }
#define LINK(l) rsp.r[l] = sp_pc + 4


#define VDREG           ((op >> 6) & 0x1f)
#define VS1REG          ((op >> 11) & 0x1f)
#define VS2REG          ((op >> 16) & 0x1f)
#define EL              ((op >> 21) & 0xf)

#define S_VREG_B(offset)            (((15 - (offset)) & 0x07) << 3)
#define S_VREG_S(offset)            (((7 - (offset)) & 0x03) << 4)
#define S_VREG_L(offset)            (((3 - (offset)) & 0x01) << 5)

#define M_VREG_B(offset)            ((UINT64)0x00FF << S_VREG_B(offset))
#define M_VREG_S(offset)            ((UINT64)0x0000FFFFul << S_VREG_S(offset))
#define M_VREG_L(offset)            ((UINT64)0x00000000FFFFFFFFull << S_VREG_L(offset))

#define R_VREG_B(reg, offset)       ((rsp.v[(reg)].d[(15 - (offset)) >> 3] >> S_VREG_B(offset)) & 0x00FF)
#define R_VREG_S(reg, offset)       (INT16)((rsp.v[(reg)].d[(7 - (offset)) >> 2] >> S_VREG_S(offset)) & 0x0000FFFFul)
#define R_VREG_L(reg, offset)       ((rsp.v[(reg)].d[(3 - (offset)) >> 1] >> S_VREG_L(offset)) & 0x00000000FFFFFFFFull)

#define W_VREG_B(reg, offset, val)  (rsp.v[(reg)].d[(15 - (offset)) >> 3] = (rsp.v[(reg)].d[(15 - (offset)) >> 3] & ~M_VREG_B(offset)) | (M_VREG_B(offset) & ((UINT64)(val) << S_VREG_B(offset))))
#define W_VREG_S(reg, offset, val)  (rsp.v[(reg)].d[(7 - (offset)) >> 2] = (rsp.v[(reg)].d[(7 - (offset)) >> 2] & ~M_VREG_S(offset)) | (M_VREG_S(offset) & ((UINT64)(val) << S_VREG_S(offset))))
#define W_VREG_L(reg, offset, val)  (rsp.v[(reg)].d[(3 - (offset)) >> 1] = (rsp.v[(reg)].d[(3 - (offset)) >> 1] & ~M_VREG_L(offset)) | (M_VREG_L(offset) & ((UINT64)(val) << S_VREG_L(offset))))


#define VEC_EL_1(x,z)           (z)
#define VEC_EL_2(x,z)           (vector_elements_2[(x)][(z)])

#define ACCUM(x)                rsp.accum[((x))].q

#define S_ACCUM_H               (3 << 4)
#define S_ACCUM_M               (2 << 4)
#define S_ACCUM_L               (1 << 4)

#define M_ACCUM_H               (((INT64)0x0000FFFF) << S_ACCUM_H)
#define M_ACCUM_M               (((INT64)0x0000FFFF) << S_ACCUM_M)
#define M_ACCUM_L               (((INT64)0x0000FFFF) << S_ACCUM_L)

#define R_ACCUM_H(x)            ((INT16)((ACCUM(x) >> S_ACCUM_H) & 0x00FFFF))
#define R_ACCUM_M(x)            ((INT16)((ACCUM(x) >> S_ACCUM_M) & 0x00FFFF))
#define R_ACCUM_L(x)            ((INT16)((ACCUM(x) >> S_ACCUM_L) & 0x00FFFF))

#define W_ACCUM_H(x, y)         (ACCUM(x) = (ACCUM(x) & ~M_ACCUM_H) | (M_ACCUM_H & ((INT64)(y) << S_ACCUM_H)))
#define W_ACCUM_M(x, y)         (ACCUM(x) = (ACCUM(x) & ~M_ACCUM_M) | (M_ACCUM_M & ((INT64)(y) << S_ACCUM_M)))
#define W_ACCUM_L(x, y)         (ACCUM(x) = (ACCUM(x) & ~M_ACCUM_L) | (M_ACCUM_L & ((INT64)(y) << S_ACCUM_L)))



RSP_REGS rsp;
static int rsp_icount;
// RSP Interface

#define rsp_sp_status (*(UINT32*)z64_rspinfo.SP_STATUS_REG)
#define sp_mem_addr (*(UINT32*)z64_rspinfo.SP_MEM_ADDR_REG)
#define sp_dram_addr (*(UINT32*)z64_rspinfo.SP_DRAM_ADDR_REG)
#define sp_semaphore (*(UINT32*)z64_rspinfo.SP_SEMAPHORE_REG)

#define sp_dma_rlength (*(UINT32*)z64_rspinfo.SP_RD_LEN_REG)
#define sp_dma_wlength (*(UINT32*)z64_rspinfo.SP_WR_LEN_REG)

INT32 sp_dma_length;

/*****************************************************************************/

UINT32 get_cop0_reg(int reg)
{
    if (reg >= 0 && reg < 8)
    {
        return sp_read_reg(reg);
    }
    else if (reg >= 8 && reg < 16)
    {
        return n64_dp_reg_r(reg - 8, 0x00000000);
    }
    else
    {
        log(M64MSG_ERROR, "RSP: get_cop0_reg: %d", reg);
	return ~0;
    }
}

void set_cop0_reg(int reg, UINT32 data)
{
    if (reg >= 0 && reg < 8)
    {
        sp_write_reg(reg, data);
    }
    else if (reg >= 8 && reg < 16)
    {
        n64_dp_reg_w(reg - 8, data, 0x00000000);
    }
    else
    {
        log(M64MSG_ERROR, "RSP: set_cop0_reg: %d, %08X\n", reg, data);
    }
}

static int got_unimp;
void unimplemented_opcode(UINT32 op)
{
    got_unimp = 1;
#ifdef MAME_DEBUG
    char string[200];
    rsp_dasm_one(string, rsp.ppc, op);
    printf("%08X: %s\n", rsp.ppc, string);
#endif

#if SAVE_DISASM
    {
        char string[200];
        int i;
        FILE *dasm;
        dasm = fopen("rsp_disasm.txt", "wt");

        for (i=0; i < 0x1000; i+=4)
        {
            UINT32 opcode = ROPCODE(0x04001000 + i);
            rsp_dasm_one(string, 0x04001000 + i, opcode);
            fprintf(dasm, "%08X: %08X   %s\n", 0x04001000 + i, opcode, string);
        }
        fclose(dasm);
    }
#endif
#if SAVE_DMEM
    {
        int i;
        FILE *dmem;
        dmem = fopen("rsp_dmem.bin", "wb");

        for (i=0; i < 0x1000; i++)
        {
            fputc(READ8(0x04000000 + i), dmem);
        }
        fclose(dmem);
    }
#endif

    log(M64MSG_ERROR, "RSP: unknown opcode %02X (%d) (%08X) at %08X\n", op >> 26, op >> 26, op, rsp.ppc);
}

/*****************************************************************************/

const int vector_elements_1[16][8] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7 },             // none
    { 0, 1, 2, 3, 4, 5, 6 ,7 },             // ???
    { 1, 3, 5, 7, 0, 2, 4, 6 },             // 0q
    { 0, 2, 4, 6, 1, 3, 5, 7 },             // 1q
    { 1, 2, 3, 5, 6, 7, 0, 4 },             // 0h
    { 0, 2, 3, 4, 6, 7, 1, 5 },             // 1h
    { 0, 1, 3, 4, 5, 7, 2, 6 },             // 2h
    { 0, 1, 2, 4, 5, 6, 3, 7 },             // 3h
    { 1, 2, 3, 4, 5, 6, 7, 0 },             // 0
    { 0, 2, 3, 4, 5, 6, 7, 1 },             // 1
    { 0, 1, 3, 4, 5, 6, 7, 2 },             // 2
    { 0, 1, 2, 4, 5, 6, 7, 3 },             // 3
    { 0, 1, 2, 3, 5, 6, 7, 4 },             // 4
    { 0, 1, 2, 3, 4, 6, 7, 5 },             // 5
    { 0, 1, 2, 3, 4, 5, 7, 6 },             // 6
    { 0, 1, 2, 3, 4, 5, 6, 7 },             // 7
};

const int vector_elements_2[16][8] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7 },             // none
    { 0, 1, 2, 3, 4, 5, 6, 7 },             // ???
    { 0, 0, 2, 2, 4, 4, 6, 6 },             // 0q
    { 1, 1, 3, 3, 5, 5, 7, 7 },             // 1q
    { 0, 0, 0, 0, 4, 4, 4, 4 },             // 0h
    { 1, 1, 1, 1, 5, 5, 5, 5 },             // 1h
    { 2, 2, 2, 2, 6, 6, 6, 6 },             // 2h
    { 3, 3, 3, 3, 7, 7, 7, 7 },             // 3h
    { 0, 0, 0, 0, 0, 0, 0, 0 },             // 0
    { 1, 1, 1, 1, 1, 1, 1, 1 },             // 1
    { 2, 2, 2, 2, 2, 2, 2, 2 },             // 2
    { 3, 3, 3, 3, 3, 3, 3, 3 },             // 3
    { 4, 4, 4, 4, 4, 4, 4, 4 },             // 4
    { 5, 5, 5, 5, 5, 5, 5, 5 },             // 5
    { 6, 6, 6, 6, 6, 6, 6, 6 },             // 6
    { 7, 7, 7, 7, 7, 7, 7, 7 },             // 7
};

void rsp_init(RSP_INFO info)
{
#if LOG_INSTRUCTION_EXECUTION
    exec_output = fopen("rsp_execute.txt", "wt");
#endif

    memset(&rsp, 0, sizeof(rsp));
    rsp.ext = info;

    sp_pc = 0; //0x4001000;
    rsp.nextpc = ~0U;
    //rsp_invalidate(0, 0x1000);
    rsp.step_count=0;
}

void rsp_reset(void)
{
    rsp.nextpc = ~0U;
}

void handle_lwc2(UINT32 op)
{
    int i, end;
    UINT32 ea;
    int dest = (op >> 16) & 0x1f;
    int base = (op >> 21) & 0x1f;
    int index = (op >> 7) & 0xf;
    int offset = (op & 0x7f);
    if (offset & 0x40)
        offset |= 0xffffffc0;

    switch ((op >> 11) & 0x1f)
    {
    case 0x00:              /* LBV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00000 | IIII | Offset |
            // --------------------------------------------------
            //
            // Load 1 byte to vector byte index

            ea = (base) ? rsp.r[base] + offset : offset;
            VREG_B(dest, index) = READ8(ea);
            break;
        }
    case 0x01:              /* LSV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00001 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads 2 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 2) : (offset * 2);

            end = index + 2;

            // VP need mask i and ea ?
            for (i=index; i < end; i++)
            {
                VREG_B(dest, i) = READ8(ea);
                ea++;
            }
            break;
        }
    case 0x02:              /* LLV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00010 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads 4 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 4) : (offset * 4);

            end = index + 4;

            // VP need mask i and ea ?
            for (i=index; i < end; i++)
            {
                VREG_B(dest, i) = READ8(ea);
                ea++;
            }
            break;
        }
    case 0x03:              /* LDV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads 8 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);

            end = index + 8;

            // VP need mask i and ea ?
            for (i=index; i < end; i++)
            {
                VREG_B(dest, i) = READ8(ea);
                ea++;
            }
            break;
        }
    case 0x04:              /* LQV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00100 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads up to 16 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            end = index + (16 - (ea & 0xf));
            if (end > 16) end = 16;
            for (i=index; i < end; i++)
            {
                VREG_B(dest, i) = READ8(ea);
                ea++;
            }
            break;
        }
    case 0x05:              /* LRV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00101 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores up to 16 bytes starting from right side until 16-byte boundary

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            index = 16 - ((ea & 0xf) - index);
            end = 16;
            ea &= ~0xf;
            //assert(index == 0);

            for (i=index; i < end; i++)
            {
                VREG_B(dest, i) = READ8(ea);
                ea++;
            }
            break;
        }
    case 0x06:              /* LPV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00110 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads a byte as the upper 8 bits of each element

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);

            for (i=0; i < 8; i++)
            {
                VREG_S(dest, i) = READ8(ea + (((16-index) + i) & 0xf)) << 8;
            }
            break;
        }
    case 0x07:              /* LUV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 00111 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads a byte as the bits 14-7 of each element

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);

            for (i=0; i < 8; i++)
            {
                VREG_S(dest, i) = READ8(ea + (((16-index) + i) & 0xf)) << 7;
            }
            break;
        }
    case 0x08:              /* LHV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 01000 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads a byte as the bits 14-7 of each element, with 2-byte stride

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            for (i=0; i < 8; i++)
            {
                VREG_S(dest, i) = READ8(ea + (((16-index) + (i<<1)) & 0xf)) << 7;
            }
            break;
        }
    case 0x09:              /* LFV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 01001 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads a byte as the bits 14-7 of upper or lower quad, with 4-byte stride

            //                      fatalerror("RSP: LFV\n");

            //if (index & 0x7)      fatalerror("RSP: LFV: index = %d at %08X\n", index, rsp.ppc);

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            // not sure what happens if 16-byte boundary is crossed...
            //if ((ea & 0xf) > 0)   fatalerror("RSP: LFV: 16-byte boundary crossing at %08X, recheck this!\n", rsp.ppc);

            end = (index >> 1) + 4;

            for (i=index >> 1; i < end; i++)
            {
                VREG_S(dest, i) = READ8(ea) << 7;
                ea += 4;
            }
            break;
        }
    case 0x0a:              /* LWV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 01010 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads the full 128-bit vector starting from vector byte index and wrapping to index 0
            // after byte index 15

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            // not sure what happens if 16-byte boundary is crossed...
            //if ((ea & 0xf) > 0) fatalerror("RSP: LWV: 16-byte boundary crossing at %08X, recheck this!\n", rsp.ppc);

            end = (16 - index) + 16;

            for (i=(16 - index); i < end; i++)
            {
                VREG_B(dest, i & 0xf) = READ8(ea);
                ea += 4;
            }
            break;
        }
    case 0x0b:              /* LTV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 01011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads one element to maximum of 8 vectors, while incrementing element index

            // FIXME: has a small problem with odd indices

            int element;
            int vs = dest;
            int ve = dest + 8;
            if (ve > 32)
                ve = 32;

            element = 7 - (index >> 1);

            //if (index & 1)        fatalerror("RSP: LTV: index = %d\n", index);

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            ea = ((ea + 8) & ~0xf) + (index & 1);
            for (i=vs; i < ve; i++)
            {
                element = ((8 - (index >> 1) + (i-vs)) << 1);
                VREG_B(i, (element & 0xf)) = READ8(ea);
                VREG_B(i, ((element+1) & 0xf)) = READ8(ea+1);

                ea += 2;
            }
            break;
        }

    default:
        {
            unimplemented_opcode(op);
            break;
        }
    }
}

void handle_swc2(UINT32 op)
{
    int i, end;
    int eaoffset;
    UINT32 ea;
    int dest = (op >> 16) & 0x1f;
    int base = (op >> 21) & 0x1f;
    int index = (op >> 7) & 0xf;
    int offset = (op & 0x7f);
    if (offset & 0x40)
        offset |= 0xffffffc0;

    switch ((op >> 11) & 0x1f)
    {
    case 0x00:              /* SBV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00000 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores 1 byte from vector byte index

            ea = (base) ? rsp.r[base] + offset : offset;
            WRITE8(ea, VREG_B(dest, index));
            break;
        }
    case 0x01:              /* SSV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00001 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores 2 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 2) : (offset * 2);

            end = index + 2;

            for (i=index; i < end; i++)
            {
                WRITE8(ea, VREG_B(dest, i));
                ea++;
            }
            break;
        }
    case 0x02:              /* SLV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00010 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores 4 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 4) : (offset * 4);

            end = index + 4;

            for (i=index; i < end; i++)
            {
                WRITE8(ea, VREG_B(dest, i));
                ea++;
            }
            break;
        }
    case 0x03:              /* SDV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores 8 bytes starting from vector byte index

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);

            end = index + 8;

            for (i=index; i < end; i++)
            {
                WRITE8(ea, VREG_B(dest, i));
                ea++;
            }
            break;
        }
    case 0x04:              /* SQV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00100 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores up to 16 bytes starting from vector byte index until 16-byte boundary

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            end = index + (16 - (ea & 0xf));
            //       if (end != 16)
            //         printf("SQV %d\n", end-index);
            //assert(end == 16);

            for (i=index; i < end; i++)
            {
                WRITE8(ea, VREG_B(dest, i & 0xf));
                ea++;
            }
            break;
        }
    case 0x05:              /* SRV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00101 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores up to 16 bytes starting from right side until 16-byte boundary

            int o;
            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            end = index + (ea & 0xf);
            o = (16 - (ea & 0xf)) & 0xf;
            ea &= ~0xf;
            //       if (end != 16)
            //         printf("SRV %d\n", end-index);
            //assert(end == 16);

            for (i=index; i < end; i++)
            {
                WRITE8(ea, VREG_B(dest, ((i + o) & 0xf)));
                ea++;
            }
            break;
        }
    case 0x06:              /* SPV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00110 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores upper 8 bits of each element

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);
            end = index + 8;

            for (i=index; i < end; i++)
            {
                if ((i & 0xf) < 8)
                {
                    WRITE8(ea, VREG_B(dest, ((i & 0xf) << 1)));
                }
                else
                {
                    WRITE8(ea, VREG_S(dest, (i & 0x7)) >> 7);
                }
                ea++;
            }
            break;
        }
    case 0x07:              /* SUV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 00111 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores bits 14-7 of each element

            ea = (base) ? rsp.r[base] + (offset * 8) : (offset * 8);
            end = index + 8;

            for (i=index; i < end; i++)
            {
                if ((i & 0xf) < 8)
                {
                    WRITE8(ea, VREG_S(dest, (i & 0x7)) >> 7);
                }
                else
                {
                    WRITE8(ea, VREG_B(dest, ((i & 0x7) << 1)));
                }
                ea++;
            }
            break;
        }
    case 0x08:              /* SHV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 01000 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores bits 14-7 of each element, with 2-byte stride

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            for (i=0; i < 8; i++)
            {
                UINT8 d = ((VREG_B(dest, ((index + (i << 1) + 0) & 0xf))) << 1) |
                    ((VREG_B(dest, ((index + (i << 1) + 1) & 0xf))) >> 7);

                WRITE8(ea, d);
                ea += 2;
            }
            break;
        }
    case 0x09:              /* SFV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 01001 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores bits 14-7 of upper or lower quad, with 4-byte stride

            // FIXME: only works for index 0 and index 8

            if (index & 0x7)
                log(M64MSG_WARNING, "SFV: index = %d at %08X\n", index, rsp.ppc);

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            eaoffset = ea & 0xf;
            ea &= ~0xf;

            end = (index >> 1) + 4;

            for (i=index >> 1; i < end; i++)
            {
                WRITE8(ea + (eaoffset & 0xf), VREG_S(dest, i) >> 7);
                eaoffset += 4;
            }
            break;
        }
    case 0x0a:              /* SWV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 01010 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores the full 128-bit vector starting from vector byte index and wrapping to index 0
            // after byte index 15

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            eaoffset = ea & 0xf;
            ea &= ~0xf;

            end = index + 16;

            for (i=index; i < end; i++)
            {
                WRITE8(ea + (eaoffset & 0xf), VREG_B(dest, i & 0xf));
                eaoffset++;
            }
            break;
        }
    case 0x0b:              /* STV */
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 01011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores one element from maximum of 8 vectors, while incrementing element index

            int element, eaoffset;
            int vs = dest;
            int ve = dest + 8;
            if (ve > 32)
                ve = 32;

            element = 8 - (index >> 1);
            //if (index & 0x1)      fatalerror("RSP: STV: index = %d at %08X\n", index, rsp.ppc);

            ea = (base) ? rsp.r[base] + (offset * 16) : (offset * 16);

            //if (ea & 0x1)         fatalerror("RSP: STV: ea = %08X at %08X\n", ea, rsp.ppc);

            eaoffset = (ea & 0xf) + (element * 2);
            ea &= ~0xf;

            for (i=vs; i < ve; i++)
            {
                WRITE16(ea + (eaoffset & 0xf), VREG_S(i, element & 0x7));
                eaoffset += 2;
                element++;
            }
            break;
        }

    default:
        {
            unimplemented_opcode(op);
            break;
        }
    }
}

#define U16MIN    0x0000
#define U16MAX    0xffff

#define S16MIN    0x8000
#define S16MAX    0x7fff

INLINE UINT16 SATURATE_ACCUM_U(int accum)
{
    if ((INT16)ACCUM_H(accum) < 0)
    {
        if ((UINT16)(ACCUM_H(accum)) != 0xffff)
        {
            return U16MIN;
        }
        else
        {
            if ((INT16)ACCUM_M(accum) >= 0)
            {
                return U16MIN;
            }
            else
            {
                return ACCUM_L(accum);
            }
        }
    }
    else
    {
        if ((UINT16)(ACCUM_H(accum)) != 0)
        {
            return U16MAX;
        }
        else
        {
            if ((INT16)ACCUM_M(accum) < 0)
            {
                return U16MAX;
            }
            else
            {
                return ACCUM_L(accum);
            }
        }
    }

    return 0;
}

INLINE UINT16 SATURATE_ACCUM_S(int accum)
{
    if ((INT16)ACCUM_H(accum) < 0)
    {
        if ((UINT16)(ACCUM_H(accum)) != 0xffff)
            return S16MIN;
        else
        {
            if ((INT16)ACCUM_M(accum) >= 0)
                return S16MIN;
            else
                return ACCUM_M(accum);
        }
    }
    else
    {
        if ((UINT16)(ACCUM_H(accum)) != 0)
            return S16MAX;
        else
        {
            if ((INT16)ACCUM_M(accum) < 0)
                return S16MAX;
            else
                return ACCUM_M(accum);
        }
    }

    return 0;
}

#define WRITEBACK_RESULT()                          \
    do {                                            \
    VREG_S(VDREG, 0) = vres[0];                     \
    VREG_S(VDREG, 1) = vres[1];                     \
    VREG_S(VDREG, 2) = vres[2];                     \
    VREG_S(VDREG, 3) = vres[3];                     \
    VREG_S(VDREG, 4) = vres[4];                     \
    VREG_S(VDREG, 5) = vres[5];                     \
    VREG_S(VDREG, 6) = vres[6];                     \
    VREG_S(VDREG, 7) = vres[7];                     \
    } while(0)


void handle_vector_ops(UINT32 op)
{
    int i;
    INT16 vres[8];

    // Opcode legend:
    //    E = VS2 element type
    //    S = VS1, Source vector 1
    //    T = VS2, Source vector 2
    //    D = Destination vector

    switch (op & 0x3f)
    {
    case 0x00:              /* VMULF */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000000 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by signed integer * 2

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                if (s1 == -32768 && s2 == -32768)
                {
                    // overflow
                    ACCUM_H(del) = 0;
                    ACCUM_M(del) = -32768;
                    ACCUM_L(del) = -32768;
                    vres[del] = 0x7fff;
                }
                else
                {
                    INT64 r =  s1 * s2 * 2;
                    r += 0x8000;    // rounding ?
                    ACCUM_H(del) = (r < 0) ? 0xffff : 0;            // sign-extend to 48-bit
                    ACCUM_M(del) = (INT16)(r >> 16);
                    ACCUM_L(del) = (UINT16)(r);
                    vres[del] = ACCUM_M(del);
                }
            }
            WRITEBACK_RESULT();

            break;
        }

    case 0x01:              /* VMULU */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000001 |
            // ------------------------------------------------------
            //

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT64 r = s1 * s2 * 2;
                r += 0x8000;    // rounding ?

                ACCUM_H(del) = (UINT16)(r >> 32);
                ACCUM_M(del) = (UINT16)(r >> 16);
                ACCUM_L(del) = (UINT16)(r);

                if (r < 0)
                {
                    vres[del] = 0;
                }
                else if (((INT16)(ACCUM_H(del)) ^ (INT16)(ACCUM_M(del))) < 0)
                {
                    vres[del] = -1;
                }
                else
                {
                    vres[del] = ACCUM_M(del);
                }
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x04:              /* VMUDL */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000100 |
            // ------------------------------------------------------
            //
            // Multiplies unsigned fraction by unsigned fraction
            // Stores the higher 16 bits of the 32-bit result to accumulator
            // The low slice of accumulator is stored into destination element

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                UINT32 s1 = (UINT32)(UINT16)VREG_S(VS1REG, del);
                UINT32 s2 = (UINT32)(UINT16)VREG_S(VS2REG, sel);
                UINT32 r = s1 * s2;

                ACCUM_H(del) = 0;
                ACCUM_M(del) = 0;
                ACCUM_L(del) = (UINT16)(r >> 16);

                vres[del] = ACCUM_L(del);
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x05:              /* VMUDM */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000101 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by unsigned fraction
            // The result is stored into accumulator
            // The middle slice of accumulator is stored into destination element

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (UINT16)VREG_S(VS2REG, sel); // not sign-extended
                INT32 r =  s1 * s2;

                ACCUM_H(del) = (r < 0) ? 0xffff : 0;            // sign-extend to 48-bit
                ACCUM_M(del) = (INT16)(r >> 16);
                ACCUM_L(del) = (UINT16)(r);

                vres[del] = ACCUM_M(del);
            }
            WRITEBACK_RESULT();
            break;

        }

    case 0x06:              /* VMUDN */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000110 |
            // ------------------------------------------------------
            //
            // Multiplies unsigned fraction by signed integer
            // The result is stored into accumulator
            // The low slice of accumulator is stored into destination element

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (UINT16)VREG_S(VS1REG, del);         // not sign-extended
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r = s1 * s2;

                ACCUM_H(del) = (r < 0) ? 0xffff : 0;            // sign-extend to 48-bit
                ACCUM_M(del) = (INT16)(r >> 16);
                ACCUM_L(del) = (UINT16)(r);

                vres[del] = ACCUM_L(del);
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x07:              /* VMUDH */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 000111 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by signed integer
            // The result is stored into highest 32 bits of accumulator, the low slice is zero
            // The highest 32 bits of accumulator is saturated into destination element

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r = s1 * s2;

                ACCUM_H(del) = (INT16)(r >> 16);
                ACCUM_M(del) = (UINT16)(r);
                ACCUM_L(del) = 0;

                if (r < -32768) r = -32768;
                if (r >  32767) r = 32767;
                vres[del] = (INT16)(r);
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x08:              /* VMACF */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001000 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by signed integer * 2
            // The result is added to accumulator

            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r = s1 * s2;

                ACCUM(del) += (INT64)(r) << 17;
                res = SATURATE_ACCUM_S(del);

                vres[del] = res;
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x09:              /* VMACU */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001001 |
            // ------------------------------------------------------
            //

            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r1 = s1 * s2;
                UINT32 r2 = (UINT16)ACCUM_L(del) + ((UINT16)(r1) * 2);
                UINT32 r3 = (UINT16)ACCUM_M(del) + (UINT16)((r1 >> 16) * 2) + (UINT16)(r2 >> 16);

                ACCUM_L(del) = (UINT16)(r2);
                ACCUM_M(del) = (UINT16)(r3);
                ACCUM_H(del) += (UINT16)(r3 >> 16) + (UINT16)(r1 >> 31);

                //res = SATURATE_ACCUM(del, 1, 0x0000, 0xffff);
                if ((INT16)ACCUM_H(del) < 0)
                {
                    res = 0;
                }
                else
                {
                    if (ACCUM_H(del) != 0)
                    {
                        res = 0xffff;
                    }
                    else
                    {
                        if ((INT16)ACCUM_M(del) < 0)
                        {
                            res = 0xffff;
                        }
                        else
                        {
                            res = ACCUM_M(del);
                        }
                    }
                }

                vres[del] = res;
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x0c:              /* VMADL */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001100 |
            // ------------------------------------------------------
            //
            // Multiplies unsigned fraction by unsigned fraction
            // Adds the higher 16 bits of the 32-bit result to accumulator
            // The low slice of accumulator is stored into destination element

            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                UINT32 s1 = (UINT32)(UINT16)VREG_S(VS1REG, del);
                UINT32 s2 = (UINT32)(UINT16)VREG_S(VS2REG, sel);
                UINT32 r1 = s1 * s2;
                UINT32 r2 = (UINT16)ACCUM_L(del) + (r1 >> 16);
                UINT32 r3 = (UINT16)ACCUM_M(del) + (r2 >> 16);

                ACCUM_L(del) = (UINT16)(r2);
                ACCUM_M(del) = (UINT16)(r3);
                ACCUM_H(del) += (INT16)(r3 >> 16);

                res = SATURATE_ACCUM_U(del);

                vres[del] = res;
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x0d:              /* VMADM */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001101 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by unsigned fraction
            // The result is added into accumulator
            // The middle slice of accumulator is stored into destination element

            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                UINT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                UINT32 s2 = (UINT16)VREG_S(VS2REG, sel);        // not sign-extended
                UINT32 r1 = s1 * s2;
                UINT32 r2 = (UINT16)ACCUM_L(del) + (UINT16)(r1);
                UINT32 r3 = (UINT16)ACCUM_M(del) + (r1 >> 16) + (r2 >> 16);

                ACCUM_L(del) = (UINT16)(r2);
                ACCUM_M(del) = (UINT16)(r3);
                ACCUM_H(del) += (UINT16)(r3 >> 16);
                if ((INT32)(r1) < 0)
                    ACCUM_H(del) -= 1;

                res = SATURATE_ACCUM_S(del);

                vres[del] = res;
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x0e:              /* VMADN */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001110 |
            // ------------------------------------------------------
            //
            // Multiplies unsigned fraction by signed integer
            // The result is added into accumulator
            // The low slice of accumulator is stored into destination element

#if 1
            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (UINT16)VREG_S(VS1REG, del);         // not sign-extended
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                ACCUM(del) += (INT64)(s1*s2)<<16;
            }

            for (i=0; i < 8; i++)
            {
                UINT16 res;
                res = SATURATE_ACCUM_U(i);
                //res = ACCUM_L(i);

                VREG_S(VDREG, i) = res;
            }
#else
            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (UINT16)VREG_S(VS1REG, del);         // not sign-extended
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                UINT32 r1 = s1 * s2;
                UINT32 r2 = (UINT16)ACCUM_L(del) + (UINT16)(r1);
                UINT32 r3 = (UINT16)ACCUM_M(del) + (r1 >> 16) + (r2 >> 16);

                ACCUM_L(del) = (UINT16)(r2);
                ACCUM_M(del) = (UINT16)(r3);
                ACCUM_H(del) += (UINT16)(r3 >> 16);
                if ((INT32)(r1) < 0)
                    ACCUM_H(del) -= 1;

                res = SATURATE_ACCUM_U(del);

                vres[del] = res;
            }
            WRITEBACK_RESULT();
#endif
            break;
        }

    case 0x0f:              /* VMADH */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 001111 |
            // ------------------------------------------------------
            //
            // Multiplies signed integer by signed integer
            // The result is added into highest 32 bits of accumulator, the low slice is zero
            // The highest 32 bits of accumulator is saturated into destination element

#if 1
            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);

                rsp.accum[del].l[1] += s1*s2;

            }
            for (i=0; i < 8; i++)
            {
                UINT16 res;
                res = SATURATE_ACCUM_S(i);
                //res = ACCUM_M(i);

                VREG_S(VDREG, i) = res;
            }
#else      
            for (i=0; i < 8; i++)
            {
                UINT16 res;
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT64 r = s1 * s2;

                ACCUM(del) += (INT64)(r) << 32;

                res = SATURATE_ACCUM_S(del);

                vres[del] = res;
            }
            WRITEBACK_RESULT();
#endif
            break;
        }

    case 0x10:              /* VADD */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 010000 |
            // ------------------------------------------------------
            //
            // Adds two vector registers and carry flag, the result is saturated to 32767

            // TODO: check VS2REG == VDREG

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r = s1 + s2 + CARRY_FLAG(del);

                ACCUM_L(del) = (INT16)(r);

                if (r > 32767) r = 32767;
                if (r < -32768) r = -32768;
                vres[del] = (INT16)(r);
            }
            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x11:              /* VSUB */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 010001 |
            // ------------------------------------------------------
            //
            // Subtracts two vector registers and carry flag, the result is saturated to -32768

            // TODO: check VS2REG == VDREG

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (INT32)(INT16)VREG_S(VS1REG, del);
                INT32 s2 = (INT32)(INT16)VREG_S(VS2REG, sel);
                INT32 r = s1 - s2 - CARRY_FLAG(del);

                ACCUM_L(del) = (INT16)(r);

                if (r > 32767) r = 32767;
                if (r < -32768) r = -32768;

                vres[del] = (INT16)(r);
            }
            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x13:              /* VABS */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 010011 |
            // ------------------------------------------------------
            //
            // Changes the sign of source register 2 if source register 1 is negative and stores
            // the result to destination register

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT16 s1 = (INT16)VREG_S(VS1REG, del);
                INT16 s2 = (INT16)VREG_S(VS2REG, sel);

                if (s1 < 0)
                {
                    if (s2 == -32768)
                    {
                        vres[del] = 32767;
                    }
                    else
                    {
                        vres[del] = -s2;
                    }
                }
                else if (s1 > 0)
                {
                    vres[del] = s2;
                }
                else
                {
                    vres[del] = 0;
                }

                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x14:              /* VADDC */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 010100 |
            // ------------------------------------------------------
            //
            // Adds two vector registers, the carry out is stored into carry register

            // TODO: check VS2REG = VDREG

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (UINT32)(UINT16)VREG_S(VS1REG, del);
                INT32 s2 = (UINT32)(UINT16)VREG_S(VS2REG, sel);
                INT32 r = s1 + s2;

                vres[del] = (INT16)(r);
                ACCUM_L(del) = (INT16)(r);

                if (r & 0xffff0000)
                {
                    SET_CARRY_FLAG(del);
                }
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x15:              /* VSUBC */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 010101 |
            // ------------------------------------------------------
            //
            // Subtracts two vector registers, the carry out is stored into carry register

            // TODO: check VS2REG = VDREG

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT32 s1 = (UINT32)(UINT16)VREG_S(VS1REG, del);
                INT32 s2 = (UINT32)(UINT16)VREG_S(VS2REG, sel);
                INT32 r = s1 - s2;

                vres[del] = (INT16)(r);
                ACCUM_L(del) = (UINT16)(r);

                if ((UINT16)(r) != 0)
                {
                    SET_ZERO_FLAG(del);
                }
                if (r & 0xffff0000)
                {
                    SET_CARRY_FLAG(del);
                }
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x1d:              /* VSAW */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 011101 |
            // ------------------------------------------------------
            //
            // Stores high, middle or low slice of accumulator to destination vector

            switch (EL)
            {
            case 0x08:              // VSAWH
                {
                    for (i=0; i < 8; i++)
                    {
                        VREG_S(VDREG, i) = ACCUM_H(i);
                    }
                    break;
                }
            case 0x09:              // VSAWM
                {
                    for (i=0; i < 8; i++)
                    {
                        VREG_S(VDREG, i) = ACCUM_M(i);
                    }
                    break;
                }
            case 0x0a:              // VSAWL
                {
                    for (i=0; i < 8; i++)
                    {
                        VREG_S(VDREG, i) = ACCUM_L(i);
                    }
                    break;
                }
            default:        log(M64MSG_ERROR, "RSP: VSAW: el = %d\n", EL);
            }
            break;
        }

    case 0x20:              /* VLT */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100000 |
            // ------------------------------------------------------
            //
            // Sets compare flags if elements in VS1 are less than VS2
            // Moves the element in VS2 to destination vector

            rsp.flag[1] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);

                if (VREG_S(VS1REG, del) < VREG_S(VS2REG, sel))
                {
                    vres[del] = VREG_S(VS1REG, del);
                    SET_COMPARE_FLAG(del);
                }
                else if (VREG_S(VS1REG, del) == VREG_S(VS2REG, sel))
                {
                    vres[del] = VREG_S(VS1REG, del);
                    if (ZERO_FLAG(del) != 0 && CARRY_FLAG(del) != 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                }
                else
                {
                    vres[del] = VREG_S(VS2REG, sel);
                }

                ACCUM_L(del) = vres[del];
            }

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x21:              /* VEQ */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100001 |
            // ------------------------------------------------------
            //
            // Sets compare flags if elements in VS1 are equal with VS2
            // Moves the element in VS2 to destination vector

            rsp.flag[1] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);

                vres[del] = VREG_S(VS2REG, sel);
                ACCUM_L(del) = vres[del];

                if (VREG_S(VS1REG, del) == VREG_S(VS2REG, sel))
                {
                    if (ZERO_FLAG(del) == 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                }
            }

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x22:              /* VNE */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100010 |
            // ------------------------------------------------------
            //
            // Sets compare flags if elements in VS1 are not equal with VS2
            // Moves the element in VS2 to destination vector

            rsp.flag[1] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);

                vres[del] = VREG_S(VS1REG, del);
                ACCUM_L(del) = vres[del];

                if (VREG_S(VS1REG, del) != VREG_S(VS2REG, sel))
                {
                    SET_COMPARE_FLAG(del);
                }
                else
                {
                    if (ZERO_FLAG(del) != 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                }
            }

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x23:              /* VGE */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100011 |
            // ------------------------------------------------------
            //
            // Sets compare flags if elements in VS1 are greater or equal with VS2
            // Moves the element in VS2 to destination vector

            rsp.flag[1] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);

                if (VREG_S(VS1REG, del) == VREG_S(VS2REG, sel))
                {
                    if (ZERO_FLAG(del) == 0 || CARRY_FLAG(del) == 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                }
                else if (VREG_S(VS1REG, del) > VREG_S(VS2REG, sel))
                {
                    SET_COMPARE_FLAG(del);
                }

                if (COMPARE_FLAG(del) != 0)
                {
                    vres[del] = VREG_S(VS1REG, del);
                }
                else
                {
                    vres[del] = VREG_S(VS2REG, sel);
                }

                ACCUM_L(del) = vres[del];
            }

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            WRITEBACK_RESULT();
            break;
        }

    case 0x24:              /* VCL */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100100 |
            // ------------------------------------------------------
            //
            // Vector clip low

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT16 s1 = VREG_S(VS1REG, del);
                INT16 s2 = VREG_S(VS2REG, sel);

                if (CARRY_FLAG(del) != 0)
                {
                    if (ZERO_FLAG(del) != 0)
                    {
                        if (COMPARE_FLAG(del) != 0)
                        {
                            ACCUM_L(del) = -(UINT16)s2;
                        }
                        else
                        {
                            ACCUM_L(del) = s1;
                        }
                    }
                    else
                    {
                        if (rsp.flag[2] & (1 << (del)))
                        {
                            if (((UINT32)(INT16)(s1) + (UINT32)(INT16)(s2)) > 0x10000)
                            {
                                ACCUM_L(del) = s1;
                                CLEAR_COMPARE_FLAG(del);
                            }
                            else
                            {
                                ACCUM_L(del) = -((UINT16)s2);
                                SET_COMPARE_FLAG(del);
                            }
                        }
                        else
                        {
                            if (((UINT32)(INT16)(s1) + (UINT32)(INT16)(s2)) != 0)
                            {
                                ACCUM_L(del) = s1;
                                CLEAR_COMPARE_FLAG(del);
                            }
                            else
                            {
                                ACCUM_L(del) = -((UINT16)s2);
                                SET_COMPARE_FLAG(del);
                            }
                        }
                    }
                }
                else
                {
                    if (ZERO_FLAG(del) != 0)
                    {
                        if (rsp.flag[1] & (1 << (8+del)))
                        {
                            ACCUM_L(del) = s2;
                        }
                        else
                        {
                            ACCUM_L(del) = s1;
                        }
                    }
                    else
                    {
                        if (((INT32)(UINT16)s1 - (INT32)(UINT16)s2) >= 0)
                        {
                            ACCUM_L(del) = s2;
                            rsp.flag[1] |= (1 << (8+del));
                        }
                        else
                        {
                            ACCUM_L(del) = s1;
                            rsp.flag[1] &= ~(1 << (8+del));
                        }
                    }
                }

                vres[del] = ACCUM_L(del);
            }
            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            rsp.flag[2] = 0;
            WRITEBACK_RESULT();
            break;
        }

    case 0x25:              /* VCH */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100101 |
            // ------------------------------------------------------
            //
            // Vector clip high

            CLEAR_ZERO_FLAGS();
            CLEAR_CARRY_FLAGS();
            rsp.flag[1] = 0;
            rsp.flag[2] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT16 s1 = VREG_S(VS1REG, del);
                INT16 s2 = VREG_S(VS2REG, sel);

                if ((s1 ^ s2) < 0)
                {
                    SET_CARRY_FLAG(del);
                    if (s2 < 0)
                    {
                        rsp.flag[1] |= (1 << (8+del));
                    }

                    if (s1 + s2 <= 0)
                    {
                        if (s1 + s2 == -1)
                        {
                            rsp.flag[2] |= (1 << (del));
                        }
                        SET_COMPARE_FLAG(del);
                        vres[del] = -((UINT16)s2);
                    }
                    else
                    {
                        vres[del] = s1;
                    }

                    if (s1 + s2 != 0)
                    {
                        if (s1 != ~s2)
                        {
                            SET_ZERO_FLAG(del);
                        }
                    }
                }
                else
                {
                    if (s2 < 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                    if (s1 - s2 >= 0)
                    {
                        rsp.flag[1] |= (1 << (8+del));
                        vres[del] = s2;
                    }
                    else
                    {
                        vres[del] = s1;
                    }

                    if ((s1 - s2) != 0)
                    {
                        if (s1 != ~s2)
                        {
                            SET_ZERO_FLAG(del);
                        }
                    }
                }

                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x26:              /* VCR */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100110 |
            // ------------------------------------------------------
            //
            // Vector clip reverse

            rsp.flag[0] = 0;
            rsp.flag[1] = 0;
            rsp.flag[2] = 0;

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                INT16 s1 = VREG_S(VS1REG, del);
                INT16 s2 = VREG_S(VS2REG, sel);

                if ((INT16)(s1 ^ s2) < 0)
                {
                    if (s2 < 0)
                    {
                        rsp.flag[1] |= (1 << (8+del));
                    }
                    if ((s1 + s2) <= 0)
                    {
                        ACCUM_L(del) = ~((UINT16)s2);
                        SET_COMPARE_FLAG(del);
                    }
                    else
                    {
                        ACCUM_L(del) = s1;
                    }
                }
                else
                {
                    if (s2 < 0)
                    {
                        SET_COMPARE_FLAG(del);
                    }
                    if ((s1 - s2) >= 0)
                    {
                        ACCUM_L(del) = s2;
                        rsp.flag[1] |= (1 << (8+del));
                    }
                    else
                    {
                        ACCUM_L(del) = s1;
                    }
                }

                vres[del] = ACCUM_L(del);
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x27:              /* VMRG */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 100111 |
            // ------------------------------------------------------
            //
            // Merges two vectors according to compare flags

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                if (COMPARE_FLAG(del) != 0)
                {
                    vres[del] = VREG_S(VS1REG, del);
                }
                else
                {
                    vres[del] = VREG_S(VS2REG, VEC_EL_2(EL, sel));
                }

                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x28:              /* VAND */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101000 |
            // ------------------------------------------------------
            //
            // Bitwise AND of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = VREG_S(VS1REG, del) & VREG_S(VS2REG, sel);
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x29:              /* VNAND */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101001 |
            // ------------------------------------------------------
            //
            // Bitwise NOT AND of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = ~((VREG_S(VS1REG, del) & VREG_S(VS2REG, sel)));
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x2a:              /* VOR */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101010 |
            // ------------------------------------------------------
            //
            // Bitwise OR of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = VREG_S(VS1REG, del) | VREG_S(VS2REG, sel);
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x2b:              /* VNOR */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101011 |
            // ------------------------------------------------------
            //
            // Bitwise NOT OR of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = ~((VREG_S(VS1REG, del) | VREG_S(VS2REG, sel)));
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x2c:              /* VXOR */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101100 |
            // ------------------------------------------------------
            //
            // Bitwise XOR of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = VREG_S(VS1REG, del) ^ VREG_S(VS2REG, sel);
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }
    case 0x2d:              /* VNXOR */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | TTTTT | DDDDD | 101101 |
            // ------------------------------------------------------
            //
            // Bitwise NOT XOR of two vector registers

            for (i=0; i < 8; i++)
            {
                int del = VEC_EL_1(EL, i);
                int sel = VEC_EL_2(EL, del);
                vres[del] = ~((VREG_S(VS1REG, del) ^ VREG_S(VS2REG, sel)));
                ACCUM_L(del) = vres[del];
            }
            WRITEBACK_RESULT();
            break;
        }

    case 0x30:              /* VRCP */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110000 |
            // ------------------------------------------------------
            //
            // Calculates reciprocal
            int del = (VS1REG & 7);
            int sel = EL&7; //VEC_EL_2(EL, del);
            INT32 rec;

            rec = (INT16)(VREG_S(VS2REG, sel));

            if (rec == 0)
            {
                // divide by zero -> overflow
                rec = 0x7fffffff;
            }
            else
            {
                int negative = 0;
                if (rec < 0)
                {
                    rec = ~rec+1;
                    negative = 1;
                }
                for (i = 15; i > 0; i--)
                {
                    if (rec & (1 << i))
                    {
                        rec &= ((0xffc0) >> (15 - i));
                        i = 0;
                    }
                }
                rec = (INT32)(0x7fffffff / (double)rec);
                for (i = 31; i > 0; i--)
                {
                    if (rec & (1 << i))
                    {
                        rec &= ((0xffff8000) >> (31 - i));
                        i = 0;
                    }
                }
                if (negative)
                {
                    rec = ~rec;
                }
            }

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                ACCUM_L(i) = VREG_S(VS2REG, element);
            }

            rsp.reciprocal_res = rec;

            VREG_S(VDREG, del) = (UINT16)(rsp.reciprocal_res);                      // store low part
            break;
        }

    case 0x31:              /* VRCPL */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110001 |
            // ------------------------------------------------------
            //
            // Calculates reciprocal low part

            int del = (VS1REG & 7);
            int sel = VEC_EL_2(EL, del);
            INT32 rec;

            rec = ((UINT16)(VREG_S(VS2REG, sel)) | ((UINT32)(rsp.reciprocal_high) << 16));

            if (rec == 0)
            {
                // divide by zero -> overflow
                rec = 0x7fffffff;
            }
            else
            {
                int negative = 0;
                if (rec < 0)
                {
                    if (((UINT32)(rec & 0xffff0000) == 0xffff0000) && ((INT16)(rec & 0xffff) < 0))
                    {
                        rec = ~rec+1;
                    }
                    else
                    {
                        rec = ~rec;
                    }
                    negative = 1;
                }
                for (i = 31; i > 0; i--)
                {
                    if (rec & (1 << i))
                    {
                        rec &= ((0xffc00000) >> (31 - i));
                        i = 0;
                    }
                }
                rec = (0x7fffffff / rec);
                for (i = 31; i > 0; i--)
                {
                    if (rec & (1 << i))
                    {
                        rec &= ((0xffff8000) >> (31 - i));
                        i = 0;
                    }
                }
                if (negative)
                {
                    rec = ~rec;
                }
            }

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                ACCUM_L(i) = VREG_S(VS2REG, element);
            }

            rsp.reciprocal_res = rec;

            VREG_S(VDREG, del) = (UINT16)(rsp.reciprocal_res);                      // store low part
            break;
        }

    case 0x32:              /* VRCPH */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110010 |
            // ------------------------------------------------------
            //
            // Calculates reciprocal high part

            int del = (VS1REG & 7);
            int sel = VEC_EL_2(EL, del);

            rsp.reciprocal_high = VREG_S(VS2REG, sel);

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                ACCUM_L(i) = VREG_S(VS2REG, element);           // perhaps accumulator is used to store the intermediate values ?
            }

            VREG_S(VDREG, del) = (INT16)(rsp.reciprocal_res >> 16); // store high part
            break;
        }

    case 0x33:              /* VMOV */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110011 |
            // ------------------------------------------------------
            //
            // Moves element from vector to destination vector

            int element = VS1REG & 7;
            VREG_S(VDREG, element) = VREG_S(VS2REG, VEC_EL_2(EL, 7-element));
            break;
        }

    case 0x35:              /* VRSQL */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110101 |
            // ------------------------------------------------------
            //
            // Calculates reciprocal square-root low part

            int del = (VS1REG & 7);
            int sel = VEC_EL_2(EL, del);
            UINT32 sqr;

            sqr = (UINT16)(VREG_S(VS2REG, sel)) | ((UINT32)(rsp.square_root_high) << 16);

            if (sqr == 0)
            {
                // square root on 0 -> overflow
                sqr = 0x7fffffff;
            }
            else if (sqr == 0xffff8000)
            {
                // overflow ?
                sqr = 0xffff8000;
            }
            else
            {
                int negative = 0;
                if (sqr > 0x7fffffff)
                {
                    if (((UINT32)(sqr & 0xffff0000) == 0xffff0000) && ((INT16)(sqr & 0xffff) < 0))
                    {
                        sqr = ~sqr+1;
                    }
                    else
                    {
                        sqr = ~sqr;
                    }
                    negative = 1;
                }
                for (i = 31; i > 0; i--)
                {
                    if (sqr & (1 << i))
                    {
                        sqr &= (0xff800000 >> (31 - i));
                        i = 0;
                    }
                }
                sqr = (INT32)(0x7fffffff / sqrt(sqr));
                for (i = 31; i > 0; i--)
                {
                    if (sqr & (1 << i))
                    {
                        sqr &= (0xffff8000 >> (31 - i));
                        i = 0;
                    }
                }
                if (negative)
                {
                    sqr = ~sqr;
                }
            }

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                ACCUM_L(i) = VREG_S(VS2REG, element);
            }

            rsp.square_root_res = sqr;

            VREG_S(VDREG, del) = (UINT16)(rsp.square_root_res);                     // store low part
            break;
        }

    case 0x36:              /* VRSQH */
        {
            // 31       25  24     20      15      10      5        0
            // ------------------------------------------------------
            // | 010010 | 1 | EEEE | SSSSS | ?FFFF | DDDDD | 110110 |
            // ------------------------------------------------------
            //
            // Calculates reciprocal square-root high part

            int del = (VS1REG & 7);
            int sel = VEC_EL_2(EL, del);

            rsp.square_root_high = VREG_S(VS2REG, sel);

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                ACCUM_L(i) = VREG_S(VS2REG, element);           // perhaps accumulator is used to store the intermediate values ?
            }

            VREG_S(VDREG, del) = (INT16)(rsp.square_root_res >> 16);        // store high part
            break;
        }

    default:        unimplemented_opcode(op); break;
    }
}

int rsp_execute(int cycles)
{
    UINT32 op;

    rsp_icount=1; //cycles;

    UINT32 ExecutedCycles=0;
    UINT32 BreakMarker=0;
    UINT32 WDCHackFlag1=0;
    UINT32 WDCHackFlag2=0;

    sp_pc = /*0x4001000 | */(sp_pc & 0xfff);
    if( rsp_sp_status & (SP_STATUS_HALT|SP_STATUS_BROKE))
    {
        log(M64MSG_WARNING, "Quit due to SP halt/broke on start");
        rsp_icount = 0;
    }


    while (rsp_icount > 0)
    {
#ifdef RSPTIMING
        uint64_t lasttime;
        lasttime = RDTSC();
#endif
        rsp.ppc = sp_pc;


        op = ROPCODE(sp_pc);
#ifdef GENTRACE
        char s[128];
        rsp_dasm_one(s, sp_pc, op);
        GENTRACE("%2x %3x\t%s\n", ((UINT8*)rsp_dmem)[0x1934], sp_pc, s);
#endif

        if (rsp.nextpc != ~0U)///DELAY SLOT USAGE
        {
            sp_pc = /*0x4001000 | */(rsp.nextpc & 0xfff); //rsp.nextpc;
            rsp.nextpc = ~0U;
        }
        else
        {
            sp_pc = /*0x4001000 | */((sp_pc+4)&0xfff);
        }

        switch (op >> 26)
        {
        case 0x00:      /* SPECIAL */
            {
                switch (op & 0x3f)
                {
                case 0x00:      /* SLL */               if (RDREG) RDVAL = (UINT32)RTVAL << SHIFT; break;
                case 0x02:      /* SRL */               if (RDREG) RDVAL = (UINT32)RTVAL >> SHIFT; break;
                case 0x03:      /* SRA */               if (RDREG) RDVAL = (INT32)RTVAL >> SHIFT; break;
                case 0x04:      /* SLLV */              if (RDREG) RDVAL = (UINT32)RTVAL << (RSVAL & 0x1f); break;
                case 0x06:      /* SRLV */              if (RDREG) RDVAL = (UINT32)RTVAL >> (RSVAL & 0x1f); break;
                case 0x07:      /* SRAV */              if (RDREG) RDVAL = (INT32)RTVAL >> (RSVAL & 0x1f); break;
                case 0x08:      /* JR */                JUMP_PC(RSVAL); break;
                case 0x09:      /* JALR */              JUMP_PC_L(RSVAL, RDREG); break;
                case 0x0d:      /* BREAK */
                    {
                        *z64_rspinfo.SP_STATUS_REG |= (SP_STATUS_HALT | SP_STATUS_BROKE );
                        if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) {
                            *z64_rspinfo.MI_INTR_REG |= 1;
                            z64_rspinfo.CheckInterrupts();
                        }
                        //sp_set_status(0x3);
                        rsp_icount = 0;

                        BreakMarker=1;

#if LOG_INSTRUCTION_EXECUTION
                        fprintf(exec_output, "\n---------- break ----------\n\n");
#endif
                        break;
                    }
                case 0x20:      /* ADD */               if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
                case 0x21:      /* ADDU */              if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
                case 0x22:      /* SUB */               if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
                case 0x23:      /* SUBU */              if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
                case 0x24:      /* AND */               if (RDREG) RDVAL = RSVAL & RTVAL; break;
                case 0x25:      /* OR */                if (RDREG) RDVAL = RSVAL | RTVAL; break;
                case 0x26:      /* XOR */               if (RDREG) RDVAL = RSVAL ^ RTVAL; break;
                case 0x27:      /* NOR */               if (RDREG) RDVAL = ~(RSVAL | RTVAL); break;
                case 0x2a:      /* SLT */               if (RDREG) RDVAL = (INT32)RSVAL < (INT32)RTVAL; break;
                case 0x2b:      /* SLTU */              if (RDREG) RDVAL = (UINT32)RSVAL < (UINT32)RTVAL; break;
                default:        unimplemented_opcode(op); break;
                }
                break;
            }

        case 0x01:      /* REGIMM */
            {
                switch (RTREG)
                {
                case 0x00:      /* BLTZ */              if ((INT32)(RSVAL) < 0) JUMP_REL(SIMM16); break;
                case 0x01:      /* BGEZ */              if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); break;
                    // VP according to the doc, link is performed even when condition fails,
                    // this sound pretty stupid but let's try it that way
                case 0x11:      /* BGEZAL */    LINK(31); if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); break;
                    //case 0x11:  /* BGEZAL */    if ((INT32)(RSVAL) >= 0) JUMP_REL_L(SIMM16, 31); break;
                default:        unimplemented_opcode(op); break;
                }
                break;
            }

        case 0x02:      /* J */                 JUMP_ABS(UIMM26); break;
        case 0x03:      /* JAL */               JUMP_ABS_L(UIMM26, 31); break;
        case 0x04:      /* BEQ */               if (RSVAL == RTVAL) JUMP_REL(SIMM16); break;
        case 0x05:      /* BNE */               if (RSVAL != RTVAL) JUMP_REL(SIMM16); break;
        case 0x06:      /* BLEZ */              if ((INT32)RSVAL <= 0) JUMP_REL(SIMM16); break;
        case 0x07:      /* BGTZ */              if ((INT32)RSVAL > 0) JUMP_REL(SIMM16); break;
        case 0x08:      /* ADDI */              if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
        case 0x09:      /* ADDIU */             if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
        case 0x0a:      /* SLTI */              if (RTREG) RTVAL = (INT32)(RSVAL) < ((INT32)SIMM16); break;
        case 0x0b:      /* SLTIU */             if (RTREG) RTVAL = (UINT32)(RSVAL) < (UINT32)((INT32)SIMM16); break;
        case 0x0c:      /* ANDI */              if (RTREG) RTVAL = RSVAL & UIMM16; break;
        case 0x0d:      /* ORI */               if (RTREG) RTVAL = RSVAL | UIMM16; break;
        case 0x0e:      /* XORI */              if (RTREG) RTVAL = RSVAL ^ UIMM16; break;
        case 0x0f:      /* LUI */               if (RTREG) RTVAL = UIMM16 << 16; break;

        case 0x10:      /* COP0 */
            {
                switch ((op >> 21) & 0x1f)
                {
                case 0x00:      /* MFC0 */              if (RTREG) RTVAL = get_cop0_reg(RDREG); break;
                case 0x04:      /* MTC0 */              set_cop0_reg(RDREG, RTVAL); break;
                default:
                    log(M64MSG_WARNING, "unimplemented cop0 %x (%x)\n", (op >> 21) & 0x1f, op);
                    break;
                }
                break;
            }

        case 0x12:      /* COP2 */
            {
                switch ((op >> 21) & 0x1f)
                {
                case 0x00:      /* MFC2 */
                    {
                        // 31       25      20      15      10     6         0
                        // ---------------------------------------------------
                        // | 010010 | 00000 | TTTTT | DDDDD | IIII | 0000000 |
                        // ---------------------------------------------------
                        //

                        int el = (op >> 7) & 0xf;
                        UINT16 b1 = VREG_B(VS1REG, (el+0) & 0xf);
                        UINT16 b2 = VREG_B(VS1REG, (el+1) & 0xf);
                        if (RTREG) RTVAL = (INT32)(INT16)((b1 << 8) | (b2));
                        break;
                    }
                case 0x02:      /* CFC2 */
                    {
                        // 31       25      20      15      10            0
                        // ------------------------------------------------
                        // | 010010 | 00010 | TTTTT | DDDDD | 00000000000 |
                        // ------------------------------------------------
                        //

                        if (RTREG)
                        {
                            if (RDREG == 2)
                            {
                                // Anciliary clipping flags
                                RTVAL = rsp.flag[RDREG] & 0x00ff;
                            }
                            else
                            {
                                // All other flags are 16 bits but sign-extended at retrieval
                                RTVAL = (UINT32)rsp.flag[RDREG] | ( ( rsp.flag[RDREG] & 0x8000 ) ? 0xffff0000 : 0 );
                            }
                        }
                        break;

                    }
                case 0x04:      /* MTC2 */
                    {
                        // 31       25      20      15      10     6         0
                        // ---------------------------------------------------
                        // | 010010 | 00100 | TTTTT | DDDDD | IIII | 0000000 |
                        // ---------------------------------------------------
                        //

                        int el = (op >> 7) & 0xf;
                        VREG_B(VS1REG, (el+0) & 0xf) = (RTVAL >> 8) & 0xff;
                        VREG_B(VS1REG, (el+1) & 0xf) = (RTVAL >> 0) & 0xff;
                        break;
                    }
                case 0x06:      /* CTC2 */
                    {
                        // 31       25      20      15      10            0
                        // ------------------------------------------------
                        // | 010010 | 00110 | TTTTT | DDDDD | 00000000000 |
                        // ------------------------------------------------
                        //

                        rsp.flag[RDREG] = RTVAL & 0xffff;
                        break;
                    }

                case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
                case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                    {
                        handle_vector_ops(op);
                        break;
                    }

                default:        unimplemented_opcode(op); break;
                }
                break;
            }

        case 0x20:      /* LB */                if (RTREG) RTVAL = (INT32)(INT8)READ8(RSVAL + SIMM16); break;
        case 0x21:      /* LH */                if (RTREG) RTVAL = (INT32)(INT16)READ16(RSVAL + SIMM16); break;
        case 0x23:      /* LW */                if (RTREG) RTVAL = READ32(RSVAL + SIMM16); break;
        case 0x24:      /* LBU */               if (RTREG) RTVAL = (UINT8)READ8(RSVAL + SIMM16); break;
        case 0x25:      /* LHU */               if (RTREG) RTVAL = (UINT16)READ16(RSVAL + SIMM16); break;
        case 0x28:      /* SB */                WRITE8(RSVAL + SIMM16, RTVAL); break;
        case 0x29:      /* SH */                WRITE16(RSVAL + SIMM16, RTVAL); break;
        case 0x2b:      /* SW */                WRITE32(RSVAL + SIMM16, RTVAL); break;
        case 0x32:      /* LWC2 */              handle_lwc2(op); break;
        case 0x3a:      /* SWC2 */              handle_swc2(op); break;

        default:
            {
                unimplemented_opcode(op);
                break;
            }
        }

#ifdef RSPTIMING
        uint64_t time = lasttime;
        lasttime = RDTSC();
        rsp_opinfo_t info;
        rsp_get_opinfo(op, &info);
        rsptimings[info.op2] += lasttime - time;
        rspcounts[info.op2]++;
#endif

#if LOG_INSTRUCTION_EXECUTION
        {
            int i, l;
            static UINT32 prev_regs[32];
            static VECTOR_REG prev_vecs[32];
            char string[200];
            rsp_dasm_one(string, rsp.ppc, op);

            fprintf(exec_output, "%08X: %s", rsp.ppc, string);

            l = strlen(string);
            if (l < 36)
            {
                for (i=l; i < 36; i++)
                {
                    fprintf(exec_output, " ");
                }
            }

            fprintf(exec_output, "| ");

            for (i=0; i < 32; i++)
            {
                if (rsp.r[i] != prev_regs[i])
                {
                    fprintf(exec_output, "R%d: %08X ", i, rsp.r[i]);
                }
                prev_regs[i] = rsp.r[i];
            }

            for (i=0; i < 32; i++)
            {
                if (rsp.v[i].d[0] != prev_vecs[i].d[0] || rsp.v[i].d[1] != prev_vecs[i].d[1])
                {
                    fprintf(exec_output, "V%d: %04X|%04X|%04X|%04X|%04X|%04X|%04X|%04X ", i,
                        (UINT16)VREG_S(i,0), (UINT16)VREG_S(i,1), (UINT16)VREG_S(i,2), (UINT16)VREG_S(i,3), (UINT16)VREG_S(i,4), (UINT16)VREG_S(i,5), (UINT16)VREG_S(i,6), (UINT16)VREG_S(i,7));
                }
                prev_vecs[i].d[0] = rsp.v[i].d[0];
                prev_vecs[i].d[1] = rsp.v[i].d[1];
            }

            fprintf(exec_output, "\n");

        }
#endif
        //              --rsp_icount;

        ExecutedCycles++;
        if( rsp_sp_status & SP_STATUS_SSTEP )
        {
            if( rsp.step_count )
            {
                rsp.step_count--;
            }
            else
            {
                rsp_sp_status |= SP_STATUS_BROKE;
            }
        }

        if( rsp_sp_status & (SP_STATUS_HALT|SP_STATUS_BROKE))
        {
            rsp_icount = 0;

            if(BreakMarker==0)
                log(M64MSG_WARNING, "Quit due to SP halt/broke set by MTC0\n");
        }

        ///WDC&SR64 hack:VERSION3:1.8x -2x FASTER & safer
        if((WDCHackFlag1==0)&&(rsp.ppc>0x137)&&(rsp.ppc<0x14D))
            WDCHackFlag1=ExecutedCycles;
        if ((WDCHackFlag1!=0)&&((rsp.ppc<=0x137)||(rsp.ppc>=0x14D)))
            WDCHackFlag1=0;
        if ((WDCHackFlag1!=0)&&((ExecutedCycles-WDCHackFlag1)>=0x20)&&(rsp.ppc>0x137)&&(rsp.ppc<0x14D)) 
        {
            //      printf("WDC hack quit 1\n");
            rsp_icount=0;//32 cycles should be enough
        }
        if((WDCHackFlag2==0)&&(rsp.ppc>0xFCB)&&(rsp.ppc<0xFD5))
            WDCHackFlag2=ExecutedCycles;
        if ((WDCHackFlag2!=0)&&((rsp.ppc<=0xFCB)||(rsp.ppc>=0xFD5)))
            WDCHackFlag2=0;
        if ((WDCHackFlag2!=0)&&((ExecutedCycles-WDCHackFlag2)>=0x20)&&(rsp.ppc>0xFCB)&&(rsp.ppc<0xFD5)) 
        {
            //      printf("WDC hack quit 2\n");
            rsp_icount=0;//32 cycles should be enough
        }


    }
    //sp_pc -= 4;

    return ExecutedCycles;
}

/*****************************************************************************/


enum sp_dma_direction
{
	SP_DMA_RDRAM_TO_IDMEM,
	SP_DMA_IDMEM_TO_RDRAM
};

static void sp_dma(enum sp_dma_direction direction)
{
    UINT8 *src, *dst;
    int i, j;
    int length;
    int count;
    int skip;


    UINT32 l = sp_dma_length;
    length = ((l & 0xfff) | 7) + 1;
    skip = (l >> 20) + length;
    count = ((l >> 12) & 0xff) + 1;

    if (direction == SP_DMA_RDRAM_TO_IDMEM) // RDRAM -> I/DMEM
    {
        //UINT32 src_address = sp_dram_addr & ~7;
        //UINT32 dst_address = (sp_mem_addr & 0x1000) ? 0x4001000 : 0x4000000;
        src = (UINT8*)&rdram[(sp_dram_addr&~7) / 4];
        dst = (sp_mem_addr & 0x1000) ? (UINT8*)&rsp_imem[(sp_mem_addr & ~7 & 0xfff) / 4] : (UINT8*)&rsp_dmem[(sp_mem_addr & ~7 &0xfff) / 4];
        ///cpuintrf_push_context(0);
#define BYTE8_XOR_BE(a) ((a)^7)// JFG, Ocarina of Time

        for (j=0; j < count; j++)
        {
            for (i=0; i < length; i++)
            {
                ///UINT8 b = program_read_byte_64be(src_address + i + (j*skip));
                ///program_write_byte_64be(dst_address + (((sp_mem_addr & ~7) + i + (j*length)) & 0xfff), b);
                dst[BYTE8_XOR_BE((i + j*length)&0xfff)] = src[BYTE8_XOR_BE(i + j*skip)];
            }
        }

        ///cpuintrf_pop_context();
        *z64_rspinfo.SP_DMA_BUSY_REG = 0;
        *z64_rspinfo.SP_STATUS_REG  &= ~SP_STATUS_DMABUSY;
    }
    else if (direction == SP_DMA_IDMEM_TO_RDRAM) // I/DMEM -> RDRAM
    {
        //UINT32 dst_address = sp_dram_addr & ~7;
        //UINT32 src_address = (sp_mem_addr & 0x1000) ? 0x4001000 : 0x4000000;

        dst = (UINT8*)&rdram[(sp_dram_addr&~7) / 4];
        src = (sp_mem_addr & 0x1000) ? (UINT8*)&rsp_imem[(sp_mem_addr & ~7 & 0xfff) / 4] : (UINT8*)&rsp_dmem[(sp_mem_addr & ~7 &0xfff) / 4];
        ///cpuintrf_push_context(0);

        for (j=0; j < count; j++)
        {
            for (i=0; i < length; i++)
            {
                ///UINT8 b = program_read_byte_64be(src_address + (((sp_mem_addr & ~7) + i + (j*length)) & 0xfff));
                ///program_write_byte_64be(dst_address + i + (j*skip), b);
                dst[BYTE8_XOR_BE(i + j*skip)] = src[BYTE8_XOR_BE((+i + j*length)&0xfff)];
            }
        }

        ///cpuintrf_pop_context();
        *z64_rspinfo.SP_DMA_BUSY_REG = 0;
        *z64_rspinfo.SP_STATUS_REG  &= ~SP_STATUS_DMABUSY;
    }


}





UINT32 n64_sp_reg_r(UINT32 offset, UINT32 dummy)
{
    switch (offset)
    {
    case 0x00/4:            // SP_MEM_ADDR_REG
        return sp_mem_addr;

    case 0x04/4:            // SP_DRAM_ADDR_REG
        return sp_dram_addr;

    case 0x08/4:            // SP_RD_LEN_REG
        return sp_dma_rlength;

    case 0x10/4:            // SP_STATUS_REG
        return rsp_sp_status;

    case 0x14/4:            // SP_DMA_FULL_REG
        return 0;

    case 0x18/4:            // SP_DMA_BUSY_REG
        return 0;

    case 0x1c/4:            // SP_SEMAPHORE_REG
        return sp_semaphore;

    default:
        log(M64MSG_WARNING, "sp_reg_r: %08X\n", offset);
        break;
    }

    return 0;
}

//UINT32 n64_sp_reg_w(RSP_REGS & rsp, UINT32 offset, UINT32 data, UINT32 dummy)
void n64_sp_reg_w(UINT32 offset, UINT32 data, UINT32 dummy)
{
    UINT32 InterruptPending=0;
    if ((offset & 0x10000) == 0)
    {
        switch (offset & 0xffff)
        {
        case 0x00/4:            // SP_MEM_ADDR_REG
            sp_mem_addr = data;
            break;

        case 0x04/4:            // SP_DRAM_ADDR_REG
            sp_dram_addr = data & 0xffffff;
            break;

        case 0x08/4:            // SP_RD_LEN_REG
            //                              sp_dma_length = data & 0xfff;
            //                              sp_dma_count = (data >> 12) & 0xff;
            //                              sp_dma_skip = (data >> 20) & 0xfff;
            sp_dma_length=data;
            sp_dma(SP_DMA_RDRAM_TO_IDMEM);
            break;

        case 0x0c/4:            // SP_WR_LEN_REG
            //                              sp_dma_length = data & 0xfff;
            //                              sp_dma_count = (data >> 12) & 0xff;
            //                              sp_dma_skip = (data >> 20) & 0xfff;
            sp_dma_length=data;
            sp_dma(SP_DMA_IDMEM_TO_RDRAM);
            break;

        case 0x10/4:            // SP_STATUS_REG
            {
                if((data&0x1)&&(data&0x2)) 
                    log(M64MSG_ERROR, "Clear halt and set halt simultaneously\n");
                if((data&0x8)&&(data&0x10))
                    log(M64MSG_ERROR, "Clear int and set int simultaneously\n");
                if((data&0x20)&&(data&0x40)) 
                    log(M64MSG_ERROR, "Clear sstep and set sstep simultaneously\n");
                if (data & 0x00000001)          // clear halt
                {
                    rsp_sp_status &= ~SP_STATUS_HALT;

                    //                                      if (first_rsp)
                    //                                      {
                    //                                              cpu_spinuntil_trigger(6789);

                    //                                              cpunum_set_input_line(1, INPUT_LINE_HALT, CLEAR_LINE);
                    //                                              rsp_sp_status &= ~SP_STATUS_HALT;
                    //                                      }
                    //                                      else
                    //                                      {
                    //                                              first_rsp = 1;
                    //                                      }
                }
                if (data & 0x00000002)          // set halt
                {
                    //                                      cpunum_set_input_line(1, INPUT_LINE_HALT, ASSERT_LINE);
                    rsp_sp_status |= SP_STATUS_HALT;
                }
                if (data & 0x00000004) rsp_sp_status &= ~SP_STATUS_BROKE;               // clear broke
                if (data & 0x00000008)          // clear interrupt
                {
                    *z64_rspinfo.MI_INTR_REG &= ~R4300i_SP_Intr;
                    ///TEMPORARY COMMENTED FOR SPEED
                    ///               printf("sp_reg_w clear interrupt");
                    //clear_rcp_interrupt(SP_INTERRUPT);
                }
                if (data & 0x00000010)          // set interrupt
                {
                    //signal_rcp_interrupt(SP_INTERRUPT);
                }
                if (data & 0x00000020) rsp_sp_status &= ~SP_STATUS_SSTEP;               // clear single step
                if (data & 0x00000040) {
                    rsp_sp_status |= SP_STATUS_SSTEP;  // set single step
                    log(M64MSG_STATUS, "RSP STATUS REG: SSTEP set\n");
                }
                if (data & 0x00000080) rsp_sp_status &= ~SP_STATUS_INTR_BREAK;  // clear interrupt on break
                if (data & 0x00000100) rsp_sp_status |= SP_STATUS_INTR_BREAK;   // set interrupt on break
                if (data & 0x00000200) rsp_sp_status &= ~SP_STATUS_SIGNAL0;     // clear signal 0
                if (data & 0x00000400) rsp_sp_status |= SP_STATUS_SIGNAL0;      // set signal 0
                if (data & 0x00000800) rsp_sp_status &= ~SP_STATUS_SIGNAL1;     // clear signal 1
                if (data & 0x00001000) rsp_sp_status |= SP_STATUS_SIGNAL1;      // set signal 1
                if (data & 0x00002000) rsp_sp_status &= ~SP_STATUS_SIGNAL2;     // clear signal 2
                if (data & 0x00004000) rsp_sp_status |= SP_STATUS_SIGNAL2;      // set signal 2
                if (data & 0x00008000) rsp_sp_status &= ~SP_STATUS_SIGNAL3;     // clear signal 3
                if (data & 0x00010000) rsp_sp_status |= SP_STATUS_SIGNAL3;      // set signal 3
                if (data & 0x00020000) rsp_sp_status &= ~SP_STATUS_SIGNAL4;     // clear signal 4
                if (data & 0x00040000) rsp_sp_status |= SP_STATUS_SIGNAL4;      // set signal 4
                if (data & 0x00080000) rsp_sp_status &= ~SP_STATUS_SIGNAL5;     // clear signal 5
                if (data & 0x00100000) rsp_sp_status |= SP_STATUS_SIGNAL5;      // set signal 5
                if (data & 0x00200000) rsp_sp_status &= ~SP_STATUS_SIGNAL6;     // clear signal 6
                if (data & 0x00400000) rsp_sp_status |= SP_STATUS_SIGNAL6;      // set signal 6
                if (data & 0x00800000) rsp_sp_status &= ~SP_STATUS_SIGNAL7;     // clear signal 7
                if (data & 0x01000000) rsp_sp_status |= SP_STATUS_SIGNAL7;      // set signal 7

                if(InterruptPending==1)
                {
                    *z64_rspinfo.MI_INTR_REG |= 1;
                    z64_rspinfo.CheckInterrupts();
                    InterruptPending=0;
                }
                break;
            }

        case 0x1c/4:            // SP_SEMAPHORE_REG
            sp_semaphore = data;
            //      mame_printf_debug("sp_semaphore = %08X\n", sp_semaphore);
            break;

        default:
            log(M64MSG_WARNING, "sp_reg_w: %08X, %08X\n", data, offset);
            break;
        }
    }
    else
    {
        switch (offset & 0xffff)
        {
        case 0x00/4:            // SP_PC_REG
            //cpunum_set_info_int(1, CPUINFO_INT_PC, 0x04001000 | (data & 0xfff));
            //break;

        default:
            log(M64MSG_WARNING, "sp_reg_w: %08X, %08X\n", data, offset);
            break;
        }
    }
}

UINT32 sp_read_reg(UINT32 reg)
{
    switch (reg)
    {
        //case 4:       return rsp_sp_status;
    default:        return n64_sp_reg_r(reg, 0x00000000);
    }
}


void sp_write_reg(UINT32 reg, UINT32 data)
{
    switch (reg)
    {
    default:        n64_sp_reg_w(reg, data, 0x00000000); break;
    }
}
