/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rdp_core.c                                              *
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

#include "rdp_core.h"

extern int g_rsp_task_consumes_dp;

extern int angrylion_sync_full_seen;

#include <string.h>

#include "device/memory/m64p_memory.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/cp0.h"
#include "plugin/plugin.h"


/* The RDP clock counter free-runs on hardware and nothing here ever moved
 * it, so it read back a constant zero.  Anything sampling it - libdragon
 * prints it in its RSP crash dump, profilers difference it across a frame -
 * saw the same nothing every time.
 *
 * Derive it from the cpu count, which is the only clock this core keeps.
 * The RDP runs at 62.5MHz against the count register's 46.875MHz, so four
 * of these to every three of those; the ratio matters less than the
 * counter advancing at a plausible rate and never going backwards.  It is
 * 24 bits wide and DPC_CLR_CLOCK_CTR restarts it, which is what the base
 * is for. */
static uint32_t dpc_clock_now(struct rdp_core* dp)
{
    const uint32_t* cp0_regs = r4300_cp0_regs(&dp->mi->r4300->cp0);

    cp0_update_count(dp->mi->r4300);

    return (uint32_t)((((uint64_t)(cp0_regs[CP0_COUNT_REG] - dp->clock_base)) * 4u) / 3u)
         & UINT32_C(0x00ffffff);
}

static void update_dpc_status(struct rdp_core* dp, uint32_t w)
{
    /* clear / set xbus_dmem_dma */
    if (w & DPC_CLR_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_XBUS_DMEM_DMA;
    if (w & DPC_SET_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;

    /* clear / set freeze */
    if (w & DPC_CLR_FREEZE)
    {
        dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FREEZE;

        if (dp->do_on_unfreeze & DELAY_DP_INT)
            signal_rcp_interrupt(dp->mi, MI_INTR_DP);
        if (dp->do_on_unfreeze & DELAY_UPDATESCREEN)
            gfx.updateScreen();
        dp->do_on_unfreeze = 0;
    dp->clock_base = 0;
    }
    if (w & DPC_SET_FREEZE) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FREEZE;

    /* clear / set flush */
    if (w & DPC_CLR_FLUSH) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FLUSH;
    if (w & DPC_SET_FLUSH) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FLUSH;

    /* clear clock counter */
    if (w & DPC_CLR_CLOCK_CTR)
    {
        const uint32_t* cp0_regs = r4300_cp0_regs(&dp->mi->r4300->cp0);

        cp0_update_count(dp->mi->r4300);
        dp->clock_base = cp0_regs[CP0_COUNT_REG];
        dp->dpc_regs[DPC_CLOCK_REG] = 0;
    }
}


void init_rdp(struct rdp_core* dp,
              struct rsp_core* sp,
              struct mi_controller* mi,
              struct memory* mem,
              struct rdram* rdram,
              struct r4300_core* r4300)
{
    dp->sp = sp;
    dp->mi = mi;

    init_fb(&dp->fb, mem, rdram, r4300);
}

void poweron_rdp(struct rdp_core* dp)
{
    memset(dp->dpc_regs, 0, DPC_REGS_COUNT*sizeof(uint32_t));
    memset(dp->dps_regs, 0, DPS_REGS_COUNT*sizeof(uint32_t));
    dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_START_GCLK;

    dp->do_on_unfreeze = 0;

    poweron_fb(&dp->fb);
}


void read_dpc_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dpc_reg(address);

    if (reg == DPC_CLOCK_REG)
        dp->dpc_regs[DPC_CLOCK_REG] = dpc_clock_now(dp);

    *value = dp->dpc_regs[reg];
}

void write_dpc_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dpc_reg(address);

    switch(reg)
    {
    case DPC_STATUS_REG:
        update_dpc_status(dp, value & mask);
    case DPC_CURRENT_REG:
    case DPC_CLOCK_REG:
    case DPC_BUFBUSY_REG:
    case DPC_PIPEBUSY_REG:
    case DPC_TMEM_REG:
        return;
    }

    masked_write(&dp->dpc_regs[reg], value, mask);

    switch(reg)
    {
    case DPC_START_REG:
        dp->dpc_regs[DPC_CURRENT_REG] = dp->dpc_regs[DPC_START_REG];
        break;
    case DPC_END_REG:
        unprotect_framebuffers(&dp->fb);
        angrylion_sync_full_seen = 0;
        /* This path consumes the DP interrupt itself, below, from
         * angrylion_sync_full_seen.  Say so, or the graphics plugin's
         * CheckInterrupts defers the same raise a second time. */
        g_rsp_task_consumes_dp = 1;
        gfx.processRDPList();
        g_rsp_task_consumes_dp = 0;
        protect_framebuffers(&dp->fb);
        if (angrylion_sync_full_seen)
            signal_rcp_interrupt(dp->mi, MI_INTR_DP);
        break;
    }
}


