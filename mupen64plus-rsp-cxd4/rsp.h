/*
 * mupen64plus-rsp-cxd4 - RSP Interpreter
 * Copyright (C) 2012-2013  RJ 'Iconoclast' Swedlow
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
 */

#ifndef _RSP_H_
#define _RSP_H_

#include "Rsp_#1.1.h"
static RSP_INFO RSP;
#ifdef _MSC_VER
inline int MessageBoxA(
    HWND hWnd, const char *lpText, const char *lpCaption, unsigned int uType)
{
    uType = 0x00000000;
    if (*(lpText + 0) == *(lpCaption + 0)) /* unused variables */
        hWnd = NULL;
    return (0);
} /* not going to maintain message boxes on the Microsoft compilers */
inline void message(char *body, int priority)
{
    priority ^= priority;
    *(body + 0) = '\0';
    return; /* Why?  Because I am keeping Win32-only builds dependency-free. */
} /* The primary target is GNU/GCC (cross-OS portability, free of APIs). */
#else
#if defined(WIN32)
__declspec(dllimport) int __stdcall MessageBoxA(
    HWND hWnd,
    const char *lpText,
    const char *lpCaption,
    unsigned int uType);
#else
int MessageBoxA(void *hwnd,
    const char *lpText,
    const char *lpCaption,
    unsigned int uType)
{
    printf("%s: %s\n", lpCaption, lpText);
    return 0;
}
#endif
/* No need to import the Windows API, just a message trace function. */
void message(char *body, int priority)
{
    const unsigned int type_index[4] = {
        0x00000000, /* no icon or effect `MB_OK`, for O.K. encounters */
        0x00000020, /* MB_ICONQUESTION -- curious situation in emulator */
        0x00000030, /* MB_ICONEXCLAMATION -- might be missing RSP support */
        0x00000010  /* MB_ICONHAND -- definite error or problem in emulator */
    };

    priority &= 03;
    switch (MINIMUM_MESSAGE_PRIORITY)
    { /* exit table for voiding messages of lower priority */
        default:  return;
        case 03:  if (priority < MINIMUM_MESSAGE_PRIORITY) return;
        case 02:  if (priority < MINIMUM_MESSAGE_PRIORITY) return;
        case 01:  if (priority < MINIMUM_MESSAGE_PRIORITY) return;
        case 00:  break;
    }
    MessageBoxA(NULL, body, NULL, type_index[priority]);
    return;
}
#endif

static int temp_PC;
#ifdef WAIT_FOR_CPU_HOST
static int MFC0_count[32];
/* Keep one C0 MF status read count for each scalar register. */
#endif

#define BES(address) (address ^ 03)
/* Do a swap on the byte endian on a 32-bit segment boundary. */
#define HES(address) (address ^ 02)
/* Do a swap on the halfword endian on a 32-bit segment boundary. */
#define MES(address) (address ^ 01)
/* Do a mixed endian swap, intermediating between byte and halfword bounds. */
#define WES(address) (address ^ 00)
/* Because MIPS and Win32 machines are both 32 bits, no endian update needed. */

// #define VR_B(v, e) (((unsigned char *)VR[v])[(e) ^ 0x1])
// #define VR_B(v, e) ((((unsigned char *)(VR+v))[(e) ^ 0x1]))
#define VR_B(v, e) (*(unsigned char *)(((unsigned char *)(VR+v)) + ((e) ^ 0x1)))
/* In `vu.h` we have defined `static short VR[32][8]`, a proper two-
 * dimensional array for accurately storing real signal vectors (big endian).
 *
 * The weakness to this is that it fixates all VR indexing to 16-bit shorts.
 * We can still use "pointer" indirection if we need to target by octet.
 */
#define VR_S(v, e) (*(short *)((unsigned char *)(*(VR + v)) + ((e + 01) & ~01)))
/* Say we are emulating:  `LSV $v0[0x0], 0x000($0)`.
 * We can accurately use a proper vector file:  `VR[0][00] = *(short *)addr`.
 *
 * What about:  `LSV $v0[0x1], 0x000($0)`?
 *
 * That is what the macro above is for.  `VR_S(0, 0x1) = *(short *)addr`.
 * With this we can span across the vector register element indexing barrier.
 */
