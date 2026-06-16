/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - libretro_storage.h                                      *
 *   parallel-n64 libretro save-RAM storage backend                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef M64P_BACKENDS_LIBRETRO_STORAGE_H
#define M64P_BACKENDS_LIBRETRO_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "api/storage_backend.h"

/* Concrete storage_backend_interface implementation for parallel-n64.
 *
 * next's cart save chips (eeprom/flashram/sram) talk to their backing store
 * exclusively through a storage_backend_interface: data(storage) returns the
 * buffer, save(storage, start, size) requests a flush. next's standalone build
 * binds these to file_storage, which owns its own files on disk.
 *
 * parallel-n64 is a libretro core: the frontend (RetroArch) owns the save-RAM
 * buffer and persists it (.srm, netplay sync, save-RAM API). This backend wraps
 * that frontend-owned buffer so next's chips can use the storage_backend_interface
 * faithfully while the bytes stay on pn64's libretro save path. We deliberately
 * do NOT import next's file_storage, which would make the core write its own
 * files and break RetroArch save compatibility. */
struct libretro_storage
{
    uint8_t* data;
    size_t size;
    void* user_data;
    void (*save)(void*);
};

void init_libretro_storage(struct libretro_storage* storage,
                           uint8_t* data, size_t size,
                           void* user_data, void (*save)(void*));

extern const struct storage_backend_interface g_ilibretro_storage;

#endif
