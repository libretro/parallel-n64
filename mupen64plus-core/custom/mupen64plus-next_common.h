/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-Next - mupen64plus-next_common.h                          *
 *   Copyright (C) 2020 M4xw <m4x@m4xw.net>                                *
 *   Copyright (C) 2020 Daniel De Matteis                                  *
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

#ifndef __M64P_NEXT_COMMON_H__
#define __M64P_NEXT_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <libretro.h>

#include "api/m64p_common.h"
#include "api/m64p_plugin.h"
#include "api/m64p_types.h"

enum rdp_plugin_type
{
   RDP_PLUGIN_NONE = 0,
   RDP_PLUGIN_GLIDEN64,
   RDP_PLUGIN_ANGRYLION,
   RDP_PLUGIN_PARALLEL,
   RDP_PLUGIN_MAX
};

enum rsp_plugin_type
{
   RSP_PLUGIN_NONE = 0,
   RSP_PLUGIN_HLE,
   RSP_PLUGIN_CXD4,
   RSP_PLUGIN_PARALLEL,
   RSP_PLUGIN_MAX
};

void plugin_connect_rsp_api(enum rsp_plugin_type type);
void plugin_connect_rdp_api(enum rdp_plugin_type type);
void plugin_connect_all();

uint32_t get_retro_screen_width();
uint32_t get_retro_screen_height();

extern enum rdp_plugin_type current_rdp_type;
extern enum rsp_plugin_type current_rsp_type;
extern retro_environment_t environ_cb;
extern bool libretro_swap_buffer;

// Misc Globals
extern CONTROL Controls[4];
extern struct xoshiro256pp_state l_mpk_idgen;

// Savestate globals
extern bool retro_savestate_complete;
extern int  retro_savestate_result;

// 64DD globals
extern char* retro_dd_path_img;
extern char* retro_dd_path_rom;

// Other Subsystems
extern char* retro_transferpak_rom_path;
extern char* retro_transferpak_ram_path;

// Threaded GL Callback
extern void gln64_thr_gl_invoke_command_loop();
extern bool threaded_gl_safe_shutdown;

// GLN64 context management (for libretro context_destroy/context_reset)
extern void gln64DestroyGfxContext(void);
extern void gln64ReinitGfxContext(void);

// Core options
extern uint32_t CoreOptionCategoriesSupported;
extern uint32_t CoreOptionUpdateDisplayCbSupported;
// GLN64
extern uint32_t bilinearMode;
extern uint32_t EnableHybridFilter;
extern uint32_t EnableDitheringPattern;
extern uint32_t EnableDitheringQuantization;
extern uint32_t RDRAMImageDitheringMode;
extern uint32_t EnableHWLighting;
extern uint32_t CorrectTexrectCoords;
extern uint32_t EnableTexCoordBounds;
extern uint32_t EnableInaccurateTextureCoordinates;
extern uint32_t enableNativeResTexrects;
extern uint32_t enableLegacyBlending;
extern uint32_t EnableCopyColorToRDRAM;
extern uint32_t EnableCopyColorFromRDRAM;
extern uint32_t EnableCopyDepthToRDRAM;
extern uint32_t AspectRatio;
extern uint32_t MaxTxCacheSize;
extern uint32_t MaxHiResTxVramLimit;
extern uint32_t txFilterMode;
extern uint32_t txEnhancementMode;
extern uint32_t txHiresEnable;
extern uint32_t txHiresFullAlphaChannel;
extern uint32_t txFilterIgnoreBG;
extern uint32_t EnableFXAA;
extern uint32_t MultiSampling;
extern uint32_t EnableFragmentDepthWrite;
extern uint32_t EnableShadersStorage;
extern uint32_t EnableTextureCache;
extern uint32_t EnableFBEmulation;
extern uint32_t EnableFrameDuping;
extern uint32_t EnableLODEmulation;
extern uint32_t EnableFullspeed;
extern uint32_t CountPerOp;
extern uint32_t CountPerOpDenomPot;
extern uint32_t CountPerScanlineOverride;
extern uint32_t BackgroundMode;
extern uint32_t EnableEnhancedTextureStorage;
extern uint32_t EnableHiResAltCRC;
extern uint32_t EnableEnhancedHighResStorage;
extern uint32_t EnableTxCacheCompression;
extern uint32_t ForceDisableExtraMem;
extern uint32_t IgnoreTLBExceptions;
extern uint32_t EnableNativeResFactor;
extern uint32_t EnableN64DepthCompare;
extern uint32_t EnableThreadedRenderer;
extern uint32_t EnableCopyAuxToRDRAM;
extern uint32_t GLideN64IniBehaviour;

// Overscan Options
extern uint32_t EnableOverscan;
extern uint32_t OverscanTop;
extern uint32_t OverscanLeft;
extern uint32_t OverscanRight;
extern uint32_t OverscanBottom;

// Others
#define RETRO_MEMORY_DD 0x100 + 1
#define RETRO_GAME_TYPE_DD 1

#define RETRO_MEMORY_TRANSFERPAK 0x100 + 2
#define RETRO_GAME_TYPE_TRANSFERPAK 2

#if defined(HAVE_PARALLEL_RDP)
#define FLAVOUR_VERSION "-Vulkan"
#elif defined(HAVE_OPENGLES2)
#define FLAVOUR_VERSION "-GLES2"
#elif defined(HAVE_OPENGLES3)
#define FLAVOUR_VERSION "-GLES3"
#else
#define FLAVOUR_VERSION "-OpenGL"
#endif

#ifndef GIT_VERSION
#define GIT_VERSION " git"
#endif

// Keep it optional (f.e. Raspberry Pi Platforms override it in Makefile)
#ifndef CORE_NAME
#define CORE_NAME "mupen64plus"
#endif

// RetroArch Extensions
#define RETRO_ENVIRONMENT_RETROARCH_START_BLOCK 0x800000
#define RETRO_ENVIRONMENT_SET_SAVE_STATE_IN_BACKGROUND (2 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)
                                            /* bool * --
                                            * Boolean value that tells the front end to save states in the
                                            * background or not.
                                            */

#define RETRO_ENVIRONMENT_GET_CLEAR_ALL_THREAD_WAITS_CB (3 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)
                                            /* retro_environment_t * --
                                            * Provides the callback to the frontend method which will cancel
                                            * all currently waiting threads.  Used when coordination is needed
                                            * between the core and the frontend to gracefully stop all threads.
                                            */

#define RETRO_ENVIRONMENT_POLL_TYPE_OVERRIDE (4 | RETRO_ENVIRONMENT_RETROARCH_START_BLOCK)
                                            /* unsigned * --
                                            * Tells the frontend to override the poll type behavior. 
                                            * Allows the frontend to influence the polling behavior of the
                                            * frontend.
                                            *
                                            * Will be unset when retro_unload_game is called.
                                            *
                                            * 0 - Don't Care, no changes, frontend still determines polling type behavior.
                                            * 1 - Early
                                            * 2 - Normal
                                            * 3 - Late
                                            */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __M64P_NEXT_COMMON_H__ */
