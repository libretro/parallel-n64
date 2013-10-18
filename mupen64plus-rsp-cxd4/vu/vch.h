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

static void VCH(int vd, int vs, int vt, int e)
{
    int ge, le, neq;
    register int i;

    VCO = 0x0000;
    VCC = 0x0000;
    VCE = 0x00;
    for (i = 0; i < 8; i++)
    {
        const signed short VS = VR[vs][i];
        const signed short VT = VR_T(i);
        const int sn = (VS ^ VT) < 0; /* sn = (unsigned short)(VS ^ VT) >> 15 */

        if (sn)
        {
            ge = (VT < 0);
            le = (VS + VT <= 0);
            neq = (VS + VT == -1); /* compare extension */
            VCE |= neq << i;
            neq ^= !(VS + VT == 0); /* !(x | y) = x ^ !(y), if (x & y) != 1 */
            ACC_R(i) = le ? -VT : VS;
            VCO |= (neq <<= (i + 0x8)) | (sn << (i + 0x0)); /* sn = 1 */
        }
        else
        {
            le = (VT < 0);
            ge = (VS - VT >= 0);
            neq = !(VS - VT == 0);
            VCE |= 0x00 << i;
            ACC_R(i) = ge ? VT : VS;
            VCO |= (neq <<= (i + 0x8)) | (sn << (i + 0x0)); /* sn = 0 */
        }
        VCC |=  (ge <<= (i + 0x8)) | (le <<= (i + 0x0));
    }
    for (i = 0; i < 8; i++)
        ACC_W(i) = ACC_R(i);
    return;
}
