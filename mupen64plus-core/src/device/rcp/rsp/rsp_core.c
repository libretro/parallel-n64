/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rsp_core.c                                              *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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

#include "rsp_core.h"

#include "../../../main/main.h"
#include "../../device.h"
#include "../../../main/rom.h"
#include "../../memory/m64p_memory.h"
#include "../../../plugin/core_plugin.h"
#include "../../r4300/r4300_core.h"
#include "../rdp/rdp_core.h"
#include "../rdp/fb.h"
#include "../ri/ri_controller.h"
#include "../../rdram/safe_rdram.h"

#include <stdio.h>
#include <string.h>

static void do_sp_dma(struct rsp_core* sp, const struct sp_dma* dma)
{
    unsigned int i,j;

    unsigned int l = dma->length;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count  = ((l >> 12) & 0xff) + 1;
    unsigned int skip   = ((l >> 20) & 0xfff);

    unsigned int memaddr  = dma->memaddr  & 0xff8;
    unsigned int dramaddr = dma->dramaddr & 0xfffff8;

    unsigned char *spmem = (unsigned char*)sp->mem + (dma->memaddr & 0x1000);
    unsigned char *dram  = (unsigned char*)sp->ri->rdram->dram;

    if (dma->dir == SP_DMA_WRITE)
    {
        /* RDRAM -> SP memory */
        for(j = 0; j < count; j++)
        {
            pre_framebuffer_dma_read(&g_dev.dp.fb, dramaddr, length);

            for(i = 0; i < length; i++)
            {
                spmem[(memaddr^S8) & 0xfffu] = rdram_safe_read_byte(dram, dramaddr^S8);
                memaddr++;
                dramaddr++;
            }
            dramaddr += skip;
        }
    }
    else
    {
        /* SP memory -> RDRAM */
        for(j = 0; j < count; j++)
        {
            for(i = 0; i < length; i++)
            {
                rdram_safe_write_byte(dram, dramaddr^S8, spmem[(memaddr^S8) & 0xfffu]);
                memaddr++;
                dramaddr++;
            }
            post_framebuffer_dma_write(&g_dev.dp.fb, dramaddr - length, length);
            dramaddr += skip;
        }
    }

    sp->regs[SP_MEM_ADDR_REG]  = memaddr  & 0xfff;
    sp->regs[SP_DRAM_ADDR_REG] = dramaddr & 0xffffff;
    sp->regs[SP_RD_LEN_REG]    = 0xff8;

    /* schedule end of dma event (hardware-accurate completion latency) */
    cp0_update_count();
    add_interrupt_event(RSP_DMA_EVT, (count * length) / 8);
}

/* Queue a DMA into the 2-deep RSP DMA FIFO. The first DMA starts
 * immediately; a DMA requested while one is in flight is queued and runs
 * when the in-flight one completes (fifo_pop, from the RSP_DMA_EVT handler).
 *
 * dir is the FIFO direction; note the N64 quirk that SP_RD_LEN writes are
 * RDRAM->SPMEM (SP_DMA_WRITE here) and SP_WR_LEN writes are SPMEM->RDRAM
 * (SP_DMA_READ here), matching mupen64plus-next's naming. */
static void fifo_push(struct rsp_core* sp, uint32_t dir)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        /* FIFO already holds a queued DMA; the hardware cannot accept a
         * third, so drop this request. */
        return;
    }

    if (sp->regs[SP_DMA_BUSY_REG])
    {
        sp->fifo[1].dir      = dir;
        sp->fifo[1].length   = (dir == SP_DMA_WRITE) ? sp->regs[SP_RD_LEN_REG] : sp->regs[SP_WR_LEN_REG];
        sp->fifo[1].memaddr  = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[1].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_FULL_REG]  = 1;
        sp->regs[SP_STATUS_REG]   |= SP_STATUS_DMA_FULL;
    }
    else
    {
        sp->fifo[0].dir      = dir;
        sp->fifo[0].length   = (dir == SP_DMA_WRITE) ? sp->regs[SP_RD_LEN_REG] : sp->regs[SP_WR_LEN_REG];
        sp->fifo[0].memaddr  = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[0].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_BUSY_REG]  = 1;
        sp->regs[SP_STATUS_REG]   |= SP_STATUS_DMA_BUSY;

        do_sp_dma(sp, &sp->fifo[0]);
    }
}

