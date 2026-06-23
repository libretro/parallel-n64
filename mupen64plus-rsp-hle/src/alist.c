/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist.c                                         *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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
#include <boolean.h>
#include <stdint.h>
#include <string.h>

#include "alist.h"
#include "arithmetics.h"
#include "audio.h"
#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"

struct ramp_t
{
    int64_t value;
    int64_t step;
    int64_t target;
};

/* local functions */
static void swap(int16_t **a, int16_t **b)
{
    int16_t* tmp = *b;
    *b = *a;
    *a = tmp;
}

static int16_t* sample(struct hle_t* hle, unsigned pos)
{
    return (int16_t*)hle->alist_buffer + ((pos ^ S) & 0xfff);
}

static uint8_t* alist_u8(struct hle_t* hle, uint16_t dmem)
{
    return (uint8_t*)(hle->alist_buffer + ((dmem ^ S8) & 0xfff));
}

static int16_t* alist_s16(struct hle_t* hle, uint16_t dmem)
{
    return (int16_t*)(hle->alist_buffer + ((dmem ^ S16) & 0xfff));
}


static void sample_mix(int16_t* dst, int16_t src, int16_t gain)
{
    /* aspMain mixes as vmulf(dst, 0x7fff) followed by vmacf(src, gain) on the
     * same vector accumulator, i.e.
     *   clamp_s16((0x8000 + 2*dst*0x7fff + 2*src*gain) >> 16),
     * the same form already used by the cxd4-validated nead mixer
     * (alist_mix_nead) and FILTER. The previous dst + (src*gain)>>15 form
     * omitted the 0x7fff per-mix attenuation of the running mix: harmless on a
     * single quiet mix, but it costs an LSB once the mixed buffer is loud and
     * the error then persists in the accumulated buffer, drifting from LLE. */
    int64_t acc = 0x8000 + 2 * (int64_t)((int32_t)(*dst) * 0x7fff)
                         + 2 * (int64_t)((int32_t)src * (int32_t)gain);
    *dst = clamp_s16((int_fast32_t)(acc >> 16));
}

static void alist_envmix_mix(size_t n, int16_t** dst, const int16_t* gains, int16_t src)
{
    size_t i;

    for(i = 0; i < n; ++i)
        sample_mix(dst[i], src, gains[i]);
}

static int16_t ramp_step(struct ramp_t* ramp)
{
    bool target_reached;

    ramp->value += ramp->step;

    target_reached = (ramp->step <= 0)
        ? (ramp->value <= ramp->target)
        : (ramp->value >= ramp->target);

    if (target_reached)
    {
        ramp->value = ramp->target;
        ramp->step  = 0;
    }

    return (int16_t)(ramp->value >> 16);
}

/* global functions */
void alist_process(struct hle_t* hle, const acmd_callback_t abi[], unsigned int abi_size)
{
    uint32_t w1, w2;
    unsigned int acmd;

    const uint32_t *alist = dram_u32(hle, *dmem_u32(hle, TASK_DATA_PTR));
    const uint32_t *const alist_end = alist + (*dmem_u32(hle, TASK_DATA_SIZE) >> 2);

    while (alist != alist_end) {
        w1 = *(alist++);
        w2 = *(alist++);

        acmd = (w1 >> 24) & 0x7f;

        if (acmd < abi_size)
            (*abi[acmd])(hle, w1, w2);
        else
            HleWarnMessage(hle->user_defined, "Invalid ABI command %u", acmd);
    }
}

uint32_t alist_get_address(struct hle_t* hle, uint32_t so, const uint32_t *segments, size_t n)
{
    uint8_t  segment = (so >> 24) & 0x3f;
    uint32_t offset  = (so & 0xffffff);

    if (segment >= n) {
        HleWarnMessage(hle->user_defined, "Invalid segment %u", segment);
        return offset;
    }

    return segments[segment] + offset;
}

void alist_set_address(struct hle_t* hle, uint32_t so, uint32_t *segments, size_t n)
{
    uint8_t  segment = (so >> 24) & 0x3f;
    uint32_t offset  = (so & 0xffffff);

    if (segment >= n) {
        HleWarnMessage(hle->user_defined, "Invalid segment %u", segment);
        return;
    }

    segments[segment] = offset;
}

void alist_clear(struct hle_t* hle, uint16_t dmem, uint16_t count)
{
    while(count != 0) {
        *alist_u8(hle, dmem++) = 0;
        --count;
    }
}

void alist_load(struct hle_t* hle, uint16_t dmem, uint32_t address, uint16_t count)
{
    /* enforce DMA alignment constraints */
    dmem    &= ~3;
    address &= ~7;
    count = align(count, 8);
    memcpy(hle->alist_buffer + dmem, hle->dram + address, count);
}

void alist_save(struct hle_t* hle, uint16_t dmem, uint32_t address, uint16_t count)
{
    /* enforce DMA alignment constraints */
    dmem    &= ~3;
    address &= ~7;
    count = align(count, 8);
    memcpy(hle->dram + address, hle->alist_buffer + dmem, count);
}

void alist_move(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    while (count != 0) {
        *alist_u8(hle, dmemo++) = *alist_u8(hle, dmemi++);
        --count;
    }
}

void alist_copy_every_other_sample(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    while (count != 0) {
        *alist_s16(hle, dmemo) = *alist_s16(hle, dmemi);
        dmemo += 2;
        dmemi += 4;
        --count;
    }
}

void alist_repeat64(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint8_t count)
{
    uint16_t buffer[64];

    memcpy(buffer, hle->alist_buffer + dmemi, 128);

    while(count != 0) {
        memcpy(hle->alist_buffer + dmemo, buffer, 128);
        dmemo += 128;
        --count;
    }
}

