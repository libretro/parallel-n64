/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - device_memory_map.h                                     *
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

#ifndef M64P_DEVICE_DEVICE_MEMORY_MAP_H
#define M64P_DEVICE_DEVICE_MEMORY_MAP_H

#include <stddef.h>

#include <libretro.h>

struct device;

enum { DEVICE_MEMORY_DESCRIPTORS_MAX = 8 };

/* Fill descs with the memory regions the frontend can read directly:
 * RDRAM (cached and uncached), RSP DMEM/IMEM, PIF ROM and RAM, cartridge
 * ROM and 64DD IPL ROM.  Each descriptor covers only the memory behind its
 * pointer.  Returns the number of descriptors written. */
size_t device_memory_descriptors(const struct device* dev,
                                 struct retro_memory_descriptor* descs,
                                 size_t max);

#endif