static void fifo_pop(struct rsp_core* sp)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        sp->fifo[0].dir      = sp->fifo[1].dir;
        sp->fifo[0].length   = sp->fifo[1].length;
        sp->fifo[0].memaddr  = sp->fifo[1].memaddr;
        sp->fifo[0].dramaddr = sp->fifo[1].dramaddr;
        sp->regs[SP_DMA_FULL_REG]  = 0;
        sp->regs[SP_STATUS_REG]   &= ~SP_STATUS_DMA_FULL;

        do_sp_dma(sp, &sp->fifo[0]);
    }
    else
    {
        sp->regs[SP_DMA_BUSY_REG]  = 0;
        sp->regs[SP_STATUS_REG]   &= ~SP_STATUS_DMA_BUSY;
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
       clear_rcp_interrupt(sp->mi, MI_INTR_SP);

    /* set SP interrupt */
    if ((w & 0x18) == 0x10)
       signal_rcp_interrupt(sp->mi, MI_INTR_SP);

    /* clear / set single step */
    if ((w & 0x60) == 0x20) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SSTEP;
    if ((w & 0x60) == 0x40) sp->regs[SP_STATUS_REG] |= SP_STATUS_SSTEP;

    /* clear / set interrupt on break */
    if ((w & 0x180) == 0x80) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_INTR_BREAK;
    if ((w & 0x180) == 0x100) sp->regs[SP_STATUS_REG] |= SP_STATUS_INTR_BREAK;

    /* clear / set signal 0 */
    if ((w & 0x600) == 0x200) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG0;
    if ((w & 0x600) == 0x400)
    {
         sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG0;
         if (sp->audio_signal)
             signal_rcp_interrupt(sp->mi, MI_INTR_SP);
     }

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

    if (sp->rsp_task_locked && (get_event(SP_INT))) return;
    if (!((w & 0x3) == 1) && !(w & 0x4) && !sp->rsp_task_locked)
        return;

    if (!(sp->regs[SP_STATUS_REG] & SP_STATUS_HALT))
        do_SP_Task(sp);
}

void init_rsp(struct rsp_core* sp,
                 struct mi_controller* mi,
                 struct rdp_core* dp,
                 struct ri_controller* ri,
		 uint32_t audio_signal)
{
    sp->mi        = mi;
    sp->dp           = dp;
    sp->ri           = ri;
    sp->audio_signal = audio_signal;
}

void poweron_rsp(struct rsp_core* sp)
{
    memset(sp->mem, 0, SP_MEM_SIZE);
    memset(sp->regs, 0, SP_REGS_COUNT*sizeof(uint32_t));
    memset(sp->regs2, 0, SP_REGS2_COUNT*sizeof(uint32_t));
    memset(sp->fifo, 0, SP_DMA_FIFO_SIZE*sizeof(struct sp_dma));

    sp->rsp_task_locked     = 0;
    sp->regs[SP_STATUS_REG] = 1;
    sp->regs[SP_RD_LEN_REG] = 0xff8;
    sp->regs[SP_WR_LEN_REG] = 0xff8;
}


int read_rsp_mem(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr       = RSP_MEM_ADDR(address);

    *value = sp->mem[addr];

    return 0;
}

int write_rsp_mem(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr       = RSP_MEM_ADDR(address);

    sp->mem[addr] = MASKED_WRITE(&sp->mem[addr], value, mask);

    return 0;
}


int read_rsp_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg        = RSP_REG(address);

    *value = (reg < SP_REGS_COUNT) ? sp->regs[reg] : 0u;

    if (reg == SP_SEMAPHORE_REG)
        sp->regs[SP_SEMAPHORE_REG] = 1;

    return 0;
}

