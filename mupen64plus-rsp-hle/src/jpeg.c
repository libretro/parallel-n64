/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - jpeg.c                                          *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "arithmetics.h"
#include "hle_external.h"
#include "hle_internal.h"
#include <string.h>

#include "memory.h"

#define SUBBLOCK_SIZE 64

typedef void (*tile_line_emitter_t)(struct hle_t* hle, const int16_t *y, const int16_t *u, uint32_t address);
typedef void (*subblock_transform_t)(int16_t *dst, const int16_t *src);

/* transposed dequantization table */
static const int16_t DEFAULT_QTABLE[SUBBLOCK_SIZE] = {
    16, 12, 14, 14,  18,  24,  49,  72,
    11, 12, 13, 17,  22,  35,  64,  92,
    10, 14, 16, 22,  37,  55,  78,  95,
    16, 19, 24, 29,  56,  64,  87,  98,
    24, 26, 40, 51,  68,  81, 103, 112,
    40, 58, 57, 87, 109, 104, 121, 100,
    51, 60, 69, 80, 103, 113, 120, 103,
    61, 55, 56, 62,  77,  92, 101,  99
};

/* zig-zag indices */
static const unsigned int ZIGZAG_TABLE[SUBBLOCK_SIZE] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

/* transposition indices */
static const unsigned int TRANSPOSE_TABLE[SUBBLOCK_SIZE] = {
    0,  8, 16, 24, 32, 40, 48, 56,
    1,  9, 17, 25, 33, 41, 49, 57,
    2, 10, 18, 26, 34, 42, 50, 58,
    3, 11, 19, 27, 35, 43, 51, 59,
    4, 12, 20, 28, 36, 44, 52, 60,
    5, 13, 21, 29, 37, 45, 53, 61,
    6, 14, 22, 30, 38, 46, 54, 62,
    7, 15, 23, 31, 39, 47, 55, 63
};

/* IDCT related constants
 * Cn = alpha * cos(n * PI / 16) (alpha is chosen such as C4 = 1) */
/* The RSP has no FPU; these are the same constants in S15.16 fixed point
 * (value * 2^16, rounded to nearest), so the transform below is integer-only. */
#define IDCT_SHIFT 16

static int16_t clamp_s12(int16_t x)
{
    if (x < -0x800)
        return -0x800;
    else if (x > 0x7f0)
        return 0x7f0;
    return x;
}

static void RescaleUVSubBlock(int16_t *dst, const int16_t *src)
{
    unsigned int i;

    for (i = 0; i < SUBBLOCK_SIZE; ++i)
        dst[i] = (((int)clamp_s12(src[i]) * 0xe00) >> 16) + 0x80;
}

static void RescaleYSubBlock(int16_t *dst, const int16_t *src)
{
    unsigned int i;

    for (i = 0; i < SUBBLOCK_SIZE; ++i)
        dst[i] = (((uint32_t)(clamp_s12(src[i]) + 0x800) * 0xdb0) >> 16) + 0x10;
}

static void EmitTilesMode0(struct hle_t* hle, const tile_line_emitter_t emit_line, const int16_t *macroblock, uint32_t address)
{
    unsigned int i;

    unsigned int y_offset = 0;
    unsigned int u_offset = 2 * SUBBLOCK_SIZE;

    for (i = 0; i < 8; ++i) {
        emit_line(hle, &macroblock[y_offset], &macroblock[u_offset], address);

        y_offset += 8;
        u_offset += 8;
        address += 32;
    }
}

static void EmitTilesMode2(struct hle_t* hle, const tile_line_emitter_t emit_line, const int16_t *macroblock, uint32_t address)
{
    unsigned int i;

    unsigned int y_offset = 0;
    unsigned int u_offset = 4 * SUBBLOCK_SIZE;

    for (i = 0; i < 8; ++i) {
        emit_line(hle, &macroblock[y_offset],     &macroblock[u_offset], address);
        emit_line(hle, &macroblock[y_offset + 8], &macroblock[u_offset], address + 32);

        y_offset += (i == 3) ? SUBBLOCK_SIZE + 16 : 16;
        u_offset += 8;
        address += 64;
    }
}

