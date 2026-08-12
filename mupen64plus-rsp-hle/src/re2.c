/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - re2.c                                           *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2016 Gilles Siberlin                                    *
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
#include <string.h>

#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"

#define SATURATE8(x) ((unsigned int) x <= 255 ? x : (x < 0 ? 0: 255))

/**************************************************************************
 * Resident evil 2 ucodes
 **************************************************************************/
/* Transcribed from the resize microcode.
 *
 * The ucode upscales a 24-bit source image (bytes ordered B,G,R per
 * pixel, 320 pixels / 960 bytes per line) to an RGBA5551 frame, walking
 * a 16.16 fixed-point lattice.  Each output row DMAs 0x780 bytes -- the
 * two source lines the row interpolates between -- to DMEM 0, and the
 * packed output row is staged right behind that window at DMEM 0x780,
 * which matters: for the right-most source column the bottom-right tap
 * address (xint * 3 + 3 + 0x3c0) runs past the window into the staging
 * area, so the tap reads bytes of this row's own first packed pixels.
 * The window below reproduces that feedback.
 *
 * All four bilinear corner weights are built from Q15 x/y fractions
 * with a truncation after every stage, exactly like the ucode:
 * weight = ((frac_a * frac_b) >> 16) * 4 in Q16, and each tap is
 * (sample * weight) >> 16, truncated before the four taps are summed.
 *
 * The ucode also carries a serial error-diffusion pipeline (per-lane
 * accumulators rotated through DMEM, three-channel VABS/VLT flag
 * AND-merge selecting between the accumulator and a constant).  Its
 * comparison constant in the ucode data is zero, and VLT can only pass
 * for |diff| < 0 (never) or a VCO-flagged tie, with VCO provably clear
 * at that point, so the selected error term is the constant 4 on every
 * pixel: the quantizer reduces to min(sum + 4, 255) & 0xf8.  The 16-bit
 * accumulate carries (VADDC into VADD) can never fire at byte scale
 * either.  Both mechanisms are therefore left out of the transcription. */
void resize_bilinear_task(struct hle_t* hle)
{
    uint32_t data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    uint32_t src_addr   = *dram_u32(hle, data_ptr)      & 0x7fffff;
    uint32_t dst_addr   = *dram_u32(hle, data_ptr + 4)  & 0x7fffff;
    uint32_t dst_width  = *dram_u32(hle, data_ptr + 8);
    uint32_t dst_height = *dram_u32(hle, data_ptr + 12);
    uint32_t x_step     = *dram_u32(hle, data_ptr + 16);
    uint32_t y_step     = *dram_u32(hle, data_ptr + 20);
    uint32_t dst_stride = *dram_u32(hle, data_ptr + 24);
    uint32_t x0         = *dram_u32(hle, data_ptr + 32);
    uint32_t y          = *dram_u32(hle, data_ptr + 36);

    uint8_t win[0x1000];
    uint32_t i, j, k;

    memset(win, 0, sizeof(win));

    for (i = 0; i < dst_height; ++i) {
        uint32_t row_src = (src_addr + (y >> 16) * 960) & 0x7fffff;
        uint32_t out = dst_addr;
        uint32_t yf = (y & 0xffff) >> 1;
        uint32_t ny = 0x7fff - yf;
        uint32_t x = x0;

        for (k = 0; k < 0x780; ++k)
            win[k] = *dram_u8(hle, row_src + k);

        for (j = 0; j < dst_width; ++j) {
            uint32_t xi = (x >> 16) * 3;
            uint32_t xf = (x & 0xffff) >> 1;
            uint32_t nx = 0x7fff - xf;
            uint32_t w_tl = ((nx * ny) >> 16) * 4;
            uint32_t w_tr = ((xf * ny) >> 16) * 4;
            uint32_t w_bl = ((nx * yf) >> 16) * 4;
            uint32_t w_br = ((xf * yf) >> 16) * 4;
            uint16_t pixel;
            unsigned c;
            uint32_t chan[3];

            for (c = 0; c < 3; ++c) {
                uint32_t tl = win[(xi + c)                 & 0xfff];
                uint32_t tr = win[(xi + 3 + c)             & 0xfff];
                uint32_t bl = win[(xi + 0x3c0 + c)         & 0xfff];
                uint32_t br = win[(xi + 0x3c0 + 3 + c)     & 0xfff];
                uint32_t v = ((tl * w_tl) >> 16) + ((tr * w_tr) >> 16)
                           + ((bl * w_bl) >> 16) + ((br * w_br) >> 16)
                           + 4;
                if (v > 255)
                    v = 255;
                chan[c] = v & 0xf8;
            }

            pixel = (uint16_t)((chan[2] << 8) | (chan[1] << 3) | (chan[0] >> 2) | 1);
            win[0x780 + 2 * j]     = (uint8_t)(pixel >> 8);
            win[0x780 + 2 * j + 1] = (uint8_t)pixel;
            dram_store_u16(hle, &pixel, out, 1);
            out += 2;
            x += x_step;
        }

        dst_addr = (dst_addr + dst_stride) & 0x7fffff;
        y += y_step;
    }

    rsp_break(hle, SP_STATUS_TASKDONE);
}

