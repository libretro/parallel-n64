/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - MIPS little-endian functions              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2013 Nebuleon                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdint.h>
#ifndef _MIPS_ARCH_MIPS32R2
#  include <sys/cachectl.h>
#endif

#include "../driver.h"

#include "osal/preproc.h"

#include "api/m64p_types.h"
#include "api/callbacks.h"

#include "assemble.h"

void FlushCodeCache(uint8_t* Code, size_t Length)
{
#ifdef _MIPS_ARCH_MIPS32R2
	void* cur = Code;
	void* end = (unsigned char*) Code + Length;
	unsigned int cont;
	int step_bytes, step_mask;
	__asm__ __volatile__ (
		".set noreorder                          \n"
		/* Read SYNCI_Step and return if using SYNCI is not needed. */
		"  rdhwr %[step_bytes], $1               \n"
		"  beq   %[step_bytes], $0, 5f           \n"
		"  nop                                   \n"
		"  addiu $sp, $sp, -4                    \n"
		"  sw    $ra, 0($sp)                     \n"
		/* Call 1: as a procedure, so jr.hb $ra can clear instruction
		 * hazards and come back here. */
		"  bal   1f                              \n"
		"  addiu %[step_mask], %[step_bytes], -1 \n" /* (delay slot) */
		/* Once 2: returns, substitute the real return address. */
		"  lw    $ra, 0($sp)                     \n"
		"  b     5f                              \n"
		"  addiu $sp, $sp, 4                     \n" /* (delay slot) */
		"1:                                      \n"
		"  beq   %[len], $0, 4f                  \n"
		/* Round down the start to the cache line containing it. */
		"  nor   %[step_mask], %[step_mask], $0  \n"
		"  and   %[cur], %[cur], %[step_mask]    \n"
		/* Make the new code visible. */
		"2:                                      \n"
		".set mips32r2                           \n"
		"  synci 0(%[cur])                       \n"
		".set mips0                              \n"
		"  addu  %[cur], %[cur], %[step_bytes]   \n"
		"  sltu  %[cont], %[cur], %[end]         \n"
		"  bne   %[cont], $0, 2b                 \n"
		"  nop                                   \n"
		/* Clear memory hazards on multiprocessor systems. */
		"  sync                                  \n"
		"3:                                      \n"
		/* Instruction hazard barrier. */
		".set mips32r2                           \n"
		"  jr.hb $ra                             \n"
		".set mips0                              \n"
		"  nop                                   \n"
		"4:                                      \n"
		/* Normal return. */
		"  jr    $ra                             \n"
		"  nop                                   \n"
		"5:                                      \n"
		".set reorder                            \n"
		: /* output */  [step_bytes] "=r" (step_bytes), [step_mask] "=r" (step_mask), [cur] "+r" (cur), [end] "+r" (end), [cont] "=r" (cont)
		: /* input */   [start] "r" (Code), [len] "r" (Length)
		: /* clobber */ "memory"
	);
#else
	cacheflush(native_op, length, BCACHE);
#endif
}

static const uint8_t virtual_int_to_native[ARCH_INT_TEMP_REGISTERS + ARCH_INT_SAVED_REGISTERS] = {
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25,
	16, 17, 18, 19, 20, 21, 22, 23, 30,
};

static uint8_t* output_dword(uint8_t* dst, uint32_t Instruction)
{
	*(uint32_t*) dst = Instruction;
	return dst + 4;
}
