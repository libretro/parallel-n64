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
* Project:  SP VU Emulation Table:  Store Packed Signed from Vector Unit       *
* Authors:  Iconoclast                                                         *
* Release:  2013.05.14                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void SPV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;
    int b;

    if (element != 0x0)
        goto ILLEGAL_EL;
    addr  = SR[base] + (offset <<= 3);
    b = addr;
    addr &= 0x00000FF8;
    switch (b &= 07)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 8);
            return;
        case 01: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][06] >> 8);
            addr += BES(0x008);
            addr &= 0x00000FFF;
            RSP.DMEM[addr] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 02: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][05] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 03: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][04] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 04: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 05: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][02] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 06: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][01] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 07: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][00] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][07] >> 8);
            return;
    }
ILLEGAL_EL:
    message("SPV\nIllegal element.", 3);
    return;
}
