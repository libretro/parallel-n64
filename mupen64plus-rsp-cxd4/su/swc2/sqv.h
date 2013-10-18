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
* Project:  SP VU Emulation Table:  Store Quadword from Vector Unit            *
* Authors:  Iconoclast                                                         *
* Release:  2013.06.09                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void SQV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;
    int b;

    addr  = SR[base] + (offset <<= 4);
    if (element != 0x0)
        goto ILLEGAL_EL;
    b = addr & 0x0000000F; /* experimental; patent says should & 07 */
    addr &= 0x00000FF0; /* World Driver Championship:  on load demo intro */
    switch (b)
    {
        case 00:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][07];
            return;
        case 02:
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][06];
            return;
        case 04:
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][05];
            return;
        case 06:
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][04];
            return;
        default:
            message("SQV\nWeird addr.", 3);
            return;
    }
ILLEGAL_EL: /* "Mia Hamm Soccer 64" SP exception override (Ville Linde) */
    b = element + 16 - (addr & 0x00F);
    for (offset = element; offset < b; offset++)
    {
        addr &= 0x00000FFF;
        RSP.DMEM[BES(addr)] = VR_B(vt, offset & 0xF);
        ++addr;
    }
    return;
}