void alist_copy_blocks(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t block_size, uint8_t count)
{
    int block_left = count;

    do
    {
        int bytes_left = block_size;

        do
        {
            memcpy(hle->alist_buffer + dmemo, hle->alist_buffer + dmemi, 0x20);
            bytes_left -= 0x20;

            dmemi += 0x20;
            dmemo += 0x20;

        } while(bytes_left > 0);

        --block_left;
    } while(block_left > 0);
}

void alist_interleave(struct hle_t* hle, uint16_t dmemo, uint16_t left, uint16_t right, uint16_t count)
{
    uint16_t       *dst  = (uint16_t*)(hle->alist_buffer + dmemo);
    const uint16_t *srcL = (uint16_t*)(hle->alist_buffer + left);
    const uint16_t *srcR = (uint16_t*)(hle->alist_buffer + right);

    count >>= 2;

    while(count != 0) {
        uint16_t l1 = *(srcL++);
        uint16_t l2 = *(srcL++);
        uint16_t r1 = *(srcR++);
        uint16_t r2 = *(srcR++);

#ifdef MSB_FIRST
        *(dst++) = l1;
        *(dst++) = r1;
        *(dst++) = l2;
        *(dst++) = r2;
#else
        *(dst++) = r2;
        *(dst++) = l2;
        *(dst++) = r1;
        *(dst++) = l1;
#endif
        --count;
    }
}


void alist_envmix_exp(
        struct hle_t* hle,
        bool init,
        bool aux,
        uint16_t dmem_dl, uint16_t dmem_dr,
        uint16_t dmem_wl, uint16_t dmem_wr,
        uint16_t dmemi, uint16_t count,
        int16_t dry, int16_t wet,
        const int16_t *vol,
        const int16_t *target,
        const int32_t *rate,
        uint32_t address)
{
    size_t n = (aux) ? 4 : 2;

    const int16_t* const in = (int16_t*)(hle->alist_buffer + dmemi);
    int16_t* const dl = (int16_t*)(hle->alist_buffer + dmem_dl);
    int16_t* const dr = (int16_t*)(hle->alist_buffer + dmem_dr);
    int16_t* const wl = (int16_t*)(hle->alist_buffer + dmem_wl);
    int16_t* const wr = (int16_t*)(hle->alist_buffer + dmem_wr);

    struct ramp_t ramps[2];
    int32_t exp_seq[2];
    int32_t exp_rates[2];

    uint32_t ptr = 0;
    int x, y;
    short save_buffer[40];

    memcpy((uint8_t *)save_buffer, (hle->dram + address), sizeof(save_buffer));
    if (init) {
        ramps[0].value  = (vol[0] << 16);
        ramps[1].value  = (vol[1] << 16);
        ramps[0].target = (target[0] << 16);
        ramps[1].target = (target[1] << 16);
        exp_rates[0]    = rate[0];
        exp_rates[1]    = rate[1];
        exp_seq[0]      = (vol[0] * rate[0]);
        exp_seq[1]      = (vol[1] * rate[1]);
    } else {
        wet             = *(int16_t *)(save_buffer +  0); /* 0-1 */
        dry             = *(int16_t *)(save_buffer +  2); /* 2-3 */
        ramps[0].target = *(int32_t *)(save_buffer +  4); /* 4-5 */
        ramps[1].target = *(int32_t *)(save_buffer +  6); /* 6-7 */
        exp_rates[0]    = *(int32_t *)(save_buffer +  8); /* 8-9 (save_buffer is a 16bit pointer) */
        exp_rates[1]    = *(int32_t *)(save_buffer + 10); /* 10-11 */
        exp_seq[0]      = *(int32_t *)(save_buffer + 12); /* 12-13 */
        exp_seq[1]      = *(int32_t *)(save_buffer + 14); /* 14-15 */
        ramps[0].value  = *(int32_t *)(save_buffer + 16); /* 12-13 */
        ramps[1].value  = *(int32_t *)(save_buffer + 18); /* 14-15 */
    }

    /* init which ensure ramp.step != 0 iff ramp.value == ramp.target */
    ramps[0].step = ramps[0].target - ramps[0].value;
    ramps[1].step = ramps[1].target - ramps[1].value;

    for (y = 0; y < count; y += 16) {

        if (ramps[0].step != 0)
        {
            exp_seq[0] = ((int64_t)exp_seq[0]*(int64_t)exp_rates[0]) >> 16;
            ramps[0].step = (exp_seq[0] - ramps[0].value) >> 3;
        }

        if (ramps[1].step != 0)
        {
            exp_seq[1] = ((int64_t)exp_seq[1]*(int64_t)exp_rates[1]) >> 16;
            ramps[1].step = (exp_seq[1] - ramps[1].value) >> 3;
        }

        for (x = 0; x < 8; ++x) {
            int16_t  gains[4];
            int16_t* buffers[4];
            int16_t l_vol = ramp_step(&ramps[0]);
            int16_t r_vol = ramp_step(&ramps[1]);

            buffers[0] = dl + (ptr^S);
            buffers[1] = dr + (ptr^S);
            buffers[2] = wl + (ptr^S);
            buffers[3] = wr + (ptr^S);

            gains[0] = clamp_s16((l_vol * dry + 0x4000) >> 15);
            gains[1] = clamp_s16((r_vol * dry + 0x4000) >> 15);
            gains[2] = clamp_s16((l_vol * wet + 0x4000) >> 15);
            gains[3] = clamp_s16((r_vol * wet + 0x4000) >> 15);

            alist_envmix_mix(n, buffers, gains, in[ptr^S]);
            ++ptr;
        }
    }

    *(int16_t *)(save_buffer +  0) = wet;               /* 0-1 */
    *(int16_t *)(save_buffer +  2) = dry;               /* 2-3 */
    *(int32_t *)(save_buffer +  4) = (int32_t)ramps[0].target;   /* 4-5 */
    *(int32_t *)(save_buffer +  6) = (int32_t)ramps[1].target;   /* 6-7 */
    *(int32_t *)(save_buffer +  8) = exp_rates[0];      /* 8-9 (save_buffer is a 16bit pointer) */
    *(int32_t *)(save_buffer + 10) = exp_rates[1];      /* 10-11 */
    *(int32_t *)(save_buffer + 12) = exp_seq[0];        /* 12-13 */
    *(int32_t *)(save_buffer + 14) = exp_seq[1];        /* 14-15 */
    *(int32_t *)(save_buffer + 16) = (int32_t)ramps[0].value;    /* 12-13 */
    *(int32_t *)(save_buffer + 18) = (int32_t)ramps[1].value;    /* 14-15 */
    memcpy(hle->dram + address, (uint8_t *)save_buffer, sizeof(save_buffer));
}

