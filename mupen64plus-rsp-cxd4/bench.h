/******************************************************************************\
* Project:  Simple Vector Unit Benchmark                                       *
* Authors:  Iconoclast                                                         *
* Release:  2013.10.08                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/
#ifndef _BENCH_H
#define _BENCH_H

/*
 * Since operations on scalar registers are much more predictable,
 * standardized, and documented, we don't really need to bench those.
 *
 * I was thinking I should also add MTC0 (SP DMA and writes to SP_STATUS_REG)
 * and, due to the SSE-style flags register file, CFC2 and CTC2, but I mostly
 * just wanted to hurry up and make this header quick with all the basics. :P
 *
 * Fortunately, because all the methods are static (no conditional jumps),
 * we can lazily leave the instruction word set to 0x00000000 for all the
 * op-codes we are benching, and it will make no difference in speed.
 */

#include <time.h>
#include "rsp.h"

#define NUMBER_OF_VU_OPCODES    38

static void (*bench_tests[NUMBER_OF_VU_OPCODES])(void) = {
    VMULF, VMACF, /* signed single-precision fractions */
    VMULU, VMACU, /* unsigned single-precision fractions */

    VMUDL, VMADL, /* double-precision multiplies using partial products */
    VMUDM, VMADM,
    VMUDN, VMADN,
    VMUDH, VMADH,

    VADD, VSUB, VABS,
    VADDC, VSUBC,
    VSAW,

    VLT, VEQ, VNE, VGE, /* normal select compares */
    VCH, VCL, /* double-precision clip select */
    VCR, /* single-precision, one's complement */
    VMRG,

    VAND, VNAND,
    VOR , VNOR ,
    VXOR, VNXOR,

    VRCPL, VRSQL, /* double-precision reciprocal look-ups */
    VRCPH, VRSQH,

    VMOV, NOP
/*
 * Careful:  Not "VNOP", because I put a message there to find ROMs using it.
 * We bench the normal NOP function from su.h to compare this dummy function.
 */
};

const char test_names[NUMBER_OF_VU_OPCODES][8] = {
    "VMULF  ","VMACF  ",
    "VMULU  ","VMACU  ",

    "VMUDL  ","VMADL  ",
    "VMUDM  ","VMADM  ",
    "VMUDN  ","VMADN  ",
    "VMUDH  ","VMADH  ",

    "VADD   ","VSUB   ","VABS   ",
    "VADDC  ","VSUBC  ",
    "VSAW   ",

    "VLT    ","VEQ    ","VNE    ","VGE    ",
    "VCH    ","VCL    ",
    "VCR    ",
    "VMRG   ",

    "VAND   ","VNAND  ",
    "VOR    ","VNOR   ",
    "VXOR   ","VNXOR  ",

    "VRCPL  ","VRSQL  ",
    "VRCPH  ","VRSQH  ",

    "VMOV   ","VNOP   "
};

const char* notice_starting =
    "Ready to start benchmarks.\n"\
    "Close this message to commence tests.  Testing could take minutes.";
const char* notice_finished =
    "Finished writing benchmark results.\n"\
    "Check working emulator directory for \"sp_bench.txt\".";

unsigned char t_DMEM[0xFFF + 1], t_IMEM[0xFFF + 1];

EXPORT void CALL DllTest(HWND hParent)
{
    FILE* log;
    clock_t t1, t2;
    register int i, j;
    register float delta, total;

    if (RSP.RDRAM != NULL)
    {
        message("Cannot run RSP tests while playing!", 3);
        return;
    }
    inst.R.rs = 0x8; /* just to shut up VSAW illegal element warnings */

    message(notice_starting, 1);
    log = fopen("sp_bench.txt", "w");
    fprintf(log, "RSP Vector Benchmarks Log\n\n");

    total = 0.0;
    for (i = 0; i < NUMBER_OF_VU_OPCODES; i++)
    {
        t1 = clock();
        for (j = -0x1000000; j < 0; j++)
            bench_tests[i]();
        t2 = clock();
        delta = (float)(t2 - t1) / CLOCKS_PER_SEC;
        fprintf(log, "%s:  %.3f s\n", test_names[i], delta);
        total += delta;
    }
    fprintf(log, "Total time spent:  %.3f s\n", total);
    fclose(log);
    message(notice_finished, 1);
    return;
}
#endif
