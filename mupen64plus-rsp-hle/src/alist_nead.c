/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - alist_nead.c                                    *
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

/* remove windows define to 0x06 */
#ifdef DUPLICATE
#undef DUPLICATE
#endif

/* audio commands definition */
/* The nead microcodes treat $v31 as a scratch constant register that
 * several handlers reload and one corrupts: ADDMIXER opens with
 * "vaddc $v31, $v31, $v31" - the author's VCO-clear idiom - which sets
 * the carry flags from v31's current content, doubles v31, and the
 * first vadd batch of the add loop then consumes those carries.  With
 * v31 freshly loaded by a preceding MIXER or ADPCM (the constant
 * vector, lane 3 = 0xffff) the "clear" injects a +1 into sample 3 of
 * the first eight samples; RESAMPLE leaves the all-ones vector (no
 * carries), and back-to-back ADDMIXERs see the doubled image
 * ([3,6,7], then onward as the doubling evolves).  v31 and the
 * consumed carries are real machine state that persists across
 * commands and tasks, so they are modelled here.  Verified against
 * cxd4 running the Star Fox 64 build: handler disassembly at text
 * +0xdf8 (vaddc), +0xaac / +0x24c (const reloads), +0x5b0 (resample
 * reload from data+0x70), and carry-signature probes for every
 * predecessor. */
static uint16_t nead_v31[8];

/* DMEM address of the state-staging slab ($23 in the microcode);
 * differs per build: 0xf90 (sf/sfj/wrjb), 0xfa0 (mk), 0xfb0
 * (oot/mm/ac lineage), 0xfc0 (ys/1080/fz - overlapping the OSTask,
 * whose fields the resample save then ships out as residue). */
static uint16_t nead_slab = 0xfb0;

/* the Mario Kart build carries the aspMain-layout resample (back
 * distance and aligned-block state) rather than the later builds'
 * residue-carrying one */
static int nead_resample_old = 0;

static int16_t nead_clamp_s16(int_fast32_t x)
{
    x = (x < INT16_MIN) ? INT16_MIN : x;
    x = (x > INT16_MAX) ? INT16_MAX : x;
    return (int16_t)x;
}

static int16_t* nead_s16(struct hle_t* hle, uint16_t dmem)
{
    return (int16_t*)(hle->alist_buffer + ((dmem ^ S16) & 0xfff));
}

/* The alist buffer shadows the sample area of DMEM; the slab sits in
 * the task-header region, whose real content (OSTask fields for the
 * 0xfc0 builds, zeros otherwise) the resample save ships out as
 * residue.  Seed the shadow from real DMEM at task start; from then
 * on only the resample state load/save updates it. */
static void nead_slab_seed(struct hle_t* hle)
{
    unsigned k;
    for (k = 0; k < 0x20; k += 2)
        *nead_s16(hle, (uint16_t)(nead_slab + k)) =
            (int16_t)((*dmem_u32(hle, (nead_slab + k) & ~3)
                       >> (((nead_slab + k) & 2) ? 0 : 16)) & 0xffff);
}


static void nead_v31_load(struct hle_t* hle, uint16_t data_off)
{
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    unsigned i;
    for (i = 0; i < 8; ++i)
        nead_v31[i] = *dram_u16(hle, ucode_data + data_off + 2*i);
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

static void LOADADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = w1;
    uint32_t address = (w2 & 0xffffff);

    {  size_t entries = count >> 1;
        const size_t cap = sizeof(hle->alist_nead.table)/sizeof(hle->alist_nead.table[0]);
        if (entries > cap) entries = cap;   /* count is unbounded alist data */
        dram_load_u16(hle, (uint16_t*)hle->alist_nead.table, address, entries);
    }
}

static void SETLOOP(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    hle->alist_nead.loop = w2 & 0xffffff;
}

static void SETBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    hle->alist_nead.in    = w1;
    hle->alist_nead.out   = (w2 >> 16);
    hle->alist_nead.count = w2;
}

static void ADPCM(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = (w2 & 0xffffff);

    nead_v31_load(hle, 0x00);
    alist_adpcm(
            hle,
            flags & 0x1,
            flags & 0x2,
            flags & 0x4,
            hle->alist_nead.out,
            hle->alist_nead.in,
            (hle->alist_nead.count + 0x1f) & ~0x1f,
            hle->alist_nead.table,
            hle->alist_nead.loop,
            address);
}