/***************************************************************************
 * Exact RSP IDCT: two passes of the microcode's vmulf/vmacf butterfly.
 * The Q15 cosines and the structure are shared by the OB and PS decoders;
 * only the final normalization lanes differ (OB divides by 64, PS by 4).
 * The mirrored second-pass outputs share the 48-bit accumulator, so each
 * pair picks up a single 0x8000 rounding term.
 **************************************************************************/
static const int16_t RSP_IDCT_C2[6] = {
     0x18f9, -0x7d8a, 0x6a6e, -0x471d, 0x471d, 0x7d8a };
static const int16_t RSP_IDCT_C3[5] = {
     0x5a82, -0x5a82, 0x30fc, -0x7642, 0x7642 };

static void RspIdctButterfly(const int16_t in[8][8], int16_t parts[8][8])
{
    int64_t acc[8];
    int16_t t4[8], t5[8], t6[8], t7[8], t8[8], t9[8], t10[8], t11[8];
    int16_t t4s[8], t5s[8];
    int c;

#define RSP_VMULF(dst, va, ca) \
    do { for (c = 0; c < 8; ++c) { \
        acc[c] = 2 * (int64_t)(va)[c] * (ca) + 0x8000; \
        (dst)[c] = clamp_s16((int32_t)(acc[c] >> 16)); } } while (0)
#define RSP_VMACF(dst, vb, cb) \
    do { for (c = 0; c < 8; ++c) { \
        acc[c] += 2 * (int64_t)(vb)[c] * (cb); \
        (dst)[c] = clamp_s16((int32_t)(acc[c] >> 16)); } } while (0)

    RSP_VMULF(t10, in[3], RSP_IDCT_C2[2]); RSP_VMACF(t10, in[5], RSP_IDCT_C2[4]);
    RSP_VMULF(t11, in[7], RSP_IDCT_C2[0]); RSP_VMACF(t11, in[1], RSP_IDCT_C2[5]);
    RSP_VMULF(t8,  in[1], RSP_IDCT_C2[0]); RSP_VMACF(t8,  in[7], RSP_IDCT_C2[1]);
    RSP_VMULF(t9,  in[5], RSP_IDCT_C2[2]); RSP_VMACF(t9,  in[3], RSP_IDCT_C2[3]);
    RSP_VMULF(t6,  in[0], RSP_IDCT_C3[0]); RSP_VMACF(t6,  in[4], RSP_IDCT_C3[1]);
    for (c = 0; c < 8; ++c) {
        t5s[c] = clamp_s16(t11[c] - t10[c]);
        t4s[c] = clamp_s16(t8[c] - t9[c]);
        parts[3][c] = clamp_s16(t8[c] + t9[c]);   /* v12 */
        parts[1][c] = clamp_s16(t11[c] + t10[c]); /* v15 */
    }
    RSP_VMULF(t8, t5s, RSP_IDCT_C3[0]); RSP_VMACF(t8, t4s, RSP_IDCT_C3[1]);
    for (c = 0; c < 8; ++c) parts[7][c] = t8[c];  /* v13 */
    RSP_VMULF(t8, t5s, RSP_IDCT_C3[0]); RSP_VMACF(t8, t4s, RSP_IDCT_C3[0]);
    for (c = 0; c < 8; ++c) parts[5][c] = t8[c];  /* v14 */
    RSP_VMULF(t4, in[0], RSP_IDCT_C3[0]); RSP_VMACF(t4, in[4], RSP_IDCT_C3[0]);
    RSP_VMULF(t5, in[6], RSP_IDCT_C3[2]); RSP_VMACF(t5, in[2], RSP_IDCT_C3[4]);
    RSP_VMULF(t7, in[2], RSP_IDCT_C3[2]); RSP_VMACF(t7, in[6], RSP_IDCT_C3[3]);
    for (c = 0; c < 8; ++c) {
        parts[0][c] = clamp_s16(t4[c] + t5[c]);   /* v8  */
        parts[2][c] = clamp_s16(t4[c] - t5[c]);   /* v11 */
        parts[4][c] = clamp_s16(t6[c] + t7[c]);   /* v9  */
        parts[6][c] = clamp_s16(t6[c] - t7[c]);   /* v10 */
    }
#undef RSP_VMACF
#undef RSP_VMULF
}

