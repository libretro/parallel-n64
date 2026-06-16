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
#include <stdlib.h>
#include <string.h>

#include "dd_disk.h"
#include "disk.h"

#include "../../api/callbacks.h"
#include "../../api/m64p_types.h"

/* Global loaded disk memory space (points at the persistent save buffer set up
 * by the frontend for MAME/SDK dumps; for D64 dumps it points at a private heap
 * buffer owned here, since D64 is expanded/relocated at device init). */
unsigned char* g_dd_disk      = NULL;
int            g_dd_disk_size = 0;

/* Set when g_dd_disk owns a heap allocation (D64); cleared for the MAME/SDK
 * case where g_dd_disk aliases the frontend's persistent save buffer. */
static int l_dd_disk_heap_owned = 0;

m64p_error open_dd_disk(const unsigned char* diskimage, unsigned int size)
{
    /* MAME / SDK dumps are loaded into the frontend's persistent save buffer
     * (connected via g_dd_disk before this call), giving them save-RAM
     * persistence. D64 dumps are a different size and get expanded/relocated by
     * scan_and_expand_disk_format at device init (which frees its input), so
     * they must live in a heap buffer we own rather than the static save
     * buffer. Format identification/expansion itself is deferred to device.c. */
    if (size == MAME_FORMAT_DUMP_SIZE || size == SDK_FORMAT_DUMP_SIZE)
    {
        if (g_dd_disk == NULL)
            return M64ERR_NO_MEMORY;

        l_dd_disk_heap_owned = 0;
        g_dd_disk_size = (int)size;

        /* Load the disk only if the save buffer does not already contain one
         * (system-area magic for JP/US disks). */
        if (*((const uint32_t*)g_dd_disk) != 0x16D348E8
         && *((const uint32_t*)g_dd_disk) != 0x56EE6322)
        {
            memcpy(g_dd_disk, diskimage, size);
        }
    }
    else
    {
        /* Treat any other size as a candidate D64 dump: copy it into a private
         * heap buffer that scan_and_expand_disk_format is free to release. If
         * it is not actually a valid disk, device.c's scan returns NULL and the
         * DD is left without a disk. */
        unsigned char* buf = (unsigned char*)malloc(size);
        if (buf == NULL)
            return M64ERR_NO_MEMORY;
        memcpy(buf, diskimage, size);

        if (l_dd_disk_heap_owned && g_dd_disk != NULL)
            free(g_dd_disk);

        g_dd_disk            = buf;
        g_dd_disk_size       = (int)size;
        l_dd_disk_heap_owned = 1;
    }

    DebugMessage(M64MSG_STATUS, "64DD Disk loaded!");
    return M64ERR_SUCCESS;
}

m64p_error close_dd_disk(void)
{
    if (g_dd_disk == NULL)
        return M64ERR_INVALID_STATE;

    /* Only free what we own; the MAME/SDK alias belongs to the frontend. Note
     * that for D64 the buffer may already have been replaced/freed by the
     * device-init expansion, in which case dd_disk_set_expanded() updated
     * g_dd_disk to the live allocation. */
    if (l_dd_disk_heap_owned)
    {
        free(g_dd_disk);
        l_dd_disk_heap_owned = 0;
    }

    g_dd_disk      = NULL;
    g_dd_disk_size = 0;

    DebugMessage(M64MSG_STATUS, "64DD Disk closed.");
    return M64ERR_SUCCESS;
}

/* device.c calls this after scan_and_expand_disk_format relocates a D64 dump,
 * so the global tracks the live (expanded) allocation rather than a freed one. */
void dd_disk_set_expanded(unsigned char* expanded, int expanded_size)
{
    g_dd_disk            = expanded;
    g_dd_disk_size       = expanded_size;
    l_dd_disk_heap_owned = 1;
}
