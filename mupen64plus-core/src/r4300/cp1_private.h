/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cp1_private.h                                           *
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

#ifndef M64P_R4300_CP1_PRIVATE_H
#define M64P_R4300_CP1_PRIVATE_H

#include <stdint.h>

#include "cp1.h"

#if !defined(__APPLE__) || !defined(__arm64__)
extern float *reg_cop1_simple[32];
extern double *reg_cop1_double[32];
extern uint32_t FCR0, FCR31;
#else
#include "new_dynarec/arm64/apple_memory_layout.h"
#define reg_cop1_simple (RECOMPILER_MEMORY->rml_reg_cop1_simple)
#define reg_cop1_double (RECOMPILER_MEMORY->rml_reg_cop1_double)
#define FCR0            (RECOMPILER_MEMORY->rml_FCR0)
#define FCR31           (RECOMPILER_MEMORY->rml_FCR31)
#endif
extern int64_t reg_cop1_fgr_64[32];
extern uint32_t rounding_mode;

#endif /* M64P_R4300_CP1_PRIVATE_H */