static void RspIdctSubBlock(int16_t *block, int16_t hi_mult, int16_t lo_mult)
{
    int16_t in[8][8], parts[8][8], p1[8][8];
    int r, c;

    for (r = 0; r < 8; ++r)
        for (c = 0; c < 8; ++c)
            in[r][c] = block[r * 8 + c];
    RspIdctButterfly((const int16_t (*)[8])in, parts);
    for (c = 0; c < 8; ++c) {
        p1[0][c] = clamp_s16(parts[0][c] + parts[1][c]);
        p1[1][c] = clamp_s16(parts[4][c] + parts[5][c]);
        p1[2][c] = clamp_s16(parts[6][c] + parts[7][c]);
        p1[3][c] = clamp_s16(parts[2][c] + parts[3][c]);
        p1[4][c] = clamp_s16(parts[2][c] - parts[3][c]);
        p1[5][c] = clamp_s16(parts[6][c] - parts[7][c]);
        p1[6][c] = clamp_s16(parts[4][c] - parts[5][c]);
        p1[7][c] = clamp_s16(parts[0][c] - parts[1][c]);
    }
    for (r = 0; r < 8; ++r)
        for (c = 0; c < 8; ++c)
            in[r][c] = p1[c][r];
    RspIdctButterfly((const int16_t (*)[8])in, parts);
    for (c = 0; c < 8; ++c) {
        int64_t a2;
#define RSP_PAIR(hi_r, lo_r, ia, ib) \
        a2 = 2 * (int64_t)parts[ia][c] * hi_mult + 0x8000 \
           + 2 * (int64_t)parts[ib][c] * hi_mult; \
        block[(hi_r) * 8 + c] = clamp_s16((int32_t)(a2 >> 16)); \
        a2 += 2 * (int64_t)parts[ib][c] * lo_mult; \
        block[(lo_r) * 8 + c] = clamp_s16((int32_t)(a2 >> 16));
        RSP_PAIR(0, 7, 0, 1)
        RSP_PAIR(3, 4, 2, 3)
        RSP_PAIR(1, 6, 4, 5)
        RSP_PAIR(2, 5, 6, 7)
#undef RSP_PAIR
    }
}

static void MultSubBlocks(int16_t *dst, const int16_t *src1, const int16_t *src2, unsigned int shift)
{
    unsigned int i;

    for (i = 0; i < SUBBLOCK_SIZE; ++i) {
        int32_t v = src1[i] * src2[i];
        dst[i] = clamp_s16(v) << shift;
    }
}

static void ReorderSubBlock(int16_t *dst, const int16_t *src, const unsigned int *table)
{
    unsigned int i;

    /* source and destination sublocks cannot overlap */
    assert(labs(dst - src) >= SUBBLOCK_SIZE);

    for (i = 0; i < SUBBLOCK_SIZE; ++i)
        dst[i] = src[table[i]];
}


static void ZigZagSubBlock(int16_t *dst, const int16_t *src)
{
    ReorderSubBlock(dst, src, ZIGZAG_TABLE);
}

static void TransposeSubBlock(int16_t *dst, const int16_t *src)
{
    ReorderSubBlock(dst, src, TRANSPOSE_TABLE);
}

