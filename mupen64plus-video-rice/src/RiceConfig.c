/*
Copyright (C) 2003 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string/stdstring.h>
#include "../../libretro/libretro_private.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "osal_preproc.h"
#include "m64p_types.h"
#include "m64p_plugin.h"
#include "m64p_config.h"

#include "Config.h"
#include "Debugger.h"
#include "RenderBase.h"
#include "TextureManager.h"
#include "Video.h"

extern uint32_t screen_width, screen_height;
extern retro_environment_t environ_cb;

static m64p_handle l_ConfigVideoRice = NULL;
static m64p_handle l_ConfigVideoGeneral = NULL;

WindowSettingStruct windowSetting;
GlobalOptions options;
RomOptions defaultRomOptions;
RomOptions currentRomOptions;
FrameBufferOptions frameBufferOptions;
GameSetting g_curRomInfo;

static void GenerateFrameBufferOptions(void)
{
   /* GL emulates neither frame buffers nor render textures: any the ROM
      or the options ask for are only ignored */
   const bool frame_buffers   = currentRomOptions.N64FrameBufferEmuType != FRM_BUF_NONE;
   const bool render_textures = currentRomOptions.N64RenderToTextureEmuType != TXT_BUF_NONE;

   if (frame_buffers)
      currentRomOptions.N64FrameBufferEmuType = FRM_BUF_IGNORE;
   if (render_textures)
      currentRomOptions.N64RenderToTextureEmuType = TXT_BUF_IGNORE;

   frameBufferOptions.bUpdateCIInfo = frame_buffers || render_textures
         || currentRomOptions.screenUpdateSetting >= SCREEN_UPDATE_AT_CI_CHANGE;

   frameBufferOptions.bCheckBackBufs        = false;
   frameBufferOptions.bWriteBackBufToRDRAM  = false;
   frameBufferOptions.bLoadBackBufFromRDRAM = false;

   frameBufferOptions.bIgnore = true;

   frameBufferOptions.bSupportRenderTextures      = render_textures;
   frameBufferOptions.bCheckRenderTextures        = false;
   frameBufferOptions.bRenderTextureWriteBack     = false;
   frameBufferOptions.bLoadRDRAMIntoRenderTexture = false;

   frameBufferOptions.bProcessCPUWrite                    = false;
   frameBufferOptions.bProcessCPURead                     = false;
   frameBufferOptions.bAtEachFrameUpdate                  = false;
   frameBufferOptions.bIgnoreRenderTextureIfHeightUnknown = false;
}

bool InitConfiguration(void)
{
    if (ConfigOpenSection("Video-General", &l_ConfigVideoGeneral) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "Unable to open Video-General configuration section");
        return false;
    }
    if (ConfigOpenSection("Video-Rice", &l_ConfigVideoRice) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "Unable to open Video-Rice configuration section");
        return false;
    }

    ConfigSetDefaultInt(l_ConfigVideoGeneral, "ScreenWidth", 640, "Width of the output");
    ConfigSetDefaultInt(l_ConfigVideoGeneral, "ScreenHeight", 480, "Height of the output");
    ConfigSetDefaultBool(l_ConfigVideoGeneral, "VerticalSync", 0, "If true, activate the SDL_GL_SWAP_CONTROL attribute");

    ConfigSetDefaultInt(l_ConfigVideoRice, "FrameBufferSetting", FRM_BUF_NONE, "Frame Buffer Emulation (0=ROM default, 1=disable)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "FrameBufferWriteBackControl", FRM_BUF_WRITEBACK_NORMAL, "Frequency to write back the frame buffer (0=every frame, 1=every other frame, etc)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "RenderToTexture", TXT_BUF_NONE, "Render-to-texture emulation (0=none, 1=ignore, 2=normal, 3=write back, 4=write back and reload)");
