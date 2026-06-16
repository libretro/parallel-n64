/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - eeprom.h                                                *
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

#ifndef M64P_SI_EEPROM_H
#define M64P_SI_EEPROM_H

#include <stddef.h>
#include <stdint.h>

struct storage_backend_interface;

/* Joybus EEPROM status type field as it appears on the wire (3 bytes).
 * 0xffffff means "no EEPROM present"; the 4K and 16K constants are
 * what the relevant N64 silicon reports for the two real EEPROM
 * variants. */
enum
{
   JDT_EEPROM_NONE = UINT32_C(0xffffff),
   JDT_EEPROM_4K   = UINT32_C(0x008000),
   JDT_EEPROM_16K  = UINT32_C(0x00c000)
};

/* Adopt mupen64plus-next's storage-backed eeprom struct: the backing buffer is
 * reached through a storage_backend_interface (in parallel-n64, the libretro
 * save-RAM backend) rather than a raw data pointer + save callback. 'type' is
 * the wire status id (JDT_EEPROM_4K / JDT_EEPROM_16K), matching next. */
struct eeprom
{
    uint16_t type;
    void* storage;
    const struct storage_backend_interface* istorage;
};

void init_eeprom(struct eeprom* eeprom,
      uint16_t type,
      void* storage,
      const struct storage_backend_interface* istorage);

void format_eeprom(uint8_t* eeprom, size_t size);

/* next-style block accessors */
void eeprom_read_block(struct eeprom* eeprom, uint8_t block, uint8_t* data);
void eeprom_write_block(struct eeprom* eeprom, uint8_t block, const uint8_t* data, uint8_t* status);

/* parallel-n64 PIF-command entry points (kept so pn64's process_cart_command
 * dispatch is unchanged). eeprom_status_command preserves pn64's documented
 * wire-shape fix (see eeprom.c). */
void eeprom_status_command(struct eeprom* eeprom, uint8_t* cmd);
void eeprom_read_command(struct eeprom* eeprom, uint8_t* cmd);
void eeprom_write_command(struct eeprom* eeprom, uint8_t* cmd);

#endif
