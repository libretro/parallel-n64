/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_naudio.c                                  *
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

#include <boolean.h>
#include <stdint.h>

#include "alist.h"
#include "common.h"
#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"
#include "ucodes.h"

enum { NAUDIO_COUNT = 0x170 }; /* ie 184 samples */
enum {
    NAUDIO_MAIN      = 0x4f0,
    NAUDIO_MAIN2     = 0x660,
    NAUDIO_DRY_LEFT  = 0x9d0,
    NAUDIO_DRY_RIGHT = 0xb40,
    NAUDIO_WET_LEFT  = 0xcb0,
    NAUDIO_WET_RIGHT = 0xe20
};


/* audio commands definition */
/* The naudio buffer space (0x4f0..) sits inside the ucode's resident
 * data section, so at task start the buffers hold ucode data, not
 * zeros; anything a list saves before writing (and the mp3/cbfd
 * builds' tail region does get shipped) carries those bytes.  Seed
 * the shadow from the task's data section. */
/* the mp3/cbfd builds place the whole buffer space 0x10 bytes higher */
static uint16_t naudio_shift = 0;

static int16_t naudio_clamp_s16(int_fast32_t x)
{
    x = (x < INT16_MIN) ? INT16_MIN : x;
    x = (x > INT16_MAX) ? INT16_MAX : x;
    return (int16_t)x;
}

static void naudio_seed(struct hle_t* hle)
{
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t data_size  = *dmem_u32(hle, TASK_UCODE_DATA_SIZE);
    unsigned k;
    if (data_size > 0x800) data_size = 0x800;
    for (k = 0; k + 1 < data_size; k += 2)
        *(int16_t*)(hle->alist_buffer + ((k ^ S16) & 0xfff)) =
            (int16_t)*dram_u16(hle, ucode_data + k);
}

static void UNKNOWN(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t acmd = (w1 >> 24);

    HleWarnMessage(hle->user_defined,
                   "Unknown audio command %d: %08x %08x",
                   acmd, w1, w2);
}


static void SPNOOP(struct hle_t* UNUSED(hle), uint32_t UNUSED(w1), uint32_t UNUSED(w2))
{
}

static void NAUDIO_0000(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    /* ??? */
    UNKNOWN(hle, w1, w2);
}

static void NAUDIO_02B0(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    /* emulate code at 0x12b0 (inside SETVOL), because PC always execute in IMEM */
    hle->alist_naudio.rate[1] &= ~0xffff;
    hle->alist_naudio.rate[1] |= (w2 & 0xffff);
}

static void NAUDIO_14(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    /* Transcribed from the Conker/JFG handler (IMEM 0x934).  The
     * command's low halfword is the whole gain, and the init flag is
     * bit 14 of it (the handler tests bit 16 of w1 << 2).  The
     * coefficient table is the codebook image in DMEM at 0x400
     * (shifted builds): the second eight coefficients are scaled in
     * place by (gain << 2) on every call - evolving state - and feed
     * the in-taps, while the batch-linking feedback tap uses the
     * unscaled values it latched before the scaling.  With the first
     * two coefficients zero the handler runs this filter; otherwise
     * it takes the pole-filter path. */
    int16_t  gain        = (int16_t)(uint16_t)w1;
    bool     init        = (w1 >> 14) & 0x1;
    uint8_t  select_main = (w2 >> 24);
    uint32_t address     = (w2 & 0xffffff);

    uint16_t dmem = (select_main == 0)
        ? (uint16_t)(NAUDIO_MAIN + naudio_shift)
        : (uint16_t)(NAUDIO_MAIN2 + naudio_shift);
    uint16_t tbl = (uint16_t)(0x3f0 + naudio_shift);
    int16_t rawB[8], scaledB[8], t0, t1;
    int16_t prev;
    unsigned b, L, j2;

    t0 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x00) ^ S16) & 0xfff));
    t1 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x02) ^ S16) & 0xfff));

    if (t0 == 0 && t1 == 0) {
        int16_t win[8];

        for (j2 = 0; j2 < 8; ++j2)
            rawB[j2] = *(int16_t*)(hle->alist_buffer +
                                   (((tbl + 0x10 + 2*j2) ^ S16) & 0xfff));
        for (j2 = 0; j2 < 8; ++j2) {
            scaledB[j2] = (int16_t)(((int32_t)rawB[j2] *
                                     (uint16_t)(w1 << 2)) >> 16);
            *(int16_t*)(hle->alist_buffer +
                        (((tbl + 0x10 + 2*j2) ^ S16) & 0xfff)) = scaledB[j2];
        }

        prev = init ? 0 : (int16_t)*dram_u16(hle, address + 6);

        for (b = 0; b < 0x170 / 0x10; ++b) {
            int16_t *blk = (int16_t*)(hle->alist_buffer + dmem + 0x10*b);
            for (L = 0; L < 8; ++L)
                win[L] = blk[L^S];
            for (L = 0; L < 8; ++L) {
                int64_t acc = (int32_t)rawB[L] * prev
                            + (int32_t)gain * win[L];
                for (j2 = 0; j2 < L; ++j2)
                    acc += (int32_t)scaledB[L-1-j2] * win[j2];
                /* the 48-bit accumulator wraps: nine s16 products can
                 * exceed 32 bits, and the recombination reads only
                 * acc[47:16], i.e. the sum truncated to 32 bits */
                blk[L^S] = naudio_clamp_s16((int32_t)acc >> 14);
            }
            prev = blk[7^S];
        }

        /* state: the last four output samples */
        for (j2 = 0; j2 < 4; ++j2)
            *dram_u16(hle, address + 2*j2) =
                (uint16_t)*(int16_t*)(hle->alist_buffer +
                    (((dmem + 0x170 - 8 + 2*j2) ^ S16) & 0xfff));
    }
    else
    {
        /* with either of the first two coefficients non-zero the
         * handler bails before touching the buffers or scaling the
         * table; the only side effect is one zero halfword written
         * into the state block */
        *dram_u16(hle, address + 4) = 0;
    }
}