#if defined(WIN32)
    ConfigSetDefaultInt(l_ConfigVideoRice, "ScreenUpdateSetting", SCREEN_UPDATE_AT_1ST_CI_CHANGE, "Control when the screen will be updated (0=ROM default, 1=VI origin update, 2=VI origin change, 3=CI change, 4=first CI change, 5=first primitive draw, 6=before screen clear, 7=after screen drawn)");  /* SCREEN_UPDATE_AT_VI_UPDATE_AND_DRAWN */
#else
    ConfigSetDefaultInt(l_ConfigVideoRice, "ScreenUpdateSetting", SCREEN_UPDATE_AT_VI_UPDATE, "Control when the screen will be updated (0=ROM default, 1=VI origin update, 2=VI origin change, 3=CI change, 4=first CI change, 5=first primitive draw, 6=before screen clear, 7=after screen drawn)");  /* SCREEN_UPDATE_AT_VI_UPDATE_AND_DRAWN */
#endif
    ConfigSetDefaultBool(l_ConfigVideoRice, "NormalAlphaBlender", false, "Force to use normal alpha blender");
    ConfigSetDefaultBool(l_ConfigVideoRice, "FastTextureLoading", false, "Use a faster algorithm to speed up texture loading and CRC computation");
    ConfigSetDefaultBool(l_ConfigVideoRice, "AccurateTextureMapping", true, "Use different texture coordinate clamping code");
    ConfigSetDefaultBool(l_ConfigVideoRice, "InN64Resolution", false, "Force emulated frame buffers to be in N64 native resolution");
    ConfigSetDefaultBool(l_ConfigVideoRice, "SaveVRAM", false, "Try to reduce Video RAM usage (should never be used)");
    ConfigSetDefaultBool(l_ConfigVideoRice, "DoubleSizeForSmallTxtrBuf", false, "Enable this option to have better render-to-texture quality");
    ConfigSetDefaultBool(l_ConfigVideoRice, "DefaultCombinerDisable", false, "Force to use normal color combiner");

    ConfigSetDefaultBool(l_ConfigVideoRice, "EnableHacks", true, "Enable game-specific settings from INI file");
    ConfigSetDefaultBool(l_ConfigVideoRice, "WinFrameMode", false, "If enabled, graphics will be drawn in WinFrame mode instead of solid and texture mode");
    ConfigSetDefaultBool(l_ConfigVideoRice, "FullTMEMEmulation", false, "N64 Texture Memory Full Emulation (may fix some games, may break others)");
    ConfigSetDefaultBool(l_ConfigVideoRice, "OpenGLVertexClipper", false, "Enable vertex clipper for fog operations");
    ConfigSetDefaultBool(l_ConfigVideoRice, "SkipFrame", false, "If this option is enabled, the plugin will skip every other frame");
    ConfigSetDefaultBool(l_ConfigVideoRice, "TexRectOnly", false, "If enabled, texture enhancement will be done only for TxtRect ucode");
    ConfigSetDefaultBool(l_ConfigVideoRice, "SmallTextureOnly", false, "If enabled, texture enhancement will be done only for textures width+height<=128");
    ConfigSetDefaultBool(l_ConfigVideoRice, "LoadHiResCRCOnly", true, "Select hi-resolution textures based only on the CRC and ignore format+size information (Glide64 compatibility)");
    ConfigSetDefaultBool(l_ConfigVideoRice, "LoadHiResTextures", false, "Enable hi-resolution texture file loading");
    ConfigSetDefaultBool(l_ConfigVideoRice, "DumpTexturesToFiles", false, "Enable texture dumping");
    ConfigSetDefaultBool(l_ConfigVideoRice, "ShowFPS", false, "Display On-screen FPS");

    ConfigSetDefaultInt(l_ConfigVideoRice, "Mipmapping", 2, "Use Mipmapping? 0=no, 1=nearest, 2=bilinear, 3=trilinear");
    ConfigSetDefaultInt(l_ConfigVideoRice, "FogMethod", 0, "Enable, Disable or Force fog generation (0=Disable, 1=Enable n64 choose, 2=Force Fog)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "ForceTextureFilter", 0, "Force to use texture filtering or not (0=auto: n64 choose, 1=force no filtering, 2=force filtering)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "TextureEnhancement", 0, "Primary texture enhancement filter (0=None, 1=2X, 2=2XSAI, 3=HQ2X, 4=LQ2X, 5=HQ4X, 6=Sharpen, 7=Sharpen More, 8=External, 9=Mirrored)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "TextureEnhancementControl", 0, "Secondary texture enhancement filter (0 = none, 1-4 = filtered)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "TextureQuality", TXT_QUALITY_DEFAULT, "Color bit depth to use for textures (0=default, 1=32 bits, 2=16 bits)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "OpenGLDepthBufferSetting", 16, "Z-buffer depth (only 16 or 32)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "MultiSampling", 0, "Enable/Disable MultiSampling (0=off, 2,4,8,16=quality)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "ColorQuality", TEXTURE_FMT_A8R8G8B8, "Color bit depth for rendering window (0=32 bits, 1=16 bits)");
    ConfigSetDefaultInt(l_ConfigVideoRice, "OpenGLRenderSetting", OGL_DEVICE, "OpenGL level to support (0=auto, 1=OGL_1.1, 2=OGL_1.2, 3=OGL_1.3, 4=OGL_1.4, 5=OGL_1.4_V2, 6=OGL_TNT2, 7=NVIDIA_OGL, 8=OGL_FRAGMENT_PROGRAM)");
    return true;
}

