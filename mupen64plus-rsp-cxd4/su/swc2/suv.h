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
* Project:  SP VU Emulation Table:  Store Packed Unsigned from Vector Unit     *
* Authors:  Iconoclast                                                         *
* Release:  2013.05.06                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void SUV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;
    int b;

    addr  = SR[base] + (offset <<= 3);
    b = addr & 0x00000007;
    addr &= 0x00000FF8; /* World Driver Championship:  on load START screen */
    if (element != 0x0)
        goto ILLEGAL_EL;
    switch (b)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 7);
            return;
        case 04: /* "Indiana Jones and the Infernal Machine" in-game */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 7);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 7);
            return;
        default: /* Completely legal, just never seen it be done. */
            message("SUV\nWeird addr.", 3);
            return;
    }
ILLEGAL_EL:
    message("SUV\nIllegal element.", 3);
    return;
}