static void SETVOL(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t flags = (w1 >> 16);

    if (flags & A_VOL) {
        if (flags & A_LEFT) {
            hle->alist_naudio.vol[0] = w1;
            hle->alist_naudio.dry    = (w2 >> 16);
            hle->alist_naudio.wet    = w2;
        }
        else { /* A_RIGHT */
            hle->alist_naudio.target[1] = w1;
            hle->alist_naudio.rate[1]   = w2;
        }
    }
    else { /* A_RATE */
        hle->alist_naudio.target[0] = w1;
        hle->alist_naudio.rate[0]   = w2;
    }
}

static void envmixer(struct hle_t* hle, uint32_t w1, uint32_t w2, enum alist_envmix_input input_mode)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = (w2 & 0xffffff);

    hle->alist_naudio.vol[1] = w1;

    alist_envmix_lin(
            hle,
            flags & A_INIT,
            (uint16_t)(NAUDIO_DRY_LEFT + naudio_shift),
            (uint16_t)(NAUDIO_DRY_RIGHT + naudio_shift),
            (uint16_t)(NAUDIO_WET_LEFT + naudio_shift),
            (uint16_t)(NAUDIO_WET_RIGHT + naudio_shift),
            (uint16_t)(NAUDIO_MAIN + naudio_shift),
            NAUDIO_COUNT,
            hle->alist_naudio.dry,
            hle->alist_naudio.wet,
            hle->alist_naudio.vol,
            hle->alist_naudio.target,
            hle->alist_naudio.rate,
            address,
            input_mode);
}

static void ENVMIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    envmixer(hle, w1, w2, ALIST_ENVMIX_IN_VXOR);
}

/* The original revision of the microcode (plain naudio, Banjo-Kazooie)
 * has no input phase feature at all. */
static void ENVMIXER_RAW(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    envmixer(hle, w1, w2, ALIST_ENVMIX_IN_RAW);
}

/* The Conker revision of the microcode routes the ENVMIXER input samples
 * through vmulf instead of the vxor phase inversion. */
static void ENVMIXER_CBFD(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    envmixer(hle, w1, w2, ALIST_ENVMIX_IN_VMULF);
}

static void CLEARBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = w1 + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t count = w2 & 0xfff;

    alist_clear(hle, dmem, count);
}

static void MIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int16_t  gain  = w1;
    uint16_t dmemi = (w2 >> 16) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t dmemo = w2 + (uint16_t)(NAUDIO_MAIN + naudio_shift);

    alist_mix(hle, dmemo, dmemi, NAUDIO_COUNT, gain);
}

static void LOADBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint32_t address = (w2 & 0xffffff);

    alist_load(hle, dmem, address, count);
}

static void SAVEBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint32_t address = (w2 & 0xffffff);

    alist_save(hle, dmem, address, count);
}

