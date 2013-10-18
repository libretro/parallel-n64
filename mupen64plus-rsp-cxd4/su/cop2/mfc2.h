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

#include "../../vu/vu.h"

void MFC2(int rt, int vd, int element)
{
/*
    if (rt == 0)
        return; // zero permanence already handled in main CPU loop
*/
    if (element == 0xF)
        goto leap;
    SR[rt] = VR_S(vd, element);
    SR[rt] = (signed short)(SR[rt]);
    return;
leap:
    message("MFC2\nCrossed segment allocation barrier.", 0);
    SR[rt] = (VR_B(vd, 0xF) << 8) | VR_B(vd, 0x0);
    SR[rt] = (signed short)(SR[rt]);
    return;
}
