/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - aleck64.h                                               *
 *   Seta Aleck64 arcade board support (SDRAM + arcade I/O ports).         *
 *                                                                         *
 *   Ported from the Aleck64 implementation in the ares emulator:          *
 *   Copyright (c) 2004-2025 ares team, Near et al                         *
 *                                                                         *
 *   Permission to use, copy, modify, and/or distribute this software for  *
 *   any purpose with or without fee is hereby granted, provided that the  *
 *   above copyright notice and this permission notice appear in all       *
 *   copies. (ISC license)                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef M64P_DEVICE_ALECK64_ALECK64_H
#define M64P_DEVICE_ALECK64_ALECK64_H

#include <stddef.h>
#include <stdint.h>

enum { ALECK64_SDRAM_SIZE = 0x400000 };

enum { MM_ALECK64_SDRAM = 0xc0000000 };
enum { MM_ALECK64_IO    = 0xc0800000 };

/* Button bits, active high, packed by aleck64_poll_buttons().
 * Bits 0-15 follow the E92 port1 layout; 16-21 the E92 port2 layout. */
#define A64_P1_UP      (UINT32_C(1) <<  0)
#define A64_P1_DOWN    (UINT32_C(1) <<  1)
#define A64_P1_LEFT    (UINT32_C(1) <<  2)
#define A64_P1_RIGHT   (UINT32_C(1) <<  3)
#define A64_P1_B1      (UINT32_C(1) <<  4)
#define A64_P1_B2      (UINT32_C(1) <<  5)
#define A64_P1_B3      (UINT32_C(1) <<  6)
#define A64_P1_B4      (UINT32_C(1) <<  7)
#define A64_P2_UP      (UINT32_C(1) <<  8)
#define A64_P2_DOWN    (UINT32_C(1) <<  9)
#define A64_P2_LEFT    (UINT32_C(1) << 10)
#define A64_P2_RIGHT   (UINT32_C(1) << 11)
#define A64_P2_B1      (UINT32_C(1) << 12)
#define A64_P2_B2      (UINT32_C(1) << 13)
#define A64_P2_B3      (UINT32_C(1) << 14)
#define A64_P2_B4      (UINT32_C(1) << 15)
#define A64_P1_START   (UINT32_C(1) << 16)
#define A64_P2_START   (UINT32_C(1) << 17)
#define A64_P1_COIN    (UINT32_C(1) << 18)
#define A64_P2_COIN    (UINT32_C(1) << 19)
#define A64_SERVICE    (UINT32_C(1) << 20)
#define A64_TEST       (UINT32_C(1) << 21)

struct aleck64
{
    uint32_t* sdram;
    uint8_t mahjong_row;
};

/* Set by the libretro frontend when a MAME Aleck64 romset is loaded. */
extern int g_aleck64_enabled;
extern int g_aleck64_e90;
extern int g_aleck64_mahjong;
/* Live dipswitch state, driven by the core options. */
extern uint8_t g_aleck64_dipswitch[2];

void init_aleck64(struct aleck64* a64);
void poweron_aleck64(struct aleck64* a64);

/* direct pointer into SDRAM, for the interpreter's instruction fetch */
uint32_t* aleck64_fast_mem(uint32_t address);

void read_aleck64_sdram(void* opaque, uint32_t address, uint32_t* value);
void write_aleck64_sdram(void* opaque, uint32_t address, uint32_t value, uint32_t mask);
void read_aleck64_io(void* opaque, uint32_t address, uint32_t* value);
void write_aleck64_io(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

/* implemented in libretro/aleck64_mame.c */
int aleck64_load_zip(const uint8_t* data, size_t size, uint8_t** out, size_t* out_size);
void aleck64_apply_dips(void);

#endif