void alist_envmix_ge(
        struct hle_t* hle,
        bool init,
        bool aux,
        uint16_t dmem_dl, uint16_t dmem_dr,
        uint16_t dmem_wl, uint16_t dmem_wr,
        uint16_t dmemi, uint16_t count,
        int16_t dry, int16_t wet,
        const int16_t *vol,
        const int16_t *target,
        const int32_t *rate,
        uint32_t address)
{
    unsigned k;
    size_t n = (aux) ? 4 : 2;

    const int16_t* const in = (int16_t*)(hle->alist_buffer + dmemi);
    int16_t* const dl = (int16_t*)(hle->alist_buffer + dmem_dl);
    int16_t* const dr = (int16_t*)(hle->alist_buffer + dmem_dr);
    int16_t* const wl = (int16_t*)(hle->alist_buffer + dmem_wl);
    int16_t* const wr = (int16_t*)(hle->alist_buffer + dmem_wr);

    struct ramp_t ramps[2];
    short save_buffer[40];

    memcpy((uint8_t *)save_buffer, (hle->dram + address), 80);
    if (init) {
        ramps[0].value  = (vol[0] << 16);
        ramps[1].value  = (vol[1] << 16);
        ramps[0].target = (target[0] << 16);
        ramps[1].target = (target[1] << 16);
        ramps[0].step   = rate[0] / 8;
        ramps[1].step   = rate[1] / 8;
    } else {
        wet             = *(int16_t *)(save_buffer +  0);   /* 0-1 */
        dry             = *(int16_t *)(save_buffer +  2);   /* 2-3 */
        ramps[0].target = *(int32_t *)(save_buffer +  4);   /* 4-5 */
        ramps[1].target = *(int32_t *)(save_buffer +  6);   /* 6-7 */
        ramps[0].step   = *(int32_t *)(save_buffer +  8);   /* 8-9 (save_buffer is a 16bit pointer) */
        ramps[1].step   = *(int32_t *)(save_buffer + 10);   /* 10-11 */
        /*                *(int32_t *)(save_buffer + 12);*/ /* 12-13 */
        /*                *(int32_t *)(save_buffer + 14);*/ /* 14-15 */
        ramps[0].value  = *(int32_t *)(save_buffer + 16);   /* 12-13 */
        ramps[1].value  = *(int32_t *)(save_buffer + 18);   /* 14-15 */
    }

    count >>= 1;
    for (k = 0; k < count; ++k) {
        int16_t  gains[4];
        int16_t* buffers[4];
        int16_t l_vol = ramp_step(&ramps[0]);
        int16_t r_vol = ramp_step(&ramps[1]);

        buffers[0] = dl + (k^S);
        buffers[1] = dr + (k^S);
        buffers[2] = wl + (k^S);
        buffers[3] = wr + (k^S);

        gains[0] = clamp_s16((l_vol * dry + 0x4000) >> 15);
        gains[1] = clamp_s16((r_vol * dry + 0x4000) >> 15);
        gains[2] = clamp_s16((l_vol * wet + 0x4000) >> 15);
        gains[3] = clamp_s16((r_vol * wet + 0x4000) >> 15);

        alist_envmix_mix(n, buffers, gains, in[k^S]);
    }

    *(int16_t *)(save_buffer +  0) = wet;               /* 0-1 */
    *(int16_t *)(save_buffer +  2) = dry;               /* 2-3 */
    *(int32_t *)(save_buffer +  4) = (int32_t)ramps[0].target;   /* 4-5 */
    *(int32_t *)(save_buffer +  6) = (int32_t)ramps[1].target;   /* 6-7 */
    *(int32_t *)(save_buffer +  8) = (int32_t)ramps[0].step;     /* 8-9 (save_buffer is a 16bit pointer) */
    *(int32_t *)(save_buffer + 10) = (int32_t)ramps[1].step;     /* 10-11 */
    /**(int32_t *)(save_buffer + 12);*/                 /* 12-13 */
    /**(int32_t *)(save_buffer + 14);*/                 /* 14-15 */
    *(int32_t *)(save_buffer + 16) = (int32_t)ramps[0].value;    /* 12-13 */
    *(int32_t *)(save_buffer + 18) = (int32_t)ramps[1].value;    /* 14-15 */
    memcpy(hle->dram + address, (uint8_t *)save_buffer, 80);
}

