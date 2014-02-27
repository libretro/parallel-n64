/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - arithmetics.h                                   *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#ifndef ARITHMETICS_H
#define ARITHMETICS_H

#include <stdint.h>

#include "hle_plugin.h"


#if 0
static inline int16_t clamp_s16(int_fast32_t x)
{
   if ((int16_t)x != x)
      x = (x >> 31) ^ 0x7FFF;

   return x;
}
#else
static inline int16_t clamp_s16(int_fast32_t x)
{
   x = (x < INT16_MIN) ? INT16_MIN: x;
   x = (x > INT16_MAX) ? INT16_MAX: x;

   return x;
}
#endif

#ifdef LOG_RSP_DEBUG_MESSAGE
#define RSP_DEBUG_MESSAGE(level, format, ...) fprintf(stderr, format, __VA_ARGS__)
#else
#define RSP_DEBUG_MESSAGE(level, format, ...) (void)0
#endif

#endif

