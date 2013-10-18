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
* Project:  SP VU Emulation Table:  Load Alternate Fourths to Vector Unit      *
* Authors:  Iconoclast                                                         *
* Release:  2013.03.10                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

void LFV(int vt, int element, signed int offset, int base)
{ /* Dummy implementation only:  Do any games execute this? */
    char debugger[24] = "LFV\t$v00[X], 0x000($00)";
    const char digits[16] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
    };
    debugger[006] |= vt / 10;
    debugger[007] |= vt % 10;
    debugger[011]  = digits[element & 0xF];
    debugger[017]  = digits[(offset >> 8) & 0xF];
    debugger[020]  = digits[(offset >> 4) & 0xF];
    debugger[021]  = digits[(offset >> 0) & 0xF];
    debugger[024] |= base / 10;
    debugger[025] |= base % 10;
    message(debugger, 3);
    return;
}