static void decode_macroblock_std(const subblock_transform_t transform_luma,
                                  const subblock_transform_t transform_chroma,
                                  int16_t *macroblock,
                                  unsigned int subblock_count,
                                  const int16_t qtables[3][SUBBLOCK_SIZE])
{
    unsigned int sb;
    unsigned int q = 0;

    for (sb = 0; sb < subblock_count; ++sb) {
        int16_t tmp_sb[SUBBLOCK_SIZE];
        const int isChromaSubBlock = (subblock_count - sb <= 2);

        if (isChromaSubBlock)
            ++q;

        MultSubBlocks(macroblock, macroblock, qtables[q], 4);
        ZigZagSubBlock(tmp_sb, macroblock);
        TransposeSubBlock(macroblock, tmp_sb);
        RspIdctSubBlock(macroblock, 0x2000, -0x4000);

        if (isChromaSubBlock) {
            if (transform_chroma != NULL)
                transform_chroma(macroblock, macroblock);
        } else {
            if (transform_luma != NULL)
                transform_luma(macroblock, macroblock);
        }

        macroblock += SUBBLOCK_SIZE;
    }
}


/* local functions */
static void jpeg_decode_std(struct hle_t* hle,
                            const char *const version,
                            const subblock_transform_t transform_luma,
                            const subblock_transform_t transform_chroma,
                            const tile_line_emitter_t emit_line)
{
    int16_t qtables[3][SUBBLOCK_SIZE];
    unsigned int mb;
    uint32_t address;
    uint32_t macroblock_count;
    uint32_t mode;
    uint32_t qtableY_ptr;
    uint32_t qtableU_ptr;
    uint32_t qtableV_ptr;
    unsigned int subblock_count;
    unsigned int macroblock_size;
    /* macroblock contains at most 6 subblocks */
    int16_t macroblock[6 * SUBBLOCK_SIZE];
    uint32_t data_ptr;

    if (*dmem_u32(hle, TASK_FLAGS) & 0x1) {
        HleWarnMessage(hle->user_defined,
                       "jpeg_decode_%s: task yielding not implemented", version);
        return;
    }

    data_ptr = *dmem_u32(hle, TASK_DATA_PTR);
    address          = *dram_u32(hle, data_ptr);
    macroblock_count = *dram_u32(hle, data_ptr + 4);
    mode             = *dram_u32(hle, data_ptr + 8);
    qtableY_ptr      = *dram_u32(hle, data_ptr + 12);
    qtableU_ptr      = *dram_u32(hle, data_ptr + 16);
    qtableV_ptr      = *dram_u32(hle, data_ptr + 20);

    if (mode != 0 && mode != 2) {
        HleWarnMessage(hle->user_defined,
                       "jpeg_decode_%s: invalid mode %d", version, mode);
        return;
    }

    subblock_count = mode + 4;
    macroblock_size = subblock_count * SUBBLOCK_SIZE;

    dram_load_u16(hle, (uint16_t *)qtables[0], qtableY_ptr, SUBBLOCK_SIZE);
    dram_load_u16(hle, (uint16_t *)qtables[1], qtableU_ptr, SUBBLOCK_SIZE);
    dram_load_u16(hle, (uint16_t *)qtables[2], qtableV_ptr, SUBBLOCK_SIZE);

    for (mb = 0; mb < macroblock_count; ++mb) {
        dram_load_u16(hle, (uint16_t *)macroblock, address, macroblock_size);
        decode_macroblock_std(transform_luma, transform_chroma,
                              macroblock, subblock_count, (const int16_t (*)[SUBBLOCK_SIZE])qtables);

        if (mode == 0)
            EmitTilesMode0(hle, emit_line, macroblock, address);
        else
            EmitTilesMode2(hle, emit_line, macroblock, address);

        address += 2 * macroblock_size;
    }
}

static uint8_t clamp_u8(int16_t x)
{
    return (x & (0xff00)) ? ((-x) >> 15) & 0xff : x;
}

