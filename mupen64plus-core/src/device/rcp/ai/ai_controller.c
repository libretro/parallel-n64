/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - ai_controller.c                                         *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#include "ai_controller.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <stdio.h>
#include "plugin/audio_libretro/audio_plugin.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rcp/vi/vi_controller.h"
#include "device/rdram/rdram.h"


#define AI_STATUS_BUSY UINT32_C(0x40000000)
#define AI_STATUS_FULL UINT32_C(0x80000000)


static uint32_t get_remaining_dma_length(struct ai_controller* ai)
{
    unsigned int* next_ai_event;
    unsigned int remaining_dma_duration;
    const uint32_t* cp0_regs;

    if (ai->fifo[0].duration == 0)
        return 0;

    cp0_update_count(ai->mi->r4300);
    next_ai_event = get_event(&ai->mi->r4300->cp0.q, AI_INT);
    if (next_ai_event == NULL)
        return 0;

    cp0_regs = r4300_cp0_regs(&ai->mi->r4300->cp0);
    if ((int)(cp0_regs[CP0_COUNT_REG] - *next_ai_event) >= 0)
        return 0;

    remaining_dma_duration = *next_ai_event - cp0_regs[CP0_COUNT_REG];

    uint64_t dma_length = (uint64_t)remaining_dma_duration * ai->fifo[0].length / ai->fifo[0].duration;
    return dma_length&~7;
}

static unsigned int get_dma_duration(struct ai_controller* ai)
{
    unsigned int bytes_per_sample = 4; /* XXX: assume 16bit stereo - should depends on bitrate instead */
    /* Counts per second is vi->clock, exactly.
     *
     * A field is vi->delay counts and the VI is emulated at clock/delay
     * fields per second, so delay * actual_refresh == clock identically.
     * This used delay * expected_refresh_rate instead, i.e. the nominal 60
     * rather than the 60.0176 the VI actually runs at, which is 0.0294% low
     * and made the DMA duration correspondingly short.
     *
     * That error lands directly on the audio rate: with counts/sec low by
     * 0.0294% the AI accepts 22053.48 frames per second of emulated time
     * while set_frequency has told the frontend 22047, and a frontend
     * running blocking audio with no dynamic rate control has nothing to
     * absorb the difference with.  Using the clock makes production match
     * the declared rate exactly. */
    unsigned int divider = 1 + ai->regs[AI_DACRATE_REG];

    if (divider == 0)
        return 0;

    /* Scale first, divide once, and let the clock cancel.
     *
     * This used to divide before multiplying, so the per-byte count was
     * truncated to an integer and then scaled by the whole transfer length,
     * multiplying the rounding error by AI_LEN.  For an NTSC title at 22047 Hz
     * the exact figure is 551.8608 counts per byte and the truncated one is
     * 551, so every DMA was reported as completing 0.156% early regardless of
     * its size - a 0x2000-byte transfer finished 7051 counts before it should
     * have.  The AI interrupt that follows is what tells the game its buffer
     * has drained, so a consistently early one makes the game refill early and
     * produce marginally more audio per second of emulated time than the rate
     * it asked for, which the frontend then has to absorb.
     *
     * Scaling first left one truncation behind: the sample rate itself.  The
     * DAC divides the clock by (1 + DACRATE) and the result is not a whole
     * number of Hz - 48681812 / 2209 is 22037.94 - so rounding it down before
     * dividing by it reported every transfer as slightly too long, by up to
     * 0.0043%, in the opposite direction to the error above.  Writing the
     * duration as counts = LEN * (1 + DACRATE) / bytes_per_sample removes it:
     * the clock appears in both the counts-per-second and the samples-per-
     * second and cancels exactly, so nothing is rounded but the final result.
     *
     * The 64-bit intermediate cannot overflow: AI_LEN is an 18-bit field and
     * the divider is 14 bits. */
    return (unsigned int)(((uint64_t)ai->regs[AI_LEN_REG] * divider)
                          / bytes_per_sample);
}


