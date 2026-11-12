/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - main.h                                                  *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2002 Blight                                             *
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

#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdint.h>

#include "api/m64p_types.h"
#include "main/cheat.h"
#include "device/device.h"
#include "osal/preproc.h"

#if defined(__GNUC__)
#define ATTR_FMT(fmtpos, attrpos) __attribute__ ((format (printf, fmtpos, attrpos)))
#else
#define ATTR_FMT(fmtpos, attrpos)
#endif

/* globals */
extern m64p_handle g_CoreConfig;

extern int g_RomWordsLittleEndian;
extern int g_EmulatorRunning;

extern struct cheat_ctx g_cheat_ctx;

extern void* g_mem_base;

extern struct device g_dev;

extern m64p_media_loader g_media_loader;

extern int g_gs_vi_counter;

void new_frame(void);
void new_vi(void);

void main_switch_next_pak(int control_id);
void main_switch_plugin_pak(int control_id);
void main_change_gb_cart(int control_id);

void main_message(m64p_msg_level level, unsigned int osd_corner, const char *format, ...) ATTR_FMT(3, 4);

m64p_error main_run(void);
void main_stop(void);

m64p_error main_reset(int do_hard_reset);

m64p_error open_pif(const unsigned char* pifimage, unsigned int size);
m64p_error close_pif(void);

#endif /* __MAIN_H__ */


/* libretro-fork threading entry points */
m64p_error main_pre_run(void);
void mupen_main_stop(void);
void mupen_main_exit(void);
extern void* g_mem_base;
