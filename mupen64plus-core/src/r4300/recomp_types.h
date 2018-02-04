/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - recomp_types.h                                          *
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

#ifndef M64P_R4300_RECOMP_TYPES_H
#define M64P_R4300_RECOMP_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "hacktarux_dynarec/assemble_struct.h"

struct precomp_instr
{
   void (*ops)(void);
   union
     {
    struct
      {
         int64_t *rs;
         int64_t *rt;
         int16_t immediate;
      } i;
    struct
      {
         uint32_t inst_index;
      } j;
    struct
      {
         int64_t *rs;
         int64_t *rt;
         int64_t *rd;
         unsigned char sa;
         unsigned char nrd;
      } r;
    struct
      {
         unsigned char base;
         unsigned char ft;
         short offset;
      } lf;
    struct
      {
         unsigned char ft;
         unsigned char fs;
         unsigned char fd;
      } cf;
     } f;
   uint32_t addr; /* word-aligned instruction address in r4300 address space */
   unsigned int local_addr; /* byte offset to start of corresponding x86_64 instructions, from start of code block */
   reg_cache_struct reg_cache_infos;
};

struct precomp_block
{
   struct precomp_instr *block;
   uint32_t start;
   uint32_t end;
   unsigned char *code;
   unsigned int code_length;
   unsigned int max_code_length;
   void *jumps_table;
   int jumps_number;
   void *riprel_table;
   int riprel_number;
   //unsigned char md5[16];
   unsigned int adler32;
};

#endif
