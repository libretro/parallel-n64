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

#include "vu.h"

static void VMULU(int vd, int vs, int vt, int e)
{
    register int i;

    for (i = 0; i < 8; i++)
        VACC[i].DW = (VR[vs][i]*VR_T(i) << 1) + 0x8000;
    for (i = 0; i < 8; i++) /* Zero-clamp bits 31..16 of ACC to dest. VR. */
    {
        VR_D(i)  = VACC[i].s[MD];   /* VD  = ACC[31..16] */
        VR_D(i) |= VR_D(i) >> 15;   /* VD |= -(result == 0x80008000) */
        VR_D(i) &= ~VACC[i].HW[03]; /* VD  = (ACC < 0) ? 0 : ACC[31..16]; */
    }
    return;
}
