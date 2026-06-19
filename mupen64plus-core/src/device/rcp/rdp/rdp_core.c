/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rdp_core.c                                              *
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

#include "rdp_core.h"

#include "../../memory/m64p_memory.h"
#include "../../../plugin/core_plugin.h"
#include "../../r4300/r4300_core.h"
#include "../rsp/rsp_core.h"

#include <string.h>

static void update_dpc_status(struct rdp_core* dp, uint32_t w)
{
   /* clear / set xbus_dmem_dma */
   if (w & DPC_STATUS_CLR_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_XBUS_DMEM_DMA;
   if (w & DPC_STATUS_SET_XBUS_DMEM_DMA) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_XBUS_DMEM_DMA;

   /* clear / set freeze */
   if (w & DPC_STATUS_CLR_FREEZE)
   {
      dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FREEZE;

      /* flush work that was deferred while DP was frozen.
       * See do_SP_Task and vi_vertical_interrupt_event for the
       * stalls that set these flags. */
      if (dp->do_on_unfreeze & DELAY_DP_INT)
         signal_rcp_interrupt(dp->mi, MI_INTR_DP);
      if (dp->do_on_unfreeze & DELAY_UPDATESCREEN)
      {
         /* The plugin latches the deferred frame; it is presented at
          * the end of the slice ended by the VI interrupt. */
         gfx.updateScreen();
      }
      dp->do_on_unfreeze = 0;
   }

   if (w & DPC_STATUS_SET_FREEZE) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FREEZE;

   /* clear / set flush */
   if (w & DPC_STATUS_CLR_FLUSH) dp->dpc_regs[DPC_STATUS_REG] &= ~DPC_STATUS_FLUSH;
   if (w & DPC_STATUS_SET_FLUSH) dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_FLUSH;

   /* clear hardware counters */
   if (w & DPC_STATUS_CLR_TMEM_CTR)  dp->dpc_regs[DPC_TMEM_REG]     = 0;
   if (w & DPC_STATUS_CLR_PIPE_CTR)  dp->dpc_regs[DPC_PIPEBUSY_REG] = 0;
   if (w & DPC_STATUS_CLR_CMD_CTR)   dp->dpc_regs[DPC_BUFBUSY_REG]  = 0;
   if (w & DPC_STATUS_CLR_CLOCK_CTR) dp->dpc_regs[DPC_CLOCK_REG]    = 0;
}


void init_rdp(struct rdp_core* dp,
                 struct mi_controller* mi,
                 struct rsp_core* sp,
                 struct ri_controller *ri)
{
    dp->mi = mi;
    dp->sp    = sp;
    dp->ri    = ri;
}

void poweron_rdp(struct rdp_core* dp)
{
    memset(dp->dpc_regs, 0, DPC_REGS_COUNT*sizeof(uint32_t));
    memset(dp->dps_regs, 0, DPS_REGS_COUNT*sizeof(uint32_t));
    dp->dpc_regs[DPC_STATUS_REG] |= DPC_STATUS_START_GCLK;
    dp->do_on_unfreeze = 0;

    poweron_fb(&dp->fb);
}


int read_dpc_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg        = DPC_REG(address);

    *value              = (reg < DPC_REGS_COUNT) ? dp->dpc_regs[reg] : 0u;

    return 0;
}

int write_dpc_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
   struct rdp_core* dp = (struct rdp_core*)opaque;
   uint32_t reg        = DPC_REG(address);

   switch(reg)
   {
      case DPC_STATUS_REG:
         update_dpc_status(dp, value & mask);
      case DPC_CURRENT_REG:
      case DPC_CLOCK_REG:
      case DPC_BUFBUSY_REG:
      case DPC_PIPEBUSY_REG:
      case DPC_TMEM_REG:
         return 0;
   }

   if (reg < DPC_REGS_COUNT)
       dp->dpc_regs[reg] = MASKED_WRITE(&dp->dpc_regs[reg], value, mask);

   switch(reg)
   {
      case DPC_START_REG:
         dp->dpc_regs[DPC_CURRENT_REG] = dp->dpc_regs[DPC_START_REG];
         break;
      case DPC_END_REG:
         unprotect_framebuffers(dp);
         gfx.processRDPList();
         protect_framebuffers(dp);
         signal_rcp_interrupt(dp->mi, MI_INTR_DP);
         break;
   }

   return 0;
}


int read_dps_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg        = DPS_REG(address);

    *value = (reg < DPS_REGS_COUNT) ? dp->dps_regs[reg] : 0u;

    return 0;
}

int write_dps_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdp_core* dp = (struct rdp_core*)opaque;
    uint32_t reg        = DPS_REG(address);

    if (reg < DPS_REGS_COUNT)
        dp->dps_regs[reg] = MASKED_WRITE(&dp->dps_regs[reg], value, mask);

    return 0;
}

void rdp_interrupt_event(struct rdp_core* dp)
{
   raise_rcp_interrupt(dp->mi, MI_INTR_DP);
}
