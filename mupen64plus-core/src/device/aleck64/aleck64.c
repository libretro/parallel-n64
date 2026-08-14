/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - aleck64.c                                               *
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

#include "aleck64.h"

#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "device/memory/m64p_memory.h"

int g_aleck64_enabled = 0;
int g_aleck64_e90 = 0;
int g_aleck64_mahjong = 0;
int g_aleck64_dpad_disabled = 0;
uint8_t g_aleck64_dipswitch[2] = { 0xff, 0xff };

extern retro_input_state_t input_cb;

static struct aleck64* l_a64;

void init_aleck64(struct aleck64* a64)
{
    /* allocated once and reused across ROM loads, like the cart buffer */
    if (a64->sdram == NULL)
        a64->sdram = (uint32_t*)malloc(ALECK64_SDRAM_SIZE);
    l_a64 = a64;
}

uint32_t* aleck64_fast_mem(uint32_t address)
{
    return &l_a64->sdram[(address & (ALECK64_SDRAM_SIZE-1)) >> 2];
}

void poweron_aleck64(struct aleck64* a64)
{
    memset(a64->sdram, 0, ALECK64_SDRAM_SIZE);
    a64->mahjong_row = 0;
    memset(a64->e90_vram, 0, sizeof(a64->e90_vram));
    memset(a64->e90_pal, 0, sizeof(a64->e90_pal));
    a64->e90_enable = 0;
}

void read_aleck64_sdram(void* opaque, uint32_t address, uint32_t* value)
{
    struct aleck64* a64 = (struct aleck64*)opaque;
    *value = a64->sdram[(address & (ALECK64_SDRAM_SIZE-1)) >> 2];
}

void write_aleck64_sdram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct aleck64* a64 = (struct aleck64*)opaque;
    masked_write(&a64->sdram[(address & (ALECK64_SDRAM_SIZE-1)) >> 2], value, mask);
}

static uint32_t poll_buttons(void)
{
    uint32_t b = 0;
    int i;

    if (input_cb == NULL)
        return 0;

    for (i = 0; i < 2; ++i) {
        uint32_t p = 0;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))     p |= A64_P1_UP;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))   p |= A64_P1_DOWN;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))   p |= A64_P1_LEFT;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))  p |= A64_P1_RIGHT;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))      p |= A64_P1_B1;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))      p |= A64_P1_B2;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))      p |= A64_P1_B3;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))      p |= A64_P1_B4;
        b |= p << (i * 8);

        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))  b |= (i == 0) ? A64_P1_START : A64_P2_START;
        if (input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT)) b |= (i == 0) ? A64_P1_COIN : A64_P2_COIN;
    }

    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3)) b |= A64_SERVICE;
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3)) b |= A64_TEST;

    return b;
}

/* Mahjong control panel matrix, MAME-style keyboard mapping:
 * A-N on keys a-n, kan=LCtrl, pon=LAlt, chi=Space, reach=LShift, ron=z */
static int key(unsigned k)
{
    return input_cb(0, RETRO_DEVICE_KEYBOARD, 0, k) != 0;
}

static uint8_t mahjong_read(uint32_t row)
{
    uint8_t v = 0xff;

    if (input_cb == NULL)
        return v;

    if (row & 0x01) {
        if (key(RETROK_b)) v &= ~0x02;
        if (key(RETROK_f)) v &= ~0x04;
        if (key(RETROK_j)) v &= ~0x08;
        if (key(RETROK_n)) v &= ~0x10;
        if (key(RETROK_LSHIFT)) v &= ~0x20; /* reach */
    }
    if (row & 0x02) {
        if (key(RETROK_1) ||
            input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) v &= ~0x01;
        if (key(RETROK_a)) v &= ~0x02;
        if (key(RETROK_e)) v &= ~0x04;
        if (key(RETROK_i)) v &= ~0x08;
        if (key(RETROK_m)) v &= ~0x10;
        if (key(RETROK_LCTRL)) v &= ~0x20; /* kan */
    }
    if (row & 0x04) {
        if (key(RETROK_c)) v &= ~0x02;
        if (key(RETROK_g)) v &= ~0x04;
        if (key(RETROK_k)) v &= ~0x08;
        if (key(RETROK_SPACE)) v &= ~0x10; /* chi */
        if (key(RETROK_z)) v &= ~0x20;     /* ron */
    }
    if (row & 0x08) {
        if (key(RETROK_d)) v &= ~0x02;
        if (key(RETROK_h)) v &= ~0x04;
        if (key(RETROK_l)) v &= ~0x08;
        if (key(RETROK_LALT)) v &= ~0x10;  /* pon */
    }

    return v;
}

