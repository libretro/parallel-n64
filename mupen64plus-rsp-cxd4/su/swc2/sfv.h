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
* Project:  SP VU Emulation Table:  Store Alternate Fourths from Vector Unit   *
* Authors:  Iconoclast                                                         *
* Release:  2013.04.20                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void SFV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;

    addr  = SR[base] + (offset <<= 4);
    addr &= 0x00000FF3;
    addr ^= BES(00);
    switch (element)
    {
        case 0x0:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][03] >> 7);
            return;
        case 0x8:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][07] >> 7);
            return;
        default:
            message("SFV\nIllegal element.", 3);
            return;
    }
}
