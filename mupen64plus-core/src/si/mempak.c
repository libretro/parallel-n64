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

#include "../memory/memory.h"

#include <stdint.h>
#include <string.h>

void mempak_read_command(uint8_t *mempak, uint8_t* cmd)
{
   uint16_t address = (cmd[3] << 8) | (cmd[4] & 0xe0);

   if (address < 0x8000)
      memcpy(&mempak[address], &cmd[5], 0x20);
   else
      memset(&cmd[5], 0, 0x20);
}

void mempak_write_command(uint8_t *mempak, uint8_t *cmd)
{
   uint16_t address = (cmd[3] << 8) | (cmd[4] & 0xe0);

   if (address < 0x8000)
      memcpy(&cmd[5], &mempak[address], 0x20);
}
