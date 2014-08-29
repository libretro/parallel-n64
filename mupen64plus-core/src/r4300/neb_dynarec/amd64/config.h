/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - AMD64 (little-endian) configuration       *
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

#ifndef __NEB_DYNAREC_AMD64_CONFIG_H__
#define __NEB_DYNAREC_AMD64_CONFIG_H__

/* Mandatory. The size, in bytes, of the cache used to hold recompiled code on
 * this platform. */
#define CODE_CACHE_SIZE (1 << 25) /* 32 MiB */

/* For the rest, refer to driver.h. */

/* - - - ARCHITECTURE SETTINGS - - - */

// #define ARCH_NEED_INIT
// #define ARCH_NEED_EXIT

#define ARCH_HAS_64BIT_REGS

#define ARCH_IS_LITTLE_ENDIAN
// #define ARCH_IS_BIG_ENDIAN

#define ARCH_INT_TEMP_REGISTERS   9 /* RAX, RCX, RDX, RSI, RDI, R8..R11 */
#define ARCH_INT_SAVED_REGISTERS  5 /* RBX, R12..R15 */
#define ARCH_FP_TEMP_REGISTERS   16 /* XMM0..XMM15 */
#define ARCH_FP_SAVED_REGISTERS   0
#define ARCH_SPECIAL_REGISTERS    0

/* - - - ARCHITECTURE INSTRUCTIONS - - - */
/*
 * Declaring the presence of an instruction on an architecture signifies that
 * no (additional) temporary register visible to the JIT is required to
 * perform its function. Its absence will trigger fallbacks that use registers
 * that the JIT can track.
 * If the instruction requires multiple opcodes to perform but it can be
 * performed while adhering to the above constraint, it can be added here.
 */

#define ARCH_HAS_ENTER_FRAME
#define ARCH_HAS_EXIT_FRAME
#define ARCH_HAS_RETURN

#define ARCH_HAS_SET_REG_IMM32U
#define ARCH_HAS_SET_REG_IMMADDR

#define ARCH_HAS_SIGN_EXTEND_REG32_TO_SELF64

#define ARCH_HAS_LOAD32_REG_FROM_MEM_REG
#define ARCH_HAS_STORE32_REG_AT_MEM_REG
#define ARCH_HAS_STORE64_REG_AT_MEM_REG
#define ARCH_HAS_STOREADDR_REG_AT_MEM_REG

#define ARCH_HAS_SLL32_IMM8U_TO_REG
#define ARCH_HAS_SLL64_IMM8U_TO_REG
#define ARCH_HAS_SRL32_IMM8U_TO_REG
#define ARCH_HAS_SRL64_IMM8U_TO_REG
#define ARCH_HAS_SRA32_IMM8U_TO_REG
#define ARCH_HAS_SRA64_IMM8U_TO_REG

#endif /* !__NEB_DYNAREC_AMD64_CONFIG_H__ */
