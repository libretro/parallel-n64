/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - hle.h                                           *
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

#ifndef HLE_H
#define HLE_H

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_plugin.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define RSP_HLE_VERSION        0x016305
#define RSP_PLUGIN_API_VERSION 0x020000

#ifdef M64P_BIG_ENDIAN
#define S 0
#define S16 0
#define S8 0
#else
#define S 1
#define S16 2
#define S8 3
#endif

extern RSP_INFO rspInfo;

typedef struct
{
    uint32_t type;
    uint32_t flags;

    uint32_t ucode_boot;
    uint32_t ucode_boot_size;

    uint32_t ucode;
    uint32_t ucode_size;

    uint32_t ucode_data;
    uint32_t ucode_data_size;

    uint32_t dram_stack;
    uint32_t dram_stack_size;

    uint32_t output_buff;
    uint32_t output_buff_size;

    uint32_t data_ptr;
    uint32_t data_size;

    uint32_t yield_data_ptr;
    uint32_t yield_data_size;
} OSTask_t;

static INLINE const OSTask_t * const get_task(void)
{
    return (OSTask_t*)(rspInfo.DMEM + 0xfc0);
}

static inline uint8_t* const dmem_u8(uint16_t address)
{
    return (uint8_t*)(&rspInfo.DMEM[(address & 0xfff) ^ S8]);
}

static inline uint16_t* const dmem_u16(uint16_t address)
{
    assert((address & 1) == 0);
    return (uint16_t*)(&rspInfo.DMEM[(address & 0xfff) ^ S16]);
}

static inline uint32_t* const dmem_u32(uint16_t address)
{
    assert((address & 3) == 0);
    return (uint32_t*)(&rspInfo.DMEM[(address & 0xfff)]);
}

static inline uint8_t* const dram_u8(uint32_t address)
{
    return (uint8_t*)&rspInfo.RDRAM[(address & 0xffffff) ^ S8];
}

static inline uint16_t* const dram_u16(uint32_t address)
{
    assert((address & 1) == 0);
    return (uint16_t*)&rspInfo.RDRAM[(address & 0xffffff) ^ S16];
}

static inline uint32_t* const dram_u32(uint32_t address)
{
    assert((address & 3) == 0);
    return (uint32_t*)&rspInfo.RDRAM[address & 0xffffff];
}

void dmem_load_u8 (uint8_t* dst, uint16_t address, size_t count);
void dmem_load_u16(uint16_t* dst, uint16_t address, size_t count);
void dmem_load_u32(uint32_t* dst, uint16_t address, size_t count);
void dmem_store_u8 (const uint8_t* src, uint16_t address, size_t count);
void dmem_store_u16(const uint16_t* src, uint16_t address, size_t count);
void dmem_store_u32(const uint32_t* src, uint16_t address, size_t count);

void dram_load_u8 (uint8_t* dst, uint32_t address, size_t count);
void dram_load_u16(uint16_t* dst, uint32_t address, size_t count);
void dram_load_u32(uint32_t* dst, uint32_t address, size_t count);
void dram_store_u8 (const uint8_t* src, uint32_t address, size_t count);
void dram_store_u16(const uint16_t* src, uint32_t address, size_t count);
void dram_store_u32(const uint32_t* src, uint32_t address, size_t count);

#ifdef LOG_RSP_DEBUG_MESSAGE
#define RSP_DEBUG_MESSAGE(level, format, ...) fprintf(stderr, format, __VA_ARGS__)
#else
#define RSP_DEBUG_MESSAGE(level, format, ...) (void)0
#endif

#define BLARGG_CLAMP16(io) \
   if ((int16_t)io != io) \
      io = (io >> 31) ^ 0x7FFF

#endif

