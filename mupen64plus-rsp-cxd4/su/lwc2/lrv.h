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
* Project:  SP VU Emulation Table:  Load Rest to Vector Unit                   *
* Authors:  Iconoclast                                                         *
* Release:  2013.03.21                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void LRV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;
    int b;

    addr  = SR[base] + (offset << 4);
    b = addr & 0x0000000F;
    addr &= 0x00000FF0;
    if (element != 0x0) /* We need an explicit `goto` for stupid compilers. */
        goto bitch; /* Blame M$ for their ineptitude with branch weighs. */
    switch (b)
    {
        case 0xE:
            VR[vt][01] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][02] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + (0x006 ^ 02));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + (0x008 ^ 02));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x00A ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x00C ^ 02));
            return;
        case 0xC:
            VR[vt][02] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + (0x006 ^ 02));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x008 ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x00A ^ 02));
            return;
        case 0xA:
            VR[vt][03] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x006 ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x008 ^ 02));
            return;
        case 0x8:
            VR[vt][04] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x006 ^ 02));
            return;
        case 0x6:
            VR[vt][05] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            return;
        case 0x4:
            VR[vt][06] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            return;
        case 0x2:
            VR[vt][07] = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            return;
        case 0x0:  return;
        default:
            message("LRV\nOdd addr.", 3);
            return;
    }
bitch:
    message("LRV\nIllegal element.", 3);
    return;
}
