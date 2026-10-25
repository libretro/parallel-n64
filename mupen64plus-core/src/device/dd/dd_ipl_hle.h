/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - dd_ipl_hle.h                                            *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Cleanroom stand-in for the 64DD IPL ROM, used when no 64DD_IPL.bin is
 * available. Nothing in here is taken from the IPL: the boot hands the
 * game the machine state the retail IPL was measured to leave at the jump
 * into the disk's boot code, and the ROM image holds only what games read
 * from the drive's ROM, with cleanroom font and silent wave data. */

#ifndef M64P_DEVICE_DD_DD_IPL_HLE_H
#define M64P_DEVICE_DD_DD_IPL_HLE_H

#include <stddef.h>
#include <stdint.h>

struct r4300_core;
struct dd_controller;

enum { DD_IPL_HLE_ROM_SIZE = 0x400000 };

/* Offsets of the ROM areas games address directly (leo.h). */
enum
{
    DD_IPL_HLE_FONT_START = 0x0a0000,
    DD_IPL_HLE_WAVE_START = 0x140000
};

void dd_ipl_hle_set_enabled(int enabled);
int  dd_ipl_hle_enabled(void);

/* Fill the DD ROM block (host-order 32-bit words, as the IPL loader leaves
 * a real image) with the stand-in ROM for a disk of the given region
 * (DDREGION_*). Returns the image size in bytes. */
size_t dd_ipl_hle_build_rom(uint32_t* rom, unsigned int region);

/* A fetch from the stand-in ROM at rom_off is about to be served. */
void dd_ipl_hle_rom_fetch(uint32_t rom_off);

/* Load the disk's boot code and leave the machine as the IPL does at the
 * jump, with PC set to run it. Returns 0 when the disk has no usable boot
 * information. */
int dd_ipl_hle_boot(struct r4300_core* r4300, struct dd_controller* dd,
                    unsigned int tv_type, unsigned int reset_type);

#endif
