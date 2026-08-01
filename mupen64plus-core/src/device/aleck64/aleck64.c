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
    /* ponytail: never freed, reused across loads (same lifetime as cart_data) */
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

static uint32_t read_port1(struct aleck64* a64, uint32_t b)
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
    case 0x0000: *value = read_port1(a64, poll_buttons()); break;
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