static void do_dma(struct ai_controller* ai, struct ai_dma* dma)
{
    /* lazy initialization of sample format */
    if (ai->samples_format_changed)
    {
        unsigned int frequency = (ai->regs[AI_DACRATE_REG] == 0)
            ? 44100 /* default sample rate */
            : ai->vi->clock / (1 + ai->regs[AI_DACRATE_REG]);

        set_audio_format_via_libretro(frequency, ai->vi->clock,
                                      (ai->regs[AI_DACRATE_REG] == 0)
                                          ? 0 : 1 + ai->regs[AI_DACRATE_REG]);

        ai->samples_format_changed = 0;
    }

    ai->last_read = dma->length;

    if (ai->delayed_carry) dma->address += 0x2000;

    if (((dma->address + dma->length) & 0x1FFF) == 0)
        ai->delayed_carry = 1;
    else
        ai->delayed_carry = 0;

    /* schedule end of dma event */
    cp0_update_count(ai->mi->r4300);
    add_interrupt_event(&ai->mi->r4300->cp0, AI_INT, dma->duration);
}

static void fifo_push(struct ai_controller* ai)
{
    /* dma_modifier is an integer percentage (100 == unscaled). Use 64-bit
     * integer math for determinism: floating-point scaling here varied by
     * platform/FP flags and lost precision past 2^24. */
    unsigned int duration = (unsigned int)(
            ((uint64_t)get_dma_duration(ai) * ai->dma_modifier) / 100);

    if (ai->regs[AI_STATUS_REG] & AI_STATUS_BUSY)
    {
        ai->fifo[1].address = ai->regs[AI_DRAM_ADDR_REG] & UINT32_C(0x00fffff8);
        ai->fifo[1].length = ai->regs[AI_LEN_REG] & ~UINT32_C(7);
        ai->fifo[1].duration = duration;
        ai->regs[AI_STATUS_REG] |= AI_STATUS_FULL;
    }
    else
    {
        ai->fifo[0].address = ai->regs[AI_DRAM_ADDR_REG] & UINT32_C(0x00fffff8);
        ai->fifo[0].length = ai->regs[AI_LEN_REG] & ~UINT32_C(7);
        ai->fifo[0].duration = duration;
        ai->regs[AI_STATUS_REG] |= AI_STATUS_BUSY;

        do_dma(ai, &ai->fifo[0]);
    }
}

static void fifo_pop(struct ai_controller* ai)
{
    if (ai->regs[AI_STATUS_REG] & AI_STATUS_FULL)
    {
        ai->fifo[0].address = ai->fifo[1].address;
        ai->fifo[0].length = ai->fifo[1].length;
        ai->fifo[0].duration = ai->fifo[1].duration;
        ai->regs[AI_STATUS_REG] &= ~AI_STATUS_FULL;

        do_dma(ai, &ai->fifo[0]);
    }
    else
    {
        ai->regs[AI_STATUS_REG] &= ~AI_STATUS_BUSY;
        ai->delayed_carry = 0;
    }
}


void init_ai(struct ai_controller* ai,
             struct mi_controller* mi,
             struct ri_controller* ri,
             struct vi_controller* vi,
             unsigned int dma_modifier)
{
    ai->mi = mi;
    ai->ri = ri;
    ai->vi = vi;
    ai->dma_modifier = dma_modifier;
}

void poweron_ai(struct ai_controller* ai)
{
    memset(ai->regs, 0, AI_REGS_COUNT*sizeof(uint32_t));
    memset(ai->fifo, 0, AI_DMA_FIFO_SIZE*sizeof(struct ai_dma));
    ai->samples_format_changed = 0;
    ai->last_read = 0;
    ai->delayed_carry = 0;
}

/* Hand the backend everything the DAC has clocked out of the current
 * transfer since the last time we looked.
 *
 * remaining is the play position derived from emulated time, so the
 * amount handed over is a function of how far the machine has run, not
 * of when anyone happened to ask.  That distinction is the whole point:
 * reading it only when the game polls AI_LEN makes delivery follow the
 * game's polling pattern, which is why titles that service their mixer
 * on alternate fields hand over two fields and then none. */