#define VR_H(v, e) (*(short *)((unsigned char *)(*(VR + v)) + e))
/* The VR_S macro above is more stable but slower.
 * In some cases, we may as well adjust the elemental offset, if it is odd.
 * If this is made flexible in advance, we can just use this macro to finish.
 */

#include "su/su.h"
#include "vu/vu.h"

#ifdef SP_EXECUTE_LOG
extern void step_SP_commands(unsigned long inst);
extern void export_SP_memory(void);
extern void trace_RSP_registers(void);
static FILE *output_log;
#endif

/* Allocate the RSP CPU loop to its own functional space. */
extern void run_task(void);
#include "execute.h"

#ifdef SP_EXECUTE_LOG
void step_SP_commands(unsigned long inst)
{
    if (output_log)
    {
        const char digits[16] = {
            '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
        };
        char text[256];
        char offset[4] = "";
        char code[9] = "";
        unsigned char endian_swap[4];

        endian_swap[00] = (unsigned char)(inst >> 24);
        endian_swap[01] = (unsigned char)(inst >> 16);
        endian_swap[02] = (unsigned char)(inst >>  8);
        endian_swap[03] = (unsigned char)inst;
        offset[00] = digits[(*RSP.SP_PC_REG & 0xF00) >> 8];
        offset[01] = digits[(*RSP.SP_PC_REG & 0x0F0) >> 4];
        offset[02] = digits[(*RSP.SP_PC_REG & 0x00F) >> 0];
        code[00] = digits[(inst & 0xF0000000) >> 28];
        code[01] = digits[(inst & 0x0F000000) >> 24];
        code[02] = digits[(inst & 0x00F00000) >> 20];
        code[03] = digits[(inst & 0x000F0000) >> 16];
        code[04] = digits[(inst & 0x0000F000) >> 12];
        code[05] = digits[(inst & 0x00000F00) >>  8];
        code[06] = digits[(inst & 0x000000F0) >>  4];
        code[07] = digits[(inst & 0x0000000F) >>  0];
        strcpy(text, offset);
        strcat(text, "\n");
        strcat(text, code);
        message(text, 0); /* PC offset, MIPS hex. */
        if (output_log == NULL) {} else /* Global pointer not updated?? */
            fwrite(endian_swap, 4, 1, output_log);
    }
}

void export_SP_memory(void)
{ /* cache memory and dynamic RAM shared by CPU */
    FILE *out = fopen("SP_CACHE.BIN", "wb");
    fwrite(RSP.DMEM, sizeof(unsigned char), 0x1FFF + 1, out);
    fclose(out);
    out = fopen("SP_DRAM.BIN", "wb");
    fwrite(RSP.RDRAM, sizeof(unsigned char), 0x3FFFFF + 1, out);
    return;
}

