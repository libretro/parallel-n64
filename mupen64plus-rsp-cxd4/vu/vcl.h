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

static void VCL(int vd, int vs, int vt, int e)
{
    const register unsigned short VCC_old = VCC;
    int ge, le;
    register int i;

    VCC = 0x0000; /* Undergo the correction phase, factoring old VCC bits. */
    for (i = 0; i < 8; i++)
    {
        const unsigned short VS = (unsigned short)VR[vs][i];
        const unsigned short VT = (unsigned short)VR_T(i);
        const int eq = (~VCO >> (i + 0x8)) & 0x0001; /* !(NOTEQUAL) */
        const int sn =  (VCO >> (i + 0x0)) & 0x0001; /* CARRY */

        le = VCC_old & (0x0001 << i); /* unless (eq & sn) */
        ge = VCC_old & (0x0100 << i); /* unless (eq & !sn) */
        if (sn)
        {
            if (eq)
            {
                const int sum = VS + VT;
                const int ce = (VCE >> i) & 0x01;
                int lz = ((sum & 0x0000FFFF) == 0x00000000);
                int uz = ((sum & 0xFFFF0000) == 0x00000000); /* !carryout */

                le = ((!ce) & (lz & uz)) | (ce & (lz | uz));
                le <<= i + 0x0;
            }
            ACC_R(i) = le ? -VT : VS;
        }
        else
        {
            if (eq)
            {
                ge = (VS - VT >= 0);
                ge <<= i + 0x8;
            }
            ACC_R(i) = ge ? VT : VS;
        }
        VCC |= ge | le;
    }
    for (i = 0; i < 8; i++)
        ACC_W(i) = ACC_R(i);
    VCO = 0x0000;
    VCE = 0x00;
    return;
}