void alist_envmix_lin(
        struct hle_t* hle,
        bool init,
        uint16_t dmem_dl, uint16_t dmem_dr,
        uint16_t dmem_wl, uint16_t dmem_wr,
        uint16_t dmemi, uint16_t count,
        int16_t dry, int16_t wet,
        const int16_t *vol,
        const int16_t *target,
        const int32_t *rate,
        uint32_t address)
{
    size_t k;
    struct ramp_t ramps[2];
    int16_t save_buffer[40];

    const int16_t * const in = (int16_t*)(hle->alist_buffer + dmemi);
    int16_t* const dl = (int16_t*)(hle->alist_buffer + dmem_dl);
    int16_t* const dr = (int16_t*)(hle->alist_buffer + dmem_dr);
    int16_t* const wl = (int16_t*)(hle->alist_buffer + dmem_wl);
    int16_t* const wr = (int16_t*)(hle->alist_buffer + dmem_wr);

    memcpy((uint8_t *)save_buffer, hle->dram + address, 80);
    if (init) {
        ramps[0].step   = rate[0] / 8;
        ramps[0].value  = (vol[0] << 16);
        ramps[0].target = (target[0] << 16);
        ramps[1].step   = rate[1] / 8;
        ramps[1].value  = (vol[1] << 16);
        ramps[1].target = (target[1] << 16);
    }
    else {
        wet             = *(int16_t *)(save_buffer +  0); /* 0-1 */
        dry             = *(int16_t *)(save_buffer +  2); /* 2-3 */
        ramps[0].target = *(int16_t *)(save_buffer +  4) << 16; /* 4-5 */
        ramps[1].target = *(int16_t *)(save_buffer +  6) << 16; /* 6-7 */
        ramps[0].step   = *(int32_t *)(save_buffer +  8); /* 8-9 (save_buffer is a 16bit pointer) */
        ramps[1].step   = *(int32_t *)(save_buffer + 10); /* 10-11 */
        ramps[0].value  = *(int32_t *)(save_buffer + 16); /* 16-17 */
        ramps[1].value  = *(int32_t *)(save_buffer + 18); /* 16-17 */
    }

    count >>= 1;
    for(k = 0; k < count; ++k) {
        int16_t  gains[4];
        int16_t* buffers[4];
        int16_t l_vol = ramp_step(&ramps[0]);
        int16_t r_vol = ramp_step(&ramps[1]);

        buffers[0] = dl + (k^S);
        buffers[1] = dr + (k^S);
        buffers[2] = wl + (k^S);
        buffers[3] = wr + (k^S);

        gains[0] = clamp_s16((l_vol * dry + 0x4000) >> 15);
        gains[1] = clamp_s16((r_vol * dry + 0x4000) >> 15);
        gains[2] = clamp_s16((l_vol * wet + 0x4000) >> 15);
        gains[3] = clamp_s16((r_vol * wet + 0x4000) >> 15);

        alist_envmix_mix(4, buffers, gains, in[k^S]);
    }

    *(int16_t *)(save_buffer +  0) = wet;            /* 0-1 */
    *(int16_t *)(save_buffer +  2) = dry;            /* 2-3 */
    *(int16_t *)(save_buffer +  4) = (int16_t)(ramps[0].target >> 16); /* 4-5 */
    *(int16_t *)(save_buffer +  6) = (int16_t)(ramps[1].target >> 16); /* 6-7 */
    *(int32_t *)(save_buffer +  8) = (int32_t)ramps[0].step;  /* 8-9 (save_buffer is a 16bit pointer) */
    *(int32_t *)(save_buffer + 10) = (int32_t)ramps[1].step;  /* 10-11 */
    *(int32_t *)(save_buffer + 16) = (int32_t)ramps[0].value; /* 16-17 */
    *(int32_t *)(save_buffer + 18) = (int32_t)ramps[1].value; /* 18-19 */
    memcpy(hle->dram + address, (uint8_t *)save_buffer, 80);
}

void alist_envmix_nead(
        struct hle_t* hle,
        bool swap_wet_LR,
        uint16_t dmem_dl,
        uint16_t dmem_dr,
        uint16_t dmem_wl,
        uint16_t dmem_wr,
        uint16_t dmemi,
        unsigned count,
        uint16_t *env_values,
        uint16_t *env_steps,
        const int16_t *xors)
{
    int16_t *in = (int16_t*)(hle->alist_buffer + dmemi);
    int16_t *dl = (int16_t*)(hle->alist_buffer + dmem_dl);
    int16_t *dr = (int16_t*)(hle->alist_buffer + dmem_dr);
    int16_t *wl = (int16_t*)(hle->alist_buffer + dmem_wl);
    int16_t *wr = (int16_t*)(hle->alist_buffer + dmem_wr);

    /* make sure count is a multiple of 8 */
    count = align(count, 8);

    if (swap_wet_LR)
        swap(&wl, &wr);

    while (count != 0) {
        size_t i;
        for(i = 0; i < 8; ++i) {
            int16_t l  = (((int32_t)in[i^S] * (uint32_t)env_values[0]) >> 16) ^ xors[0];
            int16_t r  = (((int32_t)in[i^S] * (uint32_t)env_values[1]) >> 16) ^ xors[1];
            int16_t l2 = (((int32_t)l * (uint32_t)env_values[2]) >> 16) ^ xors[2];
            int16_t r2 = (((int32_t)r * (uint32_t)env_values[2]) >> 16) ^ xors[3];

            dl[i^S] = clamp_s16(dl[i^S] + l);
            dr[i^S] = clamp_s16(dr[i^S] + r);
            wl[i^S] = clamp_s16(wl[i^S] + l2);
            wr[i^S] = clamp_s16(wr[i^S] + r2);
        }

        env_values[0] += env_steps[0];
        env_values[1] += env_steps[1];
        env_values[2] += env_steps[2];

        dl += 8;
        dr += 8;
        wl += 8;
        wr += 8;
        in += 8;
        count -= 8;
    }
}


void alist_mix(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count, int16_t gain)
{
    int16_t       *dst = (int16_t*)(hle->alist_buffer + dmemo);
    const int16_t *src = (int16_t*)(hle->alist_buffer + dmemi);

    count >>= 1;

    while(count != 0) {
        sample_mix(dst, *src, gain);

        ++dst;
        ++src;
        --count;
    }
}

