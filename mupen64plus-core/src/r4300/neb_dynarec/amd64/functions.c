/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - Neb Dynarec - AMD64 (little-endian) functions           *
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

#include "../driver.h"
#include "../../r4300.h"
#include "assemble.h"

#define OFFSET_FROM(a, b) ((uint8_t*) (b) - (uint8_t*) (a))

static uint8_t* output_byte(uint8_t* dst, uint8_t Byte)
{
	*(uint8_t*) dst = Byte;
	return dst + 1;
}

static uint8_t* output_hword(uint8_t* dst, uint16_t Hword)
{
	*(uint16_t*) dst = Hword;
	return dst + 2;
}

static uint8_t* output_dword(uint8_t* dst, uint32_t Dword)
{
	*(uint32_t*) dst = Dword;
	return dst + 4;
}

static uint8_t* output_qword(uint8_t* dst, uint64_t Qword)
{
	*(uint64_t*) dst = Qword;
	return dst + 8;
}
