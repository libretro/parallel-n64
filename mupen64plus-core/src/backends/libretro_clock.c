/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - libretro_clock.c                                        *
 *   parallel-n64 wall-clock backend for AF-RTC                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "libretro_clock.h"

#include <time.h>

static time_t libretro_clock_get_time(void* clock)
{
    (void)clock;
    return time(NULL);
}

const struct clock_backend_interface g_ilibretro_clock =
{
    libretro_clock_get_time
};
