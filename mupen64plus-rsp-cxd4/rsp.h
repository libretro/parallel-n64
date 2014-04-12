/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.12.12                                                         *
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
#ifndef _RSP_H_
#define _RSP_H_

#include "Rsp_#1.1.h"
RSP_INFO RSP;

#ifdef _MSC_VER
#define INLINE      __inline
#define NOINLINE    __declspec(noinline)
#define ALIGNED     _declspec(align(16))
#else
#define INLINE      inline
#define NOINLINE    __attribute__((noinline))
#define ALIGNED     __attribute__((aligned(16)))
#endif

/*
 * Streaming SIMD Extensions version import management
 */
#ifdef ARCH_MIN_SSSE3
#define ARCH_MIN_SSE2
#include <tmmintrin.h>
#endif
#ifdef ARCH_MIN_SSE2
#include <emmintrin.h>
#endif

typedef unsigned char byte;

#if !defined(M64P_PLUGIN_API)
NOINLINE void message(const char* body, int priority)
{ /* Avoid SHELL32/ADVAPI32/USER32 dependencies by using standard C to print. */
    char argv[4096] = "CMD /Q /D /C \"TITLE RSP Message&&ECHO ";
    int i = 0;
    int j = strlen(argv);

    priority &= 03;
    if (priority < MINIMUM_MESSAGE_PRIORITY)
        return;

/*
 * I'm just using system() to call the Windows command shell to print text.
 * When the subsystem permits, use printf to trace messages, not this crap.
 * I don't use WIN32 MessageBox because that's just extra OS dependencies. :P
 */
    while (body[i] != '\0')
    {
        if (body[i] == '\n')
        {
            strcat(argv, "&&ECHO ");
            ++i;
            j += 7;
            continue;
        }
        argv[j++] = body[i++];
    }
    strcat(argv, "&&PAUSE&&EXIT\"");
    system(argv);
    return;
}
#else
NOINLINE void message(const char* body, int priority)
{
    priority &= 03;
    if (priority < MINIMUM_MESSAGE_PRIORITY)
        return;
    printf("%s\n", body);
}
#endif

#if !defined(M64P_PLUGIN_API)
/*
 * Update RSP configuration memory from local file resource.
 */
#define CHARACTERS_PER_LINE     (80)
/* typical standard DOS text file limit per line */
NOINLINE void update_conf(const char* source)
{
    FILE* stream;
    char line[CHARACTERS_PER_LINE] = "";
    char key[CHARACTERS_PER_LINE], value[CHARACTERS_PER_LINE];
    register int i, test;

    stream = fopen(source, "r");
    if (stream == NULL)
    { /* try GetModulePath or whatever to correct the path? */
        message("Failed to read config.", 3);
        return;
    }
    do
    {
        int bvalue;

        line[0] = '\0';
        key[0] = '\0';
        value[0] = '\0';
        for (i = 0; i < CHARACTERS_PER_LINE; i++)
        {
            test = fgetc(stream);
            if (test < 0) /* either EOF or invalid ASCII characters */
                break;
            line[i] = (char)(test);
            if (line[i] == '\n')
                break;
        }
        line[i] = '\0';

        for (i = 0; i < CHARACTERS_PER_LINE && line[i] != '='; i++);
        line[i] = '\0';
        strcpy(key, line);
        strcpy(value, line + i + 1);

        bvalue = atoi(value);
        if (strcmp(key, "DisplayListToGraphicsPlugin") == 0)
            CFG_HLE_GFX = (byte)(bvalue);
        else if (strcmp(key, "AudioListToAudioPlugin") == 0)
            CFG_HLE_AUD = (byte)(bvalue);
        else if (strcmp(key, "WaitForCPUHost") == 0)
            CFG_WAIT_FOR_CPU_HOST = bvalue;
        else if (strcmp(key, "SupportCPUSemaphoreLock") == 0)
            CFG_MEND_SEMAPHORE_LOCK = bvalue;
    } while (test != EOF);
    fclose(stream);
    return;
}
#endif

#ifndef EMULATE_STATIC_PC
static int stage;
#endif
static int temp_PC;
#ifdef WAIT_FOR_CPU_HOST
static short MFC0_count[32];
/* Keep one C0 MF status read count for each scalar register. */
#endif

#ifdef SP_EXECUTE_LOG
static FILE *output_log;
extern void step_SP_commands(uint32_t inst);
#endif
extern void export_SP_memory(void);
NOINLINE void trace_RSP_registers(void);

#include "su.h"
#include "vu/vu.h"

/* Allocate the RSP CPU loop to its own functional space. */
NOINLINE extern void run_task(void);
#include "execute.h"

#ifdef SP_EXECUTE_LOG
void step_SP_commands(uint32_t inst)
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
#endif

NOINLINE void export_data_cache(void)
{
    FILE* out;
    register uint32_t addr;

    out = fopen("rcpcache.dhex", "wb");
#if (0)
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000001)
        fputc(RSP.DMEM[BES(addr & 0x00000FFF)], out);
