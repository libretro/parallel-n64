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

void CTC2(int rt, int vcr, int unused)
{
    unused = 0; /* no element specifier */
    switch (vcr & 03) /* RCP no-exception override */
    {
        case 00:
            VCO = (unsigned short)SR[rt];
            return;
        case 01:
            VCC = (unsigned short)SR[rt];
            return;
        case 02:
            message("CTC2\nVCE", 1);
            VCE = (unsigned char)SR[rt];
            return;
        case 03:
            message("CTC2\nInvalid vector control register.", 2);
            VCE = (unsigned char)SR[rt]; /* override behavior (zilmar) */
            return;
    }
}