void alist_multQ44(struct hle_t* hle, uint16_t dmem, uint16_t count, int8_t gain)
{
    int16_t *dst = (int16_t*)(hle->alist_buffer + dmem);

    count >>= 1;

    while(count != 0) {
        *dst = clamp_s16(*dst * gain >> 4);

        ++dst;
        --count;
    }
}

/* nead ADDMIXER (opcode 0x04). One known, deliberate inexactness: the
 * microcode handler opens with vaddc $v31,$v31,$v31 before its vadd
 * loop, so the first eight output samples pick up VCO carry bits
 * derived from doubling whatever the previous command left in $v31
 * (commonly the DMEM constant vector [0,1,2,0xffff,...], i.e. a +1 on
 * lane 3; data-dependent after FILTER). Reproducing it would require a
 * per-command shadow of $v31. No nead title in the validation corpus
 * (Majora's Mask, ~20k tasks) issues ADDMIXER at all, and the
 * divergence is at most +1 LSB on at most 8 samples, so the plain
 * saturating add below is kept. Verified against cxd4 with synthetic
 * probe alists; revisit with $v31 tracking if a real consumer shows
 * up in A/B testing. */
void alist_add(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    int16_t       *dst = (int16_t*)(hle->alist_buffer + dmemo);
    const int16_t *src = (int16_t*)(hle->alist_buffer + dmemi);

    count >>= 1;

    while(count != 0) {
        *dst = clamp_s16(*dst + *src);

        ++dst;
        ++src;
        --count;
    }
}

static void alist_resample_reset(struct hle_t* hle, uint16_t pos, uint32_t* pitch_accu)
{
    unsigned k;

    for(k = 0; k < 4; ++k)
        *sample(hle, pos + k) = 0;

    *pitch_accu = 0;
}

static void alist_resample_load(struct hle_t* hle, uint32_t address, uint16_t pos, uint32_t* pitch_accu)
{
    *sample(hle, pos + 0) = *dram_u16(hle, address + 0);
    *sample(hle, pos + 1) = *dram_u16(hle, address + 2);
    *sample(hle, pos + 2) = *dram_u16(hle, address + 4);
    *sample(hle, pos + 3) = *dram_u16(hle, address + 6);

    *pitch_accu = *dram_u16(hle, address + 8);
}

static void alist_resample_save(struct hle_t* hle, uint32_t address, uint16_t pos, uint32_t pitch_accu)
{
    *dram_u16(hle, address + 0) = *sample(hle, pos + 0);
    *dram_u16(hle, address + 2) = *sample(hle, pos + 1);
    *dram_u16(hle, address + 4) = *sample(hle, pos + 2);
    *dram_u16(hle, address + 6) = *sample(hle, pos + 3);

    *dram_u16(hle, address + 8) = pitch_accu;
}

void alist_resample(
        struct hle_t* hle,
        bool init,
        bool flag2,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint32_t pitch,     /* Q16.16 */
        uint32_t address)
{
    uint32_t pitch_accu;

    uint16_t ipos = dmemi >> 1;
    uint16_t opos = dmemo >> 1;
    count >>= 1;
    ipos -= 4;

    if (flag2)
        HleWarnMessage(hle->user_defined, "alist_resample: flag2 is not implemented");

    if (init)
        alist_resample_reset(hle, ipos, &pitch_accu);
    else
        alist_resample_load(hle, address, ipos, &pitch_accu);

    while (count != 0) {
        const int16_t* lut = RESAMPLE_LUT + ((pitch_accu & 0xfc00) >> 8);

        /* The microcode computes each tap with vmulf (independently
         * rounded and clamped Q15 product: (2*a*b + 0x8000) >> 16) and
         * combines them with a tree of saturating vadds:
         * sat(sat(q0+q1) + sat(q2+q3)). Summing raw products and
         * shifting once is up to a few LSBs off, which is audible as a
         * DC bias on near-silent material and breaks bit-exactness
         * against LLE. */
        int32_t q0 = (int32_t)(((int64_t)2 * *sample(hle, ipos    ) * lut[0] + 0x8000) >> 16);
        int32_t q1 = (int32_t)(((int64_t)2 * *sample(hle, ipos + 1) * lut[1] + 0x8000) >> 16);
        int32_t q2 = (int32_t)(((int64_t)2 * *sample(hle, ipos + 2) * lut[2] + 0x8000) >> 16);
        int32_t q3 = (int32_t)(((int64_t)2 * *sample(hle, ipos + 3) * lut[3] + 0x8000) >> 16);

        q0 = clamp_s16(q0);
        q1 = clamp_s16(q1);
        q2 = clamp_s16(q2);
        q3 = clamp_s16(q3);

        *sample(hle, opos++) = clamp_s16(clamp_s16(q0 + q1) + clamp_s16(q2 + q3));

        pitch_accu += pitch;
        ipos += (pitch_accu >> 16);
        pitch_accu &= 0xffff;
        --count;
    }

    alist_resample_save(hle, address, ipos, pitch_accu);
}

void alist_resample_zoh(
        struct hle_t* hle,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint32_t pitch,
        uint32_t pitch_accu)
{
    uint16_t ipos = dmemi >> 1;
    uint16_t opos = dmemo >> 1;
    count >>= 1;

    while(count != 0) {

        *sample(hle, opos++) = *sample(hle, ipos);

        pitch_accu += pitch;
        ipos += (pitch_accu >> 16);
        pitch_accu &= 0xffff;
        --count;
    }
}

typedef unsigned int (*adpcm_predict_frame_t)(struct hle_t* hle,
                                              int16_t* dst, uint16_t dmemi, unsigned char scale);

