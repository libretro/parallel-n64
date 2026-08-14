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

/* MAME maps the full 8MB range as distinct RAM and mayjin3 TLB-maps all of
 * it as one contiguous buffer; ares' 4MB+mirror corrupts that game. */
enum { ALECK64_SDRAM_SIZE = 0x800000 };

enum { MM_ALECK64_SDRAM = 0xc0000000 };
enum { MM_ALECK64_IO    = 0xc0800000 };

/* Seta E90 sprite overlay, only present on the E90 board (mtetrisc).
 * The playfield blocks are drawn by this chip, not by the RDP. */
enum { MM_ALECK64_E90_VRAM = 0xd0000000 };
enum { MM_ALECK64_E90_PAL  = 0xd0010000 };
enum { MM_ALECK64_E90_CTRL = 0xd0030000 };
enum { ALECK64_E90_VRAM_SIZE = 0x1000, ALECK64_E90_PAL_SIZE = 0x1000 };

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
    /* E90 overlay state; unused on E92 boards */
    uint32_t e90_vram[ALECK64_E90_VRAM_SIZE / 4];
    uint32_t e90_pal[ALECK64_E90_PAL_SIZE / 4];
    uint32_t e90_enable;
};

/* Set by the libretro frontend when a MAME Aleck64 romset is loaded. */
extern int g_aleck64_enabled;
extern int g_aleck64_e90;
extern int g_aleck64_mahjong;
extern int g_aleck64_dpad_disabled;
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
void read_aleck64_e90(void* opaque, uint32_t address, uint32_t* value);
void write_aleck64_e90(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

/* Composites the E90 sprites over a finished frame.  'pixels' points at the
 * top-left of the active picture in BGRA8888 (angrylion's struct rgba) and
 * 'pitch' is in pixels.  The chip places sprites in the game's own framebuffer
 * pixels, so 'src_width' is that framebuffer's width and the overlay stretches
 * horizontally to the 'width' the VI scaled it to; vertically the VI does not
 * scale mtetrisc, so rows map 1:1.
 * No-op unless an E90 romset is running and the chip is enabled.
 * Returns the number of sprites drawn, so a caller that has to spend work
 * presenting the result (the GL path) can skip an empty frame. */
int aleck64_e90_overlay(void* pixels, unsigned pitch, unsigned width, unsigned height,
                        unsigned src_width);

/* implemented in libretro/aleck64_mame.c */
int aleck64_load_zip(const uint8_t* data, size_t size, uint8_t** out, size_t* out_size);

/* As aleck64_load_zip, but when 'prefer' is non-NULL the plain-rom-in-a-zip
 * path selects that entry by name rather than the first one with a known
 * extension.  Frontends hand the core an "archive.zip#entry" path when the
 * user picks a specific rom inside a multi-rom archive, and that choice has
 * to be honoured. */
int aleck64_load_zip_named(const uint8_t* data, size_t size, const char* prefer,
                           uint8_t** out, size_t* out_size);

/* As aleck64_load_zip_named, but reads the archive from disk: only the
 * end-of-central-directory tail, the central directory and the chosen
 * entry's compressed bytes are read, so peak memory is the rom image
 * rather than the rom image plus the whole archive. */
int aleck64_load_zip_path(const char* path, const char* prefer,
                          uint8_t** out, size_t* out_size);
void aleck64_apply_dips(void);

#endif
