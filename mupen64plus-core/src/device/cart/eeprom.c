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

#include "../../api/m64p_types.h"
#include "../../api/callbacks.h"
#include "../../backends/api/storage_backend.h"

#include <string.h>

enum { EEPROM_BLOCK_SIZE = 8 };

void init_eeprom(struct eeprom* eeprom,
      uint16_t type,
      void* storage,
      const struct storage_backend_interface* istorage)
{
   eeprom->type     = type;
   eeprom->storage  = storage;
   eeprom->istorage = istorage;
}

void format_eeprom(uint8_t* eeprom, size_t size)
{
   memset(eeprom, 0xff, size);
}

/* next-style block accessors: read/write one 8-byte block through the storage
 * backend. The persisted byte layout (block * 8, linear, no byte-swap) is
 * identical to parallel-n64's previous implementation, so existing eeprom
 * saves remain compatible. */
void eeprom_read_block(struct eeprom* eeprom, uint8_t block, uint8_t* data)
{
   unsigned int address = block * EEPROM_BLOCK_SIZE;

   if (address < eeprom->istorage->size(eeprom->storage))
   {
      memcpy(data, eeprom->istorage->data(eeprom->storage) + address, EEPROM_BLOCK_SIZE);
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Invalid access to eeprom address=%04x", address);
   }
}

void eeprom_write_block(struct eeprom* eeprom, uint8_t block, const uint8_t* data, uint8_t* status)
{
   unsigned int address = block * EEPROM_BLOCK_SIZE;

   if (address < eeprom->istorage->size(eeprom->storage))
   {
      memcpy(eeprom->istorage->data(eeprom->storage) + address, data, EEPROM_BLOCK_SIZE);
      eeprom->istorage->save(eeprom->storage, address, EEPROM_BLOCK_SIZE);
      if (status != NULL)
         *status = 0x00;
   }
   else
   {
      DebugMessage(M64MSG_WARNING, "Invalid access to eeprom address=%04x", address);
   }
}

/* parallel-n64 PIF-command entry points. These keep pn64's process_cart_command
 * dispatch and wire behaviour unchanged; they delegate the data movement to the
 * block accessors above. */
void eeprom_status_command(struct eeprom* eeprom, uint8_t* cmd)
{
   /* Restore the pre-4ad4dc5b wire shape: cmd[5] is hard-zeroed
    * rather than carrying bits 16..23 of the eeprom type. Leaving
    * the high byte at zero keeps the response shape identical to
    * what every released N64 game expected. The '& 0xff' masks on
    * the low/mid byte writes are no-ops for 4K (0x008000) and 16K
    * (0x00c000) but defend the wire bytes against a future widening
    * of the type beyond 0xffff. The value is sourced from
    * eeprom->type (next's field) instead of the former id field. */
   if (cmd[1] != 3)
   {
      cmd[1] |= 0x40;
      if ((cmd[1] & 3) > 0)
         cmd[3] = (eeprom->type & 0xff);
      if ((cmd[1] & 3) > 1)
         cmd[4] = (eeprom->type >> 8) & 0xff;
      if ((cmd[1] & 3) > 2)
         cmd[5] = 0;
   }
   else
   {
      cmd[3] = (eeprom->type & 0xff);
      cmd[4] = (eeprom->type >> 8) & 0xff;
      cmd[5] = 0;
   }
}

void eeprom_read_command(struct eeprom* eeprom, uint8_t* cmd)
{
   /* cmd[3] is the block index; the 8-byte payload goes to cmd[4..11]. */
   eeprom_read_block(eeprom, cmd[3], &cmd[4]);
}

void eeprom_write_command(struct eeprom* eeprom, uint8_t* cmd)
{
   /* cmd[3] is the block index; the 8-byte payload is at cmd[4..11]. */
   eeprom_write_block(eeprom, cmd[3], &cmd[4], NULL);
}
