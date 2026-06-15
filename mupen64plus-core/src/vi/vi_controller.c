/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - vi_controller.c                                         *
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

#include "vi_controller.h"

#include "main/main.h"
#include "main/device.h"
#include "main/rom.h"
#include "memory/memory.h"
#include "plugin/plugin.h"
#include "r4300/r4300_core.h"
#include "r4300/interrupt.h"
#include "rdp/rdp_core.h"

#include <string.h>

/* XXX: timing hacks */
enum { DEFAULT_CPU_COUNT_PER_SCANLINE = 1500 };
enum { NTSC_VERTICAL_RESOLUTION = 525 };

extern unsigned alternate_vi_timing;

void init_vi(struct vi_controller* vi,
      unsigned int clock, unsigned int expected_refresh_rate,
      /* unsigned int count_per_scanline, unsigned int alternate_timing, */
      struct r4300_core* r4300)
{
   vi->clock = clock;
   vi->expected_refresh_rate = expected_refresh_rate;
#if 0
   vi->count_per_scanline    = count_per_scanline;
   vi->alternate_timing      = alternate_timing;
#endif
   vi->r4300 = r4300;
}

unsigned int vi_clock_from_tv_standard(m64p_system_type tv_standard)
{
   switch(tv_standard)
   {
      case SYSTEM_PAL:
         return 49656530;
      case SYSTEM_MPAL:
         return 48628316;
      case SYSTEM_NTSC:
      default:
         return 48681812;
   }
}

unsigned int vi_expected_refresh_rate_from_tv_standard(m64p_system_type tv_standard)
{
   switch (tv_standard)
   {
      case SYSTEM_PAL:
         return 50;

      case SYSTEM_MPAL:
      case SYSTEM_NTSC:
      default:
         return 60;
   }
}

/* Initializes the VI. */
void poweron_vi(struct vi_controller* vi)
{
   memset(vi->regs, 0, VI_REGS_COUNT*sizeof(uint32_t));

   vi->field = 0;
   vi->delay = vi->next_vi = 5000;
}

/* Reads a word from the VI MMIO register space. */
int read_vi_regs(void* opaque, uint32_t address, uint32_t *word)
{
    struct vi_controller* vi = (struct vi_controller*)opaque;
    uint32_t             reg = VI_REG(address);
    const uint32_t* cp0_regs = r4300_cp0_regs();

    if (reg == VI_CURRENT_REG)
    {
        uint32_t until_next;
        uint32_t elapsed;

        cp0_update_count();

        /* Cycles elapsed within the current field. Guard the unsigned wrap
         * for the narrow window where the VI event is momentarily overdue
         * (CP0 Count has just passed next_vi but the event has not been
         * serviced yet): without this, (next_vi - Count) underflows and
         * VI_CURRENT reads back garbage for a game polling it in a
         * raster/vblank wait loop. In the normal case (Count < next_vi)
         * the value is unchanged. */
        until_next = (vi->next_vi > cp0_regs[CP0_COUNT_REG])
                   ? (vi->next_vi - cp0_regs[CP0_COUNT_REG]) : 0u;
        elapsed    = (until_next < vi->delay) ? (vi->delay - until_next) : vi->delay;

        /* alternate_vi_timing is off by default; its line mapping is an
         * approximation (elapsed cycles folded into the field height). */
        if (alternate_vi_timing)
           vi->regs[VI_CURRENT_REG] = elapsed % (NTSC_VERTICAL_RESOLUTION + 1);
        else
        {
           unsigned int cps = (vi->regs[VI_V_SYNC_REG] != 0)
              ? (vi->clock / vi->expected_refresh_rate)
                 / (vi->regs[VI_V_SYNC_REG] + 1)
              : (unsigned int)g_count_per_scanline;
           vi->regs[VI_CURRENT_REG] = elapsed / cps;
        }
        vi->regs[VI_CURRENT_REG] = (vi->regs[VI_CURRENT_REG] & (~1)) | vi->field;
    }

    *word = (reg < VI_REGS_COUNT) ? vi->regs[reg] : 0u;

    return 0;
}

