/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

//****************************************************************
//
// Glide64 - Glide Plugin for Nintendo 64 emulators
// Project started on December 29th, 2001
//
// Authors:
// Dave2001, original author, founded the project in 2001, left it in 2002
// Gugaman, joined the project in 2002, left it in 2002
// Sergey 'Gonetz' Lipski, joined the project in 2002, main author since fall of 2002
// Hiroshi 'KoolSmoky' Morii, joined the project in 2007
//
//****************************************************************
//
// To modify Glide64:
// * Write your name and (optional)email, commented by your work, so I know who did it, and so that you can find which parts you modified when it comes time to send it to me.
// * Do NOT send me the whole project or file that you modified.  Take out your modified code sections, and tell me where to put them.  If people sent the whole thing, I would have many different versions, but no idea how to combine them all.
//
//****************************************************************

/**********************************************************************************
Common gfx plugin spec, version #1.3 maintained by zilmar (zilmar@emulation64.com)

All questions or suggestions should go through the mailing list.
http://www.egroups.com/group/Plugin64-Dev
***********************************************************************************

Notes:
------

Setting the approprate bits in the MI_INTR_REG and calling CheckInterrupts which
are both passed to the DLL in InitiateGFX will generate an Interrupt from with in
the plugin.

The Setting of the RSP flags and generating an SP interrupt  should not be done in
the plugin

**********************************************************************************/

#ifndef _GFX_H_INCLUDED__
#define _GFX_H_INCLUDED__

#include "winlnxdefs.h"
#include "m64p.h"

#include <stdio.h>
#include <SDL_mutex.h>
#include <stdlib.h>
#include <stddef.h>		// offsetof
#include <string.h>
#include <stdarg.h>
#include <glide.h>
#include "../glide_funcs.h"
#include "GlideExtensions.h"
#include "rdp.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define _ENDUSER_RELEASE_

//#define LOGGING			// log of spec functions called
//#define LOG_UCODE
//#define EXTREME_LOGGING		// lots of logging
							//  note that some of these things are inserted/removed
							//  from within the code & may not be changed by this define.
//#define VISUAL_LOGGING
//#define TLUT_LOGGING		// log every entry of the TLUT?

#define LOGKEY		0x11 // this key (CONTROL)

//#define LOG_COMMANDS		// log the whole 64-bit command as (0x........, 0x........)

#ifndef _ENDUSER_RELEASE_
#define RDP_LOGGING			// Allow logging (will not log unless checked, but allows the option)
							//  Logging functions will not be compiled if this is not present.
//#define RDP_ERROR_LOG
#define UNIMP_LOG			// Keep enabled, option in dialog
#define BRIGHT_RED			// Keep enabled, option in dialog
#endif

#define FPS_FRAMES	10		// Number of frames in which to make an FPS count

//#define SHOW_FULL_TEXVIEWER	// shows the entire contents of the texture in the cache viewer,
								// usually used to debug clamping issues.


// Usually enabled
#define LARGE_TEXTURE_HANDLING	// allow large-textured objects to be split?

// Simulations
//#define SIMULATE_VOODOO1
//#define SIMULATE_BANSHEE
//********

#define COLORED_DEBUGGER	// ;) pretty colors

// rdram mask at 0x400000 bytes (bah, not right for majora's mask)
//#define BMASK	0x7FFFFF
extern unsigned long BMASK;
#define WMASK	0x3FFFFF
#define DMASK	0x1FFFFF

extern uint32_t update_screen_count;
extern uint32_t resolutions[0x18][2];

#ifdef LOGGING
#define LOG(...) WriteLog(M64MSG_INFO, __VA_ARGS__)
#define VLOG(...) WriteLog(M64MSG_VERBOSE, __VA_ARGS__)
#define WARNLOG(...) WriteLog(M64MSG_WARNING, __VA_ARGS__)
#define ERRLOG(...) WriteLog(M64MSG_ERROR, __VA_ARGS__)
#else
#define LOG(...)
#define VLOG(...)
#define WARNLOG(...)
#define ERRLOG(...)
#endif

#define OPEN_RDP_LOG()
#define CLOSE_RDP_LOG()
#define LRDP(x)

#ifdef RDP_LOGGING
/* FIXME */
#define LRDP(x)
#else
#define LRDP(x)
#endif


#ifdef RDP_ERROR_LOG
/* FIXME */
#define RDP_E(x) 
#else
#define OPEN_RDP_E_LOG()
#define CLOSE_RDP_E_LOG()
#define RDP_E(x)
#endif

__inline void FRDP (const char *fmt, ...)
{
#ifdef RDP_LOGGING
	if (!settings.logging || !log_open) return;

	va_list ap;
	va_start(ap, fmt);
	vsprintf(out_buf, fmt, ap);
	LRDP (out_buf);
	va_end(ap);
#endif
}
__inline void FRDP_E (const char *fmt, ...)
{
#ifdef RDP_ERROR_LOG
	if (!settings.elogging || !elog_open) return;

	sprintf (out_buf, "%08lx: (%08lx, %08lx) ", rdp.pc[rdp.pc_i]-8, rdp.cmd0, rdp.cmd1);
	rdp_err << out_buf;

	va_list ap2;
	va_start(ap2, fmt);
	vsprintf(out_buf, fmt, ap2);
	rdp_err << out_buf;
	rdp_err.flush();
	va_end(ap2);
#endif
}

extern int fullscreen;
extern int romopen;
extern int debugging;

extern int evoodoo;
extern int ev_fullscreen;

extern SDL_sem *mutexProcessDList;
extern int exception;

#ifdef __LIBRETRO__ // Export InitGfx for C
extern "C" { int InitGfx(); }
#else
int InitGfx (int);
#endif
void ReleaseGfx ();

// The highest 8 bits are the segment # (1-16), and the lower 24 bits are the offset to
// add to it.
#define segoffset(so) ((rdp.segment[(so>>24)&0x0f] + (so&BMASK)) & BMASK)

/* Plugin types */
#define PLUGIN_TYPE_GFX				2

/***** Structures *****/
typedef struct {
	uint16_t Version;        /* Set to 0x0103 */
	uint16_t Type;           /* Set to PLUGIN_TYPE_GFX */
	char Name[100];      /* Name of the DLL */

	/* If DLL supports memory these memory options then set them to TRUE or FALSE
	   if it does not support it */
	int NormalMemory;    /* a normal uint8_t array */
	int MemoryBswaped;  /* a normal uint8_t array where the memory has been pre
	                          bswap on a dword (32 bits) boundry */
} PLUGIN_INFO;

#define gfx gfxInfo

extern GFX_INFO gfx;
extern bool no_dlist;

#ifndef GR_STIPPLE_DISABLE
#define GR_STIPPLE_DISABLE	0x0
#define GR_STIPPLE_PATTERN	0x1
#define GR_STIPPLE_ROTATE	0x2
#endif

int GetTexAddrUMA(int tmu, int texsize);
void ReadSettings ();
void ReadSpecialSettings (const char * name);

#if defined(__cplusplus)
}
#endif

#endif //_GFX_H_INCLUDED__
