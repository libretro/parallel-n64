/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - transferpak.h                                           *
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

#ifndef M64P_SI_TRANSFERPAK_H
#define M64P_SI_TRANSFERPAK_H

#include <stdint.h>

#include "gb/gb_cart.h"

enum cart_access_mode
{
    CART_NOT_INSERTED = 0x40,
    CART_ACCESS_MODE_0 = 0x80,
    CART_ACCESS_MODE_1 = 0x89
};

struct transferpak
{
    unsigned int enabled;
    unsigned int bank;
    unsigned int access_mode;
    unsigned int access_mode_changed;

    struct gb_cart gb_cart;
};

int init_transferpak(struct transferpak* tpk, uint8_t *rom, size_t rom_size);
void release_transferpak(struct transferpak* tpk);

void transferpak_read_command(struct transferpak* tpk, uint16_t address, uint8_t* data, size_t size);
void transferpak_write_command(struct transferpak* tpk, uint16_t address, const uint8_t* data, size_t size);

#endif