static void ReadConfiguration(void)
{
   struct retro_variable var = { "parallel-n64-screensize", 0 };
   bool ret = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

   if (!ret || !var.value
         || sscanf(var.value, "%ux%u", &screen_width, &screen_height) != 2)
   {
      screen_width  = 640;
      screen_height = 480;
   }

   windowSetting.uDisplayWidth = screen_width;
   windowSetting.uDisplayHeight = screen_height;
   windowSetting.bVerticalSync = ConfigGetParamBool(l_ConfigVideoGeneral, "VerticalSync");

   defaultRomOptions.N64FrameBufferEmuType = ConfigGetParamInt(l_ConfigVideoRice, "FrameBufferSetting");
   defaultRomOptions.N64FrameBufferWriteBackControl = ConfigGetParamInt(l_ConfigVideoRice, "FrameBufferWriteBackControl");
   defaultRomOptions.N64RenderToTextureEmuType = ConfigGetParamInt(l_ConfigVideoRice, "RenderToTexture");
   defaultRomOptions.screenUpdateSetting = ConfigGetParamInt(l_ConfigVideoRice, "screenUpdateSetting");

   defaultRomOptions.bNormalBlender = ConfigGetParamBool(l_ConfigVideoRice, "NormalAlphaBlender");
   defaultRomOptions.bFastTexCRC = ConfigGetParamBool(l_ConfigVideoRice, "FastTextureLoading");
   defaultRomOptions.bAccurateTextureMapping = ConfigGetParamBool(l_ConfigVideoRice, "AccurateTextureMapping");
   defaultRomOptions.bInN64Resolution = ConfigGetParamBool(l_ConfigVideoRice, "InN64Resolution");
   defaultRomOptions.bSaveVRAM = ConfigGetParamBool(l_ConfigVideoRice, "SaveVRAM");
   defaultRomOptions.bDoubleSizeForSmallTxtrBuf = ConfigGetParamBool(l_ConfigVideoRice, "DoubleSizeForSmallTxtrBuf");
   defaultRomOptions.bNormalCombiner = ConfigGetParamBool(l_ConfigVideoRice, "DefaultCombinerDisable");

   options.bEnableHacks = ConfigGetParamBool(l_ConfigVideoRice, "EnableHacks");
   options.bWinFrameMode = ConfigGetParamBool(l_ConfigVideoRice, "WinFrameMode");
   options.bFullTMEM = ConfigGetParamBool(l_ConfigVideoRice, "FullTMEMEmulation");
   options.bOGLVertexClipper = ConfigGetParamBool(l_ConfigVideoRice, "OpenGLVertexClipper");
   options.bSkipFrame = ConfigGetParamBool(l_ConfigVideoRice, "SkipFrame");
   options.bTexRectOnly = ConfigGetParamBool(l_ConfigVideoRice, "TexRectOnly");
   options.bSmallTextureOnly = ConfigGetParamBool(l_ConfigVideoRice, "SmallTextureOnly");
   options.bLoadHiResTextures = ConfigGetParamBool(l_ConfigVideoRice, "LoadHiResTextures");
   options.bLoadHiResCRCOnly = ConfigGetParamBool(l_ConfigVideoRice, "LoadHiResCRCOnly");
   options.bDumpTexturesToFiles = ConfigGetParamBool(l_ConfigVideoRice, "DumpTexturesToFiles");

   options.mipmapping = TEXTURE_NO_MIPMAP;
   options.fogMethod = ConfigGetParamInt(l_ConfigVideoRice, "FogMethod");
   options.forceTextureFilter = ConfigGetParamInt(l_ConfigVideoRice, "ForceTextureFilter");
   options.textureEnhancement = ConfigGetParamInt(l_ConfigVideoRice, "TextureEnhancement");
   options.textureEnhancementControl = ConfigGetParamInt(l_ConfigVideoRice, "TextureEnhancementControl");
   options.textureQuality = ConfigGetParamInt(l_ConfigVideoRice, "TextureQuality");
   options.OpenglDepthBufferSetting = ConfigGetParamInt(l_ConfigVideoRice, "OpenGLDepthBufferSetting");
   options.multiSampling = ConfigGetParamInt(l_ConfigVideoRice, "MultiSampling");
   options.colorQuality = ConfigGetParamInt(l_ConfigVideoRice, "ColorQuality");
   options.OpenglRenderSetting = ConfigGetParamInt(l_ConfigVideoRice, "OpenGLRenderSetting");
}