static int32_t sat16(int32_t x)
{
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}

static uint32_t YCbCr_to_RGBA(uint8_t Y, uint8_t Cb, uint8_t Cr)
{
    /* The RSP has no FPU: the video ucode does this with truncating VMUDM and
     * 16-bit coefficients held in DMEM. Those coefficients (read straight from
     * the RE2 ucode_data) are 38155, 45941, 23401, 11277, 58065 over 1<<16, so
     * the divide is >>16. The previous double form only approximated those
     * coefficients (e.g. 0.582199097 vs the exact 38155/65536) and disagreed
     * with the hardware in a handful of cases; this is the exact integer form. */
    /* The microcode works in a <<7 domain: LUV/LHV load the samples as
     * value<<7, each vmudm truncates its product to (x<<7)*coeff >> 16
     * individually, the terms combine through saturating vadd/vsub,
     * and the store takes bits 14:7 after a [0, 255<<7] clamp.  The
     * per-term truncation loses up to one output LSB per term relative
     * to summing full products, so it must be modeled exactly. */
    int32_t y  = ((int32_t)(Y << 7) * 38155) >> 16;
    int32_t cr = (int32_t)(uint32_t)((Cr - 128) << 7);
    int32_t cb = (int32_t)(uint32_t)((Cb - 128) << 7);
    int32_t cr_r = (int32_t)((int64_t)(int16_t)cr * 45941 >> 16);
    int32_t cr_g = (int32_t)((int64_t)(int16_t)cr * 23401 >> 16);
    int32_t cb_g = (int32_t)((int64_t)(int16_t)cb * 11277 >> 16);
    int32_t cb_b = (int32_t)((int64_t)(int16_t)cb * 58065 >> 16);
    int32_t r7 = sat16(y + cr_r);
    int32_t g7 = sat16(sat16(y - cr_g) - cb_g);
    int32_t b7 = sat16(y + cb_b);

    r7 = (r7 < 0) ? 0 : (r7 > 0x7f80) ? 0x7f80 : r7;
    g7 = (g7 < 0) ? 0 : (g7 > 0x7f80) ? 0x7f80 : g7;
    b7 = (b7 < 0) ? 0 : (b7 > 0x7f80) ? 0x7f80 : b7;

    return ((uint32_t)(r7 >> 7) << 24) | ((uint32_t)(g7 >> 7) << 16) |
           ((uint32_t)(b7 >> 7) << 8) | 0;
}