/* Writes a word to the VI MMIO register space. */
int write_vi_regs(void* opaque, uint32_t address,
      uint32_t word, uint32_t mask)
{
    struct vi_controller* vi = (struct vi_controller*)opaque;
    uint32_t reg             = VI_REG(address);

    switch (reg)
    {
       case VI_STATUS_REG:
          if ((vi->regs[VI_STATUS_REG] & mask) != (word & mask))
          {
             vi->regs[VI_STATUS_REG] = MASKED_WRITE(&vi->regs[VI_STATUS_REG], word, mask);
             gfx.viStatusChanged();
          }
          return 0;

       case VI_WIDTH_REG:
          if ((vi->regs[VI_WIDTH_REG] & mask) != (word & mask))
          {
             vi->regs[VI_WIDTH_REG] = MASKED_WRITE(&vi->regs[VI_WIDTH_REG], word, mask);
             gfx.viWidthChanged();
          }
          return 0;

       case VI_CURRENT_REG:
          clear_rcp_interrupt(vi->r4300, MI_INTR_VI);
          return 0;
    }

    if (reg < VI_REGS_COUNT)
        vi->regs[reg] = MASKED_WRITE(&vi->regs[reg], word, mask);

    return 0;
}

void vi_vertical_interrupt_event(struct vi_controller* vi)
{
   /* Re-arm the next VI event FIRST, before any of the per-frame work
    * below. The VI interrupt is the sole frame boundary for the libretro
    * frame loop -- interrupt.c calls retro_return(false) right after this
    * handler -- so the next retro_run can only terminate once another
    * VI_INT is queued. Scheduling it up front keeps that heartbeat
    * guaranteed even if the screen-update / cheat / input path below were
    * ever changed to bail out early. The target cycle depends only on
    * V_SYNC and g_count_per_scanline (cheats touch RDRAM, never VI MMIO),
    * so this is behaviourally identical to scheduling it last. */
   if (vi->regs[VI_V_SYNC_REG] == 0)
      vi->delay = 500000;
   else
   {
      /* Derive cycles-per-scanline from the VI clock and refresh rate, as
       * mupen64plus-next and cen64 do, rather than a fixed 1500. At the
       * usual V_SYNC=525 this yields 1542 (= 48681812 / 60 / 526), so the
       * VI period is 526*1542 = 811092 cycles instead of 526*1500 = 789000,
       * matching real NTSC hardware (~2.7% correction). */
      unsigned int count_per_scanline =
         (vi->clock / vi->expected_refresh_rate)
         / (vi->regs[VI_V_SYNC_REG] + 1);
      vi->delay = (vi->regs[VI_V_SYNC_REG] + 1) * count_per_scanline;
   }

   vi->next_vi += vi->delay;
   add_interrupt_event_count(VI_INT, vi->next_vi);

   /* Present the completed frame. If the DP is frozen with a pending
    * deferred DP interrupt, hold off the screen update too -- letting it
    * fire before the DP interrupt that produced its contents would show a
    * half-rendered frame (DK64 / Banjo-Kazooie boot logos are the classic
    * repros). The deferred update is flushed by update_dpc_status() when
    * CLR_FREEZE is written, paired with the deferred DP_INT. */
   if (g_dev.dp.do_on_unfreeze & DELAY_DP_INT)
      g_dev.dp.do_on_unfreeze |= DELAY_UPDATESCREEN;
   else
      gfx.updateScreen();

   /* per-frame main-module work: apply cheats and poll input (the single
    * libretro poll_cb() for this retro_run) */
   main_on_vi_event();

   /* toggle vi field if in interlaced mode */
   vi->field ^= (vi->regs[VI_STATUS_REG] >> 6) & 0x1;

   /* Raise the VI interrupt only when the half-line compare target
    * (VI_V_INTR) falls within the field. A game that parks VI_V_INTR at
    * or beyond VI_V_SYNC does so precisely to suppress the interrupt --
    * the compared scanline is never reached -- so firing it anyway feeds
    * the game spurious VI interrupts. The per-field reschedule above is
    * left unconditional: VI_INT is also this core's frame-loop heartbeat,
    * so it must keep arriving even when the interrupt itself is masked
    * out this way. */
   if (vi->regs[VI_V_INTR_REG] < vi->regs[VI_V_SYNC_REG])
      raise_rcp_interrupt(vi->r4300, MI_INTR_VI);
}
