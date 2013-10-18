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
* Project:  SP VU Emulation Table:  Store Shortword from Vector Unit           *
* Authors:  Iconoclast                                                         *
* Release:  2013.04.20                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void SSV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;

    addr  = SR[base] + (offset <<= 1);
    addr &= 0x00000FFF;
    if (element == 0xF)
        goto LOL;
    switch (addr & 03)
    {
        case 00: /* word-aligned */
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, element);
            return;
        case 01:
            *(short *)(RSP.DMEM + addr) = VR_S(vt, element);
            return;
        case 02:
            *(short *)(RSP.DMEM + addr - HES(0x000)) = VR_S(vt, element);
            return;
        case 03:
            message("SSV\nWeird addr.", 3);
            return;
    }
LOL:
    message("SSV\nIllegal element.", 3);
    return;
}
