/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - Architecture configuration inclusion      *
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

#ifndef __NEB_DYNAREC_ARCH_CONFIG_H__
#define __NEB_DYNAREC_ARCH_CONFIG_H__

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

#endif /* !__NEB_DYNAREC_ARCH_CONFIG_H__ */