static uint32_t GetUYVY(int16_t y1, int16_t y2, int16_t u, int16_t v)
{
    return (uint32_t)clamp_u8(u)  << 24 |
           (uint32_t)clamp_u8(y1) << 16 |
           (uint32_t)clamp_u8(v)  << 8 |
           (uint32_t)clamp_u8(y2);
}

static uint16_t clamp_RGBA_component(int16_t x)
{
    if (x > 0xff0)
        return 0xff0;
    else if (x < 0)
        return 0;
    return (x & 0xf80);
}

static uint16_t GetRGBA(int16_t y, int16_t u, int16_t v)
{
    /* Transcribed from the microcode's vmudm/vadd color stage: each
     * fractional term is a signed x unsigned mid product truncated to
     * acc[31:16] on its own, the integer parts arrive as separate
     * saturating adds, and the luma base picks up its +2048 the same
     * way. The unsigned coefficient reads make the constants 26378,
     * 22564, 46819, and 50659 - not the rounded S15.16 set. */
    const int32_t base = clamp_s16((int32_t)y + 2048);

    const int32_t rt = clamp_s16((int32_t)(((int64_t)v * 26378) >> 16) + v);
    const int32_t gt = clamp_s16((int32_t)(((int64_t)u * 22564) >> 16)
                                + (int32_t)(((int64_t)v * 46819) >> 16));
    const int32_t bt = clamp_s16((int32_t)(((int64_t)u * 50659) >> 16) + u);

    const uint16_t r = clamp_RGBA_component((int16_t)clamp_s16(base + rt));
    const uint16_t g = clamp_RGBA_component((int16_t)clamp_s16(base - gt));
    const uint16_t b = clamp_RGBA_component((int16_t)clamp_s16(base + bt));

    return (r << 4) | (g >> 1) | (b >> 6) | 1;
}


static void EmitYUVTileLine(struct hle_t* hle, const int16_t *y, const int16_t *u, uint32_t address)
{
    uint32_t uyvy[8];

    const int16_t *const v  = u + SUBBLOCK_SIZE;
    const int16_t *const y2 = y + SUBBLOCK_SIZE;

    uyvy[0] = GetUYVY(y[0],  y[1],  u[0], v[0]);
    uyvy[1] = GetUYVY(y[2],  y[3],  u[1], v[1]);
    uyvy[2] = GetUYVY(y[4],  y[5],  u[2], v[2]);
    uyvy[3] = GetUYVY(y[6],  y[7],  u[3], v[3]);
    uyvy[4] = GetUYVY(y2[0], y2[1], u[4], v[4]);
    uyvy[5] = GetUYVY(y2[2], y2[3], u[5], v[5]);
    uyvy[6] = GetUYVY(y2[4], y2[5], u[6], v[6]);
    uyvy[7] = GetUYVY(y2[6], y2[7], u[7], v[7]);

    dram_store_u32(hle, uyvy, address, 8);
}

static void EmitRGBATileLine(struct hle_t* hle, const int16_t *y, const int16_t *u, uint32_t address)
{
    uint16_t rgba[16];

    const int16_t *const v  = u + SUBBLOCK_SIZE;
    const int16_t *const y2 = y + SUBBLOCK_SIZE;

    rgba[0]  = GetRGBA(y[0],  u[0], v[0]);
    rgba[1]  = GetRGBA(y[1],  u[0], v[0]);
    rgba[2]  = GetRGBA(y[2],  u[1], v[1]);
    rgba[3]  = GetRGBA(y[3],  u[1], v[1]);
    rgba[4]  = GetRGBA(y[4],  u[2], v[2]);
    rgba[5]  = GetRGBA(y[5],  u[2], v[2]);
    rgba[6]  = GetRGBA(y[6],  u[3], v[3]);
    rgba[7]  = GetRGBA(y[7],  u[3], v[3]);
    rgba[8]  = GetRGBA(y2[0], u[4], v[4]);
    rgba[9]  = GetRGBA(y2[1], u[4], v[4]);
    rgba[10] = GetRGBA(y2[2], u[5], v[5]);
    rgba[11] = GetRGBA(y2[3], u[5], v[5]);
    rgba[12] = GetRGBA(y2[4], u[6], v[6]);
    rgba[13] = GetRGBA(y2[5], u[6], v[6]);
    rgba[14] = GetRGBA(y2[6], u[7], v[7]);
    rgba[15] = GetRGBA(y2[7], u[7], v[7]);

    dram_store_u16(hle, rgba, address, 16);
}


