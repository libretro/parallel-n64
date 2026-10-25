/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dd_ipl_hle_font.c                                       *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "dd_ipl_hle_font.h"

/* Both glyph formats are four bits per pixel, the left pixel in the high
 * nibble, rows packed without padding beyond the last byte. */

static void put_byte(uint32_t* font, size_t words, uint32_t off,
                     unsigned int value)
{
    uint32_t shift = (3 - (off & 3)) * 8;
    if ((off >> 2) >= words)
        return;
    font[off >> 2] = (font[off >> 2] & ~(UINT32_C(0xff) << shift))
                   | ((uint32_t)(value & 0xff) << shift);
}

void dd_ipl_hle_font_build(uint32_t* font, size_t words)
{
    unsigned int i, y, x;

    for (i = 0; i < dd_ipl_hle_kanji_count; ++i)
    {
        for (y = 0; y < 16; ++y)
        {
            unsigned int row = ((unsigned int)dd_ipl_hle_kanji[i][2 * y] << 8)
                             | dd_ipl_hle_kanji[i][2 * y + 1];
            for (x = 0; x < 8; ++x)
            {
                unsigned int hi = (row >> (15 - 2 * x)) & 1;
                unsigned int lo = (row >> (14 - 2 * x)) & 1;
                put_byte(font, words, i * 128 + y * 8 + x,
                         (hi ? 0xf0 : 0) | (lo ? 0x0f : 0));
            }
        }
    }
}

/* The drive library's ASCII lookup range-checks the code against 0x908
 * and then addresses its table with lui/addiu into v1. */
#define LATIN_CODES        0x908
#define LATIN_FONT_CHARS   192
#define OP_SLTI_AT_A0_908  UINT32_C(0x28810908)
#define OP_LUI_V1          UINT32_C(0x3c030000)
#define OP_ADDIU_V1_V1     UINT32_C(0x24630000)

static int find_latin_table(const uint32_t* dram, size_t dram_size,
                            uint32_t* table)
{
    size_t i, j, n = dram_size / 4;

    for (i = 0; i + 12 < n; ++i)
    {
        uint32_t hi = 0, lo = 0;
        int have_hi = 0, have_lo = 0;

        if (dram[i] != OP_SLTI_AT_A0_908)
            continue;
        for (j = 1; j <= 12; ++j)
        {
            uint32_t op = dram[i + j];
            if ((op & UINT32_C(0xffff0000)) == OP_LUI_V1)
            {
                hi = op & 0xffff;
                have_hi = 1;
            }
            else if ((op & UINT32_C(0xffff0000)) == OP_ADDIU_V1_V1)
            {
                lo = op & 0xffff;
                have_lo = 1;
            }
        }
        if (have_hi && have_lo)
        {
            uint32_t addr = ((hi << 16) + (uint32_t)(int32_t)(int16_t)lo)
                          & UINT32_C(0x1fffffff);
            if ((addr & 3) == 0 && addr + 4 * LATIN_CODES <= dram_size)
            {
                *table = addr;
                return 1;
            }
        }
    }
    return 0;
}

/* One glyph: the 8x16 Latin bitmap's inked box scaled into the dx x dy box
 * the game's table gives, sixteen samples a pixel for the four-bit level. */
static void draw_latin(uint32_t* font, size_t words, uint32_t off,
                       const unsigned char* bits, unsigned int dx,
                       unsigned int dy)
{
    unsigned int minx = 8, maxx = 0, miny = 16, maxy = 0;
    unsigned int x, y, w0, h0, stride = (dx + 1) / 2;

    for (y = 0; y < 16; ++y)
        for (x = 0; x < 8; ++x)
            if ((bits[y] >> (7 - x)) & 1)
            {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    if (minx > maxx || dx == 0 || dy == 0)
        return;
    w0 = maxx - minx + 1;
    h0 = maxy - miny + 1;

    for (y = 0; y < dy; ++y)
    {
        for (x = 0; x < dx; ++x)
        {
            unsigned int sx, sy, hits = 0, level;
            for (sy = 0; sy < 4; ++sy)
                for (sx = 0; sx < 4; ++sx)
                {
                    unsigned int px = minx + ((x * 4 + sx) * w0) / (dx * 4);
                    unsigned int py = miny + ((y * 4 + sy) * h0) / (dy * 4);
                    hits += (bits[py] >> (7 - px)) & 1;
                }
            level = (hits * 15 + 8) / 16;
            if (level != 0)
            {
                uint32_t at = off + y * stride + x / 2;
                uint32_t word = font[at >> 2];
                unsigned int cur = (word >> ((3 - (at & 3)) * 8)) & 0xff;
                cur |= (x & 1) ? level : (level << 4);
                put_byte(font, words, at, cur);
            }
        }
    }
}

int dd_ipl_hle_font_latin(uint32_t* font, size_t words,
                          const uint32_t* dram, size_t dram_size)
{
    uint32_t table, k;

    if (!find_latin_table(dram, dram_size, &table))
        return 0;

    for (k = 0; k < LATIN_CODES; ++k)
    {
        uint32_t e = dram[table / 4 + k];
        unsigned int b2 = (e >> 8) & 0xff;
        unsigned int b3 = e & 0xff;
        unsigned int dx = (b2 >> 4) | ((b3 & 1) << 4);
        unsigned int dy = (b2 & 15) + 1;
        uint32_t off = DD_IPL_HLE_LATIN_START + (e >> 16) * 2;
        uint32_t size = ((dx + 1) / 2) * dy;

        if (k >= 12 * LATIN_FONT_CHARS || off + size > words * 4)
            continue;
        draw_latin(font, words, off,
                   dd_ipl_hle_latin[k % LATIN_FONT_CHARS], dx, dy);
    }
    return 1;
}