/* The span buffer, as DPS_BUFTEST_ADDR / DPS_BUFTEST_DATA expose it once
 * DPS_TEST_MODE has bit 0 set. A buffer row is 72 bits - two 32-bit colour
 * columns and a coverage byte - and takes four word addresses, so the
 * third word of each four keeps its low byte and the fourth reads zero.
 * The address register is seven bits: writes past 128 words wrap and only
 * the last 128 survive. With test access off the data register reads zero
 * and ignores writes. (n64brew RDP/Interface; constants as in cen64.) */
#define DPS_SPAN_WORDS 128
static uint32_t dps_span_buf[DPS_SPAN_WORDS];

/* A renderer that models what a draw leaves in the span buffer registers
 * two hooks: arm() on any DPS register write, and take(), which lays the
 * last draw over the first 32 stored words and reports whether there was
 * one; the draw clears the rest of the buffer. */
static void (*dps_hook_arm)(void);
static int  (*dps_hook_take)(uint32_t words[32]);

void rdp_set_dps_hooks(void (*arm)(void), int (*take)(uint32_t words[32]))
{
    dps_hook_arm = arm;
    dps_hook_take = take;
}

static void dps_materialize(void)
{
    if (dps_hook_take && dps_hook_take(dps_span_buf))
        memset(dps_span_buf + 32, 0, (DPS_SPAN_WORDS - 32) * sizeof(dps_span_buf[0]));
}

static uint32_t dps_span_mask(uint32_t idx)
{
    switch (idx & 3)
    {
        case 0: case 1: return UINT32_C(0xffffffff);
        case 2:         return UINT32_C(0x000000ff);
        default:        return 0;
    }
}

void read_dps_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dps_reg(address);

    if (reg == DPS_BUFTEST_DATA_REG)
    {
        uint32_t idx = dp->dps_regs[DPS_BUFTEST_ADDR_REG] & (DPS_SPAN_WORDS - 1);
        dps_materialize();
        *value = (dp->dps_regs[DPS_TEST_MODE_REG] & 1)
               ? (dps_span_buf[idx] & dps_span_mask(idx)) : 0;
        return;
    }

    *value = dp->dps_regs[reg];
}

void write_dps_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg = dps_reg(address);

    masked_write(&dp->dps_regs[reg], value, mask);

    if (dps_hook_arm)
        dps_hook_arm();

    if (reg == DPS_BUFTEST_ADDR_REG)
        dp->dps_regs[reg] &= DPS_SPAN_WORDS - 1;
    else if (reg == DPS_BUFTEST_DATA_REG && (dp->dps_regs[DPS_TEST_MODE_REG] & 1))
    {
        uint32_t idx = dp->dps_regs[DPS_BUFTEST_ADDR_REG] & (DPS_SPAN_WORDS - 1);
        /* a manual store lands on top of any drawn image */
        dps_materialize();
        dps_span_buf[idx] = dp->dps_regs[reg] & dps_span_mask(idx);
    }
}

void rdp_interrupt_event(void* opaque)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;

    raise_rcp_interrupt(dp->mi, MI_INTR_DP);
}

