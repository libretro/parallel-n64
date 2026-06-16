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
#include "is_viewer.h"
#include "sram.h"

#include "../../backends/libretro_storage.h"
#include "../../backends/api/joybus.h"

struct r4300_core;
struct pi_controller;
struct clock_backend_interface;
struct storage_backend_interface;

/* Group the cart save chips and ROM into a single device-level struct, matching
 * mupen64plus-next's struct cart. Unlike next (which binds the save chips to
 * file_storage), parallel-n64 keeps its libretro save-RAM backends, so the
 * per-chip libretro_storage objects live here alongside the chips. The joybus
 * cart device and the cart_dom2/dom3 DMA dispatch below are imported from next
 * (region 12); they are wired into the PIF/PI paths in later steps. */
struct cart
{
    struct af_rtc af_rtc;
    struct cart_rom cart_rom;
    struct eeprom eeprom;
    struct flashram flashram;
    struct sram sram;

    /* IS-Viewer debug device (homebrew/test ROM printf output at 0x13ff0000) */
    struct is_viewer is_viewer;

    int use_flashram;

    /* parallel-n64 libretro save-RAM backends for the save chips */
    struct libretro_storage eeprom_storage;
    struct libretro_storage flashram_storage;
    struct libretro_storage sram_storage;
};

/* PI dom2 (save chip) MMIO + DMA dispatch, routing between sram and flashram by
 * use_flashram, matching mupen64plus-next. */
void read_cart_dom2(void* opaque, uint32_t address, uint32_t* value);
void write_cart_dom2(void* opaque, uint32_t address, uint32_t value, uint32_t mask);

unsigned int cart_dom2_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length);
unsigned int cart_dom2_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length);
unsigned int cart_dom3_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length);
unsigned int cart_dom3_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length);

/* Joybus cart device (eeprom + AF-RTC PIF-channel commands) */
extern const struct joybus_device_interface
    g_ijoybus_device_cart;

#endif
