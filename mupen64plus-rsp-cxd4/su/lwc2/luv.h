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
* Project:  SP VU Emulation Table:  Load Packed Unsigned to Vector Unit        *
* Authors:  Iconoclast                                                         *
* Release:  2013.06.09                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void LUV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;
    int b;

    addr  = SR[base] + (offset <<= 3);
    if (element != 0x0)
        goto ILLEGAL_EL;
    b = addr;
    addr &= 0x00000FF8;
    switch (b &= 07)
    {
        case 00:
            VR[vt][07] = RSP.DMEM[addr + BES(0x007)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][00] = RSP.DMEM[addr + BES(0x000)] << 7;
            return;
        case 01: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += BES(0x008);
            addr &= 0x00000FFF;
            VR[vt][07] = RSP.DMEM[addr] << 7;
            return;
        case 02: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][06] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x001)] << 7;
            return;
        case 03: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][05] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x002)] << 7;
            return;
        case 04: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][04] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x003)] << 7;
            return;
        case 05: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][03] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x004)] << 7;
            return;
        case 06: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][02] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x005)] << 7;
            return;
        case 07: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][01] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x006)] << 7;
            return;
    }
ILLEGAL_EL: /* "Mia Hamm Soccer 64" SP exception override (zilmar) */
    addr += -element & 0xF;
    for (offset = 0; offset < 8; offset++)
    {
        addr &= 0x00000FFF;
        VR[vt][offset] = RSP.DMEM[BES(addr)] << 7;
        --element;
        addr -= 16 * (element == 0x0);
        ++addr;
    }
    return;
}