static void CLEARBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmem  = w1;
    uint16_t count = w2 & 0xfff;

    if (count == 0)
        return;

    alist_clear(hle, dmem, count);
}

static void LOADBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff);
    uint32_t address = (w2 & 0xffffff);

    alist_load(hle, dmem, address, count);
}

static void SAVEBUFF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count   = (w1 >> 12) & 0xfff;
    uint16_t dmem    = (w1 & 0xfff);
    uint32_t address = (w2 & 0xffffff);

    alist_save(hle, dmem, address, count);
}

static void MIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = (w1 >> 12) & 0xff0;
    int16_t  gain  = w1;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;

    nead_v31_load(hle, 0x00);
    /* the mix loop processes two 8-sample batches per iteration and
     * only then tests the counter, so the extent rounds up to 0x20 */
    alist_mix_nead(hle, dmemo, dmemi, (uint16_t)((count + 0x1f) & ~0x1f), gain);
}


static void RESAMPLE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint16_t pitch   = w1;
    uint32_t address = (w2 & 0xffffff);

    nead_v31_load(hle, 0x70);
    if (nead_resample_old) {
        alist_resample_audio(
                hle,
                nead_slab,
                flags & 0x1,
                flags & 0x2,
                hle->alist_nead.out,
                hle->alist_nead.in,
                (hle->alist_nead.count + 0xf) & ~0xf,
                pitch << 1,
                address);
        return;
    }
    alist_resample_nead(
            hle,
            nead_slab,
            flags,
            hle->alist_nead.out,
            hle->alist_nead.in,
            (hle->alist_nead.count + 0xf) & ~0xf,
            pitch << 1,
            address);
}

static void RESAMPLE_ZOH(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t pitch      = w1;
    uint16_t pitch_accu = w2;

    alist_resample_zoh(
            hle,
            hle->alist_nead.out,
            hle->alist_nead.in,
            hle->alist_nead.count,
            pitch << 1,
            pitch_accu);
}

static void DMEMMOVE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t dmemi = w1;
    uint16_t dmemo = (w2 >> 16);
    uint16_t count = w2;

    if (count == 0)
        return;

    alist_move(hle, dmemo, dmemi, (count + 3) & ~3);
}

static void ENVSETUP1_MK(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    hle->alist_nead.env_values[2] = (w1 >> 8) & 0xff00;
    hle->alist_nead.env_steps[2]  = 0;
    hle->alist_nead.env_steps[0]  = (w2 >> 16);
    hle->alist_nead.env_steps[1]  = w2;
}

static void ENVSETUP1(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    hle->alist_nead.env_values[2] = (w1 >> 8) & 0xff00;
    hle->alist_nead.env_steps[2]  = w1;
    hle->alist_nead.env_steps[0]  = (w2 >> 16);
    hle->alist_nead.env_steps[1]  = w2;
}

static void ENVSETUP2(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    hle->alist_nead.env_values[0] = (w2 >> 16);
    hle->alist_nead.env_values[1] = w2;
}

static void ENVMIXER_MK(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int16_t xors[4];

    uint16_t dmemi = (w1 >> 12) & 0xff0;
    uint8_t  count = (w1 >>  8) & 0xff;
    uint16_t dmem_dl = (w2 >> 20) & 0xff0;
    uint16_t dmem_dr = (w2 >> 12) & 0xff0;
    uint16_t dmem_wl = (w2 >>  4) & 0xff0;
    uint16_t dmem_wr = (w2 <<  4) & 0xff0;

    xors[2] = 0;    /* unsupported by this ucode */
    xors[3] = 0;    /* unsupported by this ucode */
    xors[0] = 0 - (int16_t)((w1 & 0x2) >> 1);
    xors[1] = 0 - (int16_t)((w1 & 0x1)     );

    alist_envmix_nead(
            hle,
            false,  /* unsupported by this ucode */
            dmem_dl, dmem_dr,
            dmem_wl, dmem_wr,
            dmemi, count,
            hle->alist_nead.env_values,
            hle->alist_nead.env_steps,
            xors);
}

