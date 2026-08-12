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

/* DMEM address of the state staging buffer shared by the plain audio
 * (aspMain) microcode's stateful commands.  ADPCM bounces a partial
 * vector through its head, RESAMPLE stages its 0x20 byte state block
 * in bytes 0x00..0x1f, and ENVMIXER stages its 0x50 byte state image
 * in bytes 0x00..0x4f, so each command's state save can carry residue
 * of the previous occupant in the bytes it does not itself write. */
enum { ALIST_AUDIO_STATE_SLAB = 0xf90 };

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

/* The naudio ENVMIXER routes the input samples through vmulf before the
 * accumulating mix: src' = vmulf(src, +-0x7fff), with the sign selected by
 * the LSB of the dry (resp. wet) gain. vmulf(x, 0x7fff) is not the
 * identity: it is off by one for |x| > 0x4000, so modelling it is required
 * for bit-exact output. */
static int16_t alist_envmix_premix(int16_t src, int16_t phase)
{
    return clamp_s16((((int32_t)src * phase) * 2 + 0x8000) >> 16);
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

    while (alist_end - alist >= 2) {
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

/* SP DMA alignment.
 *
 * The RSP's DMA engine transfers in 8-byte units and ignores the low three
 * bits of both addresses: cxd4 computes its offsets as
 *
 *     offC = (... + SP_MEM_ADDR + i) & 0x00001FF8
 *     offD = (... + SP_DRAM_ADDR + i) & 0x00FFFFF8
 *
 * i.e. ~7 on the DMEM side as well as the DRAM side.  This masked the DMEM
 * address to ~3, so a transfer whose DMEM address was 4-byte but not 8-byte
 * aligned landed four bytes - two samples - away from where the hardware
 * puts it, in every direction and for every microcode. */
/* The DMA engine advances its DMEM pointer one 8-byte unit at a time and
 * masks each one, so a transfer that runs off the end of the buffer wraps
 * rather than continuing into whatever follows:
 *
 *     offC = (count*length + SP_MEM_ADDR + i) & 0x00001FF8
 *
 * A straight memcpy of the whole span has no such bound.  alist_buffer is
 * 0x1000 bytes and struct hle_t places alist_audio - the in/out/count,
 * volumes, targets and rates - immediately after it, so a transfer whose
 * DMEM offset plus length passed 0x1000 wrote through the audio state of
 * the very commands driving it.  LOADBUFF and SAVEBUFF take both operands
 * from the alist (in/out via SETBUFF, count as w2's low half), and neither
 * was bounded, so reaching it needed nothing more than a buffer near the
 * top of DMEM.
 *
 * Masking per unit, the way the hardware does, both bounds the access and
 * reproduces the wrap. */
static void alist_dma(struct hle_t* hle, uint16_t dmem, uint32_t address,
                      uint16_t count, int to_dram)
{
    uint32_t i;

    /* enforce DMA alignment constraints */
    dmem    &= ~7;
    address &= ~7;
    count = align(count, 8);

    for (i = 0; i < count; i += 8) {
        uint8_t* const sp  = hle->alist_buffer + ((dmem + i) & 0xff8);
        uint8_t* const ram = hle->dram + address + i;

        if (to_dram)
            memcpy(ram, sp, 8);
        else
            memcpy(sp, ram, 8);
    }
}

void alist_load(struct hle_t* hle, uint16_t dmem, uint32_t address, uint16_t count)
{
    alist_dma(hle, dmem, address, count, 0);
}

void alist_save(struct hle_t* hle, uint16_t dmem, uint32_t address, uint16_t count)
{
    alist_dma(hle, dmem, address, count, 1);
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

/* Reads count bytes from each source and writes twice that, so it is the
 * command most able to run past the end of DMEM: it only takes an output
 * buffer in the upper half with a count to match.  The pointers were walked
 * unbounded, and alist_buffer is followed by alist_audio_t, so the tail of
 * an over-long interleave landed on the in/out/count and the volume state.
 * DMEM offsets wrap inside SP memory on hardware; wrap here too. */
#define ALIST_W(off) (*(uint16_t*)(hle->alist_buffer + (((off)) & 0xffe)))

void alist_interleave(struct hle_t* hle, uint16_t dmemo, uint16_t left, uint16_t right, uint16_t count)
{
    uint16_t o = dmemo, l = left, r = right;

    count >>= 2;

    while(count != 0) {
        uint16_t l1 = ALIST_W(l); l += 2;
        uint16_t l2 = ALIST_W(l); l += 2;
        uint16_t r1 = ALIST_W(r); r += 2;
        uint16_t r2 = ALIST_W(r); r += 2;

#ifdef MSB_FIRST
        ALIST_W(o) = l1; o += 2;
        ALIST_W(o) = r1; o += 2;
        ALIST_W(o) = l2; o += 2;
        ALIST_W(o) = r2; o += 2;
#else
        ALIST_W(o) = r2; o += 2;
        ALIST_W(o) = l2; o += 2;
        ALIST_W(o) = r1; o += 2;
        ALIST_W(o) = l1; o += 2;
#endif
        --count;
    }
}


/* ENVMIXER as the audio 1.0 aspMain microcode computes it: the state is
 * the raw 0x50-byte DMEM image of five vector quads -- the left volume
 * hi/lo pair, the right volume hi/lo pair, and a parameter vector of
 * [l_target, l_rate_hi, l_rate_lo, r_target, r_rate_hi, r_rate_lo, dry,
 * wet] -- and games seed that image from the CPU and start voices with
 * non-init commands, so the layout is a contract, not an internal
 * detail. Each 8-sample block multiplies the per-lane 32-bit volumes by
 * the Q16.16 rate through the vmudl/vmadm/vmadn/vmadh idiom (the RSP's
 * saturations included) and clamps the result to the target, and each
 * sample mixes as clamp((2*dst*0x7fff + 2*in*gain + 0x8000) >> 16) --
 * one vmulf/vmacf accumulator, not an integer add. */
static int16_t audio1_acc_hi(int64_t acc)
{
    int64_t v = acc >> 16;
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static uint16_t audio1_acc_lo(int64_t acc)
{
    int64_t hi = acc >> 31;
    if (hi == 0 || hi == -1)
        return (uint16_t)(acc & 0xffff);
    return (acc < 0) ? 0u : 0xffffu;
}

void alist_envmix_audio1(
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
    /* Indexed through wrapping accessors rather than raw pointers: the four
     * destinations each take count bytes from offsets that come out of the
     * alist, and alist_buffer is followed by alist_audio_t, so an output
     * buffer high enough in DMEM wrote its tail over the in/out/count and
     * the volume state driving it.  DMEM wraps inside SP memory. */
#define EM_IN(i)  (*(const int16_t*)(hle->alist_buffer + ((dmemi   + 2*(i)) & 0xffe)))
#define EM_DL(i)  (*(int16_t*)      (hle->alist_buffer + ((dmem_dl + 2*(i)) & 0xffe)))
#define EM_DR(i)  (*(int16_t*)      (hle->alist_buffer + ((dmem_dr + 2*(i)) & 0xffe)))
#define EM_WL(i)  (*(int16_t*)      (hle->alist_buffer + ((dmem_wl + 2*(i)) & 0xffe)))
#define EM_WR(i)  (*(int16_t*)      (hle->alist_buffer + ((dmem_wr + 2*(i)) & 0xffe)))

    int16_t  lhi[8], rhi[8];
    uint16_t llo[8], rlo[8];
    int16_t  params[8];
    unsigned i, ptr = 0;

    /* On init the microcode interpolates the eight volume lanes from
     * the base volume toward vol * rate through the ramp-fraction table
     * [0x2000..0xffff], which seeds the per-sample stagger the steady
     * blocks then carry multiplicatively; the base volume's low half
     * starts at zero. */
    if (init) {
        static const uint16_t frac[8] = {
            0x2000, 0x4000, 0x6000, 0x8000, 0xa000, 0xc000, 0xe000, 0xffff
        };
        int64_t acc;
        int32_t dhi_l, dhi_r;
        uint16_t dlo_l, dlo_r;

        params[0] = target[0];
        params[1] = (int16_t)(rate[0] >> 16);
        params[2] = (int16_t)(rate[0] & 0xffff);
        params[3] = target[1];
        params[4] = (int16_t)(rate[1] >> 16);
        params[5] = (int16_t)(rate[1] & 0xffff);
        params[6] = dry;
        params[7] = wet;

        /* delta = vol * rate - (vol:stale_lo), as the vmudm/vmadh pair
         * followed by the borrow subtract computes it */
        acc  = (int64_t)vol[0] * (uint16_t)params[2];
        acc += ((int64_t)vol[0] * params[1]) << 16;
        dlo_l = (uint16_t)(acc & 0xffff);
        dhi_l = audio1_acc_hi(acc);
        {
            dhi_l = dhi_l - vol[0];
        }
        acc  = (int64_t)vol[1] * (uint16_t)params[5];
        acc += ((int64_t)vol[1] * params[4]) << 16;
        dlo_r = (uint16_t)(acc & 0xffff);
        dhi_r = audio1_acc_hi(acc);
        {
            dhi_r = dhi_r - vol[1];
        }

        for (i = 0; i < 8; ++i) {
            acc  = ((int64_t)((uint32_t)frac[i] * dlo_l)) >> 16;
            acc += (int64_t)frac[i] * (int16_t)dhi_l;
            acc += ((int64_t)1 * vol[0]) << 16;
            lhi[i] = audio1_acc_hi(acc);
            llo[i] = audio1_acc_lo(acc);

            acc  = ((int64_t)((uint32_t)frac[i] * dlo_r)) >> 16;
            acc += (int64_t)frac[i] * (int16_t)dhi_r;
            acc += ((int64_t)1 * vol[1]) << 16;
            rhi[i] = audio1_acc_hi(acc);
            rlo[i] = audio1_acc_lo(acc);
        }

        /* the first block's volumes clamp to the target before use */
        for (i = 0; i < 8; ++i) {
            lhi[i] = (params[1] > 0)
                ? (lhi[i] > params[0] ? params[0] : lhi[i])
                : (lhi[i] < params[0] ? params[0] : lhi[i]);
            rhi[i] = (params[4] > 0)
                ? (rhi[i] > params[3] ? params[3] : rhi[i])
                : (rhi[i] < params[3] ? params[3] : rhi[i]);
        }

        {
            int16_t lgd[8], lgw[8], rgd[8], rgw[8];
            for (i = 0; i < 8; ++i) {
                lgd[i] = clamp_s16((int32_t)(((int64_t)2*lhi[i]*params[6] + 0x8000) >> 16));
                lgw[i] = clamp_s16((int32_t)(((int64_t)2*lhi[i]*params[7] + 0x8000) >> 16));
                rgd[i] = clamp_s16((int32_t)(((int64_t)2*rhi[i]*params[6] + 0x8000) >> 16));
                rgw[i] = clamp_s16((int32_t)(((int64_t)2*rhi[i]*params[7] + 0x8000) >> 16));
            }
            for (i = 0; i < 8 && count >= 2; ++i, ++ptr, count -= 2) {
                int16_t x = EM_IN(ptr^S);
                acc = (int64_t)2*EM_DL(ptr^S)*32767 + 0x8000 + (int64_t)2*x*lgd[i];
                EM_DL(ptr^S) = audio1_acc_hi(acc);
                acc = (int64_t)2*EM_DR(ptr^S)*32767 + 0x8000 + (int64_t)2*x*rgd[i];
                EM_DR(ptr^S) = audio1_acc_hi(acc);
                if (aux) {
                    acc = (int64_t)2*EM_WL(ptr^S)*32767 + 0x8000 + (int64_t)2*x*lgw[i];
                    EM_WL(ptr^S) = audio1_acc_hi(acc);
                    acc = (int64_t)2*EM_WR(ptr^S)*32767 + 0x8000 + (int64_t)2*x*rgw[i];
                    EM_WR(ptr^S) = audio1_acc_hi(acc);
                }
            }
        }
    } else {
        for (i = 0; i < 8; ++i) {
            lhi[i] = (int16_t)*dram_u16(hle, address + i*2);
            llo[i] = *dram_u16(hle, address + 0x10 + i*2);
            rhi[i] = (int16_t)*dram_u16(hle, address + 0x20 + i*2);
            rlo[i] = *dram_u16(hle, address + 0x30 + i*2);
        }
        for (i = 0; i < 8; ++i)
            params[i] = (int16_t)*dram_u16(hle, address + 0x40 + i*2);
    }

    for (; count >= 16; count -= 16) {
        int16_t lgain_dry[8], lgain_wet[8], rgain_dry[8], rgain_wet[8];

        /* per-lane 32-bit volume *= Q16.16 rate, RSP saturations included */
        for (i = 0; i < 8; ++i) {
            int64_t acc;
            int16_t hi;

            acc  = ((int64_t)((uint32_t)llo[i] * (uint16_t)params[2])) >> 16;
            acc += (int64_t)lhi[i] * (uint16_t)params[2];
            acc += (int64_t)llo[i] * params[1];
            acc += ((int64_t)lhi[i] * params[1]) << 16;
            hi = audio1_acc_hi(acc);
            llo[i] = audio1_acc_lo(acc);
            /* rate is Q16.16: a nonzero integer part means the volume
             * is rising and the microcode clips it to the target from
             * above (vcl); a pure fraction is a decay and the target is
             * a floor (vge). The direction registers are literally the
             * rate-hi lanes read back out of the parameter vector. */
            lhi[i] = (params[1] > 0)
                ? (hi > params[0] ? params[0] : hi)
                : (hi < params[0] ? params[0] : hi);

            acc  = ((int64_t)((uint32_t)rlo[i] * (uint16_t)params[5])) >> 16;
            acc += (int64_t)rhi[i] * (uint16_t)params[5];
            acc += (int64_t)rlo[i] * params[4];
            acc += ((int64_t)rhi[i] * params[4]) << 16;
            hi = audio1_acc_hi(acc);
            rlo[i] = audio1_acc_lo(acc);
            rhi[i] = (params[4] > 0)
                ? (hi > params[3] ? params[3] : hi)
                : (hi < params[3] ? params[3] : hi);
        }

        for (i = 0; i < 8; ++i) {
            lgain_dry[i] = clamp_s16((int32_t)(((int64_t)2*lhi[i]*params[6] + 0x8000) >> 16));
            lgain_wet[i] = clamp_s16((int32_t)(((int64_t)2*lhi[i]*params[7] + 0x8000) >> 16));
            rgain_dry[i] = clamp_s16((int32_t)(((int64_t)2*rhi[i]*params[6] + 0x8000) >> 16));
            rgain_wet[i] = clamp_s16((int32_t)(((int64_t)2*rhi[i]*params[7] + 0x8000) >> 16));
        }

        for (i = 0; i < 8; ++i, ++ptr) {
            int16_t x = EM_IN(ptr^S);
            int64_t acc;

            acc = (int64_t)2*EM_DL(ptr^S)*32767 + 0x8000 + (int64_t)2*x*lgain_dry[i];
            EM_DL(ptr^S) = audio1_acc_hi(acc);
            acc = (int64_t)2*EM_DR(ptr^S)*32767 + 0x8000 + (int64_t)2*x*rgain_dry[i];
            EM_DR(ptr^S) = audio1_acc_hi(acc);
            if (aux) {
                acc = (int64_t)2*EM_WL(ptr^S)*32767 + 0x8000 + (int64_t)2*x*lgain_wet[i];
                EM_WL(ptr^S) = audio1_acc_hi(acc);
                acc = (int64_t)2*EM_WR(ptr^S)*32767 + 0x8000 + (int64_t)2*x*rgain_wet[i];
                EM_WR(ptr^S) = audio1_acc_hi(acc);
            }
        }
    }

    for (i = 0; i < 8; ++i) {
        *dram_u16(hle, address + i*2)        = (uint16_t)lhi[i];
        *dram_u16(hle, address + 0x10 + i*2) = llo[i];
        *dram_u16(hle, address + 0x20 + i*2) = (uint16_t)rhi[i];
        *dram_u16(hle, address + 0x30 + i*2) = rlo[i];
        *dram_u16(hle, address + 0x40 + i*2) = (uint16_t)params[i];
    }
}
#undef EM_IN
#undef EM_DL
#undef EM_DR
#undef EM_WL
#undef EM_WR


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

    /* The microcode DMAs the state image into the shared staging slab
     * (skipped on init, which leaves the previous occupant's bytes in
     * place); keep the slab mirror in sync so later commands see the
     * same residue a real RSP would. */
    if (!init)
        memcpy(hle->alist_buffer + ALIST_AUDIO_STATE_SLAB,
               hle->dram + address, sizeof(save_buffer));

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

    /* The saved image is what the slab holds afterwards on hardware. */
    memcpy(hle->alist_buffer + ALIST_AUDIO_STATE_SLAB,
           (uint8_t *)save_buffer, sizeof(save_buffer));
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
        uint32_t address,
        enum alist_envmix_input input_mode)
{
    /* Exact model of the naudio ENVMIXER inner loop.
     *
     * The microcode does not step the envelope once per sample. It keeps,
     * per channel, eight 32-bit lane accumulators seeded with
     *   lane[j] = (vol << 16) + rate_hi * idx[j] + ((rate_lo * idx[j]) >> 16)
     * where idx[] holds j/8 in unsigned 0.16 (the last lane being 0xffff,
     * one LSB short of 1.0), then adds the full 32-bit rate once per block
     * of eight samples and clamps only the high halves against the target.
     * The input path depends on the ucode revision (input_mode): the
     * original revision (plain naudio, Banjo-Kazooie) feeds the samples
     * to the mix unchanged and has no phase feature at all; the
     * naudio_mp3/naudio_dk revisions (Perfect Dark, Banjo-Tooie, Jet
     * Force Gemini, Mickey's Speedway, Donkey Kong 64) apply a
     * one's-complement (vxor) inversion; the Conker revision routes the
     * samples through vmulf(src, +-0x7fff). In both transforming
     * revisions the microcode inverts the input once with the mask/scale
     * derived from the DRY gain LSB, mixes the two LEFT outputs, then
     * re-derives the input with the WET gain LSB mask/scale and mixes
     * the two RIGHT outputs - so the selection is per stereo side, not
     * per dry/wet send.
     *
     * The saved state matches the microcode layout: four vector registers
     * (hi/lo lane pairs for both channels) followed by the parameter
     * vector [target_l, rate_l_hi, rate_l_lo, target_r, rate_r_hi,
     * rate_r_lo, dry, wet]. */
    static const uint16_t ramp_idx[8] = {
        0x2000, 0x4000, 0x6000, 0x8000, 0xa000, 0xc000, 0xe000, 0xffff
    };

    size_t m, j;
    int32_t lane[2][8];
    int16_t tgt[2];
    int32_t rt[2];
    int16_t save_buffer[40];
    size_t blocks = (count >> 1) / 8;

    const int16_t * const in = (int16_t*)(hle->alist_buffer + dmemi);
    int16_t* const dl = (int16_t*)(hle->alist_buffer + dmem_dl);
    int16_t* const dr = (int16_t*)(hle->alist_buffer + dmem_dr);
    int16_t* const wl = (int16_t*)(hle->alist_buffer + dmem_wl);
    int16_t* const wr = (int16_t*)(hle->alist_buffer + dmem_wr);

    if (init) {
        int c;
        tgt[0] = target[0];
        tgt[1] = target[1];
        rt[0]  = rate[0];
        rt[1]  = rate[1];
        for (c = 0; c < 2; ++c) {
            int16_t  hi = (int16_t)((uint32_t)rt[c] >> 16);
            uint16_t lo = (uint16_t)rt[c];
            for (j = 0; j < 8; ++j) {
                /* The microcode accumulates this in the 48-bit multiply
                 * accumulator and extracts the result with vmadh/vmadn,
                 * which saturate the pair to a signed 32-bit value. */
                int64_t v = ((int64_t)vol[c] << 16)
                    + (int64_t)hi * ramp_idx[j]
                    + (int64_t)(((uint32_t)lo * ramp_idx[j]) >> 16);
                if (v >  2147483647LL) v =  2147483647LL;
                if (v < -2147483648LL) v = -2147483648LL;
                lane[c][j] = (int32_t)v;
            }
        }
    }
    else {
        memcpy((uint8_t *)save_buffer, hle->dram + address, 80);
        for (j = 0; j < 8; ++j) {
            lane[0][j] = ((int32_t)save_buffer[j]      << 16) | (uint16_t)save_buffer[j +  8];
            lane[1][j] = ((int32_t)save_buffer[j + 16] << 16) | (uint16_t)save_buffer[j + 24];
        }
        tgt[0] = save_buffer[32];
        rt[0]  = ((int32_t)save_buffer[33] << 16) | (uint16_t)save_buffer[34];
        tgt[1] = save_buffer[35];
        rt[1]  = ((int32_t)save_buffer[36] << 16) | (uint16_t)save_buffer[37];
        dry    = save_buffer[38];
        wet    = save_buffer[39];
    }

    {
        /* ALIST_ENVMIX_IN_VMULF: the input scale differs on the first block of
         * every call, because the setup code computes it as
         * vmulf(0x7fff, phase) (0x7ffe / -0x7fff) while the block loop
         * regenerates it with vmudh as the exact phase value
         * (0x7fff / -0x8000). */
        int16_t dry_phase = (dry & 1) ? -0x8000 : 0x7fff;
        int16_t wet_phase = (wet & 1) ? -0x8000 : 0x7fff;
        int16_t dry_scale0 = alist_envmix_premix(0x7fff, dry_phase);
        int16_t wet_scale0 = alist_envmix_premix(0x7fff, wet_phase);
        int16_t dry_mask = (dry & 1) ? -1 : 0;
        int16_t wet_mask = (wet & 1) ? -1 : 0;

        for (m = 0; m < blocks; ++m) {
            int c;
            int16_t l_vol[8], r_vol[8];

            for (c = 0; c < 2; ++c) {
                bool ascend = ((int16_t)((uint32_t)rt[c] >> 16) >= 0);
                for (j = 0; j < 8; ++j) {
                    int16_t hi;
                    if (!init || m != 0) {
                        /* vaddc/vadd pair: the low half wraps with carry
                         * out, the high half is a saturating signed add. */
                        uint32_t lo = ((uint32_t)lane[c][j] & 0xffff)
                                    + ((uint32_t)rt[c] & 0xffff);
                        int32_t  h  = (lane[c][j] >> 16)
                                    + (int16_t)((uint32_t)rt[c] >> 16)
                                    + (int32_t)(lo >> 16);
                        if (h >  32767) h =  32767;
                        if (h < -32768) h = -32768;
                        lane[c][j] = (h << 16) | (lo & 0xffff);
                    }
                    hi = (int16_t)((uint32_t)lane[c][j] >> 16);
                    if (ascend) {
                        if (hi > tgt[c]) hi = tgt[c];
                    } else {
                        if (hi < tgt[c]) hi = tgt[c];
                    }
                    lane[c][j] = ((int32_t)hi << 16) | ((uint32_t)lane[c][j] & 0xffff);
                    if (c == 0) l_vol[j] = hi; else r_vol[j] = hi;
                }
            }

            for (j = 0; j < 8; ++j) {
                size_t k = m * 8 + j;
                int16_t in_left;
                int16_t in_right;

                switch (input_mode) {
                case ALIST_ENVMIX_IN_VMULF:
                    in_left  = alist_envmix_premix(in[k^S], (m == 0) ? dry_scale0 : dry_phase);
                    in_right = alist_envmix_premix(in[k^S], (m == 0) ? wet_scale0 : wet_phase);
                    break;
                case ALIST_ENVMIX_IN_VXOR:
                    in_left  = in[k^S] ^ dry_mask;
                    in_right = in[k^S] ^ wet_mask;
                    break;
                default:
                    in_left  = in[k^S];
                    in_right = in[k^S];
                    break;
                }

                sample_mix(dl + (k^S), in_left,  clamp_s16((l_vol[j] * dry + 0x4000) >> 15));
                sample_mix(dr + (k^S), in_right, clamp_s16((r_vol[j] * dry + 0x4000) >> 15));
                sample_mix(wl + (k^S), in_left,  clamp_s16((l_vol[j] * wet + 0x4000) >> 15));
                sample_mix(wr + (k^S), in_right, clamp_s16((r_vol[j] * wet + 0x4000) >> 15));
            }
        }
    }

    for (j = 0; j < 8; ++j) {
        save_buffer[j]      = (int16_t)((uint32_t)lane[0][j] >> 16);
        save_buffer[j +  8] = (int16_t)((uint16_t)lane[0][j]);
        save_buffer[j + 16] = (int16_t)((uint32_t)lane[1][j] >> 16);
        save_buffer[j + 24] = (int16_t)((uint16_t)lane[1][j]);
    }
    save_buffer[32] = tgt[0];
    save_buffer[33] = (int16_t)((uint32_t)rt[0] >> 16);
    save_buffer[34] = (int16_t)((uint16_t)rt[0]);
    save_buffer[35] = tgt[1];
    save_buffer[36] = (int16_t)((uint32_t)rt[1] >> 16);
    save_buffer[37] = (int16_t)((uint16_t)rt[1]);
    save_buffer[38] = dry;
    save_buffer[39] = wet;
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

void alist_resample_audio(
        struct hle_t* hle,
        bool init,
        bool flag2,
        uint16_t dmemo,
        uint16_t dmemi,
        uint16_t count,
        uint32_t pitch,     /* Q16.16 */
        uint32_t address)
{
    /* The plain audio (aspMain) microcode's RESAMPLE, modelled after
     * its disassembly.  It stages a 0x20 byte state block in the
     * shared slab: bytes 0x00..0x07 hold the four-sample
     * interpolation window at the final input position, 0x08..0x09
     * the pitch accumulator, 0x0a..0x0b the distance from the final
     * window back to the last 16-byte-aligned input address, and
     * 0x10..0x1f the 16 input bytes at that aligned address.  Bytes
     * 0x0c..0x0f are not written, so the save carries whatever the
     * previous slab occupant (normally ENVMIXER's state image) left
     * there -- Dark Rift reads the saved block back every frame and
     * drives the fight scenes' palette lighting from it, so the whole
     * block has to match the microcode byte for byte. */
    uint8_t* const slab = hle->alist_buffer + ALIST_AUDIO_STATE_SLAB;
    uint32_t pitch_accu;
    uint16_t in = dmemi;
    uint16_t ipos;
    uint16_t opos = dmemo >> 1;
    uint16_t residue;
    uint16_t aligned;

    count >>= 1;

    if (init) {
        /* The init path only clears the samples and the pitch
         * accumulator; the rest of the slab keeps its residue. */
        memset(slab, 0, 10);
    }
    else
        memcpy(slab, hle->dram + address, 0x20);

    if (flag2) {
        /* Restore the 16 input bytes saved at the last aligned
         * address to just before the input buffer, then back the
         * input pointer up to the position inside them where the
         * previous run's window ended. */
        uint16_t back = (uint16_t)*alist_s16(hle, ALIST_AUDIO_STATE_SLAB + 0xa);
        memcpy(hle->alist_buffer + ((dmemi - 16) & 0xfff), slab + 0x10, 16);
        in -= back;
    }

    in -= 8;
    memcpy(hle->alist_buffer + (in & 0xfff), slab, 8);
    pitch_accu = (uint16_t)*alist_s16(hle, ALIST_AUDIO_STATE_SLAB + 0x8);
    ipos = in >> 1;

    while (count != 0) {
        const int16_t* lut = RESAMPLE_LUT + ((pitch_accu & 0xfc00) >> 8);

        /* Identical arithmetic to alist_resample: per-tap vmulf with
         * a tree of saturating vadds. */
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

    memcpy(slab, hle->alist_buffer + ((ipos << 1) & 0xfff), 8);
    *alist_s16(hle, ALIST_AUDIO_STATE_SLAB + 0x8) = (int16_t)pitch_accu;

    aligned = (ipos << 1) + 8;
    residue = (aligned - dmemi) & 0xf;
    aligned -= residue;
    *alist_s16(hle, ALIST_AUDIO_STATE_SLAB + 0xa) =
        (residue != 0) ? (int16_t)(16 - residue) : 0;
    memcpy(slab + 0x10, hle->alist_buffer + (aligned & 0xfff), 16);

    memcpy(hle->dram + address, slab, 0x20);
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
            /* The microcode accumulates every term -- the gained input,
             * the two recursive taps and the staircase dot product --
             * in the RSP's 48-bit accumulator and only saturates the
             * final >> 14 read-out. Up to eleven full-scale 32-bit
             * products go in, which overflows a 32-bit sum: the first
             * loud passage through the filter wraps instead of
             * saturating and the output picks up broadband error
             * against the LLE RSP. */
            unsigned j;
            int64_t accu = (int64_t)frame[i] * gain;
            accu += (int64_t)h1[i] * l1 + (int64_t)h2_before[i] * l2;
            for(j = 0; j < i; ++j)
                accu += (int64_t)h2[j] * frame[i - 1 - j];
            dst[i^S] = clamp_s16((int32_t)(accu >> 14));
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
