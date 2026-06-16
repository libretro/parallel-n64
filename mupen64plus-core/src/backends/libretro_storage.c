/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - libretro_storage.c                                      *
 *   parallel-n64 libretro save-RAM storage backend                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "libretro_storage.h"

#include <stddef.h>
#include <stdint.h>

void init_libretro_storage(struct libretro_storage* storage,
                           uint8_t* data, size_t size,
                           void* user_data, void (*save)(void*))
{
    storage->data = data;
    storage->size = size;
    storage->user_data = user_data;
    storage->save = save;
}

static uint8_t* libretro_storage_data(const void* storage)
{
    const struct libretro_storage* st = (const struct libretro_storage*)storage;
    return st->data;
}

static size_t libretro_storage_size(const void* storage)
{
    const struct libretro_storage* st = (const struct libretro_storage*)storage;
    return st->size;
}

static void libretro_storage_save(void* storage, size_t start, size_t size)
{
    struct libretro_storage* st = (struct libretro_storage*)storage;
    /* pn64's libretro save path flushes the whole frontend-owned save-RAM
     * buffer; the (start, size) sub-range hint from next's interface is not
     * needed because the frontend persists the entire buffer on the save
     * callback. */
    (void)start;
    (void)size;
    if (st->save != NULL)
        st->save(st->user_data);
}

const struct storage_backend_interface g_ilibretro_storage =
{
    libretro_storage_data,
    libretro_storage_size,
    libretro_storage_save
};