static void LOADADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = w1;
    uint32_t address = (w2 & 0xffffff);

    {  size_t entries = count >> 1;
        const size_t cap = sizeof(hle->alist_naudio.table)/sizeof(hle->alist_naudio.table[0]);
        if (entries > cap) entries = cap;   /* count is unbounded alist data */
        dram_load_u16(hle, (uint16_t*)hle->alist_naudio.table, address, entries);
    }

    {
        /* the DMA lands the raw book at DMEM 0x3f0 (0x400 on the
         * shifted builds); the decoder indexes coefficients from
         * there, so a predictor beyond the loaded book reads whatever
         * the buffer holds - mirror the bytes so the shadow agrees */
        uint16_t dmem = (uint16_t)(0x3f0 + naudio_shift);
        unsigned k;
        for (k = 0; k + 1 < count && dmem + k + 1 < 0x1000; k += 2)
            *(int16_t*)(hle->alist_buffer + (((dmem + k) ^ S16) & 0xfff)) =
                (int16_t)*dram_u16(hle, address + k);
    }
}

static void DMEMMOVE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmemi = w1 + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t dmemo = (w2 >> 16) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t count = w2;

    /* 0x10 bytes per iteration, counter tested after */
    alist_move(hle, dmemo, dmemi, (count + 0xf) & ~0xf);
}

static void SETLOOP(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    hle->alist_naudio.loop = (w2 & 0xffffff);
}

static void ADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint32_t address = (w1 & 0xffffff);
    uint8_t  flags   = (w2 >> 28);
    uint16_t count   = (w2 >> 16) & 0xfff;
    uint16_t dmemi   = ((w2 >> 12) & 0xf) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t dmemo   = (w2 & 0xfff) + (uint16_t)(NAUDIO_MAIN + naudio_shift);

    {
        /* coefficients come from the book image in DMEM at 0x3f0
         * (0x400 shifted); a frame's predictor can reach past the
         * loaded book into the sample buffers, and the decoder reads
         * whatever is there */
        int16_t table[16 * 16];
        uint16_t base = (uint16_t)(0x3f0 + naudio_shift);
        unsigned k;
        for (k = 0; k < 16 * 16; ++k)
            table[k] = *(int16_t*)(hle->alist_buffer +
                                   (((base + 2*k) ^ S16) & 0xfff));

        alist_adpcm(
                hle,
                flags & A_INIT,
                flags & A_LOOP,
                false,          /* unsupported by this ucode */
                dmemo,
                dmemi,
                (count + 0x1f) & ~0x1f,
                table,
                hle->alist_naudio.loop,
                address);
    }
}

static void RESAMPLE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint32_t address = (w1 & 0xffffff);
    uint8_t  flags   = (w2 >> 30);
    uint16_t pitch   = (w2 >> 14);
    uint16_t dmemi   = ((w2 >> 2) & 0xfff) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    uint16_t dmemo   = (w2 & 0x3) ? (uint16_t)(NAUDIO_MAIN2 + naudio_shift) : (uint16_t)(NAUDIO_MAIN + naudio_shift);

    /* the handler branches to the fresh-state path whenever the flag
     * field is non-zero, not just on bit 0 */
    alist_resample_naudio(
            hle,
            flags != 0,
            dmemo,
            dmemi,
            NAUDIO_COUNT,
            pitch << 1,
            address);
}

static void INTERLEAVE(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t UNUSED(w2))
{
    alist_interleave(hle, (uint16_t)(NAUDIO_MAIN + naudio_shift), (uint16_t)(NAUDIO_DRY_LEFT + naudio_shift), (uint16_t)(NAUDIO_DRY_RIGHT + naudio_shift), NAUDIO_COUNT);
}

static void MP3ADDY(struct hle_t* UNUSED(hle), uint32_t UNUSED(w1), uint32_t UNUSED(w2))
{
}

static void MP3(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    unsigned index = (w1 & 0x1e);
    uint32_t address = (w2 & 0xffffff);

    mp3_task(hle, index, address);
}

static void OVERLOAD(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    /* Overload distortion effect for Conker's Bad Fur Day */
    uint16_t dmem = (w1 & 0xfff) + (uint16_t)(NAUDIO_MAIN + naudio_shift);
    int16_t gain = (int16_t)(uint16_t)w2;
    uint16_t attenuation = w2 >> 16;

    alist_overload(hle, dmem, NAUDIO_COUNT, gain, attenuation);
}