int write_rsp_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct rsp_core* sp = (struct rsp_core*)opaque;
   uint32_t reg        = RSP_REG(address);

    switch(reg)
    {
       case SP_STATUS_REG:
          update_sp_status(sp, value & mask);
          return 0;
       case SP_DMA_FULL_REG:
       case SP_DMA_BUSY_REG:
          return 0;
    }

    if (reg < SP_REGS_COUNT)
        sp->regs[reg] = MASKED_WRITE(&sp->regs[reg], value, mask);

    switch(reg)
    {
       case SP_RD_LEN_REG:
          /* RDRAM -> SP memory; length decode happens in do_sp_dma */
          fifo_push(sp, SP_DMA_WRITE);
          break;
       case SP_WR_LEN_REG:
          /* SP memory -> RDRAM */
          fifo_push(sp, SP_DMA_READ);
          break;
       case SP_SEMAPHORE_REG:
          sp->regs[SP_SEMAPHORE_REG] = 0;
          break;
    }

    return 0;
}


int read_rsp_regs2(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg        = RSP_REG2(address);

    *value = (reg < SP_REGS2_COUNT) ? sp->regs2[reg] : 0u;

    if (reg == SP_PC_REG)
        *value &= 0xffc;

    return 0;
}

int write_rsp_regs2(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg        = RSP_REG2(address);

    if (reg == SP_PC_REG)
        mask &= 0xffc;

    if (reg < SP_REGS2_COUNT)
        sp->regs2[reg] = MASKED_WRITE(&sp->regs2[reg], value, mask);

    return 0;
}

/* forward declaration */
unsigned int hleDoRspCycles(unsigned int value);
extern uint32_t send_allist_to_hle_rsp;

void do_SP_Task(struct rsp_core* sp)
{
    uint32_t save_pc = sp->regs2[SP_PC_REG] & ~0xfff;
    uint32_t sp_delay_time = 0;

    if (sp->mem[0xfc0/4] == 1)
    {
        unprotect_framebuffers(sp->dp);

        sp->regs2[SP_PC_REG] &= 0xfff;
        rsp.doRspCycles(0xffffffff);
        sp->regs2[SP_PC_REG] |= save_pc;
        new_frame();

        if (sp->mi->regs[MI_INTR_REG] & MI_INTR_DP)
	{
	    sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
	    /* If the DP is frozen the game expects no DP interrupt
	     * to be observed until it later clears FREEZE -- holding
	     * the interrupt back avoids races between the gfx task
	     * we just ran and the game's freeze-window bookkeeping
	     * (DK64 / Banjo-Kazooie / Perfect Dark are the classic
	     * repros). The deferred interrupt fires in
	     * update_dpc_status() when CLR_FREEZE is written. */
	    if (sp->dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE)
	    {
	        sp->dp->do_on_unfreeze |= DELAY_DP_INT;
	    }
	    else
	    {
	        cp0_update_count();
	        add_interrupt_event(DP_INT, 4000);
	    }
	}

        sp_delay_time = 1000;

        protect_framebuffers(sp->dp);
    }
    else if (sp->mem[0xfc0/4] == 2)
    {
       /* Audio List */
        sp->regs2[SP_PC_REG] &= 0xfff;
        if (send_allist_to_hle_rsp == 0)
           rsp.doRspCycles(0xffffffff);
        else
        {
           /* Ensure HLE state is live before the first audio task is
            * routed to it; covers the option being toggled on at
            * runtime, not just enabled at load. Idempotent. */
           plugin_ensure_hle_audio_ready();
           hleDoRspCycles(0xffffffff);
        }
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 4000;
    }
    else
    {
       /* Unknown list */
        sp->regs2[SP_PC_REG] &= 0xfff;
        rsp.doRspCycles(0xffffffff);
        sp->regs2[SP_PC_REG] |= save_pc;
    }

    sp->rsp_task_locked = 0;
     if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0)
     {
        cp0_update_count();

	sp->rsp_task_locked = 1;
	add_interrupt_event(SP_INT, sp_delay_time);
    }
}

void rsp_interrupt_event(struct rsp_core* sp)
{
   if ((sp->regs[SP_STATUS_REG] & SP_STATUS_INTR_BREAK) != 0)
      raise_rcp_interrupt(sp->mi, MI_INTR_SP);
}

void rsp_dma_event(struct rsp_core* sp)
{
   /* An in-flight RSP DMA has completed: advance the FIFO, starting the
    * next queued transfer if one is pending. */
   fifo_pop(sp);
}
