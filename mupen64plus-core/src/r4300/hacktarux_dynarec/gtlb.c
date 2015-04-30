/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - gtlb.c                                                  *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>

#include "assemble.h"

#include "r4300/cached_interp.h"
#include "r4300/recomph.h"
#include "r4300/r4300.h"
#include "r4300/ops.h"

void gentlbwi(void)
{
   gencallinterp((native_type)cached_interpreter_table.TLBWI, 0);
}

void gentlbp(void)
{
   gencallinterp((native_type)cached_interpreter_table.TLBP, 0);
}

void gentlbr(void)
{
   gencallinterp((native_type)cached_interpreter_table.TLBR, 0);
}

void generet(void)
{
   gencallinterp((native_type)cached_interpreter_table.ERET, 1);
}

void gentlbwr(void)
{
   gencallinterp((native_type)cached_interpreter_table.TLBWR, 0);
}

