/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - MIPS little-endian configuration          *
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

#ifndef __NEB_DYNAREC_MIPSEL_CONFIG_H__
#define __NEB_DYNAREC_MIPSEL_CONFIG_H__

/* Mandatory. The size, in bytes, of the cache used to hold recompiled code on
 * this platform. */
#define CODE_CACHE_SIZE (1 << 25) /* 32 MiB */

/* For the rest, refer to driver.h. */

/* - - - ARCHITECTURE SETTINGS - - - */

// #define ARCH_NEED_INIT
// #define ARCH_NEED_EXIT

// #define ARCH_HAS_64BIT_REGISTERS

#define ARCH_INT_TEMP_REGISTERS  16 /* $2 to $15, $24 and $25; $1 is used to form addresses */
#define ARCH_INT_SAVED_REGISTERS  9 /* $16 to $23 and $30 */
#define ARCH_FP_TEMP_REGISTERS   16 /* $f0, $f2, ..., $f30 */
#define ARCH_FP_SAVED_REGISTERS   0
#define ARCH_SPECIAL_REGISTERS    4 /* $0, HI, LO, Coprocessor 1 condition */

#endif /* !__NEB_DYNAREC_MIPSEL_CONFIG_H__ */