bool LoadConfiguration(void)
{
    if (l_ConfigVideoGeneral == NULL || l_ConfigVideoRice == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Rice Video configuration sections are not open!");
        return false;
    }

    /* Read config parameters from core config API and set up internal variables */
    ReadConfiguration();

    return true;
}

void GenerateCurrentRomOptions(void)
{
    currentRomOptions.N64FrameBufferEmuType     =g_curRomInfo.dwFrameBufferOption;
    currentRomOptions.N64FrameBufferWriteBackControl        =defaultRomOptions.N64FrameBufferWriteBackControl;
    currentRomOptions.N64RenderToTextureEmuType =g_curRomInfo.dwRenderToTextureOption;
    currentRomOptions.screenUpdateSetting       =g_curRomInfo.dwScreenUpdateSetting;
    currentRomOptions.bNormalCombiner           =g_curRomInfo.dwNormalCombiner;
    currentRomOptions.bNormalBlender            =g_curRomInfo.dwNormalBlender;
    currentRomOptions.bFastTexCRC               =g_curRomInfo.dwFastTextureCRC;
    currentRomOptions.bAccurateTextureMapping   =g_curRomInfo.dwAccurateTextureMapping;

    options.enableHackForGames = NO_HACK_FOR_GAME;
    if ((strncmp((char*)g_curRomInfo.szGameName, "BANJO TOOIE", 11) == 0))
    {
        options.enableHackForGames = HACK_FOR_BANJO_TOOIE;
    }
    else if ((strncmp((char*)g_curRomInfo.szGameName, "DR.MARIO", 8) == 0))
    {
        options.enableHackForGames = HACK_FOR_DR_MARIO;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "Pilot"))
    {
        options.enableHackForGames = HACK_FOR_PILOT_WINGS;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "YOSHI"))
    {
        options.enableHackForGames = HACK_FOR_YOSHI;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "NITRO"))
    {
        options.enableHackForGames = HACK_FOR_NITRO;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "TONY HAWK"))
    {
        options.enableHackForGames = HACK_FOR_TONYHAWK;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "THPS"))
    {
        options.enableHackForGames = HACK_FOR_TONYHAWK;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "SPIDERMAN"))
    {
        options.enableHackForGames = HACK_FOR_TONYHAWK;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "NASCAR"))
    {
        options.enableHackForGames = HACK_FOR_NASCAR;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "ZELDA") != 0) && (strstr((char*)g_curRomInfo.szGameName, "MASK") != 0))
    {
        options.enableHackForGames = HACK_FOR_ZELDA_MM;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "ZELDA") != 0))
    {
        options.enableHackForGames = HACK_FOR_ZELDA;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "Ogre") != 0))
    {
        options.enableHackForGames = HACK_FOR_OGRE_BATTLE;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "TWINE") != 0))
    {
        options.enableHackForGames = HACK_FOR_TWINE;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "Squadron") != 0))
    {
        options.enableHackForGames = HACK_FOR_ROGUE_SQUADRON;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "Baseball") != 0) && (strstr((char*)g_curRomInfo.szGameName, "Star") != 0))
    {
        options.enableHackForGames = HACK_FOR_ALL_STAR_BASEBALL;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "Tigger") != 0) && (strstr((char*)g_curRomInfo.szGameName, "Honey") != 0))
    {
        options.enableHackForGames = HACK_FOR_TIGER_HONEY_HUNT;
    }
    else if ((strstr((char*)g_curRomInfo.szGameName, "Bust") != 0) && (strstr((char*)g_curRomInfo.szGameName, "Move") != 0))
    {
        options.enableHackForGames = HACK_FOR_BUST_A_MOVE;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "MarioTennis"))
    {
        options.enableHackForGames = HACK_FOR_MARIO_TENNIS;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "SUPER BOWLING"))
    {
        options.enableHackForGames = HACK_FOR_SUPER_BOWLING;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "CONKER"))
    {
        options.enableHackForGames = HACK_FOR_CONKER;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "MK_MYTHOLOGIES"))
    {
        options.enableHackForGames = HACK_REVERSE_Y_COOR;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "Fighting Force"))
    {
        options.enableHackForGames = HACK_REVERSE_XY_COOR;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "GOLDENEYE"))
    {
        options.enableHackForGames = HACK_FOR_GOLDEN_EYE;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "F-ZERO"))
    {
        options.enableHackForGames = HACK_FOR_FZERO;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "Command&Conquer"))
    {
        options.enableHackForGames = HACK_FOR_COMMANDCONQUER;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "READY 2 RUMBLE"))
    {
        options.enableHackForGames = HACK_FOR_RUMBLE;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "READY to RUMBLE"))
    {
        options.enableHackForGames = HACK_FOR_RUMBLE;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "South Park Rally"))
    {
        options.enableHackForGames = HACK_FOR_SOUTH_PARK_RALLY;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "Extreme G 2"))
    {
        options.enableHackForGames = HACK_FOR_EXTREME_G2;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "MarioGolf64"))
    {
        options.enableHackForGames = HACK_FOR_MARIO_GOLF;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "MLB FEATURING"))
    {
        options.enableHackForGames = HACK_FOR_MLB;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "POLARISSNOCROSS"))
    {
        options.enableHackForGames = HACK_FOR_POLARISSNOCROSS;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "TOP GEAR RALLY"))
    {
        options.enableHackForGames = HACK_FOR_TOPGEARRALLY;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "DUKE NUKEM"))
    {
        options.enableHackForGames = HACK_FOR_DUKE_NUKEM;
    }
    else if (string_starts_with_case_insensitive((const char*)g_curRomInfo.szGameName, "MARIOKART64"))
    {
        options.enableHackForGames = HACK_FOR_MARIO_KART;
    }

    if (options.enableHackForGames != NO_HACK_FOR_GAME)
        DebugMessage(M64MSG_INFO, "Enabled hacks for game: '%s'", g_curRomInfo.szGameName);

    if( currentRomOptions.N64FrameBufferEmuType == 0 )      currentRomOptions.N64FrameBufferEmuType = defaultRomOptions.N64FrameBufferEmuType;
    else currentRomOptions.N64FrameBufferEmuType--;
    if( currentRomOptions.N64RenderToTextureEmuType == 0 )  currentRomOptions.N64RenderToTextureEmuType = defaultRomOptions.N64RenderToTextureEmuType;
    else currentRomOptions.N64RenderToTextureEmuType--;
    if( currentRomOptions.screenUpdateSetting == 0 )        currentRomOptions.screenUpdateSetting = defaultRomOptions.screenUpdateSetting;

    options.bUseFullTMEM = ((options.bFullTMEM && (g_curRomInfo.dwFullTMEM == 0)) || g_curRomInfo.dwFullTMEM == 2);

    GenerateFrameBufferOptions();

    if( options.enableHackForGames == HACK_FOR_MARIO_GOLF || options.enableHackForGames == HACK_FOR_MARIO_TENNIS )
    {
        frameBufferOptions.bIgnoreRenderTextureIfHeightUnknown = true;
    }
}