#else
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000004)
    {
        fputc(RSP.DMEM[addr + 0x000 + BES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x001 + MES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x002 - MES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x003 - BES(0x000)], out);
    }
#endif
    fclose(out);
    return;
}
NOINLINE void export_instruction_cache(void)
{
    FILE* out;
    register uint32_t addr;

    out = fopen("rcpcache.ihex", "wb");
#if (0)
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000001)
        fputc(RSP.IMEM[BES(addr & 0x00000FFF)], out);
#else
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000004)
    {
        fputc(RSP.IMEM[addr + 0x000 + BES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x001 + MES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x002 - MES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x003 - BES(0x000)], out);
    }
#endif
    fclose(out);
    return;
}
void export_SP_memory(void)
{
    export_data_cache();
    export_instruction_cache();
    return;
}

const char CR_names[16][14] = {
    "SP_MEM_ADDR  ","SP_DRAM_ADDR ","SP_DMA_RD_LEN","SP_DMA_WR_LEN",
    "SP_STATUS    ","SP_DMA_FULL  ","SP_DMA_BUSY  ","SP_SEMAPHORE ",
    "CMD_START    ","CMD_END      ","CMD_CURRENT  ","CMD_STATUS   ",
    "CMD_CLOCK    ","CMD_BUSY     ","CMD_PIPE_BUSY","CMD_TMEM_BUSY",
};
const char SR_names[32][5] = {
    "zero","$at:"," v0:"," v1:"," a0:"," a1:"," a2:"," a3:",
    " t0:"," t1:"," t2:"," t3:"," t4:"," t5:"," t6:"," t7:",
    " s0:"," s1:"," s2:"," s3:"," s4:"," s5:"," s6:"," s7:",
    " t8:"," t9:"," k0:"," k1:"," gp:","$sp:","$s8:","$ra:",
};
const char* Boolean_names[2] = {
    "false",
    "true"
};

NOINLINE void trace_RSP_registers(void)
{
    register int i;
    FILE* out;

    out = fopen("SP_STATE.TXT", "w");
    fprintf(out, "RCP Communications Register Display\n\n");

/*
 * The precise names for RSP CP0 registers are somewhat difficult to define.
 * Generally, I have tried to adhere to the names shared from bpoint/zilmar,
 * while also merging the concrete purpose and correct assembler references.
 * Whether or not you find these names agreeable is mostly a matter of seeing
 * them from the RCP's point of view or the CPU host's mapped point of view.
 */
    for (i = 0; i < 8; i++)
        fprintf(out, "%s:  %08"PRIX32"    %s:  %08"PRIX32"\n",
            CR_names[i+0], *(CR[i+0]), CR_names[i+8], *(CR[i+8]));
    fprintf(out, "\n");
/*
 * There is no memory map for remaining registers not shared by the CPU.
 * The scalar register (SR) file is straightforward and based on the
 * GPR file in the MIPS ISA.  However, the RSP assembly language is still
 * different enough from the MIPS assembly language, in that tokens such as
 * "$zero" and "$s0" are no longer valid.  "$k0", for example, is not a valid
 * RSP register name because on MIPS it was kernel-use, but on the RSP, free.
 * To be colorful/readable, however, I have set the modern MIPS names anyway.
 */
    for (i = 0; i < 16; i++)
        fprintf(out, "%s  %08X,  %s  %08X,\n",
            SR_names[i+0], SR[i+0], SR_names[i+16], SR[i+16]);
    fprintf(out, "\n");

    for (i = 0; i < 32; i++)
    {
        register int j;

        if (i < 10)
            fprintf(out, "$v%i :  ", i);
        else
            fprintf(out, "$v%i:  ", i);
        for (j = 0; j < N; j++)
            fprintf(out, "[%04hX]", VR[i][j]);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");

/*
 * The SU has its fair share of registers, but the VU has its counterparts.
 * Just like we have the scalar 16 system control registers for the RSP CP0,
 * we have also a tiny group of special-purpose, vector control registers.
 */
    fprintf(out, "$%s:  0x%04X\n", tokens_CR_V[0], get_VCO());
    fprintf(out, "$%s:  0x%04X\n", tokens_CR_V[1], get_VCC());
    fprintf(out, "$%s:  0x%02X\n", tokens_CR_V[2], get_VCE());
    fprintf(out, "\n");

/*
 * 48-bit RSP accumulators
 * Most vector unit patents traditionally call this register file "VACC".
 * However, typically in discussion about SGI's implementation, we say "ACC".
 */
    for (i = 0; i < 8; i++)
    {
        register int j;

        fprintf(out, "ACC[%o]:  ", i);
        for (j = 0; j < 3; j++)
            fprintf(out, "[%04hX]", VACC[j][i]);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");

/*
 * special-purpose vector unit registers for vector divide operations only
 */
    fprintf(out, "%s:  %i\n", "DivIn", DivIn);
    fprintf(out, "%s:  %i\n", "DivOut", DivOut);
    /* MovIn:  This reg. might exist for VMOV, but it is obsolete to emulate. */
    fprintf(out, "DPH:  %s\n", Boolean_names[DPH]);
    fclose(out);
    return;
}
#endif
