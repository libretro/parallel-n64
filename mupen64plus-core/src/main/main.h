/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - main.h                                                  *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
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

#include "api/m64p_types.h"
#include "osal/preproc.h"

#include <stdint.h>

struct r4300_core;
struct rsp_core;
struct ai_controller;
struct pi_controller;
struct ri_controller;
struct si_controller;
struct vi_controller;
struct dd_controller;

enum { RDRAM_MAX_SIZE = 0x800000 };

/* globals */
extern m64p_handle g_CoreConfig;

extern int g_MemHasBeenBSwapped;
extern int g_DDMemHasBeenBSwapped;
extern int g_EmulatorRunning;

extern ALIGN(16, uint32_t g_rdram[RDRAM_MAX_SIZE/4]);

extern struct ai_controller g_ai;
extern struct pi_controller g_pi;
extern struct ri_controller g_ri;
extern struct si_controller g_si;
extern struct vi_controller g_vi;
extern struct dd_controller g_dd;

extern struct r4300_core g_r4300;
extern struct rdp_core g_dp;
extern struct rsp_core g_sp;


extern m64p_frame_callback g_FrameCallback;

extern int g_delay_si;

extern int g_gs_vi_counter;

void new_frame(void);

int  main_set_core_defaults(void);
void main_message(m64p_msg_level level, unsigned int osd_corner, const char *format, ...);

m64p_error main_init(void);
m64p_error main_run(void);
void mupen_main_exit(void);
void mupen_main_stop(void);
void main_toggle_pause(void);
void main_advance_one(void);
void main_check_inputs(void);

void new_vi(void);

m64p_error main_core_state_query(m64p_core_param param, int *rval);
m64p_error main_core_state_set(m64p_core_param param, int val);

m64p_error main_read_screen(void *pixels, int bFront);

m64p_error main_reset(int do_hard_reset);

#endif /* __MAIN_H__ */

