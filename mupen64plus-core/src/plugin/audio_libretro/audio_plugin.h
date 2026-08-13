/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - emulate_speaker_via_audio_plugin.h                      *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

#ifndef M64P_PLUGIN_EMULATE_SPEAKER_VIA_LIBRETRO_H
#define M64P_PLUGIN_EMULATE_SPEAKER_VIA_LIBRETRO_H

#include <stddef.h>

void     init_audio_libretro(void);
void     deinit_audio_libretro(void);
void     flush_audio_libretro(void);
double   get_audio_sample_rate_libretro(void);

/* Called straight from the AI controller.  This fork has one audio
 * backend and no second implementation for a vtable to choose between,
 * so the calls go directly rather than through a one-entry interface. */
void set_audio_format_via_libretro(unsigned int frequency,
      unsigned int clock, unsigned int divider);
void push_audio_samples_via_libretro(const void *buffer, size_t size);

#endif
