/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cart.h                                                  *
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

#ifndef M64P_DEVICE_CART_CART_H
#define M64P_DEVICE_CART_CART_H

#include "af_rtc.h"
#include "cart_rom.h"
#include "eeprom.h"
#include "flashram.h"
#include "sram.h"

#include "../../backends/libretro_storage.h"

/* Group the cart save chips and ROM into a single device-level struct, matching
 * mupen64plus-next's struct cart. Unlike next (which binds the save chips to
 * file_storage), parallel-n64 keeps its libretro save-RAM backends, so the
 * per-chip libretro_storage objects live here alongside the chips. The cart
 * dispatch / joybus device (next's cart.c, get_pi_dma_handler) is NOT adopted;
 * pn64 keeps its existing PIF and PI DMA dispatch, so this is a passive grouping
 * struct. */
struct cart
{
    struct af_rtc af_rtc;
    struct cart_rom cart_rom;
    struct eeprom eeprom;
    struct flashram flashram;
    struct sram sram;

    int use_flashram;

    /* parallel-n64 libretro save-RAM backends for the save chips */
    struct libretro_storage eeprom_storage;
    struct libretro_storage flashram_storage;
    struct libretro_storage sram_storage;
};

#endif