static unsigned int adpcm_predict_frame_4bits(struct hle_t* hle,
                                              int16_t* dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    unsigned int rshift = (scale < 12) ? 12 - scale : 0;

    for(i = 0; i < 8; ++i) {
        uint8_t byte = *alist_u8(hle, dmemi++);

        *(dst++) = adpcm_predict_sample(byte, 0xf0,  8, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x0f, 12, rshift);
    }

    return 8;
}

static unsigned int adpcm_predict_frame_2bits(struct hle_t* hle,
                                              int16_t* dst, uint16_t dmemi, unsigned char scale)
{
    unsigned int i;
    unsigned int rshift = (scale < 14) ? 14 - scale : 0;

    for(i = 0; i < 4; ++i) {
        uint8_t byte = *alist_u8(hle, dmemi++);

        *(dst++) = adpcm_predict_sample(byte, 0xc0,  8, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x30, 10, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x0c, 12, rshift);
        *(dst++) = adpcm_predict_sample(byte, 0x03, 14, rshift);
    }

    return 4;
}

void alist_adpcm(
        struct hle_t* hle,
        bool init,
        bool loop,
        bool two_bit_per_sample,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        const int16_t* codebook,
        uint32_t loop_address,
        uint32_t last_frame_address)
{
    int16_t last_frame[16];
    size_t i;

    adpcm_predict_frame_t predict_frame = (two_bit_per_sample)
        ? adpcm_predict_frame_2bits
        : adpcm_predict_frame_4bits;

    assert((count & 0x1f) == 0);

    if (init)
        memset(last_frame, 0, 16*sizeof(last_frame[0]));
    else
        dram_load_u16(hle, (uint16_t*)last_frame, (loop) ? loop_address : last_frame_address, 16);

    for(i = 0; i < 16; ++i, dmemo += 2)
        *alist_s16(hle, dmemo) = last_frame[i];

    while (count != 0) {
        int16_t frame[16];
        uint8_t code = *alist_u8(hle, dmemi++);
        unsigned char scale = (code & 0xf0) >> 4;
        const int16_t* const cb_entry = codebook + ((code & 0xf) << 4);

        dmemi += predict_frame(hle, frame, dmemi, scale);

        adpcm_compute_residuals(last_frame    , frame    , cb_entry, last_frame + 14, 8);
        adpcm_compute_residuals(last_frame + 8, frame + 8, cb_entry, last_frame + 6 , 8);

        for(i = 0; i < 16; ++i, dmemo += 2)
            *alist_s16(hle, dmemo) = last_frame[i];

        count -= 32;
    }

    dram_store_u16(hle, (uint16_t*)last_frame, last_frame_address, 16);
}


/* Bit-exact implementation of the aspMain S8DEC command (opcode 0x17,
 * A_S8DEC, issued by the OoT/MM-era audio driver for CODEC_S8
 * samples), derived from the Zelda MM aspMain disassembly and
 * validated against cxd4 with synthetic probe alists. The command
 * takes its in/out/count from the previous SETBUFF and decodes 8-bit
 * samples to 16-bit (s8 << 8, no rounding or gain). The first 32
 * bytes of the output buffer act as a state header, mirroring ADPCM:
 * zeroed when flags bit 0 (init) is set, otherwise DMAed in from the
 * state address (or from the SETLOOP address when flags bit 1 is
 * set). Decoding starts after the header, processing ceil(count/0x20)
 * output blocks, and the last 32 output bytes (the header itself when
 * count is zero) are stored back to the state address. */
void alist_s8dec(struct hle_t* hle, bool init, bool loop, uint16_t dmemi, uint16_t dmemo, uint16_t count, uint32_t loop_address, uint32_t address)
{
    unsigned int i;
    int32_t remaining;

    for (i = 0; i < 0x20; i += 2)
        *alist_s16(hle, dmemo + (uint16_t)i) = 0;

    if (!init) {
        uint32_t src = loop ? loop_address : address;
        for (i = 0; i < 0x20; i += 2)
            *alist_s16(hle, dmemo + (uint16_t)i) = *dram_u16(hle, src + i);
    }

    dmemo += 0x20;

    remaining = (int32_t)count;
    if (remaining != 0) {
        do {
            for (i = 0; i < 0x10; ++i) {
                int8_t b = (int8_t)*alist_u8(hle, dmemi + (uint16_t)i);
                *alist_s16(hle, dmemo + (uint16_t)(i << 1)) = (int16_t)((int16_t)b << 8);
            }
            dmemi += 0x10;
            dmemo += 0x20;
            remaining -= 0x20;
        } while (remaining > 0);
    }

    for (i = 0; i < 0x20; i += 2)
        *dram_u16(hle, address + i) = (uint16_t)*alist_s16(hle, dmemo - 0x20 + (uint16_t)i);
}

/* Bit-exact reimplementation of the aspMain MIXER command (opcode 0x0c),
 * validated against cxd4. Each lane computes
 *   out = clamp_s16((0x8000 + 2*dst*0x7fff + 2*src*gain) >> 16)
 * (vmulf dst by the 0x7fff constant, vmacf src by gain), i.e. the
 * destination term decays by 1/32768 and the result is rounded, unlike
 * the legacy dst + ((src*gain) >> 15). Processing follows the ucode's
 * software-pipelined 0x20-byte block schedule: the next source block is
 * fetched before the current results are stored and the next
 * destination block is fetched between the two result stores. This
 * ordering is observable when source and destination overlap, which
 * games rely on for delay-style mixes. At least one block is always
 * processed and trailing prefetches can read past the surface; both
 * match the microcode. */