static void ENVMIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int16_t xors[4];

    uint16_t dmemi = (w1 >> 12) & 0xff0;
    uint8_t  count = (w1 >>  8) & 0xff;
    bool     swap_wet_LR = (w1 >> 4) & 0x1;
    uint16_t dmem_dl = (w2 >> 20) & 0xff0;
    uint16_t dmem_dr = (w2 >> 12) & 0xff0;
    uint16_t dmem_wl = (w2 >>  4) & 0xff0;
    uint16_t dmem_wr = (w2 <<  4) & 0xff0;

    xors[2] = 0 - (int16_t)((w1 & 0x8) >> 1);
    xors[3] = 0 - (int16_t)((w1 & 0x4) >> 1);
    xors[0] = 0 - (int16_t)((w1 & 0x2) >> 1);
    xors[1] = 0 - (int16_t)((w1 & 0x1)     );

    alist_envmix_nead(
            hle,
            swap_wet_LR,
            dmem_dl, dmem_dr,
            dmem_wl, dmem_wr,
            dmemi, count,
            hle->alist_nead.env_values,
            hle->alist_nead.env_steps,
            xors);
}

static void DUPLICATE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  count = (w1 >> 16);
    uint16_t dmemi = w1;
    uint16_t dmemo = (w2 >> 16);

    alist_repeat64(hle, dmemo, dmemi, count);
}

static void INTERL(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = w1;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;

    alist_copy_every_other_sample(hle, dmemo, dmemi, count);
}

static void INTERLEAVE_MK(struct hle_t* hle, uint32_t UNUSED(w1), uint32_t w2)
{
    uint16_t left = (w2 >> 16);
    uint16_t right = w2;

    if (hle->alist_nead.count == 0)
        return;

    alist_interleave(hle, hle->alist_nead.out, left, right, hle->alist_nead.count);
}

static void INTERLEAVE(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = ((w1 >> 12) & 0xff0);
    uint16_t dmemo = w1;
    uint16_t left = (w2 >> 16);
    uint16_t right = w2;

    alist_interleave(hle, dmemo, left, right, count);
}

static void ADDMIXER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = (w1 >> 12) & 0xff0;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;
    uint8_t carry[8];
    unsigned i;

    /* the entry vaddc: VCO = carry-out of v31 + v31, and v31 doubles */
    for (i = 0; i < 8; ++i) {
        carry[i] = (uint8_t)(((uint32_t)nead_v31[i] * 2) >> 16);
        nead_v31[i] = (uint16_t)(nead_v31[i] << 1);
    }

    /* four 8-sample batches per iteration, counter tested after, so
     * the extent rounds up to 0x40; the first vadd batch consumes the
     * carries and clears them */
    count = (uint16_t)((count + 0x3f) & ~0x3f);
    for (i = 0; i < 8; ++i) {
        int16_t* o = nead_s16(hle, (uint16_t)(dmemo + 2*i));
        *o = nead_clamp_s16(*o + *nead_s16(hle, (uint16_t)(dmemi + 2*i)) + carry[i]);
    }
    alist_add(hle, (uint16_t)(dmemo + 16), (uint16_t)(dmemi + 16),
              (uint16_t)(count - 16));
}

static void HILOGAIN(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    int8_t   gain  = (w1 >> 16); /* Q4.4 signed */
    uint16_t count = w1 & 0xfff;
    uint16_t dmem  = (w2 >> 16);

    alist_multQ44(hle, dmem, count, gain);
}

static void FILTER(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = (w2 & 0xffffff);

    if (flags > 1) {
        /* setup: latch the byte count and snapshot the 16-byte
         * coefficient table from DRAM (the ucode DMAs it into DMEM;
         * later DRAM changes must not be observed by the run). */
        hle->alist_nead.filter_count = w1;
        dram_load_u16(hle, (uint16_t*)hle->alist_nead.filter_table, address, 8);
    }
    else {
        uint16_t dmem = w1;

        /* run: flags==1 starts from zeroed state, flags==0 resumes
         * from the 32-byte state at `address`. */
        alist_filter(hle, (flags & 0x1) != 0, dmem,
                     hle->alist_nead.filter_count, address,
                     hle->alist_nead.filter_table);
    }
}

static void S8DEC(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint32_t address = (w2 & 0xffffff);

    alist_s8dec(
            hle,
            (flags & 0x1) != 0,
            (flags & 0x2) != 0,
            hle->alist_nead.in,
            hle->alist_nead.out,
            hle->alist_nead.count,
            hle->alist_nead.loop,
            address);
}

