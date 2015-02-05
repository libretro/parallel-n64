/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
* Mupen64plus - mempak.c *
* Mupen64Plus homepage: http://code.google.com/p/mupen64plus/ *
* Copyright (C) 2014 Bobby Smiles *
* *
* This program is free software; you can redistribute it and/or modify *
* it under the terms of the GNU General Public License as published by *
* the Free Software Foundation; either version 2 of the License, or *
* (at your option) any later version. *
* *
* This program is distributed in the hope that it will be useful, *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the *
* GNU General Public License for more details. *
* *
* You should have received a copy of the GNU General Public License *
* along with this program; if not, write to the *
* Free Software Foundation, Inc., *
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. *
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "mempak.h"
#include "game_controller.h"

#include "../api/m64p_types.h"
#include "../api/callbacks.h"

#include "../main/main.h"
#include "../main/rom.h"
#include "../main/util.h"
#include "../memory/memory.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void mempak_read_command(struct game_controllers* controllers, int channel, uint8_t* cmd)
{
   /* address is in fact an offset (11bit) | CRC (5 bits) */
   uint16_t address = (cmd[3] << 8) | cmd[4];

   if (address == 0x8001)
   {
      memset(&cmd[5], 0, 0x20);
      cmd[0x25] = pak_crc(&cmd[5]);
   }
   else
   {
      address &= 0xFFE0;
      if (address <= 0x7FE0)
         memcpy(&saved_memory.mempack[channel][address], &cmd[5], 0x20);
      else
         memset(&cmd[5], 0, 0x20);
      cmd[0x25] = pak_crc(&cmd[5]);
   }
}

void mempak_write_command(struct game_controllers *controllers, int channel, uint8_t *cmd)
{
   /* address is in fact an offset (11bit) | CRC (5 bits) */
   uint16_t address = (cmd[3] << 8) | cmd[4];

   if (address == 0x8001)
   {
      cmd[0x25] = pak_crc(&cmd[5]);
   }
   else
   {
      address &= 0xFFE0;

      if (address <= 0x7FE0)
         memcpy(&cmd[5], &saved_memory.mempack[channel][address], 0x20);

      cmd[0x25] = pak_crc(&cmd[5]);
   }
}
