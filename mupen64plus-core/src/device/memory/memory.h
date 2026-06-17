/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - memory.h                                                *
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

#ifndef M64P_MEMORY_MEMORY_H
#define M64P_MEMORY_MEMORY_H

#include <stdint.h>

#ifndef MASKED_WRITE
#define MASKED_WRITE(dst, value, mask) ((*(dst) & ~(mask)) | ((value) & (mask)))
#endif

#include "libretro_memory.h"

#define AI_STATUS_FIFO_FULL	0x80000000		/* Bit 31: full */
#define AI_STATUS_DMA_BUSY	   0x40000000		/* Bit 30: busy */

#define read_word_in_memory() readmem[mupencoreaddress>>16]()
#define read_byte_in_memory() readmemb[mupencoreaddress>>16]()
#define read_hword_in_memory() readmemh[mupencoreaddress>>16]()
#define read_dword_in_memory() readmemd[mupencoreaddress>>16]()
#define write_word_in_memory() writemem[mupencoreaddress>>16]()
#define write_byte_in_memory() writememb[mupencoreaddress >>16]()
#define write_hword_in_memory() writememh[mupencoreaddress >>16]()
#define write_dword_in_memory() writememd[mupencoreaddress >>16]()

#if !defined(__arm64__) && !defined(__aarch64__)
extern uint32_t address, cpu_word;
extern uint64_t cpu_dword;
#define mupencoreaddress address
#if defined(_M_X64)
/* region 14 / Phase 2d (increment 6): on x64 the width-specific read/write
 * staging bytes cpu_byte/cpu_hword have their storage in
 * g_dev.r4300.new_dynarec_hot_state (members hs_cpu_byte/hs_cpu_hword -- the hs_
 * prefix avoids colliding with these very macros). The memory layer, the Ari64
 * JIT and the Hacktarux dynarec all reach the same member through these aliases;
 * every use-site is in a function body where g_dev is in scope. The remaining
 * staging globals (address, cpu_word, cpu_dword) are migrated in later
 * increments. */
#define cpu_byte         (g_dev.r4300.new_dynarec_hot_state.hs_cpu_byte)
#define cpu_hword        (g_dev.r4300.new_dynarec_hot_state.hs_cpu_hword)
#else
/* x86 (32-bit ari64): still flat globals defined in the memory layer. */
extern uint8_t cpu_byte;
extern uint16_t cpu_hword;
#endif
#else
#include "../r4300/new_dynarec/arm64/memory_layout_arm64.h"
#define mupencoreaddress (RECOMPILER_MEMORY->rml_address)
#define cpu_word         (RECOMPILER_MEMORY->rml_cpu_word)
#define cpu_byte         (RECOMPILER_MEMORY->rml_cpu_byte)
#define cpu_hword        (RECOMPILER_MEMORY->rml_cpu_hword)
#define cpu_dword        (RECOMPILER_MEMORY->rml_cpu_dword)
#endif
extern uint64_t *rdword;

extern void (*readmem[0x10000])(void);
extern void (*readmemb[0x10000])(void);
extern void (*readmemh[0x10000])(void);
extern void (*readmemd[0x10000])(void);
extern void (*writemem[0x10000])(void);
extern void (*writememb[0x10000])(void);
extern void (*writememh[0x10000])(void);
extern void (*writememd[0x10000])(void);

#ifdef MSB_FIRST
#define sl(mot) mot
#define S8 0
#define S16 0
#define Sh16 0
#else
#define sl(mot) \
( \
((mot & 0x000000FF) << 24) | \
((mot & 0x0000FF00) <<  8) | \
((mot & 0x00FF0000) >>  8) | \
((mot & 0xFF000000) >> 24) \
)

#define S8 3
#define S16 2
#define Sh16 1
#endif

void poweron_memory(void);

void map_region(uint16_t region,
      int type,
 void (*read8)(void),
 void (*read16)(void),
 void (*read32)(void),
 void (*read64)(void),
 void (*write8)(void),
 void (*write16)(void),
 void (*write32)(void),
 void (*write64)(void));

/* XXX: cannot make them static because of dynarec + rdp fb */
void read_rdram(void);
void read_rdramb(void);
void read_rdramh(void);
void read_rdramd(void);
void write_rdram(void);
void write_rdramb(void);
void write_rdramh(void);
void write_rdramd(void);

void read_rdramFB(void);
void read_rdramFBb(void);
void read_rdramFBh(void);
void read_rdramFBd(void);
void write_rdramFB(void);
void write_rdramFBb(void);
void write_rdramFBh(void);
void write_rdramFBd(void);

/* Returns a pointer to a block of contiguous memory
 * Can access RDRAM, SP_DMEM, SP_IMEM and ROM, using TLB if necessary
 * Useful for getting fast access to a zone with executable code. */
uint32_t *fast_mem_access(uint32_t address);


#endif