static uint32_t read_port1(uint32_t b)
{
    uint32_t v;

    if (!g_aleck64_e90) {
        /* E92: bits 0-15 match the A64_* layout directly, active low */
        v = 0xffff & ~b;
    }
    else {
        /* E90: directions at the same place, but only 2 buttons + start */
        v = 0xffff;
        if (b & A64_P1_UP)    v &= ~(UINT32_C(1) <<  0);
        if (b & A64_P1_DOWN)  v &= ~(UINT32_C(1) <<  1);
        if (b & A64_P1_LEFT)  v &= ~(UINT32_C(1) <<  2);
        if (b & A64_P1_RIGHT) v &= ~(UINT32_C(1) <<  3);
        if (b & A64_P1_B1)    v &= ~(UINT32_C(1) <<  4);
        if (b & A64_P1_B2)    v &= ~(UINT32_C(1) <<  5);
        if (b & A64_P1_START) v &= ~(UINT32_C(1) <<  7);
        if (b & A64_P2_UP)    v &= ~(UINT32_C(1) <<  8);
        if (b & A64_P2_DOWN)  v &= ~(UINT32_C(1) <<  9);
        if (b & A64_P2_LEFT)  v &= ~(UINT32_C(1) << 10);
        if (b & A64_P2_RIGHT) v &= ~(UINT32_C(1) << 11);
        if (b & A64_P2_B1)    v &= ~(UINT32_C(1) << 12);
        if (b & A64_P2_B2)    v &= ~(UINT32_C(1) << 13);
        if (b & A64_P2_START) v &= ~(UINT32_C(1) << 15);
    }

    v |= (uint32_t)g_aleck64_dipswitch[1] << 16;
    v |= (uint32_t)g_aleck64_dipswitch[0] << 24;
    return v;
}

static uint32_t read_port2(uint32_t b)
{
    uint32_t v = 0xffffffff;

    if (!g_aleck64_e90) {
        if (b & A64_P1_START) v &= ~(UINT32_C(1) << 16);
        if (b & A64_P2_START) v &= ~(UINT32_C(1) << 17);
        if (b & A64_P1_COIN)  v &= ~(UINT32_C(1) << 18);
        if (b & A64_P2_COIN)  v &= ~(UINT32_C(1) << 19);
        if (b & A64_SERVICE)  v &= ~(UINT32_C(1) << 20);
        if (b & A64_TEST)     v &= ~(UINT32_C(1) << 21);
    }
    else {
        if (b & A64_P1_COIN)  v &= ~(UINT32_C(1) << 0);
        if (b & A64_P2_COIN)  v &= ~(UINT32_C(1) << 1);
        if (b & A64_SERVICE)  v &= ~(UINT32_C(1) << 4);
        if (b & A64_TEST)     v &= ~(UINT32_C(1) << 5);
    }

    return v;
}

void read_aleck64_io(void* opaque, uint32_t address, uint32_t* value)
{
    struct aleck64* a64 = (struct aleck64*)opaque;

    switch (address & 0xfffc)
    {
    case 0x0000: *value = read_port1(poll_buttons()); break;
    case 0x0004: *value = read_port2(poll_buttons()); break;
    case 0x0008: /* expansion port: mahjong panel matrix on the games wired with one */
        *value = 0xffffffff;
        if (g_aleck64_mahjong)
            *value = 0xff00ffff | ((uint32_t)mahjong_read(a64->mahjong_row) << 16);
        break;
    case 0x0100: *value = 0; break;
    default:     *value = 0xffffffff; break;
    }
}

