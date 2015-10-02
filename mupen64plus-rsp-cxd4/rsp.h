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
#define NOINLINE    __declspec(noinline)
#define ALIGNED     _declspec(align(16))
#else
#define NOINLINE    __attribute__((noinline))
#define ALIGNED     __attribute__((aligned(16)))
#endif

#ifdef USE_SSE_SUPPORT
#define ARCH_MIN_SSE2
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

NOINLINE void message(const char* body, int priority)
{
    priority &= 03;
    if (priority < MINIMUM_MESSAGE_PRIORITY)
        return;
    printf("%s\n", body);
}

/*
 * This allows us to update the program counter register in the RSP
 * interpreter in a much faster way overall to the running CPU loop.
 *
 * Branch delay slots and such with the MIPS architecture make the
 * PC register emulation complicate the interpreter switches when
 * emulated normally.
 */
#define EMULATE_STATIC_PC

#ifndef EMULATE_STATIC_PC
static int stage;
#endif
static int temp_PC;
#ifdef WAIT_FOR_CPU_HOST
static short MFC0_count[32];
/* Keep one C0 MF status read count for each scalar register. */
#endif

#include "su.h"
#include "vu/vu.h"

/* Allocate the RSP CPU loop to its own functional space. */
NOINLINE extern void run_task(void);
#include "execute.h"

#endif
