#include <stdio.h>
#include <stdlib.h>
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rsp_core.c                                              *
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

#include <stdio.h>
#include <stdlib.h>
#include "rsp_core.h"

#include <string.h>

#include "device/memory/m64p_memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rdram/rdram.h"
#include "main/main.h"
#if defined(PROFILE)
#include "main/profile.h"
#endif
#include "plugin/plugin.h"
#include "api/callbacks.h"

static void do_sp_dma(struct rsp_core* sp, const struct sp_dma* dma)
{
    unsigned int i,j;

    unsigned int l = dma->length;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count = ((l >> 12) & 0xff) + 1;
    unsigned int skip = ((l >> 20) & 0xfff);

    unsigned int memaddr = dma->memaddr & 0xff8;
    unsigned int dramaddr = dma->dramaddr & 0xfffff8;

    unsigned char *spmem = (unsigned char*)sp->mem + (dma->memaddr & 0x1000);
    unsigned char *dram = (unsigned char*)sp->ri->rdram->dram;

    if (dma->dir == SP_DMA_READ)
    {
        for(j=0; j<count; j++) {
            for(i=0; i<length; i++) {
                dram[(dramaddr^S8) & 0x7fffff] = spmem[(memaddr^S8) & 0xfff];
                memaddr++;
                dramaddr++;
            }
            if (dramaddr <= 0x800000)
                post_framebuffer_write(&sp->dp->fb, dramaddr - length, length);
            dramaddr+=skip;
        }

        sp->regs[SP_MEM_ADDR_REG] = memaddr & 0xfff;
        sp->regs[SP_DRAM_ADDR_REG] = dramaddr & 0xffffff;
        sp->regs[SP_RD_LEN_REG] = 0xff8;
    }
    else
    {
        for(j=0; j<count; j++) {
            if (dramaddr < 0x800000)
                pre_framebuffer_read(&sp->dp->fb, dramaddr);

            for(i=0; i<length; i++) {
                spmem[(memaddr^S8) & 0xfff] = dram[(dramaddr^S8) & 0x7fffff];
                memaddr++;
                dramaddr++;
            }
            dramaddr+=skip;
        }

        sp->regs[SP_MEM_ADDR_REG] = memaddr & 0xfff;
        sp->regs[SP_DRAM_ADDR_REG] = dramaddr & 0xffffff;
        sp->regs[SP_RD_LEN_REG] = 0xff8;
    }

    /* schedule end of dma event */
    cp0_update_count(sp->mi->r4300);
    add_interrupt_event(&sp->mi->r4300->cp0, RSP_DMA_EVT, (count * length) / 8);
}

static void fifo_push(struct rsp_core* sp, uint32_t dir)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        DebugMessage(M64MSG_WARNING, "RSP DMA attempted but FIFO queue already full.");
        return;
    }

    if (sp->regs[SP_DMA_BUSY_REG])
    {
        sp->fifo[1].dir = dir;
        sp->fifo[1].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[1].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[1].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_FULL_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_FULL;
    }
    else
    {
        sp->fifo[0].dir = dir;
        sp->fifo[0].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[0].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[0].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_BUSY_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_BUSY;

        do_sp_dma(sp, &sp->fifo[0]);
    }
}

static void fifo_pop(struct rsp_core* sp)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        sp->fifo[0].dir = sp->fifo[1].dir;
        sp->fifo[0].length = sp->fifo[1].length;
        sp->fifo[0].memaddr = sp->fifo[1].memaddr;
        sp->fifo[0].dramaddr = sp->fifo[1].dramaddr;
        sp->regs[SP_DMA_FULL_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_FULL;

        do_sp_dma(sp, &sp->fifo[0]);
    }
    else
    {
        sp->regs[SP_DMA_BUSY_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_BUSY;
    }
}