void alist_mix_nead(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count, int16_t gain)
{
    int16_t d0[8], d1[8], s0[8], s1[8], r0[8], r1[8];
    int32_t remaining = (int32_t)count;
    unsigned int i;

    for (i = 0; i < 8; ++i) {
        d0[i] = *alist_s16(hle, dmemo + (uint16_t)(i << 1));
        s0[i] = *alist_s16(hle, dmemi + (uint16_t)(i << 1));
        d1[i] = *alist_s16(hle, dmemo + 0x10 + (uint16_t)(i << 1));
        s1[i] = *alist_s16(hle, dmemi + 0x10 + (uint16_t)(i << 1));
    }

    do {
        for (i = 0; i < 8; ++i) {
            int64_t acc;
            acc = 0x8000 + 2 * (int64_t)((int32_t)d0[i] * 0x7fff)
                         + 2 * (int64_t)((int32_t)s0[i] * (int32_t)gain);
            r0[i] = clamp_s16((int_fast32_t)(acc >> 16));
            acc = 0x8000 + 2 * (int64_t)((int32_t)d1[i] * 0x7fff)
                         + 2 * (int64_t)((int32_t)s1[i] * (int32_t)gain);
            r1[i] = clamp_s16((int_fast32_t)(acc >> 16));
        }

        remaining -= 0x20;
        dmemi += 0x20;

        for (i = 0; i < 8; ++i)
            s0[i] = *alist_s16(hle, dmemi + (uint16_t)(i << 1));
        for (i = 0; i < 8; ++i)
            *alist_s16(hle, dmemo + (uint16_t)(i << 1)) = r0[i];
        for (i = 0; i < 8; ++i)
            d0[i] = *alist_s16(hle, dmemo + 0x20 + (uint16_t)(i << 1));
        for (i = 0; i < 8; ++i)
            *alist_s16(hle, dmemo + 0x10 + (uint16_t)(i << 1)) = r1[i];
        for (i = 0; i < 8; ++i)
            s1[i] = *alist_s16(hle, dmemi + 0x10 + (uint16_t)(i << 1));

        dmemo += 0x20;

        for (i = 0; i < 8; ++i)
            d1[i] = *alist_s16(hle, dmemo + 0x10 + (uint16_t)(i << 1));
    } while (remaining > 0);
}

/* Bit-exact reimplementation of the aspMain FILTER command (opcode 0x07),
 * derived from the F3DZEX-era audio microcode (Zelda MM aspMain) and
 * validated word-for-word against cxd4. The previous implementation
 * descended from ancient plugin folklore and had five behavioral
 * divergences, including a write-back that corrupted the caller's
 * coefficient table in RDRAM. See FILTER() in alist_nead.c for the
 * setup/run command split.
 *
 * Semantics (run, flags 0/1):
 *   state in DRAM at `address`: 8 previous input samples (+0x00) and the
 *   previous working table (+0x10). flags==1 (init) uses zeros instead of
 *   loading the state. The working table is the rounded average of the
 *   16-byte table snapshot taken at setup time and the previous working
 *   table (vmacf with a 0x4000 scalar: acc = 0x8000 + (a + b) << 15).
 *   Each output is an 8-tap FIR over the window of the last 15 samples:
 *     out[n] = clamp_s16((0x8000 + 2 * sum_k t[k] * s[n + 7 - k]) >> 16)
 *   with s = [prev[1..7], cur[0..7]], processed in-place on the DMEM
 *   buffer in 16-byte blocks (at least one block, count rounded up).
 *   Afterwards the last raw input block and the working table are stored
 *   back to DRAM at `address` (32 bytes). The setup-address table is
 *   never written. */
void alist_filter(
        struct hle_t* hle,
        bool init,
        uint16_t dmem,
        uint16_t count,
        uint32_t address,
        const int16_t* table)
{
    int16_t state_samples[8];
    int16_t state_table[8];
    int16_t t[8];
    int16_t win[15];
    int16_t cur[8];
    int16_t out[8];
    unsigned int i;
    int32_t remaining;

    if (init) {
        memset(state_samples, 0, sizeof(state_samples));
        memset(state_table,   0, sizeof(state_table));
    }
    else {
        dram_load_u16(hle, (uint16_t*)state_samples, address,        8);
        dram_load_u16(hle, (uint16_t*)state_table,   address + 0x10, 8);
    }

    for (i = 0; i < 8; ++i)
        t[i] = (int16_t)(((int32_t)table[i] + (int32_t)state_table[i] + 1) >> 1);

    for (i = 0; i < 7; ++i)
        win[i] = state_samples[i + 1];

    remaining = (int32_t)count;
    do {
        for (i = 0; i < 8; ++i) {
            cur[i] = *alist_s16(hle, dmem + (uint16_t)(i << 1));
            win[7 + i] = cur[i];
        }

        for (i = 0; i < 8; ++i) {
            int64_t acc = 0x8000;
            unsigned int k;
            for (k = 0; k < 8; ++k)
                acc += 2 * (int64_t)((int32_t)t[k] * (int32_t)win[i + 7 - k]);
            acc >>= 16;
            if (acc >  32767) acc =  32767;
            if (acc < -32768) acc = -32768;
            out[i] = (int16_t)acc;
        }

        for (i = 0; i < 8; ++i)
            *alist_s16(hle, dmem + (uint16_t)(i << 1)) = out[i];
        dmem += 16;
        remaining -= 16;

        for (i = 0; i < 7; ++i)
            win[i] = cur[i + 1];
    } while (remaining > 0);

    dram_store_u16(hle, (uint16_t*)cur, address,        8);
    dram_store_u16(hle, (uint16_t*)t,   address + 0x10, 8);
}

