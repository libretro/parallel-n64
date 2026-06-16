/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - libretro_clock.h                                        *
 *   parallel-n64 wall-clock backend for AF-RTC                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef M64P_BACKENDS_LIBRETRO_CLOCK_H
#define M64P_BACKENDS_LIBRETRO_CLOCK_H

#include "api/clock_backend.h"

/* Concrete clock_backend_interface for parallel-n64's AF-RTC. next drives
 * af_rtc through a clock_backend returning time_t; this backend returns host
 * wall-clock time (ctime). No per-instance state is required, so the opaque
 * clock pointer is unused. */
extern const struct clock_backend_interface g_ilibretro_clock;

#endif
