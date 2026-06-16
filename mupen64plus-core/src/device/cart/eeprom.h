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

/* Joybus EEPROM status type field as it appears on the wire (3 bytes).
 * 0xffffff means "no EEPROM present"; the 4K and 16K constants are
 * what the relevant N64 silicon reports for the two real EEPROM
 * variants. Stored 32-bit so the all-ones "none" value can be carried
 * unambiguously alongside the 16-bit hardware IDs. */
enum
{
   JDT_EEPROM_NONE = UINT32_C(0xffffff),
   JDT_EEPROM_4K   = UINT32_C(0x008000),
   JDT_EEPROM_16K  = UINT32_C(0x00c000)
};

struct eeprom
{
    /* external eep storage */
    void* user_data;
    void (*save)(void*);
    uint8_t* data;
    size_t size;
    uint32_t id;
};

void init_eeprom(struct eeprom *eeprom,
      void *user_data,
      void (*save)(void*),
      uint8_t *data,
      size_t size,
      uint32_t id);

void eeprom_save(struct eeprom* eeprom);

void format_eeprom(uint8_t* eeprom, size_t size);

void eeprom_status_command(struct eeprom* eeprom, uint8_t* cmd);
void eeprom_read_command(struct eeprom* eeprom, uint8_t* cmd);
void eeprom_write_command(struct eeprom* eeprom, uint8_t* cmd);

#endif
