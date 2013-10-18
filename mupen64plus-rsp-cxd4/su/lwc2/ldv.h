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
* Project:  SP VU Emulation Table:  Load Doubleword to Vector Unit             *
* Authors:  Iconoclast                                                         *
* Release:  2013.05.06                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void LDV(int vt, int element, signed int offset, int base)
{
    register unsigned int addr;

    addr  = SR[base] + (offset <<= 3);
    addr &= 0x00000FFF;
    element += 0x1;
    element &= ~01; /* advance adaptation to odd-indexed halfword entries */
    switch (addr & 07)
    {
        case 00:
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr + HES(0x006));
            return;
        case 01: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr + (0x000 ^ 00));
            VR_H(vt, element+0x2)  = RSP.DMEM[addr - 0x001] << 8;
            VR_H(vt, element+0x2) |= RSP.DMEM[addr + 0x006];
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr + (0x004 ^ 00));
            VR_H(vt, element+0x6)  = RSP.DMEM[addr + 0x003] << 8;
            addr += 0x007 + 03; /* byte endian swap adjust */
            addr &= 0x00000FFF;
            VR_H(vt, element+0x6) |= RSP.DMEM[addr];
            return;
        case 02:
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr - (0x000 ^ 02));
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr + (0x006 ^ 02));
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            addr += 0x006 + 02; /* halfword endian swap adjust */
            addr &= 0x00000FFF;
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr);
            return;
        case 03: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_H(vt, element+0x0)  = RSP.DMEM[addr - 0x003] << 8;
            VR_H(vt, element+0x0) |= RSP.DMEM[addr + 0x004];
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr + (0x002 ^ 00));
            VR_H(vt, element+0x4)  = RSP.DMEM[addr + 0x001] << 8;
            addr += 0x005 + 03;
            addr &= 0x00000FFF;
            VR_H(vt, element+0x4) |= RSP.DMEM[addr];
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr - (0x002 ^ 00));
            return;
        case 04:
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            addr += 0x004;
            addr &= 0x00000FFF;
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr + HES(0x002));
            return;
        case 05: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr + (0x000 ^ 00));
            VR_H(vt, element+0x2)  = RSP.DMEM[addr - 0x001] << 8;
            addr += 0x003 + 03;
            addr &= 0x00000FFF;
            VR_H(vt, element+0x2) |= RSP.DMEM[addr];
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr - (0x002 ^ 00));
            VR_H(vt, element+0x6)  = RSP.DMEM[addr - 0x003] << 8;
            VR_H(vt, element+0x6) |= RSP.DMEM[addr + 0x004];
            return;
        case 06:
            VR_H(vt, element+0x0) = *(short *)(RSP.DMEM + addr - (0x000 ^ 02));
            addr += 0x002;
            addr &= 0x00000FFF;
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr + (0x000 ^ 02));
            VR_H(vt, element+0x4) = *(short *)(RSP.DMEM + addr + (0x002 ^ 02));
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr + (0x004 ^ 02));
            return;
        case 07: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
            VR_H(vt, element+0x0)  = RSP.DMEM[addr - 0x003] << 8;
            addr += 0x001 + 03;
            addr &= 0x00000FFF;
            VR_H(vt, element+0x0) |= RSP.DMEM[addr];
            VR_H(vt, element+0x2) = *(short *)(RSP.DMEM + addr - (0x002 ^ 00));
            VR_H(vt, element+0x4)  = RSP.DMEM[addr - 0x003] << 8;
            VR_H(vt, element+0x4) |= RSP.DMEM[addr + 0x004];
            VR_H(vt, element+0x6) = *(short *)(RSP.DMEM + addr + (0x002 ^ 00));
            return;
    }
}
