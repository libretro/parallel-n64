/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus - dd_disk.h                                               *
*   Mupen64Plus homepage: https://mupen64plus.org/                        *
*   Copyright (C) 2015 LuigiBlood                                         *
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

#ifndef M64P_DEVICE_DD_DISK_H
#define M64P_DEVICE_DD_DISK_H

#include "../../api/m64p_types.h"

/* region 13: 64DD disk lifecycle glue for the libretro core command interface.
 * The disk-format machinery now lives in disk.c / dd_controller.c. */

/* Loaded disk buffer (the persistent save buffer connected by the frontend). */
extern unsigned char* g_dd_disk;
extern int            g_dd_disk_size;

m64p_error open_dd_disk(const unsigned char* diskimage, unsigned int size);
m64p_error close_dd_disk(void);

/* Update the global disk pointer after a D64 dump is expanded/relocated at
 * device init (the original buffer is freed by that expansion). */
void dd_disk_set_expanded(unsigned char* expanded, int expanded_size);

#endif