void trace_RSP_registers(void)
{ /* no interface--using file I/O only */
    FILE *out = fopen("SP_STATE.TXT", "w");

    fprintf(out, "RCP Communications Register Display\n\n");
    fclose(out);
    out = fopen("SP_STATE.TXT", "a");
/* The precise names for RSP CP0 registers are somewhat difficult to define.
 * Generally, I have tried to adhere to the names shared from bpoint/zilmar,
 * while also merging the concrete purpose and correct assembler references.
 * Whether or not you find these names agreeable is mostly a matter of seeing
 * them from the RCP's point of view or the CPU host's mapped point of view.
 */
    fprintf(out, "SP_MEM_ADDR:    %08X    CMD_START:      %08X\n",
        *RSP.SP_MEM_ADDR_REG, *RSP.DPC_START_REG);
    fprintf(out, "SP_DRAM_ADDR:   %08X    CMD_END:        %08X\n",
        *RSP.SP_DRAM_ADDR_REG, *RSP.DPC_END_REG);
    fprintf(out, "SP_DMA_RD_LEN:  %08X    CMD_CURRENT:    %08X\n",
        *RSP.SP_RD_LEN_REG, *RSP.DPC_CURRENT_REG);
    fprintf(out, "SP_DMA_WR_LEN:  %08X    CMD_STATUS:     %08X\n",
        *RSP.SP_WR_LEN_REG, *RSP.DPC_STATUS_REG);
    fprintf(out, "SP_STATUS:      %08X    CMD_CLOCK:      %08X\n",
        *RSP.SP_STATUS_REG, *RSP.DPC_CLOCK_REG);
    fprintf(out, "SP_DMA_FULL:    %08X    CMD_BUSY:       %08X\n",
        *RSP.SP_DMA_FULL_REG, *RSP.DPC_BUFBUSY_REG);
    fprintf(out, "SP_DMA_BUSY:    %08X    CMD_PIPE_BUSY:  %08X\n",
        *RSP.SP_DMA_BUSY_REG, *RSP.DPC_PIPEBUSY_REG);
    fprintf(out, "SP_SEMAPHORE:   %08X    CMD_TMEM_BUSY:  %08X\n",
        *RSP.SP_SEMAPHORE_REG, *RSP.DPC_TMEM_REG);
    fprintf(out, "SP_PC_REG:      04001%03X\n\n", *RSP.SP_PC_REG & 0x00000FFF);
/* (PC is only from the CPU point of view, mapped between both halves.) */
/* There is no memory map for remaining registers not shared by the CPU.
 * The scalar register (SR) file is straightforward and based on the
 * GPR file in the MIPS ISA.  However, the RSP assembly language is still
 * different enough from the MIPS assembly language, in that tokens such as
 * "$zero" and "$s0" are no longer valid.  "$k0", for example, is not a valid
 * RSP register name because on MIPS it was kernel-use, but on the RSP free.
 * To be colorful/readable, however, I have set the modern MIPS names anyway.
 */
    fprintf(out, "zero  %08X,  s0:  %08X,\n", SR[ 0], SR[16]);
    fprintf(out, "$at:  %08X,  s1:  %08X,\n", SR[ 1], SR[17]);
    fprintf(out, " v0:  %08X,  s2:  %08X,\n", SR[ 2], SR[18]);
    fprintf(out, " v1:  %08X,  s3:  %08X,\n", SR[ 3], SR[19]);
    fprintf(out, " a0:  %08X,  s4:  %08X,\n", SR[ 4], SR[20]);
    fprintf(out, " a1:  %08X,  s5:  %08X,\n", SR[ 5], SR[21]);
    fprintf(out, " a2:  %08X,  s6:  %08X,\n", SR[ 6], SR[22]);
    fprintf(out, " a3:  %08X,  s7:  %08X,\n", SR[ 7], SR[23]);
    fprintf(out, " t0:  %08X,  t8:  %08X,\n", SR[ 8], SR[24]);
    fprintf(out, " t1:  %08X,  t9:  %08X,\n", SR[ 9], SR[25]);
    fprintf(out, " t2:  %08X,  k0:  %08X,\n", SR[10], SR[26]);
    fprintf(out, " t3:  %08X,  k1:  %08X,\n", SR[11], SR[27]);
    fprintf(out, " t4:  %08X,  gp:  %08X,\n", SR[12], SR[28]);
    fprintf(out, " t5:  %08X, $sp:  %08X,\n", SR[13], SR[29]);
    fprintf(out, " t6:  %08X, $fp:  %08X,\n", SR[14], SR[30]);
    fprintf(out, " t7:  %08X, $ra:  %08X\n\n", SR[15], SR[31]);
/* (Yes, I rebelliously used the MIPS32 "$fp" modern alias over lazy "$s8".) */
/* The RSP vector registers are incontestedly named $v[n] and nothing else.
 * The problem is organizing the contents by HW/B elements and proper endian.
 */
    fprintf(out, " $v0:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 0][00], VR[ 0][01], VR[ 0][02], VR[ 0][03],
        VR[ 0][04], VR[ 0][05], VR[ 0][06], VR[ 0][07]);
    fprintf(out, " $v1:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 1][00], VR[ 1][01], VR[ 1][02], VR[ 1][03],
        VR[ 1][04], VR[ 1][05], VR[ 1][06], VR[ 1][07]);
    fprintf(out, " $v2:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 2][00], VR[ 2][01], VR[ 2][02], VR[ 2][03],
        VR[ 2][04], VR[ 2][05], VR[ 2][06], VR[ 2][07]);
    fprintf(out, " $v3:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 3][00], VR[ 3][01], VR[ 3][02], VR[ 3][03],
        VR[ 3][04], VR[ 3][05], VR[ 3][06], VR[ 3][07]);
    fprintf(out, " $v4:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 4][00], VR[ 4][01], VR[ 4][02], VR[ 4][03],
        VR[ 4][04], VR[ 4][05], VR[ 4][06], VR[ 4][07]);
    fprintf(out, " $v5:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 5][00], VR[ 5][01], VR[ 5][02], VR[ 5][03],
        VR[ 5][04], VR[ 5][05], VR[ 5][06], VR[ 5][07]);
    fprintf(out, " $v6:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 6][00], VR[ 6][01], VR[ 6][02], VR[ 6][03],
        VR[ 6][04], VR[ 6][05], VR[ 6][06], VR[ 6][07]);
    fprintf(out, " $v7:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 7][00], VR[ 7][01], VR[ 7][02], VR[ 7][03],
        VR[ 7][04], VR[ 7][05], VR[ 7][06], VR[ 7][07]);
    fprintf(out, " $v8:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 8][00], VR[ 8][01], VR[ 8][02], VR[ 8][03],
        VR[ 8][04], VR[ 8][05], VR[ 8][06], VR[ 8][07]);
    fprintf(out, " $v9:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[ 9][00], VR[ 9][01], VR[ 9][02], VR[ 9][03],
        VR[ 9][04], VR[ 9][05], VR[ 9][06], VR[ 9][07]);
    fprintf(out, "$v10:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[10][00], VR[10][01], VR[10][02], VR[10][03],
        VR[10][04], VR[10][05], VR[10][06], VR[10][07]);
    fprintf(out, "$v11:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[11][00], VR[11][01], VR[11][02], VR[11][03],
        VR[11][04], VR[11][05], VR[11][06], VR[11][07]);
    fprintf(out, "$v12:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[12][00], VR[12][01], VR[12][02], VR[12][03],
        VR[12][04], VR[12][05], VR[12][06], VR[12][07]);
    fprintf(out, "$v13:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[13][00], VR[13][01], VR[13][02], VR[13][03],
        VR[13][04], VR[13][05], VR[13][06], VR[13][07]);
    fprintf(out, "$v14:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[14][00], VR[14][01], VR[14][02], VR[14][03],
        VR[14][04], VR[14][05], VR[14][06], VR[14][07]);
    fprintf(out, "$v15:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[15][00], VR[15][01], VR[15][02], VR[15][03],
        VR[15][04], VR[15][05], VR[15][06], VR[15][07]);
    fprintf(out, "$v16:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[16][00], VR[16][01], VR[16][02], VR[16][03],
        VR[16][04], VR[16][05], VR[16][06], VR[16][07]);
    fprintf(out, "$v17:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[17][00], VR[17][01], VR[17][02], VR[17][03],
        VR[17][04], VR[17][05], VR[17][06], VR[17][07]);
    fprintf(out, "$v18:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[18][00], VR[18][01], VR[18][02], VR[18][03],
        VR[18][04], VR[18][05], VR[18][06], VR[18][07]);
    fprintf(out, "$v19:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[19][00], VR[19][01], VR[19][02], VR[19][03],
        VR[19][04], VR[19][05], VR[19][06], VR[19][07]);
    fprintf(out, "$v20:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[20][00], VR[20][01], VR[20][02], VR[20][03],
        VR[20][04], VR[20][05], VR[20][06], VR[20][07]);
    fprintf(out, "$v21:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[21][00], VR[21][01], VR[21][02], VR[21][03],
        VR[21][04], VR[21][05], VR[21][06], VR[21][07]);
    fprintf(out, "$v22:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[22][00], VR[22][01], VR[22][02], VR[22][03],
        VR[22][04], VR[22][05], VR[22][06], VR[22][07]);
    fprintf(out, "$v23:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[23][00], VR[23][01], VR[23][02], VR[23][03],
        VR[23][04], VR[23][05], VR[23][06], VR[23][07]);
    fprintf(out, "$v24:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[24][00], VR[24][01], VR[24][02], VR[24][03],
        VR[24][04], VR[24][05], VR[24][06], VR[24][07]);
    fprintf(out, "$v25:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[25][00], VR[25][01], VR[25][02], VR[25][03],
        VR[25][04], VR[25][05], VR[25][06], VR[25][07]);
    fprintf(out, "$v26:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[26][00], VR[26][01], VR[26][02], VR[26][03],
        VR[26][04], VR[26][05], VR[26][06], VR[26][07]);
    fprintf(out, "$v27:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[27][00], VR[27][01], VR[27][02], VR[27][03],
        VR[27][04], VR[27][05], VR[27][06], VR[27][07]);
    fprintf(out, "$v28:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[28][00], VR[28][01], VR[28][02], VR[28][03],
        VR[28][04], VR[28][05], VR[28][06], VR[28][07]);
    fprintf(out, "$v29:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[29][00], VR[29][01], VR[29][02], VR[29][03],
        VR[29][04], VR[29][05], VR[29][06], VR[29][07]);
    fprintf(out, "$v30:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n",
        VR[30][00], VR[30][01], VR[30][02], VR[30][03],
        VR[30][04], VR[30][05], VR[30][06], VR[30][07]);
    fprintf(out, "$v31:  [%04X][%04X][%04X][%04X][%04X][%04X][%04X][%04X]\n\n"
      , VR[31][00], VR[31][01], VR[31][02], VR[31][03],
        VR[31][04], VR[31][05], VR[31][06], VR[31][07]);
/* The SU has its fair share of registers, but the VU has its counterparts.
 * Just like we have the scalar 16 system control registers for the RSP CP0,
 * we have also a tiny group of special-purpose, vector control registers.
 */
    fprintf(out, "$vco:  [%02X][%02X]\n", (VCO >> 8), VCO & 0x00FF);
    fprintf(out, "$vcc:  [%02X][%02X]\n", (VCC >> 8), VCC & 0x00FF);
    fprintf(out, "$vce:  %02X\n\n", VCE); /* 8-bit vector cnd. flags register */
/* Last and least are the 48-bit accumulator elements, which literally may or
 * may not be considered as "registers" but are still useful to debug, in
 * spite of how near-pointless emulating them is (mostly VSAR/VSAW, VMAC*).
 *
 * I have not confirmed memory endianness controversies for ordering bytes.
 * I decided, better to print 48 bits, not 3 pairs of 16, cause basically
 * every write to the accumulator updates potentially all 48 bits, not 1 HW.
 * ("%012X" seems a better idea than [%04X][%04X][%04X], similar to above.)
 * Without VSAR/VSAW, all single HW R/W's are the low 16 bits of acc. only.
 */
    fprintf(out, "VACC[0]:  %012X\n", VACC[00].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[1]:  %012X\n", VACC[01].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[2]:  %012X\n", VACC[02].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[3]:  %012X\n", VACC[03].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[4]:  %012X\n", VACC[04].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[5]:  %012X\n", VACC[05].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[6]:  %012X\n", VACC[06].DW & 0xFFFFFFFFFFFF);
    fprintf(out, "VACC[7]:  %012X\n\n", VACC[07].DW & 0xFFFFFFFFFFFF);
/* I lied (sort of).  Can't possibly forget the internal registers used by
 * the computational vector divide operations!  (Hmmm okay now I lied.)
 */
    fprintf(out, "DivIn:  %i\n", DivIn);
    fprintf(out, "DivOut:  %i\n", DivOut);
    /* MovIn:  This reg. might exist for VMOV, but it is obsolete to emulate. */
    fprintf(out, DPH ? "DPH:  true\n" : "DPH:  false\n");
    fclose(out);
    return;
}
#endif

#endif
