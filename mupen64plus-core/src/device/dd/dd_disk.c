/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*   Mupen64plus - dd_disk.c                                               *
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

/* region 13: parallel-n64's 64DD disk machinery (zone/track/sector addressing,
 * SDK->MAME conversion) has been replaced by mupen64plus-next's disk.c +
 * dd_controller.c, which additionally support the D64 format and a fuller DD
 * ASIC. This file now only retains the libretro disk lifecycle glue
 * (open/close + the loaded-disk globals) used by the core command interface;
 * format detection and expansion are handled by scan_and_expand_disk_format in
 * device.c at init time. */

#include <stddef.h>
#include <stdint.h>

#include "dd_disk.h"
#include "disk.h"

#include "../../api/callbacks.h"
#include "../../api/m64p_types.h"

/* Global loaded disk memory space (points at the persistent save buffer set up
 * by the frontend; the DD subsystem reads/writes the disk image in place). */
unsigned char* g_dd_disk      = NULL;
int            g_dd_disk_size = 0;

m64p_error open_dd_disk(const unsigned char* diskimage, unsigned int size)
{
    /* The buffer (g_dd_disk) is connected by the frontend before this call.
     * We validate the dump size and copy the image in unless the destination
     * already holds a saved disk (detected by the system-area magic words).
     * Format identification/expansion is deferred to device.c. */
    if (g_dd_disk == NULL)
        return M64ERR_NO_MEMORY;

    if (size != MAME_FORMAT_DUMP_SIZE && size != SDK_FORMAT_DUMP_SIZE)
    {
        DebugMessage(M64MSG_STATUS, "64DD Disk Format: Unknown, don't load.");
        return M64ERR_FILES;
    }

    g_dd_disk_size = (int)size;

    /* Load the disk only if the save buffer does not already contain one
     * (system-area magic for JP/US disks). */
    if (*((const uint32_t*)g_dd_disk) != 0x16D348E8
     && *((const uint32_t*)g_dd_disk) != 0x56EE6322)
    {
        memcpy(g_dd_disk, diskimage, size);
    }

    DebugMessage(M64MSG_STATUS, "64DD Disk loaded!");
    return M64ERR_SUCCESS;
}

m64p_error close_dd_disk(void)
{
    if (g_dd_disk == NULL)
        return M64ERR_INVALID_STATE;

    DebugMessage(M64MSG_STATUS, "64DD Disk closed.");
    return M64ERR_SUCCESS;
}