/* global functions */
void alist_process_naudio(struct hle_t* hle)
{
    naudio_shift = 0;
    naudio_seed(hle);
    static const acmd_callback_t ABI[0x10] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER_RAW,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       NAUDIO_0000,
        NAUDIO_0000,    SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
    };

    alist_process(hle, ABI, 0x10);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_naudio_bk(struct hle_t* hle)
{
    naudio_shift = 0;
    naudio_seed(hle);
    /* Banjo-Kazooie's audio library emits A_POLEF at opcode 0x0e, but
     * this ucode does not implement a pole filter: its dispatch table
     * sends the command to 0x2b0, the middle of the SETVOL handler, past
     * the instruction that derives $v0 from the command words. The code
     * there stores $v0 and w2 as the right-channel rate high/low halves,
     * so the effective rate is built from register residue: $v0 still
     * holds the DRAM address of the preceding LOADADPCM (the library
     * always pairs A_POLEF with one), unless the 0x140-byte command
     * chunk was refilled in between, in which case it holds the alist
     * read pointer. The custom walk below models that residue; the
     * NAUDIO_02B0 table entry is bypassed for this ucode. */
    static const acmd_callback_t ABI[0x10] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER_RAW,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       NAUDIO_0000,
        NAUDIO_0000,    SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
    };

    {
        uint32_t w1, w2;
        unsigned int acmd;
        long idx = 0;
        uint32_t dptr = *dmem_u32(hle, TASK_DATA_PTR);
        const uint32_t *alist = dram_u32(hle, dptr);
        const uint32_t *const alist_end = alist + (*dmem_u32(hle, TASK_DATA_SIZE) >> 2);
        uint32_t v0 = dptr;

        while (alist_end - alist >= 2) {
            if (idx != 0 && (idx % 40) == 0)
                v0 = dptr + 8 * idx;

            w1 = *(alist++);
            w2 = *(alist++);
            acmd = (w1 >> 24) & 0x7f;

            if (acmd == 0x0e) {
                hle->alist_naudio.rate[1] = (int32_t)((v0 << 16) | (w2 & 0xffff));
            } else if (acmd < 0x10) {
                (*ABI[acmd])(hle, w1, w2);
                if (acmd == 0x0b)
                    v0 = w2 & 0xffffff;
            }
            ++idx;
        }
    }
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_naudio_dk(struct hle_t* hle)
{
    naudio_shift = 0;
    naudio_seed(hle);
    /* Differs from alist_process_naudio only at opcodes 7 and 8, which
     * dispatch to MIXER here instead of the unknown-command handler. */
    static const acmd_callback_t ABI[0x10] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      ENVMIXER,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       MIXER,
        MIXER,          SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     NAUDIO_02B0,    SETLOOP
    };

    alist_process(hle, ABI, 0x10);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_naudio_mp3(struct hle_t* hle)
{
    naudio_shift = 0x10;
    naudio_seed(hle);
    static const acmd_callback_t ABI[0x10] = {
        OVERLOAD,       ADPCM,          CLEARBUFF,      ENVMIXER,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       MP3,
        MP3ADDY,        SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     NAUDIO_14,      SETLOOP
    };

    alist_process(hle, ABI, 0x10);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_naudio_cbfd(struct hle_t* hle)
{
    naudio_shift = 0x10;
    naudio_seed(hle);
    /* What differs from alist_process_naudio_mp3?
     *
     * JoshW: It appears that despite being a newer game, CBFD appears to have a slightly older ucode version
     * compared to JFG, B.T. et al.
     * For naudio_mp3, the functions DMEM parameters have an additional protective AND on them
     * (basically dmem & 0xffff).
     * But there are minor differences are in the RESAMPLE and ENVMIXER functions.
     * I don't think it is making any noticeable difference, as it could be just a simplification of the logic.
     *
     * bsmiles32: The only difference I could remember between mp3 and cbfd variants is in the MP3ADDY command.
     * And the MP3 overlay is also different.
     */
    static const acmd_callback_t ABI[0x10] = {
        OVERLOAD,       ADPCM,          CLEARBUFF,      ENVMIXER_CBFD,
        LOADBUFF,       RESAMPLE,       SAVEBUFF,       MP3,
        MP3ADDY,        SETVOL,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     NAUDIO_14,      SETLOOP
    };

    alist_process(hle, ABI, 0x10);
    rsp_break(hle, SP_STATUS_TASKDONE);
}