void write_aleck64_io(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct aleck64* a64 = (struct aleck64*)opaque;

    if ((address & 0xfffc) == 0x0008)
        a64->mahjong_row = (value >> 8) & 0xff; /* expansion port: row select */
}

/* Seta E90 sprite overlay.
 *
 * Only mtetrisc uses it, and it draws the whole tetris playfield: without it
 * the wells stay empty because the RDP only renders the cabinet artwork.
 * The chip is barely documented; the register and drawing behaviour below
 * follows MAME's aleck64 driver, which reverse-engineered it far enough for
 * this one game.  The 0xd0000000 window is a single 256KB mapping split into
 * three sub-ranges the same way MAME's e90_map does.
 */
enum {
    E90_VRAM_BASE = 0x00000,
    E90_PAL_BASE  = 0x10000,
    E90_CTRL_BASE = 0x30000,
    E90_WINDOW_MASK = 0x3ffff
};

void read_aleck64_e90(void* opaque, uint32_t address, uint32_t* value)
{
    struct aleck64* a64 = (struct aleck64*)opaque;
    uint32_t offset = address & E90_WINDOW_MASK;

    if (offset < E90_VRAM_BASE + ALECK64_E90_VRAM_SIZE) {
        *value = a64->e90_vram[(offset & (ALECK64_E90_VRAM_SIZE-1)) >> 2];
    }
    else if (offset >= E90_PAL_BASE && offset < E90_PAL_BASE + ALECK64_E90_PAL_SIZE) {
        *value = a64->e90_pal[(offset & (ALECK64_E90_PAL_SIZE-1)) >> 2];
    }
    else if (offset >= E90_CTRL_BASE && offset < E90_CTRL_BASE + 0x20) {
        /* mtetrisc polls the 16-bit status word at 0x00, which ares found to
         * read back as the complement of the enable bit.  It sits in the high
         * half of the word on this big-endian bus. */
        *value = ((offset & 0x1c) == 0) ? ((a64->e90_enable ? 0u : 1u) << 16)
                                        : 0xffffffff;
    }
    else {
        *value = 0xffffffff;
    }
}

void write_aleck64_e90(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct aleck64* a64 = (struct aleck64*)opaque;
    uint32_t offset = address & E90_WINDOW_MASK;

    if (offset < E90_VRAM_BASE + ALECK64_E90_VRAM_SIZE) {
        masked_write(&a64->e90_vram[(offset & (ALECK64_E90_VRAM_SIZE-1)) >> 2], value, mask);
    }
    else if (offset >= E90_PAL_BASE && offset < E90_PAL_BASE + ALECK64_E90_PAL_SIZE) {
        masked_write(&a64->e90_pal[(offset & (ALECK64_E90_PAL_SIZE-1)) >> 2], value, mask);
    }
    else if (offset >= E90_CTRL_BASE && offset < E90_CTRL_BASE + 0x20) {
        /* 16-bit register at byte 0x1e, i.e. the low half of the word at 0x1c */
        if ((offset & 0x1c) == 0x1c)
            a64->e90_enable = value & 1;
    }
}

static uint8_t pal5(uint32_t c)
{
    c &= 0x1f;
    return (uint8_t)((c << 3) | (c >> 2));
}

