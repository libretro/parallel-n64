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

static void VGE(int vd, int vs, int vt, int e)
{
    int ge; /* greater than or, unless (CARRY && NOTEQUAL), equal */
    register unsigned char VCO_VCE;
    register int i;

    VCC = 0x0000;
    VCO_VCE = ~(unsigned char)(VCO >> 8); /* NOT ah; MOV ax, ah; # eq = ~neq */
    for (i = 0; i < 8; i++)
    {
        const signed short VS = VR[vs][i];
        const signed short VT = VR_T(i);

        ge  = ((~VCO >> i) & 0x0001) | ((VCO_VCE >> i) & 0x01);
        ge &= (VS == VT);
        ge |= (VS > VT);
        VCC |= ge <<= i;
        ACC_R(i) = ge ? VS : VT;
    }
    for (i = 0; i < 8; i++)
        ACC_W(i) = ACC_R(i);
    VCO = 0x0000;
    return;
}