/* Hand over a span of the transfer the game programmed, keeping the read
 * inside RDRAM.  The address and length are whatever went into
 * AI_DRAM_ADDR and AI_LEN, and nothing made them stay in range: a
 * transfer near the top of memory, or a stale fifo entry mid-setup,
 * walked off the end of the allocation and took the emulator down.  That
 * is a crash on guest-supplied values, so clamp and hand over the part
 * that exists. */
static void ai_push_span(struct ai_controller* ai, unsigned int diff,
                         unsigned int length)
{
    size_t   dram_size = ai->ri->rdram->dram_size;
    uint32_t start     = ai->fifo[0].address & ~UINT32_C(7);

    if (start >= dram_size || diff > dram_size - start)
        return;

    if (length > dram_size - start - diff)
        length = (unsigned int)(dram_size - start - diff);

    if (length < 4)
        return;

    push_audio_samples_via_libretro(
        (unsigned char*)ai->ri->rdram->dram + start + diff, length);
}

static unsigned int ai_hand_over_played(struct ai_controller* ai, uint32_t remaining)
{
    unsigned int diff;
    unsigned int handed;

    if (remaining >= ai->last_read)
        return 0;

    diff   = ai->fifo[0].length - ai->last_read;
    handed = ai->last_read - remaining;
    ai_push_span(ai, diff, handed);
    ai->last_read = remaining;
    return handed;
}

void read_ai_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct ai_controller* ai = (struct ai_controller*)opaque;
    uint32_t reg = ai_reg(address);

    if (reg == AI_LEN_REG)
    {
        *value = get_remaining_dma_length(ai);
        ai_hand_over_played(ai, *value);
    }
    else
    {
        *value = ai->regs[reg];
    }
}

void write_ai_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct ai_controller* ai = (struct ai_controller*)opaque;
    uint32_t reg = ai_reg(address);

    switch (reg)
    {
    case AI_LEN_REG:
        masked_write(&ai->regs[AI_LEN_REG], value, mask);
        if (ai->regs[AI_LEN_REG] != 0) {
            fifo_push(ai);
        }
        else {
            /* stop sound */
        }
        return;

    case AI_STATUS_REG:
        clear_rcp_interrupt(ai->mi, MI_INTR_AI);
        return;

    case AI_DACRATE_REG:
        /* lazy audio format setting */
        if ((ai->regs[reg]) != (value & mask))
            ai->samples_format_changed = 1;

        masked_write(&ai->regs[reg], value, mask);
        return;
    }

    masked_write(&ai->regs[reg], value, mask);
}

void ai_end_of_dma_event(void* opaque)
{
    struct ai_controller* ai = (struct ai_controller*)opaque;

    if (ai->last_read != 0)
    {
        unsigned int diff = ai->fifo[0].length - ai->last_read;
        ai_push_span(ai, diff, ai->last_read);
        ai->last_read = 0;
    }

    fifo_pop(ai);
    raise_rcp_interrupt(ai->mi, MI_INTR_AI);
}

/* Called once per frame by the frontend.  Nothing here depends on the
 * game having looked at AI_LEN: the play position comes from emulated
 * time, so each frame hands over exactly what the DAC clocked out
 * during it. */
void ai_deliver_frame(struct ai_controller* ai)
{
    unsigned int divider = 1 + ai->regs[AI_DACRATE_REG];
    unsigned int want;

    if (ai->fifo[0].duration != 0
            && ai_hand_over_played(ai, get_remaining_dma_length(ai)) != 0)
        return;

    /* Nothing was clocked out this frame: either the game has queued no
     * transfer - across a load, or whenever it has nothing to play - or
     * the one it queued has already finished and the next has not
     * arrived.  The DAC does not stop for that; it keeps clocking, and
     * what comes out is silence.  Hand the same amount of silence over
     * rather than handing over nothing, so the stream stays continuous
     * and the frontend has no gap to underrun on.
     *
     * A field is delay counts and the DAC takes (1 + DACRATE) counts a
     * sample, so the two divide to give what a field is worth without
     * needing the clock. */
    if (ai->regs[AI_DACRATE_REG] == 0 || ai->vi->delay == 0)
        return;

    want = ai->vi->delay / divider;
    if (want != 0)
        push_audio_silence_via_libretro(want);
}
