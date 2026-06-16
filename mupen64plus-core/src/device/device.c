/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - device.c                                                *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2016 Bobby Smiles                                       *
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

#include "device.h"

#include "../backends/libretro_clock.h"

#include "rcp/ai/ai_controller.h"
#include "memory/memory.h"
#include "rcp/pi/pi_controller.h"
#include "r4300/r4300_core.h"
#include "rcp/rdp/rdp_core.h"
#include "rcp/ri/ri_controller.h"
#include "rcp/rsp/rsp_core.h"
#include "rcp/si/si_controller.h"
#include "rcp/vi/vi_controller.h"


void init_device(
      struct device *dev,
      /* r4300 */
      unsigned int emumode,
      unsigned int count_per_op,
      int special_rom,
      /* ai */
      void * ai_user_data, void (*ai_set_audio_format)(void*,unsigned int, unsigned int),
      void (*ai_push_audio_samples)(void*,const void*,size_t),
      unsigned int fixed_audio_pos,
      /* pi */
      uint8_t *rom,
      size_t rom_size,
      void* flashram_user_data, void (*flashram_save)(void*), uint8_t* flashram_data,
      void* sram_user_data, void (*sram_save)(void*), uint8_t* sram_data,
      /* ri */
      uint32_t* dram, size_t dram_size,
      /* si */
      void* eeprom_user_data, void (*eeprom_save)(void*), uint8_t* eeprom_data, size_t eeprom_size, uint32_t eeprom_id,
      void* af_rtc_user_data, const struct tm* (*af_rtc_get_time)(void*),
      /* sp */
      unsigned int audio_signal,
      /* vi */
      unsigned int vi_clock,
      unsigned int expected_refresh_rate,
      uint8_t *ddrom,
      size_t ddrom_size,
      uint8_t *dd_disk,
      size_t dd_disk_size
      )
{
   init_r4300(&dev->r4300, emumode, count_per_op, special_rom);
   dev->r4300.mi = &dev->mi;
   dev->mi.r4300 = &dev->r4300;
   init_rdp(&dev->dp, &dev->mi, &dev->sp, &dev->ri);
   init_rsp(&dev->sp, &dev->mi, &dev->dp, &dev->ri, audio_signal);
   init_ai(&dev->ai, ai_user_data,
         ai_set_audio_format,
         ai_push_audio_samples,
         &dev->mi, &dev->ri, &dev->vi,
	 fixed_audio_pos);
   dev->pi.cart = &dev->cart;
   init_pi(&dev->pi,
         rom, rom_size,
         ddrom, ddrom_size,
         flashram_user_data, flashram_save, flashram_data,
         sram_user_data, sram_save, sram_data,
         &dev->dd, &dev->mi, &dev->ri, &dev->dp);
   dev->ri.rdram = &dev->rdram;
   init_ri(&dev->ri, dram, dram_size);
   dev->si.pif = &dev->pif;
   dev->pif.cart = &dev->cart;
   init_si(&dev->si,
         eeprom_user_data,
         eeprom_save,
         eeprom_data,
         eeprom_size,
         eeprom_id,
         af_rtc_user_data,
         af_rtc_get_time,
         ((ddrom != NULL) && (ddrom_size != 0) && (rom == NULL) && (rom_size == 0)) ?
         (ddrom + 0x40) : (rom + 0x40),                       /* ipl3 */
         &dev->mi, &dev->ri);

   init_vi(&dev->vi, vi_clock, expected_refresh_rate, &dev->mi, &dev->dp);

   /* region 13: set up the 64DD disk in next's storage-backed model, then init
    * the DD controller. Only wire a disk if one was supplied. */
   if (dd_disk != NULL && dd_disk_size != 0)
   {
      unsigned int format = 0, development = 0;
      size_t offset_sys = 0, offset_id = 0, offset_ram = 0, size_ram = 0;
      uint8_t* disk_data;

      /* scan_and_expand_disk_format inspects the raw dump, reports the format
       * and the system/id/ram offsets, and returns the (possibly relocated)
       * disk buffer. For D64 it allocates a fresh expanded buffer AND frees the
       * input; for MAME/SDK it returns the input unchanged; on an unrecognised
       * dump it returns NULL. The returned pointer is what we must hand to the
       * storage backend.
       *
       * NOTE: the D64 path frees its input, so it must only ever be given a
       * malloc'd buffer. parallel-n64's libretro loader currently gates
       * open_dd_disk to MAME/SDK sizes, so D64 dumps do not reach here and the
       * input (the persistent saved_memory.disk buffer) is never freed. Enabling
       * D64 requires routing a heap-owned disk buffer through the frontend. */
      disk_data = scan_and_expand_disk_format(dd_disk, dd_disk_size,
            &format, &development, &offset_sys, &offset_id, &offset_ram, &size_ram);

      if (disk_data == NULL)
      {
         /* Unrecognised disk: leave the DD without a disk rather than wiring a
          * bad image. */
         init_dd(&dev->dd,
               NULL, &g_ilibretro_clock,
               (const uint32_t*)ddrom, ddrom_size,
               NULL, NULL,
               &dev->r4300);
      }
      else
      {
         size_t disk_data_size =
            (disk_data == dd_disk) ? dd_disk_size : (size_t)MAME_FORMAT_DUMP_SIZE;

         /* underlying bytes wrapped by the libretro storage backend */
         init_libretro_storage(&dev->dd_disk_storage, disk_data, disk_data_size, NULL, NULL);

         dev->dd_disk.storage        = &dev->dd_disk_storage;
         dev->dd_disk.istorage       = &g_ilibretro_storage;
         dev->dd_disk.save_storage   = &dev->dd_disk_storage;
         dev->dd_disk.isave_storage  = &g_ilibretro_storage;
         dev->dd_disk.format         = (uint8_t)format;
         dev->dd_disk.development     = (uint8_t)development;
         dev->dd_disk.offset_sys     = offset_sys;
         dev->dd_disk.offset_id      = offset_id;
         dev->dd_disk.offset_ram     = offset_ram;

         GenerateLBAToPhysTable(&dev->dd_disk);

         init_dd(&dev->dd,
               NULL, &g_ilibretro_clock,
               (const uint32_t*)ddrom, ddrom_size,
               &dev->dd_disk, &g_istorage_disk_full,
               &dev->r4300);
      }
   }
   else
   {
      init_dd(&dev->dd,
            NULL, &g_ilibretro_clock,
            (const uint32_t*)ddrom, ddrom_size,
            NULL, NULL,
            &dev->r4300);
   }
}

void poweron_device(struct device* dev)
{
    poweron_r4300(&dev->r4300);
    poweron_rdp(&dev->dp);
    poweron_rsp(&dev->sp);
    poweron_ai(&dev->ai);
    poweron_pi(&dev->pi);
    poweron_ri(&dev->ri);
    poweron_si(&dev->si);
    poweron_vi(&dev->vi);
    poweron_dd(&dev->dd);
    poweron_memory();

    /* Defer the first VI interrupt until the game configures the VI (V_SYNC != 0),
     * matching mupen64plus-next. Arming it unconditionally at power-on (at next_vi
     * = 5000) fired spurious early VI interrupts before the game had set up the VI,
     * skewing boot timing by ~15 frames vs hardware/next. The VI is armed instead
     * from write_vi_regs (set_vi_vertical_interrupt) once V_SYNC/V_INTR are set. */
    if (dev->vi.regs[VI_V_SYNC_REG] != 0)
        add_interrupt_event_count(VI_INT, dev->vi.next_vi);
}