int aleck64_e90_overlay(void* pixels, unsigned pitch, unsigned width, unsigned height,
                        unsigned src_width)
{
    /* Per-pixel shade index of an 8x8 block.  The real chip presumably reads
     * this from its character rom (tet-01m.u5), whose format nobody has worked
     * out, so MAME hardcodes the gradient and so do we. */
    static const uint8_t shade[8*8] = {
        8, 6, 6, 6, 6, 5, 4, 3,
        9, 7, 5, 6, 4, 1, 1, 4,
        9, 8, 6, 5, 1, 1, 1, 5,
        9, 8, 7, 5, 1, 1, 4, 6,
        9, 8, 7, 6, 5, 5, 6, 6,
        9, 8, 6, 7, 7, 6, 5, 6,
        9, 8, 8, 8, 8, 8, 7, 6,
        8, 9, 9, 9, 9, 9, 9, 8
    };
    /* An empty cell is not a blank block: the chip draws a soft-edged column
     * separator there, the same for every row of the cell.  Read off a
     * reference capture of the real machine -- palette bank 4 entries 0x0d,
     * 0x0e and 0x0f, which are a dark grey ramp (16,16,16) (33,33,33)
     * (66,66,66) that MAME's block gradient never touches.  0 means the pixel
     * is left alone. */
    static const uint8_t shade_empty[8] = { 0x0e, 0x0d, 0, 0, 0, 0x0d, 0x0e, 0x0f };
    struct aleck64* a64 = l_a64;
    uint8_t* out = (uint8_t*)pixels;
    unsigned offs;
    int drawn = 0;

    if (!g_aleck64_enabled || !g_aleck64_e90 || a64 == NULL || !a64->e90_enable)
        return 0;
    if (src_width == 0 || width == 0 || height == 0)
        return 0;

    for (offs = 0; offs < ALECK64_E90_VRAM_SIZE/4; offs += 2) {
        uint32_t attr = a64->e90_vram[offs] & 0xffff;
        uint32_t pal;
        int32_t x, y;
        int xi, yi;
        /* Empty cells (bit 5) tile the whole playfield; the real machine
         * draws the shade_empty separator there instead of a block. */
        int empty = (attr & 0x20) != 0;

        pal = (attr & 0x06) >> 1;
        x = (int16_t)(a64->e90_vram[offs+1] >> 16);
        y = (int16_t)(a64->e90_vram[offs+1] & 0xffff);
        x >>= 1;
        if ((y & 0xff00) == 0xbc00)
            pal |= 0x40 >> 1;      /* ghost piece */
        pal |= (attr & 0xc0) >> 4; /* colour bank */
        drawn++;

        for (yi = 0; yi < 8; yi++) {
            int ry = (y & 0xff) + yi + 7;

            if (ry < 0 || (unsigned)ry >= height)
                continue;

            for (xi = 0; xi < 8; xi++) {
                /* Source column [x+4+xi, +1) stretched to its output span, so
                 * adjacent blocks stay flush however the VI scaled the frame. */
                int sx = x + xi + 4;
                int rx0 = (int)((int64_t)sx * width / src_width);
                int rx1 = (int)((int64_t)(sx + 1) * width / src_width);
                unsigned po;
                uint32_t raw;
                uint8_t b, g, r;
                int rx;

                /* Untouched columns must stay transparent for the GL
                 * overlay path, so skip them rather than paint black. */
                if (empty && shade_empty[xi] == 0)
                    continue;

                po = pal * 0x10 + (empty ? shade_empty[xi] : shade[xi + yi*8]);
                raw = a64->e90_pal[po >> 1] >> ((po & 1) ? 0 : 16);
                b = pal5(raw >> 10);
                g = pal5(raw >> 5);
                r = pal5(raw);

                if (rx1 <= rx0)
                    rx1 = rx0 + 1;
                if (rx0 < 0)
                    rx0 = 0;
                if ((unsigned)rx1 > width)
                    rx1 = (int)width;

                for (rx = rx0; rx < rx1; rx++) {
                    uint8_t* px = out + ((unsigned)ry * pitch + (unsigned)rx) * 4;
                    px[0] = b;
                    px[1] = g;
                    px[2] = r;
                    px[3] = 0xff;
                }
            }
        }
    }

    return drawn;
}