/* OoT aspMain command 0x03 (AudioSynth_UnkCmd3 in the OoT decomp,
 * issued for bookOffset == 2 samples). w1 low 16 = byte count,
 * w2 = dmemi << 16 | dmemo. */
static void UNKCMD3(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint16_t count = w1;
    uint16_t dmemi = (w2 >> 16);
    uint16_t dmemo = w2;

    alist_unkcmd3(hle, dmemo, dmemi, count);
}

static void SEGMENT(struct hle_t* UNUSED(hle), uint32_t UNUSED(w1), uint32_t UNUSED(w2))
{
}

static void NEAD_16(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  count      = (w1 >> 16);
    uint16_t dmemi      = w1;
    uint16_t dmemo      = (w2 >> 16);
    uint16_t block_size = w2;

    alist_copy_blocks(hle, dmemo, dmemi, block_size, count);
}

static void POLEF(struct hle_t* hle, uint32_t w1, uint32_t w2)
{
    uint8_t  flags   = (w1 >> 16);
    uint16_t gain    = w1;
    uint32_t address = (w2 & 0xffffff);

    if (hle->alist_nead.count == 0)
        return;

    alist_polef(
            hle,
            flags & A_INIT,
            hle->alist_nead.out,
            hle->alist_nead.in,
            hle->alist_nead.count,
            gain,
            hle->alist_nead.table,
            address);
}


void alist_process_nead_mk(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        SPNOOP,         RESAMPLE,       SPNOOP,         SEGMENT,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1_MK,   ENVMIXER_MK,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xfa0;
    nead_resample_old = 1;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x20);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_sf(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      SPNOOP,
        HILOGAIN,       UNKNOWN,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x20);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_sfj(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE_MK,  POLEF,          SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN,
        HILOGAIN,       UNKNOWN,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x20);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_fz(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       SPNOOP,         SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     SPNOOP,         SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN,
        SPNOOP,         UNKNOWN,        DUPLICATE,      SPNOOP,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xfc0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x20);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_wrjb(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x20] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      UNKNOWN,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   SPNOOP,
        SETBUFF,        SPNOOP,         DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     SPNOOP,         SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN,
        HILOGAIN,       UNKNOWN,        DUPLICATE,      FILTER,
        SPNOOP,         SPNOOP,         SPNOOP,         SPNOOP
    };

    nead_slab = 0xf90;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x20);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_ys(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      UNKNOWN,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN
    };

    nead_slab = 0xfc0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_1080(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      UNKNOWN,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN
    };

    nead_slab = 0xfc0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_oot(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mm(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mmb(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        SPNOOP,         ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN
    };

    nead_slab = 0xfb0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_ac(struct hle_t* hle)
{
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      SPNOOP,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      UNKNOWN
    };

    nead_slab = 0xfb0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_mats(struct hle_t* hle)
{
    /* Mario Artist: Talent Studio (64DD), ucode signature 0x1f701238.
     * The command jump table (ucode_data + 0x10, indexed by cmd = w1 >> 24)
     * was extracted from the running ucode and matches the nead_oot command
     * layout for all 0x18 entries. */
    static const acmd_callback_t ABI[0x18] = {
        UNKNOWN,        ADPCM,          CLEARBUFF,      UNKCMD3,
        ADDMIXER,       RESAMPLE,       RESAMPLE_ZOH,   FILTER,
        SETBUFF,        DUPLICATE,      DMEMMOVE,       LOADADPCM,
        MIXER,          INTERLEAVE,     HILOGAIN,       SETLOOP,
        NEAD_16,        INTERL,         ENVSETUP1,      ENVMIXER,
        LOADBUFF,       SAVEBUFF,       ENVSETUP2,      S8DEC
    };

    nead_slab = 0xfb0;
    nead_resample_old = 0;
    nead_slab_seed(hle);
    alist_process(hle, ABI, 0x18);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

void alist_process_nead_efz(struct hle_t* hle)
{
    /* F-Zero X: Expansion Kit (64DD), ucode signature 0x1f4c1230.
     * The Expansion Kit reuses the F-Zero X cartridge audio engine and shares
     * its nead_fz command layout, so dispatch through the same processor. */
    alist_process_nead_fz(hle);
}