void decode_video_frame_task(struct hle_t* hle)
{
    int data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    int pLuminance = *dram_u32(hle, data_ptr);
    int pCb = *dram_u32(hle, data_ptr + 4);
    int pCr = *dram_u32(hle, data_ptr + 8);
    int pDestination = *dram_u32(hle, data_ptr + 12);
    int nMovieWidth = *dram_u32(hle, data_ptr + 16);
    int nMovieHeight = *dram_u32(hle, data_ptr + 20);
    int nScreenDMAIncrement = *dram_u32(hle, data_ptr + 36);

    int i, j;
    uint8_t Y, Cb, Cr;
    uint32_t pixel;
    int pY_1st_row, pY_2nd_row, pDest_1st_row, pDest_2nd_row;

    for (i = 0; i < nMovieHeight; i += 2)
    {
        int pCrPair = pCr;
        pY_1st_row = pLuminance;
        pY_2nd_row = pLuminance + nMovieWidth;
        pDest_1st_row = pDestination;
        pDest_2nd_row = pDestination + (nScreenDMAIncrement >> 1);

        for (j = 0; j < nMovieWidth; j += 2)
        {
            dram_load_u8(hle, (uint8_t*)&Cb, pCb++, 1);
            dram_load_u8(hle, (uint8_t*)&Cr, pCr++, 1);

            /*1st row*/
            /* the chroma DMAs move a full width of bytes into the
             * half-width buffers, and the Cr transfer spills its tail
             * past DMEM 0x300 into the output staging; the first-row
             * stores never touch every fourth staging byte, so those
             * pixels ship the spilled chroma bytes */
            dram_load_u8(hle, (uint8_t*)&Y, pY_1st_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            if (j*4 + 3 < nMovieWidth - 0x80) {
                uint8_t spill;
                dram_load_u8(hle, &spill, pCrPair + 0x80 + j*4 + 3, 1);
                pixel |= spill;
            }
            dram_store_u32(hle, &pixel, pDest_1st_row, 1);
            pDest_1st_row += 4;

            dram_load_u8(hle, (uint8_t*)&Y, pY_1st_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            if (j*4 + 7 < nMovieWidth - 0x80) {
                uint8_t spill;
                dram_load_u8(hle, &spill, pCrPair + 0x80 + j*4 + 7, 1);
                pixel |= spill;
            }
            dram_store_u32(hle, &pixel, pDest_1st_row, 1);
            pDest_1st_row += 4;

            /*2nd row*/
            dram_load_u8(hle, (uint8_t*)&Y, pY_2nd_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            dram_store_u32(hle, &pixel, pDest_2nd_row, 1);
            pDest_2nd_row += 4;

            dram_load_u8(hle, (uint8_t*)&Y, pY_2nd_row++, 1);
            pixel = YCbCr_to_RGBA(Y, Cb, Cr);
            dram_store_u32(hle, &pixel, pDest_2nd_row, 1);
            pDest_2nd_row += 4;
        }

        pLuminance += (nMovieWidth << 1);
        pDestination += nScreenDMAIncrement;
    }

    rsp_break(hle, SP_STATUS_TASKDONE);
}

void fill_video_double_buffer_task(struct hle_t* hle)
{
    int data_ptr = *dmem_u32(hle, TASK_UCODE_DATA);

    int pSrc = *dram_u32(hle, data_ptr);
    int pDest = *dram_u32(hle, data_ptr + 0x4);
    int width = *dram_u32(hle, data_ptr + 0x8) >> 1;
    int height = *dram_u32(hle, data_ptr + 0x10) << 1;
    int stride = *dram_u32(hle, data_ptr + 0x1c) >> 1;

    assert((*dram_u32(hle, data_ptr + 0x28) >> 16) == 0x8000);

    int i, j;
    int r, g, b;
    uint32_t pixel, pixel1, pixel2;

    for(i = 0; i < height; i++)
    {
      for(j = 0; j < width; j=j+4)
      {
        pixel1 = *dram_u32(hle, pSrc+j);
        pixel2 = *dram_u32(hle, pDest+j);
      
        r = (((pixel1 >> 24) & 0xff) + ((pixel2 >> 24) & 0xff)) >> 1;
        g = (((pixel1 >> 16) & 0xff) + ((pixel2 >> 16) & 0xff)) >> 1;
        b = (((pixel1 >> 8) & 0xff) + ((pixel2 >> 8) & 0xff)) >> 1;
      
        pixel = (r << 24) | (g << 16) | (b << 8) | 0;
      
        dram_store_u32(hle, &pixel, pDest+j, 1);
      }
      pSrc += stride;
      pDest += stride;
    }

    rsp_break(hle, SP_STATUS_TASKDONE);
}