/* global functions */

/***************************************************************************
 * JPEG decoding ucode found in Japanese exclusive version of Pokemon Stadium.
 **************************************************************************/
void jpeg_decode_PS0(struct hle_t* hle)
{
    jpeg_decode_std(hle, "PS0", RescaleYSubBlock, RescaleUVSubBlock, EmitYUVTileLine);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

/***************************************************************************
 * JPEG decoding ucode found in Ocarina of Time, Pokemon Stadium 1 and
 * Pokemon Stadium 2.
 **************************************************************************/
void jpeg_decode_PS(struct hle_t* hle)
{
    jpeg_decode_std(hle, "PS", NULL, NULL, EmitRGBATileLine);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

static void RShiftSubBlock(int16_t *dst, const int16_t *src, unsigned int shift)
{
    unsigned int i;

    for (i = 0; i < SUBBLOCK_SIZE; ++i)
        dst[i] = src[i] >> shift;
}

static void ScaleSubBlock(int16_t *dst, const int16_t *src, int16_t scale)
{
    unsigned int i;

    for (i = 0; i < SUBBLOCK_SIZE; ++i) {
        int32_t v = src[i] * scale;
        dst[i] = clamp_s16(v);
    }
}



/***************************************************************************
 * JPEG decoding ucode found in Ogre Battle and Bottom of the 9th.
 **************************************************************************/
void jpeg_decode_OB(struct hle_t* hle)
{
    /* Transcribed from the microcode (Ogre Battle boot scene, 300
     * macroblocks, verified byte-exact against cxd4):
     *
     * - The quantization scale is applied to the table with a rounded
     *   arithmetic shift, q' = (q + (1 << (s-1))) >> s for qscale -s;
     *   a zero qscale selects a unit table (the coefficients still run
     *   through the same pipeline).
     * - Dequantization is vmudh/vmudn: the product clamps to 16 bits,
     *   then the multiply by 16 keeps only the low 16 bits (wrapping).
     * - The IDCT is two passes of a vmulf/vmacf butterfly with Q15
     *   cosines and the 48-bit accumulator shared between the mirrored
     *   outputs, so both pick up a single 0x8000 rounding term.
     * - Emission packs U/Y/V/Y byte pairs (4:2:0, chroma rows halved),
     *   and the write-back DMA moves 0x300 bytes per macroblock from
     *   the staging base, carrying the 0x100 bytes of DMEM that follow
     *   the payload - including the OSTask words - along with it. */
    static const int16_t ob_c2[6] = {
         0x18f9, -0x7d8a, 0x6a6e, -0x471d, 0x471d, 0x7d8a };
    static const int16_t ob_c3[5] = {
         0x5a82, -0x5a82, 0x30fc, -0x7642, 0x7642 };
    static const uint8_t ob_zigzag[64] = {
         0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

    int16_t qtable[SUBBLOCK_SIZE];
    unsigned int mb, i;
    int32_t y_dc = 0;
    int32_t u_dc = 0;
    int32_t v_dc = 0;

    uint32_t           address          = *dmem_u32(hle, TASK_DATA_PTR);
    const unsigned int macroblock_count = *dmem_u32(hle, TASK_DATA_SIZE);
    const int          qscale           = *dmem_u32(hle, TASK_YIELD_DATA_SIZE);

    if (qscale == 0) {
        for (i = 0; i < SUBBLOCK_SIZE; ++i)
            qtable[i] = 1;
    } else if (qscale > 0) {
        ScaleSubBlock(qtable, DEFAULT_QTABLE, qscale);
    } else {
        const int s = -qscale;
        for (i = 0; i < SUBBLOCK_SIZE; ++i)
            qtable[i] = (DEFAULT_QTABLE[i] + (1 << (s - 1))) >> s;
    }

    for (mb = 0; mb < macroblock_count; ++mb) {
        int16_t macroblock[6 * SUBBLOCK_SIZE];
        int16_t pixels[6][SUBBLOCK_SIZE];
        uint16_t line[16];
        int sb, r, c;

        dram_load_u16(hle, (uint16_t *)macroblock, address, 6 * SUBBLOCK_SIZE);

        for (sb = 0; sb < 6; ++sb) {
            const int16_t *src = macroblock + sb * SUBBLOCK_SIZE;
            int16_t coef[SUBBLOCK_SIZE];
            int32_t dc = (int32_t)src[0];

            if (sb < 4)      { y_dc = (int16_t)(y_dc + dc); dc = y_dc; }
            else if (sb == 4){ u_dc = (int16_t)(u_dc + dc); dc = u_dc; }
            else             { v_dc = (int16_t)(v_dc + dc); dc = v_dc; }

            memset(coef, 0, sizeof(coef));
            coef[0] = (int16_t)dc;
            for (i = 1; i < SUBBLOCK_SIZE; ++i)
                coef[ob_zigzag[i]] = src[i];

            /* fused dequantization: clamp the product, wrap the <<4 */
            for (i = 0; i < SUBBLOCK_SIZE; ++i)
                coef[i] = (int16_t)((uint32_t)clamp_s16(coef[i] * qtable[i]) << 4);

            RspIdctSubBlock(coef, 0x200, -0x400);
            memcpy(pixels[sb], coef, sizeof(coef));
        }

        /* emit sixteen U/Y/V/Y lines; chroma advances every other row */
        for (r = 0; r < 16; ++r) {
            const int ysb = (r < 8) ? 0 : 2;
            const int16_t *urow = pixels[4] + (r >> 1) * 8;
            const int16_t *vrow = pixels[5] + (r >> 1) * 8;
            int j;
            for (j = 0; j < 8; ++j) {
                const int c0 = 2 * j;
                const int c1 = 2 * j + 1;
                int y0 = pixels[ysb + (c0 >= 8)][(r % 8) * 8 + (c0 % 8)];
                int y1 = pixels[ysb + (c1 >= 8)][(r % 8) * 8 + (c1 % 8)];
                int u  = urow[j];
                int v  = vrow[j];
                if (y0 < 0) y0 = 0; else if (y0 > 255) y0 = 255;
                if (y1 < 0) y1 = 0; else if (y1 > 255) y1 = 255;
                if (u < 0)  u = 0;  else if (u > 255)  u = 255;
                if (v < 0)  v = 0;  else if (v > 255)  v = 255;
                line[2 * j]     = (uint16_t)((u << 8) | y0);
                line[2 * j + 1] = (uint16_t)((v << 8) | y1);
            }
            dram_store_u16(hle, line, address + r * 32, 16);
        }

        /* the write-back DMA carries the 0x100 bytes of DMEM after the
         * staging payload - garbage to the game, but part of the
         * transfer */
        for (r = 0; r < 8; ++r) {
            for (c = 0; c < 16; ++c)
                line[c] = *dmem_u16(hle, 0xef0 + r * 32 + 2 * c);
            dram_store_u16(hle, line, address + 0x200 + r * 32, 16);
        }

        address += (2 * 6 * SUBBLOCK_SIZE);
    }
    rsp_break(hle, SP_STATUS_TASKDONE);
}

