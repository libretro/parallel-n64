/*
 * mupen64plus-rsp-cxd4 - RSP Interpreter
 * Copyright (C) 2012-2013  RJ 'Iconoclast' Swedlow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/******************************************************************************\
* Project:  MSP Emulation Table for System Control Coprocessor Operations      *
* Creator:  Iconoclast                                                         *
* Release:  2013.03.09                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

extern void MFC0(int rt, int cr);
extern void MTC0(int rt, int cr);

#include "cop0/mfc0.h"
#include "cop0/mtc0.h"

void res_020(int rt, int rd)
{
    rd = rt = 0;
    message("COP0\nRESERVED", 3);
    return;
}

static void (*SP_COP0[32])(int, int) = {
    MFC0   ,res_020,res_020,res_020,MTC0   ,res_020,res_020,res_020, /* 00 */
    res_020,res_020,res_020,res_020,res_020,res_020,res_020,res_020, /* 01 */
    res_020,res_020,res_020,res_020,res_020,res_020,res_020,res_020, /* 10 */
    res_020,res_020,res_020,res_020,res_020,res_020,res_020,res_020  /* 11 */
}; /* 000     001     010     011     100     101     110     111 */
