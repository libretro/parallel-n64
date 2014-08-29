/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Cross-platform driver                     *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2013-2014 Nebuleon                                      *
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

#ifndef __NEB_DYNAREC_DRIVER_H__
#define __NEB_DYNAREC_DRIVER_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../recomp.h"
#include "../ops.h" /* for cpu_instruction_table */

#define NEB_DYNAREC_MIPSEL32 1
#define NEB_DYNAREC_MIPSEB32 2
#define NEB_DYNAREC_MIPSEL64 3
#define NEB_DYNAREC_MIPSEB64 4
#define NEB_DYNAREC_ARMEL32  5
#define NEB_DYNAREC_ARMEB32  6
#define NEB_DYNAREC_ARMEL64  7
#define NEB_DYNAREC_ARMEB64  8

#define NEB_DYNAREC_I386     9
#define NEB_DYNAREC_AMD64    10

#if NEB_DYNAREC == NEB_DYNAREC_MIPSEL32
#  include "mipsel32/config.h"
#elif NEB_DYNAREC == NEB_DYNAREC_AMD64
#  include "amd64/config.h"
#elif defined(NEB_DYNAREC)
#  error "No architecture configuration file is present for this Neb Dynarec"
#endif

extern const cpu_instruction_table neb_fallback_table;

enum nd_invalidate_reason {
	/* The code cache is being emptied because it cannot contain more code. */
	INVALIDATE_FULL_CACHE,
	/* The code cache is being emptied because the floating-point register
	 * layout is being changed to MIPS I. */
	INVALIDATE_FPR_LAYOUT_MIPS_I,
	/* The code cache is being emptied because the floating-point register
	 * layout is being changed to MIPS III. */
	INVALIDATE_FPR_LAYOUT_MIPS_III,
};

typedef enum nd_invalidate_reason nd_invalidate_reason_t;

/* Describes the current layout of floating-point registers (FPRs) on the
 * Nintendo 64's side. One of two different layouts may be selected by a game,
 * or the game may switch between them.
 */
enum fpr_layout {
	/* The MIPS I layout for floating-point registers. In this layout,
	 * 32-bit values ('float') may occupy any FPR ($f0 to $f31), and
	 * 64-bit values ('double') may only occupy even-numbered FPRs
	 * ($f0, $f2, ..., $f30). MFC1, MTC1, LWC1 and SWC1 may read (merge)
	 * or write (split) parts of registers containing 'double' values;
	 * LDC1 and SDC1 may be used to read (split) or write (merge) either
	 * two 'float' values at once or a 'double' value.
	 * 
	 * When allocating a floating-point register in the MIPS I layout,
	 * an operation reading a 'double' value while either the even or
	 * the odd register contains a 'float' value must first write the
	 * 'float' value(s), then reinterpret the result as 'double'.
	 * An operation reading a 'float' value while the register pair
	 * containing it contains a 'double' value must first write the
	 * 'double' value, then reinterpret part of the result as 'float'.
	 * 
	 * Operations may assume that a certain numbered FPR will always be
	 * stored at the same location in Mupen's FPR storage.
	 */
	FPR_LAYOUT_MIPS_I,
	/* The MIPS III layout for floating-point registers. In this layout,
	 * any FPR may contain a 32-bit ('float') or 64-bit ('double') value.
	 * MFC1, MTC1, LWC1, SWC1, DMFC1, DMTC1, LDC1 and SDC1 do not need
	 * to split or merge any register.
	 * 
	 * Operations may assume that a certain numbered FPR will always be
	 * stored at the same location in Mupen's FPR storage.
	 */
	FPR_LAYOUT_MIPS_III,
	/* This pseudo-layout is used when the current game switches often
	 * between the MIPS I and MIPS III layouts.
	 * 
	 * Operations may not assume that a certain numbered FPR will always
	 * be stored at the same location in Mupen's FPR storage, and must
	 * reget the address at which the FPR is stored whenever an FPR must
	 * be loaded or stored. (single indirection)
	 * 
	 * An MTC0 instruction targetting Coprocessor 0 register 12 (Status)
	 * bit 26, 'Floating-point register width selection', may change the
	 * FPR layout.
	 */
	FPR_LAYOUT_UNKNOWN,
};

typedef enum fpr_layout fpr_layout_t;

void nd_init(void);
void nd_exit(void);
void nd_init_page(precomp_block* page);
void nd_free_page(precomp_block* page);

void nd_invalidate_code(nd_invalidate_reason_t reason);

/* If the architecture requires some initialisation code, the following must
 * be set, and the function
 * void nd_arch_init(void)
 * must be provided in [arch]/functions.c. */
#ifdef ARCH_NEED_INIT
void nd_arch_init(void);
#endif

/* If the architecture requires some finalisation code, the following must
 * be set, and the function
 * void nd_arch_exit(void)
 * must be provided in [arch]/functions.c. */
#ifdef ARCH_NEED_FINI
void nd_arch_exit(void);
#endif

#endif /* !__NEB_DYNAREC_DRIVER_H__ */
