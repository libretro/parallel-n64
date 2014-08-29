/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Branch types and their determination      *
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

#ifndef __NEB_DYNAREC_BRANCHES_H__
#define __NEB_DYNAREC_BRANCHES_H__

#include <stdint.h>

/* A branch is specified to target a Nintendo 64 instruction, i.e. the first
 * native instruction that implements the Nintendo 64 instructions. */
#define BRANCH_N64            0x4000
/* A branch is specified to target a native instruction, which may implement a
 * part of a Nintendo 64 instruction. */
#define BRANCH_NATIVE         0x8000

/* A branch is specified to occur conditionally. */
#define BRANCH_CONDITIONAL    0x0001
/* A branch is specified to occur unconditionally. */
#define BRANCH_UNCONDITIONAL  0x0002
/* A branch is specified to place a return address link into $31 on the
 * Nintendo 64. */
#define BRANCH_LINK           0x0004
/* A branch is specified to be "likely" on the Nintendo 64. This means that
 * the instruction following the branch (its delay slot) is executed only if
 * the branch is taken. */
#define BRANCH_LIKELY         0x0008

/* A branch targets a Nintendo 64 instruction that is contained in the same
 * memory page as the block being compiled to native code. */
#define BRANCH_IN_PAGE        0x0100
/* A branch targets a Nintendo 64 instruction that is contained in a memory
 * page other than the one containing the block being compiled to native
 * code. */
#define BRANCH_OUT_OF_PAGE    0x0200

/* A branch targets an instruction that has yet to be compiled (Nintendo 64)
 * or is ahead of the current instruction (native), so the actual
 * destination of the branch has to be written later. */
#define BRANCH_FORWARD        0x0400
/* A branch targets an instruction that has been compiled (Nintendo 64) or
 * emitted (native) already, so the actual destination of the branch can be
 * written immediately. */
#define BRANCH_BACKWARD       0x0800

/* A branch targets an absolute address on the Nintendo 64, or can target an
 * absolute address into native memory in native code. */
#define BRANCH_ABSOLUTE       0x0010
/* A branch targets a relative address. On the Nintendo 64, this is used for
 * conditional branches (+/- 128 KiB), including conditional jumps that are
 * really unconditional. In native code, this is used for branches within code
 * that may be moved. */
#define BRANCH_RELATIVE       0x0020
/* A branch on the Nintendo 64 is to an address contained within a register.
 * This sort of branch must be resolved dynamically, unless it can be proven
 * that the register contains a constant address, in which case it becomes a
 * BRANCH_ABSOLUTE. */
#define BRANCH_VIA_REGISTER   0x1000
/* A branch in native code uses the short form allowed by the architecture.
 * On some architectures, short forms are allowed for absolute branches, if
 * some conditions are met for the source and target addresses. Short forms
 * are often available for relative branches. */
#define BRANCH_SHORT          0x0040
/* A branch in native code uses the long form allowed by the architecture.
 * It is used when the branch is to an absolute address, or to a relative
 * address outside of the allowed range of short relative branch instructions,
 * or in the case of a forward branch when it would be expensive to evaluate
 * whether a branch can be emitted as the short form or the long form after
 * the branch target gets compiled. */
#define BRANCH_LONG           0x0080
/* A branch on the Nintendo 64 has a delay slot - not SYSCALL and ERET. */
#define BRANCH_DELAY_SLOT     0x2000

#endif /* !__NEB_DYNAREC_BRANCHES_H__ */
