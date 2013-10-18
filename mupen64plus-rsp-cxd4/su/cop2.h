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
* Project:  MSP Emulation Table for Vector Unit Coprocessor Operations (COP2)  *
* Authors:  Iconoclast                                                         *
* Release:  2013.03.10                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

extern void CFC2(int rt, int vcr, int unused);
extern void CTC2(int rt, int vcr, int unused);
extern void MFC2(int rt, int vd, int element);
extern void MTC2(int rt, int vd, int element);

#include "cop2/mfc2.h"
#include "cop2/cfc2.h"
#include "cop2/mtc2.h"
#include "cop2/ctc2.h"

void res_022(int rt, int rd, int element)
{
    element = rd = rt = 0;
    message("COP2\nRESERVED", 3);
    return;
}

static void (*SP_COP2[32])(int, int, int) = {
    MFC2   ,res_022,CFC2   ,res_022,MTC2   ,res_022,CTC2   ,res_022, /* 00 */
    res_022,res_022,res_022,res_022,res_022,res_022,res_022,res_022, /* 01 */
    res_022,res_022,res_022,res_022,res_022,res_022,res_022,res_022, /* 10 */
    res_022,res_022,res_022,res_022,res_022,res_022,res_022,res_022  /* 11 */
}; /* 000     001     010     011     100     101     110     111 */
