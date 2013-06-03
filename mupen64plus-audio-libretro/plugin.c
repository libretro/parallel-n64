/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-sdl-audio - main.c                                        *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2007-2009 Richard Goedeken                              *
 *   Copyright (C) 2007-2008 Ebenblues                                     *
 *   Copyright (C) 2003 JttL                                               *
 *   Copyright (C) 2002 Hactarux                                           *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"
extern retro_audio_sample_batch_t audio_batch_cb;

#define M64P_PLUGIN_PROTOTYPES 1
#include "api/m64p_types.h"
#include "api/m64p_plugin.h"
#include "api/m64p_common.h"
#include "api/m64p_config.h"

/* version info */
#define AUDIO_PLUGIN_VERSION 0x016305
#define AUDIO_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

/* Read header for type definition */
static AUDIO_INFO AudioInfo;
static int GameFreq = 33600;

/* Mupen64Plus plugin functions */
EXPORT m64p_error CALL audioPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    if (PluginType != NULL)		*PluginType = M64PLUGIN_AUDIO;
    if (PluginVersion != NULL)	*PluginVersion = AUDIO_PLUGIN_VERSION;
	if (APIVersion != NULL)		*APIVersion = AUDIO_PLUGIN_API_VERSION;
    if (PluginNamePtr != NULL)	*PluginNamePtr = "Mupen64Plus libretro Audio Plugin";
    if (Capabilities != NULL)	*Capabilities = 0;                    
    return M64ERR_SUCCESS;
}

/* ----------- Audio Functions ------------- */
EXPORT void CALL audioAiDacrateChanged( int SystemType )
{
    switch (SystemType)
    {
        case SYSTEM_NTSC: GameFreq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1); break;
        case SYSTEM_PAL:  GameFreq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1); break;
        case SYSTEM_MPAL: GameFreq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1); break;
    }
}


EXPORT void CALL audioAiLenChanged(void)
{
	uint32_t len = *AudioInfo.AI_LEN_REG;
	uint8_t* p = (uint8_t*)(AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));
    audio_batch_cb((const int16_t*)p, len / 4);
}

EXPORT m64p_error CALL audioPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context, void (*DebugCallback)(void *, int, const char *)){return M64ERR_SUCCESS;}
EXPORT m64p_error CALL audioPluginShutdown(void){return M64ERR_SUCCESS;}
EXPORT int CALL audioInitiateAudio(AUDIO_INFO Audio_Info){ AudioInfo = Audio_Info; return 1;}
EXPORT int CALL audioRomOpen(void){return 1;}
EXPORT void CALL audioRomClosed(void){}
EXPORT void CALL audioProcessAList(void){}
EXPORT void CALL audioSetSpeedFactor(int percentage){}
EXPORT void CALL audioVolumeMute(void){}
EXPORT void CALL audioVolumeUp(void){}
EXPORT void CALL audioVolumeDown(void){}
EXPORT int CALL audioVolumeGetLevel(void){return 100;}
EXPORT void CALL audioVolumeSetLevel(int level){}
EXPORT const char * CALL audioVolumeGetString(void){return "Not Supported";}

