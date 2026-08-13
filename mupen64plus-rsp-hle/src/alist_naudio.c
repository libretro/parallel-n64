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

#include <string.h>
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

/* the two revisions that bank MP3 state through DRAM do it with
 * different windows, so the wrapper needs to know which one is running:
 * the Conker revision moves 0x440 bytes from DMEM 0x8a0, the
 * mp3/Banjo-Tooie revision moves 0x800 bytes from DMEM 0x800 */
static uint16_t naudio_mp3_bank_dmem = 0;
static uint16_t naudio_mp3_bank_size = 0;

static int16_t naudio_clamp_s16(int_fast32_t x)
{
    x = (x < INT16_MIN) ? INT16_MIN : x;
    x = (x > INT16_MAX) ? INT16_MAX : x;
    return (int16_t)x;
}

/* the DMEM scratch the naudio state transfers stage through */
enum { NAUDIO_STATE_SLAB = 0xfa0 };

static void naudio_seed(struct hle_t* hle)
{
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t data_size  = *dmem_u32(hle, TASK_UCODE_DATA_SIZE);
    unsigned k;
    if (data_size > 0x800) data_size = 0x800;
    for (k = 0; k + 1 < data_size; k += 2)
        *(int16_t*)(hle->alist_buffer + ((k ^ S16) & 0xfff)) =
            (int16_t)*dram_u16(hle, ucode_data + k);

    /* The scratch the state transfers stage through is DMEM, not
     * private state: it keeps whatever the previous task left there,
     * and the first command to spill residue from it does so before
     * anything in this task has staged.  Start it from DMEM. */
    for (k = 0; k < sizeof(hle->alist_naudio.state_scratch); ++k)
        hle->alist_naudio.state_scratch[k] =
            (uint8_t)(*dmem_u16(hle, (NAUDIO_STATE_SLAB + (k & ~1)) & 0xfff)
                      >> ((k & 1) ? 0 : 8));
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

static int16_t naudio_vmulf(int16_t a, int16_t b)
{
    /* vmulf: the signed product doubled, rounded and saturated */
    if (a == -32768 && b == -32768)
        return 32767;
    return naudio_clamp_s16((int32_t)(((int32_t)a * b * 2 + 0x8000) >> 16));
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

        /* The handler seeds its history vector from the state block
         * (ldv into the upper half, or vxor on an init call) and spills
         * that vector to the sixteen bytes below the buffer with the
         * first block's sqv, before any output overwrites them.  On a
         * resumed call the upper half is the four saved samples, so
         * those eight bytes are observable state a real RSP leaves
         * behind; the lower half is whatever the vector register held
         * on entry and is not modelled here. */
        for (j2 = 0; j2 < 4; ++j2) {
            *(int16_t*)(hle->alist_buffer + (((dmem - 0x10 + 2*j2) ^ S16) & 0xfff)) =
                init ? 0 : hle->alist_naudio.filter_spill[j2];
            *(int16_t*)(hle->alist_buffer + (((dmem - 8 + 2*j2) ^ S16) & 0xfff)) =
                init ? 0 : (int16_t)*dram_u16(hle, address + 2*j2);
        }

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
        /* The pole path (IMEM 0x6cc).  With a codebook loaded at DMEM
         * 0x400 the command runs a two-pole, three-zero filter over
         * the buffer in place: the feed-forward taps are the first two
         * codebook coefficients, applied to the current sample and to
         * the two samples below it through misaligned window loads,
         * and the recursive taps are the first two coefficients of the
         * following row, each doubled by a vadd of the product with
         * itself.  Every term is a vmulf, so each is rounded and
         * saturated on its own before the accumulation, which itself
         * saturates pairwise.
         *
         * The buffer is the one the command's own word selects, not
         * the fixed MAIN pair the other path uses.  The state block
         * holds the two input samples below the buffer followed by the
         * last two outputs; an init call starts both histories at
         * zero. */
        uint16_t base = (uint16_t)((w1 + 0x500) & 0xfff);
        int16_t  ff0, ff1, fb0, fb1;
        int16_t  xm1, xm2, ym1, ym2;
        unsigned k;

        ff0 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x00) ^ S16) & 0xfff));
        ff1 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x02) ^ S16) & 0xfff));
        fb0 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x10) ^ S16) & 0xfff));
        fb1 = *(int16_t*)(hle->alist_buffer + (((tbl + 0x12) ^ S16) & 0xfff));

        if (init) {
            xm1 = xm2 = ym1 = ym2 = 0;
        } else {
            xm2 = (int16_t)*dram_u16(hle, address + 0);
            xm1 = (int16_t)*dram_u16(hle, address + 2);
            ym2 = (int16_t)*dram_u16(hle, address + 4);
            ym1 = (int16_t)*dram_u16(hle, address + 6);
        }

        for (k = 0; k < NAUDIO_COUNT / 2; ++k) {
            int16_t *px = (int16_t*)(hle->alist_buffer +
                                     (((base + 2*k) ^ S16) & 0xfff));
            int16_t  xs = *px;
            int16_t  drive, out;

            drive = naudio_clamp_s16(naudio_clamp_s16(
                        naudio_vmulf(xs,  ff0) + naudio_vmulf(xm1, ff1)) +
                        naudio_vmulf(xm2, ff0));
            out   = naudio_clamp_s16(naudio_clamp_s16(
                        drive + 2*naudio_vmulf(fb0, ym1)) +
                        2*naudio_vmulf(fb1, ym2));

            *px = out;
            xm2 = xm1; xm1 = xs;
            ym2 = ym1; ym1 = out;
        }

        *dram_u16(hle, address + 0) = (uint16_t)xm2;
        *dram_u16(hle, address + 2) = (uint16_t)xm1;
        *dram_u16(hle, address + 4) = (uint16_t)ym2;
        *dram_u16(hle, address + 6) = (uint16_t)ym1;
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

    /* the handler loads the ramp-index row at DMEM 0x90 into the
     * register the filter command spills */
    {
        unsigned k;
        for (k = 0; k < 4; ++k)
            hle->alist_naudio.filter_spill[k] =
                *(int16_t*)(hle->alist_buffer + (((0x90 + 2*k) ^ S16) & 0xfff));
    }

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

    /* the handler moves the gain into lane 0 of the register the
     * filter command later spills */
    hle->alist_naudio.filter_spill[0] = gain;

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
    /* the handler loads the constant row at DMEM 0x60 into the same
     * register, so a later filter command spills these four lanes */
    {
        unsigned k;
        for (k = 0; k < 4; ++k)
            hle->alist_naudio.filter_spill[k] =
                *(int16_t*)(hle->alist_buffer + (((0x60 + 2*k) ^ S16) & 0xfff));
    }

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