void alist_polef(
        struct hle_t* hle,
        bool init,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint16_t gain,
        int16_t* table,
        uint32_t address)
{
    int16_t *dst = (int16_t*)(hle->alist_buffer + dmemo);

    const int16_t* const h1 = table;
          int16_t* const h2 = table + 8;

    unsigned i;
    int16_t l1, l2;
    int16_t h2_before[8];

    count = align(count, 16);

    if (init) {
        l1 = 0;
        l2 = 0;
    }
    else {
        l1 = *dram_u16(hle, address + 4);
        l2 = *dram_u16(hle, address + 6);
    }

    for(i = 0; i < 8; ++i) {
        h2_before[i] = h2[i];
        h2[i] = (((int32_t)h2[i] * gain) >> 14);
    }

    do
    {
        int16_t frame[8];

        for(i = 0; i < 8; ++i, dmemi += 2)
            frame[i] = *alist_s16(hle, dmemi);

        for(i = 0; i < 8; ++i) {
            int32_t accu = frame[i] * gain;
            accu += h1[i]*l1 + h2_before[i]*l2 + rdot(i, h2, frame);
            dst[i^S] = clamp_s16(accu >> 14);
        }

        l1 = dst[6^S];
        l2 = dst[7^S];

        dst += 8;
        count -= 16;
    } while (count != 0);

    dram_store_u32(hle, (uint32_t*)(dst - 4), address, 2);
}

void alist_iirf(
        struct hle_t* hle,
        bool init,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        int16_t* table,
        uint32_t address)
{
    int16_t *dst = (int16_t*)(hle->alist_buffer + dmemo);
    int32_t i, prev;
    int16_t frame[8];
    int16_t ibuf[4];
    uint16_t index = 7;


    count = align(count, 16);

    if(init)
    {
        for(i = 0; i < 8; ++i)
            frame[i] = 0;
        ibuf[1] = 0;
        ibuf[2] = 0;
    }
    else
    {
        frame[6] = *dram_u16(hle, address + 4);
        frame[7] = *dram_u16(hle, address + 6);
        ibuf[1] = (int16_t)*dram_u16(hle, address + 8);
        ibuf[2] = (int16_t)*dram_u16(hle, address + 10);
    }

    prev = vmulf(table[9], frame[6]) * 2;
    do
    {
        for(i = 0; i < 8; ++i)
        {
            int32_t accu;
            ibuf[index&3] = *alist_s16(hle, dmemi);

            accu = prev + vmulf(table[0], ibuf[index&3]) + vmulf(table[1], ibuf[(index-1)&3]) + vmulf(table[0], ibuf[(index-2)&3]);
            accu += vmulf(table[8], frame[index]) * 2;
            prev = vmulf(table[9], frame[index]) * 2;
            dst[i^S] = frame[i] = accu;

            index=(index+1)&7;
            dmemi += 2;
        }
        dst += 8;
        count -= 0x10;
    } while (count > 0);

    dram_store_u16(hle, (uint16_t*)&frame[6], address + 4, 2);
    dram_store_u16(hle, (uint16_t*)&ibuf[(index-2)&3], address+8, 1);
    dram_store_u16(hle, (uint16_t*)&ibuf[(index-1)&3], address+10, 1);
}

/* Perform a clamped gain, then attenuate it back by an amount */
void alist_overload(struct hle_t* hle, uint16_t dmem, int16_t count, int16_t gain, uint16_t attenuation)
{
    int16_t accu;
    int16_t * sample = (int16_t*)(hle->alist_buffer + dmem);

    while (count != 0)
    {
        accu = clamp_s16(*sample * gain);
        *sample = (accu * attenuation) >> 16;
        sample++;
        count --;
    }
}

/* OoT aspMain command 0x03 ("AudioSynth_UnkCmd3" in the OoT
 * decompilation, emitted after the final resample for samples with
 * bookOffset == 2). Processes 32-byte chunks: within each chunk the
 * first 8 samples A (signed) and the next 8 samples B (reinterpreted
 * unsigned) are multiplied pairwise by a vmudn/vmadn pair, giving
 * out[0..7]  = sat(acc = 2*B*A)  (RSP unsigned-low clamp on the 48-bit
 *                                 accumulator: hi > 0x7fff -> 0xffff,
 *                                 hi < -0x8000 -> 0x0000, else acc lo)
 * out[8..15] = lo16(B*A)         (single mul: hi is always within
 *                                 [-0x8000,0x7fff], so never clamped).
 * The ucode loop is do-while on (count -= 0x20) > 0, so count <= 0x20
 * (including 0) still processes exactly one chunk. */
void alist_unkcmd3(struct hle_t* hle, uint16_t dmemo, uint16_t dmemi, uint16_t count)
{
    int32_t n = (int32_t)count;

    do
    {
        unsigned i;
        int32_t  a[8];
        uint32_t b[8];

        for (i = 0; i < 8; ++i)
        {
            a[i] = *alist_s16(hle, (uint16_t)(dmemi + (i << 1)));
            b[i] = (uint16_t)*alist_s16(hle, (uint16_t)(dmemi + 0x10 + (i << 1)));
        }

        for (i = 0; i < 8; ++i)
        {
            int64_t acc = (int64_t)b[i] * (int64_t)a[i];
            int32_t hi;

            *alist_s16(hle, (uint16_t)(dmemo + 0x10 + (i << 1))) = (int16_t)(acc & 0xffff);

            acc += (int64_t)b[i] * (int64_t)a[i];
            hi = (int32_t)(acc >> 16);
            *alist_s16(hle, (uint16_t)(dmemo + (i << 1))) =
                (hi > 32767) ? (int16_t)0xffff :
                (hi < -32768) ? (int16_t)0x0000 : (int16_t)(acc & 0xffff);
        }
        dmemi += 0x20;
        dmemo += 0x20;
        n -= 0x20;
    } while (n > 0);
}