static void update_sp_status(struct rsp_core* sp, uint32_t w)
{
    /* clear / set halt */
    if ((w & 0x3) == 0x1) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_HALT;
    if ((w & 0x3) == 0x2) sp->regs[SP_STATUS_REG] |= SP_STATUS_HALT;

    /* clear broke */
    if (w & 0x4) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_BROKE;

    /* clear SP interrupt */
    if ((w & 0x18) == 0x8)
    {
        clear_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
    /* set SP interrupt */
    if ((w & 0x18) == 0x10)
    {
        signal_rcp_interrupt(sp->mi, MI_INTR_SP);
    }

    /* clear / set single step */
    if ((w & 0x60) == 0x20) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SSTEP;
    if ((w & 0x60) == 0x40) sp->regs[SP_STATUS_REG] |= SP_STATUS_SSTEP;

    /* clear / set interrupt on break */
    if ((w & 0x180) == 0x80) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_INTR_BREAK;
    if ((w & 0x180) == 0x100) sp->regs[SP_STATUS_REG] |= SP_STATUS_INTR_BREAK;

    /* clear / set signal 0 */
    if ((w & 0x600) == 0x200) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG0;
    if ((w & 0x600) == 0x400) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG0;

    /* clear / set signal 1 */
    if ((w & 0x1800) == 0x800) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG1;
    if ((w & 0x1800) == 0x1000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG1;

    /* clear / set signal 2 */
    if ((w & 0x6000) == 0x2000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG2;
    if ((w & 0x6000) == 0x4000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG2;

    /* clear / set signal 3 */
    if ((w & 0x18000) == 0x8000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG3;
    if ((w & 0x18000) == 0x10000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG3;

    /* clear / set signal 4 */
    if ((w & 0x60000) == 0x20000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG4;
    if ((w & 0x60000) == 0x40000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG4;

    /* clear / set signal 5 */
    if ((w & 0x180000) == 0x80000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG5;
    if ((w & 0x180000) == 0x100000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG5;

    /* clear / set signal 6 */
    if ((w & 0x600000) == 0x200000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG6;
    if ((w & 0x600000) == 0x400000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG6;

    /* clear / set signal 7 */
    if ((w & 0x1800000) == 0x800000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG7;
    if ((w & 0x1800000) == 0x1000000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG7;

    if (sp->rsp_task_locked && (get_event(&sp->mi->r4300->cp0.q, SP_INT))) return;
    if (!((w & 0x3) == 1) && !(w & 0x4) && !sp->rsp_task_locked)
        return;

    if (!(sp->regs[SP_STATUS_REG] & SP_STATUS_HALT))
        do_SP_Task(sp);
}

void init_rsp(struct rsp_core* sp,
              uint32_t* sp_mem,
              struct mi_controller* mi,
              struct rdp_core* dp,
              struct ri_controller* ri)
{
    sp->mem = sp_mem;
    sp->mi = mi;
    sp->dp = dp;
    sp->ri = ri;
}

void poweron_rsp(struct rsp_core* sp)
{
    memset(sp->mem, 0, SP_MEM_SIZE);
    memset(sp->regs, 0, SP_REGS_COUNT*sizeof(uint32_t));
    memset(sp->regs2, 0, SP_REGS2_COUNT*sizeof(uint32_t));
    memset(sp->fifo, 0, SP_DMA_FIFO_SIZE*sizeof(struct sp_dma));

    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    sp->regs[SP_STATUS_REG] = 1;
    sp->regs[SP_RD_LEN_REG] = 0xff8;
    sp->regs[SP_WR_LEN_REG] = 0xff8;
}


void read_rsp_mem(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    *value = sp->mem[addr];
}

void write_rsp_mem(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    masked_write(&sp->mem[addr], value, mask);
}


void read_rsp_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    *value = sp->regs[reg];

    if (reg == SP_SEMAPHORE_REG)
    {
        sp->regs[SP_SEMAPHORE_REG] = 1;
    }
}

void write_rsp_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    switch(reg)
    {
    case SP_STATUS_REG:
        update_sp_status(sp, value & mask);
    case SP_DMA_FULL_REG:
    case SP_DMA_BUSY_REG:
        return;
    }

    masked_write(&sp->regs[reg], value, mask);

    switch(reg)
    {
    case SP_RD_LEN_REG:
        fifo_push(sp, SP_DMA_WRITE);
        break;
    case SP_WR_LEN_REG:
        fifo_push(sp, SP_DMA_READ);
        break;
    case SP_SEMAPHORE_REG:
        sp->regs[SP_SEMAPHORE_REG] = 0;
        break;
    }
}


void read_rsp_regs2(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    *value = sp->regs2[reg];

    if (reg == SP_PC_REG)
        *value &= 0xffc;

}

void write_rsp_regs2(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    if (reg == SP_PC_REG)
        mask &= 0xffc;

    masked_write(&sp->regs2[reg], value, mask);
}

static int l_zb_shadow_armed = 1;
static unsigned l_zb_fresh_n;

void do_SP_Task(struct rsp_core* sp)
{
    uint32_t save_pc = sp->regs2[SP_PC_REG] & ~0xfff;

    uint32_t sp_delay_time;

    if (sp->mem[0xfc0/4] == 1)
    {
        {
            static unsigned g1;
            const char *gc = getenv("HARNESS_GFX_CAP");
            if (gc && g1 == (unsigned)strtoul(gc, NULL, 0)) {
                const char *fn = getenv("HARNESS_GFX_CAP_FILE");
                FILE *f = fopen(fn ? fn : "/tmp/gfx_rdram.bin", "wb");
                if (f) { fwrite(sp->ri->rdram->dram, 1, 0x800000, f);
                         fwrite(sp->mem, 1, 0x2000, f); fclose(f); }
            }
            g1++;
        }
        unprotect_framebuffers(&sp->dp->fb);

        /* zboss shadow: on each fresh BOSS graphics task under the LLE,
         * run the HLE walker read-only over the same state and dump its
         * stream for oracle comparison (env ZB_SHADOW). Freshness =
         * first gfx dispatch after the previous task broke. */
        if (getenv("ZB_SHADOW") && l_zb_shadow_armed)
        {
            extern int angrylion_zboss_shadow(unsigned char *rdram,
                                              unsigned int rdram_size,
                                              unsigned char *dmem);
            unsigned want = (unsigned)strtoul(getenv("ZB_SHADOW"), NULL, 0);
            l_zb_shadow_armed = 0;
            l_zb_fresh_n++;
            if ((want == 0 || l_zb_fresh_n == want)
                && !getenv("ZB_SHADOW_END"))
                angrylion_zboss_shadow((unsigned char*)sp->ri->rdram->dram,
                                       0x800000u,
                                       (unsigned char*)sp->mem);
        }

        //gfx.processDList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_GFX);
#endif
        rsp.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_GFX);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;
        new_frame();

        if (sp->mi->regs[MI_INTR_REG] & MI_INTR_DP)
        {
            sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
            if (sp->dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE) {
                sp->dp->do_on_unfreeze |= DELAY_DP_INT;
            } else {
                /* Defer the DP interrupt past the submitting task:
                 * BOSS Game Studios titles measure the interval during
                 * boot (three samples against a floor around 0x2000
                 * count units) and leave the 3D renderer unregistered
                 * when the RDP looks impossibly fast, which a same
                 * cycle raise does.
                 *
                 * The interval stays fixed. Scaling it by the emitted
                 * byte count -- which this modelled once -- reads well
                 * but measures badly: the intro movie drives a
                 * persistent streaming task whose per frame handshakes
                 * the CPU paces, and stretching the completion of the
                 * large submissions around it moved the microcode's
                 * park and release off the CPU's cadence. Sampling the
                 * task dispatches per field across the movie handover
                 * (World Driver Championship, fields 256 to 260)
                 * against the interpreter puts the scaled model at
                 * 803/808/610 where the reference runs 802/805/740,
                 * and the field the reference spends entirely in the
                 * server's spin does not happen at all; the fixed
                 * interval reproduces 802/805/740 and the game keeps
                 * feeding the movie instead of dropping into the
                 * garbage submission that followed. */
                cp0_update_count(sp->mi->r4300);
                add_interrupt_event(&sp->mi->r4300->cp0, DP_INT, 4000);
            }
        }
        sp_delay_time = 1000;

        protect_framebuffers(&sp->dp->fb);
    }
    else if (sp->mem[0xfc0/4] == 2)
    {
        //audio.processAList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_AUDIO);
#endif
        rsp_audio.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_AUDIO);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 4000;
    }
    else
    {
        sp->regs2[SP_PC_REG] &= 0xfff;
        rsp.doRspCycles(0xffffffff);
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 0;
    }

    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0)
    {
        /* Incomplete (streaming) task: schedule the self-sustaining pump
         * event directly instead of riding MI_INTR_SP as its carrier.
         * Consuming the register bit here swallowed any interrupt the
         * task itself raised mid-flight: the BOSS Game Studios
         * microcode's WAITSIGNAL handshake (World Driver Championship,
         * Stunt Racer 64) raises SIG3 plus the SP interrupt and then
         * suspends until the CPU clears the signal. With the bit
         * cleared before the CPU's handler could read MI_INTR_REG the
         * handler found no interrupt source, SIG3 was never cleared,
         * every game thread ended up parked in osRecvMesg, and the race
         * transition sat on a black screen -- under the HLE walker and
         * the LLE fallback alike (the microcode's own status write
         * lands in update_sp_status and was consumed the same way).
         * A task-raised MI_INTR_SP now stays asserted for the CPU to
         * service while the pump keeps re-dispatching the task. */
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        cp0_update_count(sp->mi->r4300);
        add_interrupt_event(&sp->mi->r4300->cp0, SP_INT, sp_delay_time);
    }
    else if (sp->mi->regs[MI_INTR_REG] & MI_INTR_SP)
    {
        cp0_update_count(sp->mi->r4300);
        /* Leave MI_INTR_SP asserted for the CPU's handler to find.
         * Consuming it here scheduled the event and then removed the
         * only evidence of where it came from, so a handler that
         * dispatches on MI_INTR_REG - as libdragon's does - saw an
         * interrupt with no source and ignored it.  Its rspq syncpoints
         * are signalled exactly this way, so every wait on one timed
         * out.  The incomplete-task branch above already keeps the bit
         * for the same reason; the completing branch has to as well.
         * The CPU clears it by writing SP_STATUS with SP_CLR_INTR,
         * which update_sp_status already handles. */
        add_interrupt_event(&sp->mi->r4300->cp0, SP_INT, sp_delay_time);
    }
    if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)))
    {
        if (getenv("ZB_SHADOW_END") && !l_zb_shadow_armed
            && sp->mem[0xfc0/4] == 1
            && l_zb_fresh_n == (unsigned)strtoul(getenv("ZB_SHADOW_END"),
                                                 NULL, 0))
        {
            extern int angrylion_zboss_shadow(unsigned char *rdram,
                                              unsigned int rdram_size,
                                              unsigned char *dmem);
            angrylion_zboss_shadow((unsigned char*)sp->ri->rdram->dram,
                                   0x800000u,
                                   (unsigned char*)sp->mem);
        }
        l_zb_shadow_armed = 1;
    }

    sp->regs[SP_STATUS_REG] &=
        ~(SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT);
}

void rsp_interrupt_event(void* opaque)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;

    if (sp->rsp_task_locked)
    {
        /* An incomplete (streaming) task is still in flight. The real
         * RSP keeps executing on its own; the HLE equivalent only runs
         * when dispatched, and a CPU that has finished feeding the
         * stream just sleeps on the task-done interrupt without ever
         * writing SP_STATUS again -- so without a self-sustaining
         * pump the task would never be serviced again and the system
         * would deadlock. Re-dispatch it from here: each incomplete
         * return schedules another SP_INT, so the task keeps getting
         * time slices until it completes for real. */
        do_SP_Task(sp);
        return;
    }

    if (!sp->rsp_task_locked)
    {
        sp->regs[SP_STATUS_REG] |=
            SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT;
    }

    if ((sp->regs[SP_STATUS_REG] & SP_STATUS_INTR_BREAK) != 0)
    {
        raise_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
}

void rsp_end_of_dma_event(void* opaque)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    fifo_pop(sp);
}
