/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dd_ipl_hle_font.h                                       *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Font area of the built-in 64DD IPL's ROM image. The drive's ROM keeps
 * 16x16 four-bit glyphs for the JIS characters at fixed slots, followed by
 * proportional Latin fonts whose positions and sizes each game's drive
 * library keeps in a table of its own. The stand-in fills the JIS slots
 * from public-domain Shinonome glyphs when the image is built, and the
 * Latin fonts from the game's own table once its code is in RDRAM. */

#ifndef M64P_DEVICE_DD_DD_IPL_HLE_FONT_H
#define M64P_DEVICE_DD_DD_IPL_HLE_FONT_H

#include <stddef.h>
#include <stdint.h>

/* glyph data, dd_ipl_hle_font_data.c */
extern const unsigned int dd_ipl_hle_kanji_count;
extern const unsigned char dd_ipl_hle_kanji[][32];
extern const unsigned char dd_ipl_hle_latin[192][16];

/* Offset of the Latin fonts within the font area. */
#define DD_IPL_HLE_LATIN_START 0x7ee80

/* Fill the JIS glyph slots of the font area (host-order words). */
void dd_ipl_hle_font_build(uint32_t* font, size_t words);

/* Fill the Latin fonts from the drive library's character table, found in
 * RDRAM (host-order words). Returns 1 once filled, 0 when no table is
 * there yet. */
int dd_ipl_hle_font_latin(uint32_t* font, size_t words,
                          const uint32_t* dram, size_t dram_size);

#endif
