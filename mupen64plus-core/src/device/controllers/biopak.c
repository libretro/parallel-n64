/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - biopak.c                                                *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2017 Bobby Smiles                                       *
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

/* Implementation based on notes from raphnet
 * See http://www.raphnet.net/divers/n64_bio_sensor/index_en.php
 *
 * Adapted to parallel-n64's command-based pak interface (no pak_interface
 * dispatch table / backends layer). The sensor synthesises a square wave at
 * the configured pulse rate entirely from the wall clock, so it needs no
 * external input source. */

#include "biopak.h"
#include "game_controller.h"

#include "../../api/m64p_types.h"
#include "../../api/callbacks.h"

#include <time.h>
#include <string.h>

void init_biopak(struct biopak* bpk, unsigned int bpm)
{
   /* guard against a zero rate (would divide by zero below) */
   bpk->bpm = (bpm == 0) ? 72 : bpm;
}

void biopak_read_command(struct biopak* bpk, uint16_t address, uint8_t* data, size_t size)
{
   if (address == 0xc000)
   {
      /* Report the current phase of the pulse square wave. period is the
       * pulse length in ms; the first half of each period reads 0x00, the
       * second half reads 0x03 -- the game counts those edges as a heartbeat. */
      time_t now      = time(NULL) * 1000;
      uint32_t period = UINT32_C(60 * 1000) / bpk->bpm;
      uint32_t k      = (uint32_t)(now % period);

      memset(data, (2 * k < period) ? 0x00 : 0x03, size);
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Unexpected bio sensor read address %04x", address);
   }
}

void biopak_write_command(struct biopak* bpk, uint16_t address, const uint8_t* data, size_t size)
{
   DebugMessage(M64MSG_WARNING, "Unexpected bio sensor write address %04x", address);
}

/* mupen64plus-next pak_interface vtable (region 12c). Bridges the joybus
 * controller device onto parallel-n64's biopak read/write command functions.
 * plug/unplug are no-ops: pn64 paks are always-present nested structs. */
static void plug_biopak(void* pak)   { (void)pak; }
static void unplug_biopak(void* pak) { (void)pak; }
static void read_biopak(void* pak, uint16_t address, uint8_t* data, size_t size)
{
    biopak_read_command((struct biopak*)pak, address, data, size);
}
static void write_biopak(void* pak, uint16_t address, const uint8_t* data, size_t size)
{
    biopak_write_command((struct biopak*)pak, address, data, size);
}
const struct pak_interface g_ibiopak =
{
    "Bio pak",
    plug_biopak,
    unplug_biopak,
    read_biopak,
    write_biopak
};