void ROM_GetRomNameFromHeader(unsigned char * szName, ROMHeader * pHdr)
{
    unsigned char * p;

    memcpy(szName, pHdr->szName, 20);
    szName[20] = '\0';

    p = szName + (strlen((char*)szName) -1);        /* -1 to skip null */
    while (p >= szName && *p == ' ')
    {
        *p = 0;
        p--;
    }
}

uint32_t CountryCodeToTVSystem(uint32_t countryCode)
{
    uint32_t system;
    switch(countryCode)
    {
        /* Demo */
    case 0:
        system = TV_SYSTEM_NTSC;
        break;

    case '7':
        system = TV_SYSTEM_NTSC;
        break;

    case 0x41:
        system = TV_SYSTEM_NTSC;
        break;

        /* Germany */
    case 0x44:
        system = TV_SYSTEM_PAL;
        break;

        /* USA */
    case 0x45:
        system = TV_SYSTEM_NTSC;
        break;

        /* France */
    case 0x46:
        system = TV_SYSTEM_PAL;
        break;

        /* Italy */
    case 'I':
        system = TV_SYSTEM_PAL;
        break;

        /* Japan */
    case 0x4A:
        system = TV_SYSTEM_NTSC;
        break;

        /* Europe - PAL */
    case 0x50:
        system = TV_SYSTEM_PAL;
        break;

    case 'S':   /* Spain */
        system = TV_SYSTEM_PAL;
        break;

        /* Australia */
    case 0x55:
        system = TV_SYSTEM_PAL;
        break;

    case 0x58:
        system = TV_SYSTEM_PAL;
        break;

        /* Australia */
    case 0x59:
        system = TV_SYSTEM_PAL;
        break;

    case 0x20:
    case 0x21:
    case 0x38:
    case 0x70:
        system = TV_SYSTEM_PAL;
        break;

        /* ??? */
    default:
        system = TV_SYSTEM_PAL;
        break;
    }

    return system;
}

/* the options OGLTexture.c makes and puts textures on the GPU by */
uint32_t RiceConfig_TextureQuality(void) { return options.textureQuality; }
uint32_t RiceConfig_ColorQuality(void)   { return options.colorQuality; }
uint32_t RiceConfig_Mipmapping(void)     { return options.mipmapping; }

/* ConvertImage.c's: textures converted from TMEM itself */
bool RiceConfig_UseFullTMEM(void)       { return options.bUseFullTMEM; }
