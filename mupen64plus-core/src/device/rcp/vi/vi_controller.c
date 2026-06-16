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

#include "../../../main/main.h"
#include "../../../main/device.h"
#include "../../../main/rom.h"
#include "../../memory/memory.h"
#include "../../../plugin/core_plugin.h"
#include "../../r4300/r4300_core.h"
#include "../../r4300/interrupt.h"
#include "../rdp/rdp_core.h"

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

           /* Wrap VI_CURRENT into [0, VI_V_SYNC) as real hardware does and as
            * mupen64plus-next does; a game polling VI_CURRENT in a raster wait
            * loop expects the line counter to stay within the field height
            * rather than run past it. */
           if (vi->regs[VI_V_SYNC_REG] != 0
               && vi->regs[VI_CURRENT_REG] >= vi->regs[VI_V_SYNC_REG])
              vi->regs[VI_CURRENT_REG] -= vi->regs[VI_V_SYNC_REG];
        }
        vi->regs[VI_CURRENT_REG] = (vi->regs[VI_CURRENT_REG] & (~1)) | vi->field;
    }

    *word = (reg < VI_REGS_COUNT) ? vi->regs[reg] : 0u;

    return 0;
}

/* Writes a word to the VI MMIO register space. */
/* Arm the VI interrupt event when (and only when) the game has configured a valid
 * VI_V_INTR compare line within the field (V_INTR < V_SYNC) and no VI event is already
 * queued. Mirrors mupen64plus-next's set_vi_vertical_interrupt. This is what defers the
 * first VI until the game sets up the VI -- avoiding the spurious early boot VIs that the
 * old unconditional power-on arm produced. */
static void set_vi_vertical_interrupt(struct vi_controller* vi)
{
   if (get_event(VI_INT) == NULL
       && (vi->regs[VI_V_INTR_REG] < vi->regs[VI_V_SYNC_REG]))
   {
      unsigned int cps;
      cp0_update_count();
      if (vi->regs[VI_V_SYNC_REG] == 0)
         vi->delay = 500000;
      else
      {
         cps = (vi->clock / vi->expected_refresh_rate) / (vi->regs[VI_V_SYNC_REG] + 1);
         vi->delay = (vi->regs[VI_V_SYNC_REG] + 1) * cps;
      }
      vi->next_vi = r4300_cp0_regs()[CP0_COUNT_REG] + vi->delay;
      add_interrupt_event_count(VI_INT, vi->next_vi);
   }
}

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

       case VI_V_SYNC_REG:
          vi->regs[VI_V_SYNC_REG] = MASKED_WRITE(&vi->regs[VI_V_SYNC_REG], word, mask);
          set_vi_vertical_interrupt(vi);
          return 0;

       case VI_V_INTR_REG:
          vi->regs[VI_V_INTR_REG] = MASKED_WRITE(&vi->regs[VI_V_INTR_REG], word, mask);
          set_vi_vertical_interrupt(vi);
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

   /* Raise the VI interrupt every field, matching mupen64plus-next and
    * hardware. An earlier revision gated this on (VI_V_INTR < VI_V_SYNC)
    * to suppress supposedly-spurious interrupts, but that is wrong: when a
    * game parks VI_V_INTR at or beyond VI_V_SYNC (a normal idiom, e.g.
    * Excitebike 64) the gate silently drops the entire VI interrupt
    * stream. Because VI_INT is also this core's frame-loop heartbeat, that
    * stalls rendering rather than merely dropping a vblank. The MI mask
    * (raise_rcp_interrupt checks MI_INTR_MASK_REG) is the correct place
    * for a game to suppress the interrupt if it wants to. */
   raise_rcp_interrupt(vi->r4300, MI_INTR_VI);
}