static void MP3ADDY(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    /* Transcribed from the Conker handler (IMEM 0x3fc): the command
     * stores the MP3 synthesis-state DRAM address in DMEM 0xff4 and
     * returns.  The MP3 command consumes it: on every call it first
     * restores 0x440 bytes of windowing state from that address into
     * DMEM 0x8a0 and writes the 0x140-byte overlap tail from DMEM
     * 0x2c0 back to it, so per-stream state lives in DRAM rather than
     * in the RSP. */
    *dmem_u32(hle, 0xff4) = (w2 & 0xffffff);
}

static void MP3(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    unsigned index = (w1 & 0x1e);
    uint32_t address = (w2 & 0xffffff);
    uint32_t state = *dmem_u32(hle, 0xff4) & 0xffffff;
    uint8_t old_bank[0x440];
    unsigned k;

    /* The microcode banks the MP3 synthesis state per stream through
     * the DRAM address MP3ADDY stores in DMEM 0xff4 (the TASK_DATA_SIZE
     * header slot, recycled once the size has been consumed; the
     * library always pairs MP3ADDY with the MP3 commands that use it).
     * On every call the handler DMAs the 0x440-byte bank from that
     * address into the DMEM window region and writes the previous
     * call's 0x140-byte overlap tail back out.  mp3.c keeps the same
     * region at mp3_buffer 0x8a0 in host halfword order, so the copies
     * swap bytes per halfword. */
    /* only the Conker revision's window is transcribed and verified so
     * far; the mp3 revision moves a different one and is left alone
     * until it can be checked against the interpreter */
    if (naudio_mp3_bank_size != 0x440 || naudio_mp3_bank_dmem != 0x8a0) {
        mp3_task(hle, index, address);
        return;
    }

    for (k = 0; k < 0x440; ++k)
        old_bank[k] = hle->dram[(state + k) ^ S8];
    for (k = 0; k < 0x440; ++k)
        hle->mp3_buffer[(0x8a0 + k) ^ 1] = old_bank[k];

    mp3_task(hle, index, address);

    for (k = 0; k < 0x440; ++k)
        hle->dram[(state + k) ^ S8] = hle->mp3_buffer[(0x8a0 + k) ^ 1];
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
    naudio_mp3_bank_dmem = 0;
    naudio_mp3_bank_size = 0;
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
    naudio_mp3_bank_dmem = 0;
    naudio_mp3_bank_size = 0;
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
    naudio_mp3_bank_dmem = 0;
    naudio_mp3_bank_size = 0;
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
    naudio_mp3_bank_dmem = 0x800;
    naudio_mp3_bank_size = 0x800;
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
    naudio_mp3_bank_dmem = 0x8a0;
    naudio_mp3_bank_size = 0x440;
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
