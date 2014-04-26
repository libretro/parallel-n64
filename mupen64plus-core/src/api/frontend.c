/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-core - api/frontend.c                                     *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 CasualJames                                        *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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
                       
/* This file contains the Core front-end functions which will be exported
 * outside of the core library.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define M64P_CORE_PROTOTYPES 1
#include "m64p_types.h"
#include "callbacks.h"
#include "m64p_config.h"
#include "m64p_frontend.h"
#include "config.h"
#include "vidext.h"

#include "main/main.h"
#include "main/rom.h"
#include "main/version.h"
#include "main/util.h"
#include "plugin/plugin.h"

/* some local state variables */
static int l_CoreInit = 0;
static int l_ROMOpen = 0;

/* functions exported outside of libmupen64plus to front-end application */
EXPORT m64p_error CALL CoreStartup(int APIVersion, const char *ConfigPath, const char *DataPath, void *Context,
                                   void (*DebugCallback)(void *, int, const char *), void *Context2,
                                   void (*StateCallback)(void *, m64p_core_param, int))
{
    if (l_CoreInit)
        return M64ERR_ALREADY_INIT;

    /* very first thing is to set the callback functions for debug info and state changing*/
    SetDebugCallback(DebugCallback, Context);
    SetStateCallback(StateCallback, Context2);

    /* check front-end's API version */
    if ((APIVersion & 0xffff0000) != (FRONTEND_API_VERSION & 0xffff0000))
    {
        DebugMessage(M64MSG_ERROR, "CoreStartup(): Front-end (API version %i.%i.%i) is incompatible with this core (API %i.%i.%i)",
                     VERSION_PRINTF_SPLIT(APIVersion), VERSION_PRINTF_SPLIT(FRONTEND_API_VERSION));
        return M64ERR_INCOMPATIBLE;
    }

    /* next, start up the configuration handling code by loading and parsing the config file */
    if (ConfigInit(ConfigPath, DataPath) != M64ERR_SUCCESS)
        return M64ERR_INTERNAL;

    /* set default configuration parameter values for Core */
    if (ConfigOpenSection("Core", &g_CoreConfig) != M64ERR_SUCCESS || g_CoreConfig == NULL)
        return M64ERR_INTERNAL;

    if (!main_set_core_defaults())
        return M64ERR_INTERNAL;

    /* The ROM database contains MD5 hashes, goodnames, and some game-specific parameters */
    romdatabase_open();

    l_CoreInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL CoreShutdown(void)
{
    if (!l_CoreInit)
        return M64ERR_NOT_INIT;

    /* close down some core sub-systems */
    romdatabase_close();
    ConfigShutdown();

    l_CoreInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL CoreDoCommand(m64p_command Command, int ParamInt, void *ParamPtr)
{
    m64p_error rval;
    int keysym, keymod;
	(void)keysym;
	(void)keymod;

    if (!l_CoreInit)
        return M64ERR_NOT_INIT;

    switch(Command)
    {
        case M64CMD_NOP:
            return M64ERR_SUCCESS;
        case M64CMD_ROM_OPEN:
            if (g_EmulatorRunning || l_ROMOpen)
                return M64ERR_INVALID_STATE;
            if (ParamPtr == NULL || ParamInt < 4096)
                return M64ERR_INPUT_ASSERT;
            rval = open_rom((const unsigned char *) ParamPtr, ParamInt);
            if (rval == M64ERR_SUCCESS)
            {
                l_ROMOpen = 1;
            }
            return rval;
        case M64CMD_ROM_CLOSE:
            if (g_EmulatorRunning || !l_ROMOpen)
                return M64ERR_INVALID_STATE;
            l_ROMOpen = 0;
            return close_rom();
        case M64CMD_ROM_GET_HEADER:
            if (!l_ROMOpen)
                return M64ERR_INVALID_STATE;
            if (ParamPtr == NULL)
                return M64ERR_INPUT_ASSERT;
            if (sizeof(m64p_rom_header) < ParamInt)
                ParamInt = sizeof(m64p_rom_header);
            memcpy(ParamPtr, &ROM_HEADER, ParamInt);
            // Mupen64Plus used to keep a m64p_rom_header with a clean ROM name
            // Keep returning a clean ROM name for backwards compatibility
            if (ParamInt >= 0x20)
            {
                int size = (ParamInt >= 0x20 + 20) ? 20 : (ParamInt - 0x20);
                memcpy((char *)ParamPtr + 0x20, ROM_PARAMS.headername, size);
            }
            return M64ERR_SUCCESS;
        case M64CMD_ROM_GET_SETTINGS:
            if (!l_ROMOpen)
                return M64ERR_INVALID_STATE;
            if (ParamPtr == NULL)
                return M64ERR_INPUT_ASSERT;
            if (sizeof(m64p_rom_settings) < ParamInt)
                ParamInt = sizeof(m64p_rom_settings);
            memcpy(ParamPtr, &ROM_SETTINGS, ParamInt);
            return M64ERR_SUCCESS;
        case M64CMD_EXECUTE:
            if (g_EmulatorRunning || !l_ROMOpen)
                return M64ERR_INVALID_STATE;
            /* the main_run() function will not return until the player has quit the game */
            rval = main_run();
            return rval;
        case M64CMD_STOP:
            if (!g_EmulatorRunning)
                return M64ERR_INVALID_STATE;
            /* this stop function is asynchronous.  The emulator may not terminate until later */
            return main_core_state_set(M64CORE_EMU_STATE, M64EMU_STOPPED);
        case M64CMD_PAUSE:
            if (!g_EmulatorRunning)
                return M64ERR_INVALID_STATE;
            return main_core_state_set(M64CORE_EMU_STATE, M64EMU_PAUSED);
        case M64CMD_RESUME:
            if (!g_EmulatorRunning)
                return M64ERR_INVALID_STATE;
            return main_core_state_set(M64CORE_EMU_STATE, M64EMU_RUNNING);
        case M64CMD_CORE_STATE_QUERY:
            if (ParamPtr == NULL)
                return M64ERR_INPUT_ASSERT;
            return main_core_state_query((m64p_core_param) ParamInt, (int *) ParamPtr);
        case M64CMD_CORE_STATE_SET:
            if (ParamPtr == NULL)
                return M64ERR_INPUT_ASSERT;
            return main_core_state_set((m64p_core_param) ParamInt, *((int *)ParamPtr));
        case M64CMD_SET_FRAME_CALLBACK:
            g_FrameCallback = (m64p_frame_callback) ParamPtr;
            return M64ERR_SUCCESS;
        case M64CMD_READ_SCREEN:
            if (!g_EmulatorRunning)
                return M64ERR_INVALID_STATE;
            if (ParamPtr == NULL)
                return M64ERR_INPUT_ASSERT;
            if (ParamInt < 0 || ParamInt > 1)
                return M64ERR_INPUT_INVALID;
            return main_read_screen(ParamPtr, ParamInt);
        case M64CMD_RESET:
            if (!g_EmulatorRunning)
                return M64ERR_INVALID_STATE;
            if (ParamInt < 0 || ParamInt > 1)
                return M64ERR_INPUT_INVALID;
            return main_reset(ParamInt);
        default:
            return M64ERR_INPUT_INVALID;
    }

    return M64ERR_INTERNAL;
}

EXPORT m64p_error CALL CoreGetRomSettings(m64p_rom_settings *RomSettings, int RomSettingsLength, int Crc1, int Crc2)
{
    romdatabase_entry* entry;
    int i;

    if (!l_CoreInit)
        return M64ERR_NOT_INIT;
    if (RomSettings == NULL)
        return M64ERR_INPUT_ASSERT;
    if (RomSettingsLength < sizeof(m64p_rom_settings))
        return M64ERR_INPUT_INVALID;

    /* Look up this ROM in the .ini file and fill in goodname, etc */
    entry = ini_search_by_crc(Crc1, Crc2);
    if (entry == NULL)
        return M64ERR_INPUT_NOT_FOUND;

    strncpy(RomSettings->goodname, entry->goodname, 255);
    RomSettings->goodname[255] = '\0';
    for (i = 0; i < 16; i++)
        sprintf(RomSettings->MD5 + i*2, "%02X", entry->md5[i]);
    RomSettings->MD5[32] = '\0';
    RomSettings->savetype = entry->savetype;
    RomSettings->status = entry->status;
    RomSettings->players = entry->players;
    RomSettings->rumble = entry->rumble;

    return M64ERR_SUCCESS;
}


