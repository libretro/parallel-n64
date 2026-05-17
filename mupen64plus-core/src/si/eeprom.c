/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - eeprom.c                                                *
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

#include "eeprom.h"

#include "../api/m64p_types.h"
#include "../api/callbacks.h"

#include <string.h>

void init_eeprom(struct eeprom *eeprom,
      void *user_data,
      void (*save)(void*),
      uint8_t *data,
      size_t size,
      uint32_t id)
{
   /* connect saved_memory.eeprom to eeprom */
   eeprom->user_data = user_data;
   eeprom->save      = save;
   eeprom->data      = data;
   eeprom->size      = size;
   eeprom->id        = id;
}

void eeprom_save(struct eeprom* eeprom)
{
   eeprom->save(eeprom->user_data);
}

void format_eeprom(uint8_t* eeprom, size_t size)
{
   memset(eeprom, 0xff, size);
}

void eeprom_status_command(struct eeprom* eeprom, uint8_t* cmd)
{
   /* The Joybus EEPROM status response is three bytes wide; the
    * top byte (cmd[5]) used to be hard-coded to 0 because eeprom->id
    * was only 16 bits and the "no EEPROM" sentinel JDT_EEPROM_NONE
    * (0xffffff) couldn't be represented. With id widened to 32 bits,
    * propagate bits 16..23 so the wire response can be 0xffffff
    * when the ROM doesn't use EEPROM. */
   if (cmd[1] != 3)
   {
      cmd[1] |= 0x40;
      if ((cmd[1] & 3) > 0)
         cmd[3] = (eeprom->id & 0xff);
      if ((cmd[1] & 3) > 1)
         cmd[4] = (eeprom->id >> 8) & 0xff;
      if ((cmd[1] & 3) > 2)
         cmd[5] = (eeprom->id >> 16) & 0xff;
   }
   else
   {
      cmd[3] = (eeprom->id & 0xff);
      cmd[4] = (eeprom->id >> 8) & 0xff;
      cmd[5] = (eeprom->id >> 16) & 0xff;
   }
}

void eeprom_read_command(struct eeprom* eeprom, uint8_t* cmd)
{
   uint16_t address = cmd[3] * 8;
   uint8_t* data = &cmd[4];
   /* read 8-byte block */
   if (address < eeprom->size)
   {
      memcpy(data, &eeprom->data[address], 8);
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Invalid access to eeprom address=%04x", address);
   }
}

void eeprom_write_command(struct eeprom* eeprom, uint8_t* cmd)
{
   uint16_t address = cmd[3] * 8;
   const uint8_t* data = &cmd[4];
   /* write 8-byte block */
   if (address < eeprom->size)
   {
      memcpy(&eeprom->data[address], data, 8);
      eeprom_save(eeprom);
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Invalid access to eeprom address=%04x", address);
   }
}
