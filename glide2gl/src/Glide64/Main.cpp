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

#include "Gfx_1.3.h"
#include "Ini.h"
#include "Config.h"
#include "Util.h"
#include "3dmath.h"
#include "Combine.h"
#include "TexCache.h"
#include "CRC.h"
#include "FBtoScreen.h"
#include "DepthBufferRender.h"

#if defined(__GNUC__)
#include <sys/time.h>
#elif defined(__MSC__)
#include <time.h>
#define PATH_MAX MAX_PATH
#endif
#ifdef TEXTURE_FILTER // Hiroshi Morii <koolsmoky@users.sourceforge.net>
#include <stdarg.h>
int  ghq_dmptex_toggle_key = 0;
#endif
#if defined(__MINGW32__)
#define swprintf _snwprintf
#define vswprintf _vsnwprintf
#endif

#define G64_VERSION "G64 Mk2"
#define RELTIME "Date: " __DATE__// " Time: " __TIME__

#ifdef EXT_LOGGING
std::ofstream extlog;
#endif

#ifdef LOGGING
std::ofstream loga;
#endif

#ifdef RDP_LOGGING
int log_open = FALSE;
std::ofstream rdp_log;
#endif

#ifdef RDP_ERROR_LOG
int elog_open = FALSE;
std::ofstream rdp_err;
#endif

#ifdef __LIBRETRO__ // Prefix API
#define VIDEO_TAG(X) glide64##X

#define ReadScreen2 VIDEO_TAG(ReadScreen2)
#define PluginStartup VIDEO_TAG(PluginStartup)
#define PluginShutdown VIDEO_TAG(PluginShutdown)
#define PluginGetVersion VIDEO_TAG(PluginGetVersion)
#define CaptureScreen VIDEO_TAG(CaptureScreen)
#define ChangeWindow VIDEO_TAG(ChangeWindow)
#define CloseDLL VIDEO_TAG(CloseDLL)
#define DllTest VIDEO_TAG(DllTest)
#define DrawScreen VIDEO_TAG(DrawScreen)
#define GetDllInfo VIDEO_TAG(GetDllInfo)
#define InitiateGFX VIDEO_TAG(InitiateGFX)
#define MoveScreen VIDEO_TAG(MoveScreen)
#define RomClosed VIDEO_TAG(RomClosed)
#define RomOpen VIDEO_TAG(RomOpen)
#define ShowCFB VIDEO_TAG(ShowCFB)
#define SetRenderingCallback VIDEO_TAG(SetRenderingCallback)
#define UpdateScreen VIDEO_TAG(UpdateScreen)
#define ViStatusChanged VIDEO_TAG(ViStatusChanged)
#define ViWidthChanged VIDEO_TAG(ViWidthChanged)
#define ReadScreen VIDEO_TAG(ReadScreen)
#define FBGetFrameBufferInfo VIDEO_TAG(FBGetFrameBufferInfo)
#define FBRead VIDEO_TAG(FBRead)
#define FBWrite VIDEO_TAG(FBWrite)
#define ProcessDList VIDEO_TAG(ProcessDList)
#define ProcessRDPList VIDEO_TAG(ProcessRDPList)
#define ResizeVideoOutput VIDEO_TAG(ResizeVideoOutput)
#endif

GFX_INFO gfx;

int fullscreen = FALSE;
int romopen = FALSE;
GrContext_t gfx_context = 0;
int exception = FALSE;

int evoodoo = 0;
int ev_fullscreen = 0;

uint32_t region = 0;

// ref rate
// 60=0x0, 70=0x1, 72=0x2, 75=0x3, 80=0x4, 90=0x5, 100=0x6, 85=0x7, 120=0x8, none=0xff

unsigned long BMASK = 0x7FFFFF;
// Reality display processor structure
RDP rdp;

SETTINGS settings = { FALSE, 640, 480, GR_RESOLUTION_640x480, 0 };

VOODOO voodoo = {0, 0, 0, 0,
                 0, 0
                };

uint32_t   offset_textures = 0;
uint32_t   offset_texbuf1 = 0;

int    capture_screen = 0;
char    capture_path[256];

// SOME FUNCTION DEFINITIONS 

static void DrawFrameBuffer ();


static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;

void _ChangeSize ()
{
  rdp.scale_1024 = settings.scr_res_x / 1024.0f;
  rdp.scale_768 = settings.scr_res_y / 768.0f;

//  float res_scl_x = (float)settings.res_x / 320.0f;
  float res_scl_y = (float)settings.res_y / 240.0f;

  uint32_t scale_x = *gfx.VI_X_SCALE_REG & 0xFFF;
  if (!scale_x) return;
  uint32_t scale_y = *gfx.VI_Y_SCALE_REG & 0xFFF;
  if (!scale_y) return;

  float fscale_x = (float)scale_x / 1024.0f;
  float fscale_y = (float)scale_y / 2048.0f;

  uint32_t dwHStartReg = *gfx.VI_H_START_REG;
  uint32_t dwVStartReg = *gfx.VI_V_START_REG;

  uint32_t hstart = dwHStartReg >> 16;
  uint32_t hend = dwHStartReg & 0xFFFF;

  // dunno... but sometimes this happens
  if (hend == hstart) hend = (int)(*gfx.VI_WIDTH_REG / fscale_x);

  uint32_t vstart = dwVStartReg >> 16;
  uint32_t vend = dwVStartReg & 0xFFFF;

  rdp.vi_width = (hend - hstart) * fscale_x;
  rdp.vi_height = (vend - vstart) * fscale_y * 1.0126582f;
  float aspect = (settings.adjust_aspect && (fscale_y > fscale_x) && (rdp.vi_width > rdp.vi_height)) ? fscale_x/fscale_y : 1.0f;

#ifdef LOGGING
  sprintf (out_buf, "hstart: %d, hend: %d, vstart: %d, vend: %d\n", hstart, hend, vstart, vend);
  LOG (out_buf);
  sprintf (out_buf, "size: %d x %d\n", (int)rdp.vi_width, (int)rdp.vi_height);
  LOG (out_buf);
#endif

  rdp.scale_x = (float)settings.res_x / rdp.vi_width;
  if (region > 0 && settings.pal230)
  {
    // odd... but pal games seem to want 230 as height...
    rdp.scale_y = res_scl_y * (230.0f / rdp.vi_height)  * aspect;
  }
  else
  {
    rdp.scale_y = (float)settings.res_y / rdp.vi_height * aspect;
  }
  //  rdp.offset_x = settings.offset_x * res_scl_x;
  //  rdp.offset_y = settings.offset_y * res_scl_y;
  //rdp.offset_x = 0;
  //  rdp.offset_y = 0;
  rdp.offset_y = ((float)settings.res_y - rdp.vi_height * rdp.scale_y) * 0.5f;
  if (((uint32_t)rdp.vi_width <= (*gfx.VI_WIDTH_REG)/2) && (rdp.vi_width > rdp.vi_height))
    rdp.scale_y *= 0.5f;

  rdp.scissor_o.ul_x = 0;
  rdp.scissor_o.ul_y = 0;
  rdp.scissor_o.lr_x = (uint32_t)rdp.vi_width;
  rdp.scissor_o.lr_y = (uint32_t)rdp.vi_height;

  rdp.update |= UPDATE_VIEWPORT | UPDATE_SCISSOR;
}

void ChangeSize ()
{
  switch (settings.aspectmode)
  {
     case 0: //4:3
        if (settings.scr_res_x >= settings.scr_res_y * 4.0f / 3.0f) {
           settings.res_y = settings.scr_res_y;
           settings.res_x = (uint32_t)(settings.res_y * 4.0f / 3.0f);
        } else {
           settings.res_x = settings.scr_res_x;
           settings.res_y = (uint32_t)(settings.res_x / 4.0f * 3.0f);
        }
        break;
     case 1: //16:9
        if (settings.scr_res_x >= settings.scr_res_y * 16.0f / 9.0f) {
           settings.res_y = settings.scr_res_y;
           settings.res_x = (uint32_t)(settings.res_y * 16.0f / 9.0f);
        } else {
           settings.res_x = settings.scr_res_x;
           settings.res_y = (uint32_t)(settings.res_x / 16.0f * 9.0f);
        }
        break;
     default: //stretch or original
        settings.res_x = settings.scr_res_x;
        settings.res_y = settings.scr_res_y;
  }
  _ChangeSize ();
  rdp.offset_x = (settings.scr_res_x - settings.res_x) / 2.0f;
  float offset_y = (settings.scr_res_y - settings.res_y) / 2.0f;
  settings.res_x += (uint32_t)rdp.offset_x;
  settings.res_y += (uint32_t)offset_y;
  rdp.offset_y += offset_y;
  if (settings.aspectmode == 3) // original
  {
	  rdp.scale_x = rdp.scale_y = 1.0f;
	  rdp.offset_x = (settings.scr_res_x - rdp.vi_width) / 2.0f;
	  rdp.offset_y = (settings.scr_res_y - rdp.vi_height) / 2.0f;
  }
  //	settings.res_x = settings.scr_res_x;
  //	settings.res_y = settings.scr_res_y;
}

void WriteLog(m64p_msg_level level, const char *msg, ...)
{
  char buf[1024];
  va_list args;
  va_start(args, msg);
  vsnprintf(buf, 1023, msg, args);
  buf[1023]='\0';
  va_end(args);
  if (l_DebugCallback)
  {
    l_DebugCallback(l_DebugCallContext, level, buf);
  }
  else
     fprintf(stdout, buf);
}

void ReadSettings ()
{
  //  LOG("ReadSettings\n");
  if (!Config_Open())
  {
    ERRLOG("Could not open configuration!");
    return;
  }

  settings.card_id = (uint8_t)Config_ReadInt ("card_id", "Card ID", 0, TRUE, FALSE);
  //settings.lang_id not needed
  // depth_bias = -Config_ReadInt ("depth_bias", "Depth bias level", 0, TRUE, FALSE);
  settings.scr_res_x = settings.res_x = Config_ReadScreenInt("ScreenWidth");
  settings.scr_res_y = settings.res_y = Config_ReadScreenInt("ScreenHeight");

  settings.vsync = (int)Config_ReadInt ("vsync", "Vertical sync", 0);
  settings.ssformat = (int)Config_ReadInt("ssformat", "TODO:ssformat", 0);
  //settings.fast_crc = (int)Config_ReadInt ("fast_crc", "Fast CRC", 0);

  settings.show_fps = (uint8_t)Config_ReadInt ("show_fps", "Display performance stats (add together desired flags): 1=FPS counter, 2=VI/s counter, 4=% speed, 8=FPS transparent", 0, TRUE, FALSE);
  settings.clock = (int)Config_ReadInt ("clock", "Clock enabled", 0);
  settings.clock_24_hr = (int)Config_ReadInt ("clock_24_hr", "Clock is 24-hour", 0);
  // settings.advanced_options only good for GUI config
  // settings.texenh_options = only good for GUI config
  //settings.use_hotkeys = ini->Read("hotkeys", 1l);

  settings.wrpResolution = (uint8_t)Config_ReadInt ("wrpResolution", "Wrapper resolution", 0, TRUE, FALSE);
  settings.wrpVRAM = (uint8_t)Config_ReadInt ("wrpVRAM", "Wrapper VRAM", 0, TRUE, FALSE);
  settings.wrpFBO = (int)Config_ReadInt ("wrpFBO", "Wrapper FBO", 1, TRUE, TRUE);
  settings.wrpAnisotropic = (int)Config_ReadInt ("wrpAnisotropic", "Wrapper Anisotropic Filtering", 0, TRUE, TRUE);

#ifndef _ENDUSER_RELEASE_
  settings.autodetect_ucode = (int)Config_ReadInt ("autodetect_ucode", "Auto-detect microcode", 1);
  settings.ucode = (uint32_t)Config_ReadInt ("ucode", "Force microcode", 2, TRUE, FALSE);
  settings.wfmode = (int)Config_ReadInt ("wfmode", "Wireframe mode: 0=Normal colors, 1=Vertex colors, 2=Red only", 1, TRUE, FALSE);

  settings.logging = (int)Config_ReadInt ("logging", "Logging", 0);
  settings.log_clear = (int)Config_ReadInt ("log_clear", "", 0);

  settings.run_in_window = (int)Config_ReadInt ("run_in_window", "", 0);

  settings.elogging = (int)Config_ReadInt ("elogging", "", 0);
  settings.filter_cache = (int)Config_ReadInt ("filter_cache", "Filter cache", 0);
  settings.unk_as_red = (int)Config_ReadInt ("unk_as_red", "Display unknown combines as red", 0);
  settings.log_unk = (int)Config_ReadInt ("log_unk", "Log unknown combines", 0);
  settings.unk_clear = (int)Config_ReadInt ("unk_clear", "", 0);
#else
  settings.autodetect_ucode = TRUE;
  settings.ucode = 2;
  settings.wfmode = 0;
  settings.logging = FALSE;
  settings.log_clear = FALSE;
  settings.run_in_window = FALSE;
  settings.elogging = FALSE;
  settings.filter_cache = FALSE;
  settings.unk_as_red = FALSE;
  settings.log_unk = FALSE;
  settings.unk_clear = FALSE;
#endif

#ifdef TEXTURE_FILTER
  
  // settings.ghq_fltr range is 0 through 6
  // Filters:\nApply a filter to either smooth or sharpen textures.\nThere are 4 different smoothing filters and 2 different sharpening filters.\nThe higher the number, the stronger the effect,\ni.e. \"Smoothing filter 4\" will have a much more noticeable effect than \"Smoothing filter 1\".\nBe aware that performance may have an impact depending on the game and/or the PC.\n[Recommended: your preference]
  // _("None"),
  // _("Smooth filtering 1"),
  // _("Smooth filtering 2"),
  // _("Smooth filtering 3"),
  // _("Smooth filtering 4"),
  // _("Sharp filtering 1"),
  // _("Sharp filtering 2")

// settings.ghq_cmpr 0=S3TC and 1=FXT1

//settings.ghq_ent is ___
// "Texture enhancement:\n7 different filters are selectable here, each one with a distinctive look.\nBe aware of possible performance impacts.\n\nIMPORTANT: 'Store' mode - saves textures in cache 'as is'. It can improve performance in games, which load many textures.\nDisable 'Ignore backgrounds' option for better result.\n\n[Recommended: your preference]"



  settings.ghq_fltr = Config_ReadInt ("ghq_fltr", "Texture Enhancement: Smooth/Sharpen Filters", 0, TRUE, FALSE);
  settings.ghq_cmpr = Config_ReadInt ("ghq_cmpr", "Texture Compression: 0 for S3TC, 1 for FXT1", 0, TRUE, FALSE);
  settings.ghq_enht = Config_ReadInt ("ghq_enht", "Texture Enhancement: More filters", 0, TRUE, FALSE);
  settings.ghq_hirs = Config_ReadInt ("ghq_hirs", "Hi-res texture pack format (0 for none, 1 for Rice)", 0, TRUE, FALSE);
  settings.ghq_enht_cmpr  = Config_ReadInt ("ghq_enht_cmpr", "Compress texture cache with S3TC or FXT1", 0, TRUE, TRUE);
  settings.ghq_enht_tile = Config_ReadInt ("ghq_enht_tile", "Tile textures (saves memory but could cause issues)", 0, TRUE, FALSE);
  settings.ghq_enht_f16bpp = Config_ReadInt ("ghq_enht_f16bpp", "Force 16bpp textures (saves ram but lower quality)", 0, TRUE, TRUE);
  settings.ghq_enht_gz  = Config_ReadInt ("ghq_enht_gz", "Compress texture cache", 1, TRUE, TRUE);
  settings.ghq_enht_nobg  = Config_ReadInt ("ghq_enht_nobg", "Don't enhance textures for backgrounds", 0, TRUE, TRUE);
  settings.ghq_hirs_cmpr  = Config_ReadInt ("ghq_hirs_cmpr", "Enable S3TC and FXT1 compression", 0, TRUE, TRUE);
  settings.ghq_hirs_tile = Config_ReadInt ("ghq_hirs_tile", "Tile hi-res textures (saves memory but could cause issues)", 0, TRUE, TRUE);
  settings.ghq_hirs_f16bpp = Config_ReadInt ("ghq_hirs_f16bpp", "Force 16bpp hi-res textures (saves ram but lower quality)", 0, TRUE, TRUE);
  settings.ghq_hirs_gz  = Config_ReadInt ("ghq_hirs_gz", "Compress hi-res texture cache", 1, TRUE, TRUE);
  settings.ghq_hirs_altcrc = Config_ReadInt ("ghq_hirs_altcrc", "Alternative CRC calculation -- emulates Rice bug", 1, TRUE, TRUE);
  settings.ghq_cache_save = Config_ReadInt ("ghq_cache_save", "Save tex cache to disk", 1, TRUE, TRUE);
  settings.ghq_cache_size = Config_ReadInt ("ghq_cache_size", "Texture Cache Size (MB)", 128, TRUE, FALSE);
  settings.ghq_hirs_let_texartists_fly = Config_ReadInt ("ghq_hirs_let_texartists_fly", "Use full alpha channel -- could cause issues for some tex packs", 0, TRUE, TRUE);
  settings.ghq_hirs_dump = Config_ReadInt ("ghq_hirs_dump", "Dump textures", 0, FALSE, TRUE);
#endif
}

void ReadSpecialSettings (const char * name)
{
  //  char buf [256];
  //  sprintf(buf, "ReadSpecialSettings. Name: %s\n", name);
  //  LOG(buf);
  settings.hacks = 0;

  //detect games which require special hacks
  if (strstr(name, (const char *)"ZELDA") || strstr(name, (const char *)"MASK"))
    settings.hacks |= hack_Zelda;
  else if (strstr(name, (const char *)"ROADSTERS TROPHY"))
    settings.hacks |= hack_Zelda;
  else if (strstr(name, (const char *)"Diddy Kong Racing"))
    settings.hacks |= hack_Diddy;
  else if (strstr(name, (const char *)"Tonic Trouble"))
    settings.hacks |= hack_Tonic;
  else if (strstr(name, (const char *)"All") && strstr(name, (const char *)"Star") && strstr(name, (const char *)"Baseball"))
    settings.hacks |= hack_ASB;
  else if (strstr(name, (const char *)"Beetle") || strstr(name, (const char *)"BEETLE") || strstr(name, (const char *)"HSV"))
    settings.hacks |= hack_BAR;
  else if (strstr(name, (const char *)"I S S 64") || strstr(name, (const char *)"J WORLD SOCCER3") || strstr(name, (const char *)"PERFECT STRIKER") || strstr(name, (const char *)"RONALDINHO SOCCER"))
    settings.hacks |= hack_ISS64;
  else if (strstr(name, (const char *)"MARIOKART64"))
    settings.hacks |= hack_MK64;
  else if (strstr(name, (const char *)"NITRO64"))
    settings.hacks |= hack_WCWnitro;
  else if (strstr(name, (const char *)"CHOPPER_ATTACK") || strstr(name, (const char *)"WILD CHOPPERS"))
    settings.hacks |= hack_Chopper;
  else if (strstr(name, (const char *)"Resident Evil II") || strstr(name, (const char *)"BioHazard II"))
    settings.hacks |= hack_RE2;
  else if (strstr(name, (const char *)"YOSHI STORY"))
    settings.hacks |= hack_Yoshi;
  else if (strstr(name, (const char *)"F-Zero X") || strstr(name, (const char *)"F-ZERO X"))
    settings.hacks |= hack_Fzero;
  else if (strstr(name, (const char *)"PAPER MARIO") || strstr(name, (const char *)"MARIO STORY"))
    settings.hacks |= hack_PMario;
  else if (strstr(name, (const char *)"TOP GEAR RALLY 2"))
    settings.hacks |= hack_TGR2;
  else if (strstr(name, (const char *)"TOP GEAR RALLY"))
    settings.hacks |= hack_TGR;
  else if (strstr(name, (const char *)"Top Gear Hyper Bike"))
    settings.hacks |= hack_Hyperbike;
  else if (strstr(name, (const char *)"Killer Instinct Gold") || strstr(name, (const char *)"KILLER INSTINCT GOLD"))
    settings.hacks |= hack_KI;
  else if (strstr(name, (const char *)"Knockout Kings 2000"))
    settings.hacks |= hack_Knockout;
  else if (strstr(name, (const char *)"LEGORacers"))
    settings.hacks |= hack_Lego;
  else if (strstr(name, (const char *)"OgreBattle64"))
    settings.hacks |= hack_Ogre64;
  else if (strstr(name, (const char *)"Pilot Wings64"))
    settings.hacks |= hack_Pilotwings;
  else if (strstr(name, (const char *)"Supercross"))
    settings.hacks |= hack_Supercross;
  else if (strstr(name, (const char *)"STARCRAFT 64"))
    settings.hacks |= hack_Starcraft;
  else if (strstr(name, (const char *)"BANJO KAZOOIE 2") || strstr(name, (const char *)"BANJO TOOIE"))
    settings.hacks |= hack_Banjo2;
  else if (strstr(name, (const char *)"FIFA: RTWC 98") || strstr(name, (const char *)"RoadToWorldCup98"))
    settings.hacks |= hack_Fifa98;
  else if (strstr(name, (const char *)"Mega Man 64") || strstr(name, (const char *)"RockMan Dash"))
    settings.hacks |= hack_Megaman;
  else if (strstr(name, (const char *)"MISCHIEF MAKERS") || strstr(name, (const char *)"TROUBLE MAKERS"))
    settings.hacks |= hack_Makers;
  else if (strstr(name, (const char *)"GOLDENEYE"))
    settings.hacks |= hack_GoldenEye;
  else if (strstr(name, (const char *)"PUZZLE LEAGUE"))
    settings.hacks |= hack_PPL;

  Ini * ini = Ini::OpenIni();
  if (!ini)
    return;
  ini->SetPath(name);

  ini->Read("alt_tex_size", &(settings.alt_tex_size));
  ini->Read("use_sts1_only", &(settings.use_sts1_only));
  ini->Read("force_calc_sphere", &(settings.force_calc_sphere));
  ini->Read("correct_viewport", &(settings.correct_viewport));
  ini->Read("increase_texrect_edge", &(settings.increase_texrect_edge));
  ini->Read("decrease_fillrect_edge", &(settings.decrease_fillrect_edge));
  if (ini->Read("texture_correction", -1) == 0) settings.texture_correction = 0;
  else settings.texture_correction = 1;
  if (ini->Read("pal230", -1) == 1) settings.pal230 = 1;
  else settings.pal230 = 0;
  ini->Read("stipple_mode", &(settings.stipple_mode));
  int stipple_pattern = ini->Read("stipple_pattern", -1);
  if (stipple_pattern > 0) settings.stipple_pattern = (uint32_t)stipple_pattern;
  ini->Read("force_microcheck", &(settings.force_microcheck));
  ini->Read("force_quad3d", &(settings.force_quad3d));
  ini->Read("clip_zmin", &(settings.clip_zmin));
  ini->Read("clip_zmax", &(settings.clip_zmax));
  ini->Read("fast_crc", &(settings.fast_crc));
  ini->Read("adjust_aspect", &(settings.adjust_aspect), 1);
  ini->Read("zmode_compare_less", &(settings.zmode_compare_less));
  ini->Read("old_style_adither", &(settings.old_style_adither));
  ini->Read("n64_z_scale", &(settings.n64_z_scale));
  if (settings.n64_z_scale)
    ZLUT_init();

  //frame buffer
  int optimize_texrect = ini->Read("optimize_texrect", -1);
  int ignore_aux_copy = ini->Read("ignore_aux_copy", -1);
  int hires_buf_clear = ini->Read("hires_buf_clear", -1);
  int read_alpha = ini->Read("fb_read_alpha", -1);
  int useless_is_useless = ini->Read("useless_is_useless", -1);
  int fb_crc_mode = ini->Read("fb_crc_mode", -1);

  if (optimize_texrect > 0) settings.frame_buffer |= fb_optimize_texrect;
  else if (optimize_texrect == 0) settings.frame_buffer &= ~fb_optimize_texrect;
  if (ignore_aux_copy > 0) settings.frame_buffer |= fb_ignore_aux_copy;
  else if (ignore_aux_copy == 0) settings.frame_buffer &= ~fb_ignore_aux_copy;
  if (hires_buf_clear > 0) settings.frame_buffer |= fb_hwfbe_buf_clear;
  else if (hires_buf_clear == 0) settings.frame_buffer &= ~fb_hwfbe_buf_clear;
  if (read_alpha > 0) settings.frame_buffer |= fb_read_alpha;
  else if (read_alpha == 0) settings.frame_buffer &= ~fb_read_alpha;
  if (useless_is_useless > 0) settings.frame_buffer |= fb_useless_is_useless;
  else settings.frame_buffer &= ~fb_useless_is_useless;
  if (fb_crc_mode >= 0) settings.fb_crc_mode = (SETTINGS::FBCRCMODE)fb_crc_mode;

  //  if (settings.custom_ini)
  {
    ini->Read("filtering", &(settings.filtering));
    ini->Read("fog", &(settings.fog));
    ini->Read("buff_clear", &(settings.buff_clear));
    ini->Read("swapmode", &(settings.swapmode));
    ini->Read("aspect", &(settings.aspectmode));
    ini->Read("lodmode", &(settings.lodmode));
	
	PackedScreenResolution tmpRes = Config_ReadScreenSettings();
	settings.res_data = tmpRes.resolution;
	settings.scr_res_x = settings.res_x = tmpRes.width;
	settings.scr_res_y = settings.res_y = tmpRes.height;

    //frame buffer
    int smart_read = ini->Read("fb_smart", -1);
    int hires = ini->Read("fb_hires", -1);
    int read_always = ini->Read("fb_read_always", -1);
    int read_back_to_screen = ini->Read("read_back_to_screen", -1);
    int cpu_write_hack = ini->Read("detect_cpu_write", -1);
    int get_fbinfo = ini->Read("fb_get_info", -1);
    int depth_render = ini->Read("fb_render", -1);

    if (smart_read > 0) settings.frame_buffer |= fb_emulation;
    else if (smart_read == 0) settings.frame_buffer &= ~fb_emulation;
    if (hires > 0) settings.frame_buffer |= fb_hwfbe;
    else if (hires == 0) settings.frame_buffer &= ~fb_hwfbe;
    if (read_always > 0) settings.frame_buffer |= fb_ref;
    else if (read_always == 0) settings.frame_buffer &= ~fb_ref;
    if (read_back_to_screen == 1) settings.frame_buffer |= fb_read_back_to_screen;
    else if (read_back_to_screen == 2) settings.frame_buffer |= fb_read_back_to_screen2;
    else if (read_back_to_screen == 0) settings.frame_buffer &= ~(fb_read_back_to_screen|fb_read_back_to_screen2);
    if (cpu_write_hack > 0) settings.frame_buffer |= fb_cpu_write_hack;
    else if (cpu_write_hack == 0) settings.frame_buffer &= ~fb_cpu_write_hack;
    if (get_fbinfo > 0) settings.frame_buffer |= fb_get_info;
    else if (get_fbinfo == 0) settings.frame_buffer &= ~fb_get_info;
    if (depth_render > 0) settings.frame_buffer |= fb_depth_render;
    else if (depth_render == 0) settings.frame_buffer &= ~fb_depth_render;
    settings.frame_buffer |= fb_motionblur;

  }

  if (
           strstr(name, (const char *)"Blast Corps")
        || strstr(name, (const char *)"ZELDA MAJORA'S MASK")
        || strstr(name, (const char *)"ZELDA")
        || strstr(name, (const char *)"MASK")
        || strstr(name, (const char *)"Banjo-Kazooie")
        || strstr(name, (const char *)"MARIOKART64")
        || strstr(name, (const char *)"Quake")
        || strstr(name, (const char *)"Perfect Dark")
     )
     settings.frame_buffer = 1;

  if (
        strstr(name, (const char *)"Resident Evil II")
        || strstr(name, (const char *)"BioHazard II")
     )
     settings.frame_buffer = fb_cpu_write_hack;

  if (
        strstr(name, (const char *)"Banjo-Kazooie")
        || strstr(name, (const char *)"MARIOKART64")
           )
     settings.frame_buffer |= fb_ref;

  settings.flame_corona = (settings.hacks & hack_Zelda) && !fb_depth_render_enabled;
}

int GetTexAddrUMA(int tmu, int texsize)
{
  int addr = voodoo.tex_min_addr[0] + voodoo.tmem_ptr[0];
  voodoo.tmem_ptr[0] += texsize;
  voodoo.tmem_ptr[1] = voodoo.tmem_ptr[0];
  return addr;
}

// guLoadTextures
void guLoadTextures ()
{
    int tbuf_size = 0;
    if (settings.scr_res_x <= 1024)
    {
      grTextureBufferExt(  GR_TMU0, voodoo.tex_min_addr[GR_TMU0], GR_LOD_LOG2_1024, GR_LOD_LOG2_1024,
        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH );
      tbuf_size = grTexCalcMemRequired(GR_LOD_LOG2_1024, GR_LOD_LOG2_1024,
        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565);
      grRenderBuffer( GR_BUFFER_TEXTUREBUFFER_EXT );
      grBufferClear (0, 0, 0xFFFF);
      grRenderBuffer( GR_BUFFER_BACKBUFFER );
    }
    else
    {
      grTextureBufferExt(  GR_TMU0, voodoo.tex_min_addr[GR_TMU0], GR_LOD_LOG2_2048, GR_LOD_LOG2_2048,
        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH );
      tbuf_size = grTexCalcMemRequired(GR_LOD_LOG2_2048, GR_LOD_LOG2_2048,
        GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565);
      grRenderBuffer( GR_BUFFER_TEXTUREBUFFER_EXT );
      grBufferClear (0, 0, 0xFFFF);
      grRenderBuffer( GR_BUFFER_BACKBUFFER );
    }

    rdp.texbufs[0].tmu = GR_TMU0;
    rdp.texbufs[0].begin = voodoo.tex_min_addr[GR_TMU0];
    rdp.texbufs[0].end = rdp.texbufs[0].begin+tbuf_size;
    rdp.texbufs[0].count = 0;
    rdp.texbufs[0].clear_allowed = TRUE;
    {
      rdp.texbufs[1].tmu = GR_TMU1;
      rdp.texbufs[1].begin = rdp.texbufs[0].end;
      rdp.texbufs[1].end = rdp.texbufs[1].begin+tbuf_size;
      rdp.texbufs[1].count = 0;
      rdp.texbufs[1].clear_allowed = TRUE;
    }
    offset_textures = tbuf_size + 16;
}

#ifdef TEXTURE_FILTER
void DisplayLoadProgress(const wchar_t *format, ...)
{
  va_list args;
  wchar_t wbuf[INFO_BUF];
  char buf[INFO_BUF];

  // process input
  va_start(args, format);
  vswprintf(wbuf, INFO_BUF, format, args);
  va_end(args);

  // XXX: convert to multibyte
  wcstombs(buf, wbuf, INFO_BUF);

  //LOADING TEXTURES. PLEASE WAIT...
}
#endif

int InitGfx ()
{
  wchar_t romname[256];
  wchar_t foldername[PATH_MAX + 64];
  wchar_t cachename[PATH_MAX + 64];
  if (fullscreen)
    ReleaseGfx ();

  OPEN_RDP_LOG ();  // doesn't matter if opens again; it will check for it
  OPEN_RDP_E_LOG ();
  VLOG ("InitGfx ()\n");

  rdp_reset ();

  // Initialize Glide
  grGlideInit ();

  // Select the Glide device
  grSstSelect (settings.card_id);

  gfx_context = grSstWinOpen (
        0,
        GR_REFRESH_60Hz,
        GR_COLORFORMAT_RGBA,
        GR_ORIGIN_UPPER_LEFT,
        2,    // Double-buffering
        1);   // 1 auxillary buffer

  if (!gfx_context)
  {
    ERRLOG("Error setting display mode");
    //    grSstWinClose (gfx_context);
    grGlideShutdown ();
    return FALSE;
  }

  fullscreen = TRUE;

  // get the # of TMUs available
  grGet (GR_NUM_TMU, 4, (FxI32*)&voodoo.num_tmu);
  // get maximal texture size
  grGet (GR_MAX_TEXTURE_SIZE, 4, (FxI32*)&voodoo.max_tex_size);
  voodoo.sup_large_tex = !(settings.hacks & hack_PPL);

  voodoo.tex_min_addr[0] = voodoo.tex_min_addr[1] = grTexMinAddress(GR_TMU0);
  voodoo.tex_max_addr[0] = voodoo.tex_max_addr[1] = grTexMaxAddress(GR_TMU0);

  grStipplePattern(settings.stipple_pattern);

  InitCombine();

  grCoordinateSpace (GR_WINDOW_COORDS);
  grVertexLayout (GR_PARAM_XY, offsetof(VERTEX,x), GR_PARAM_ENABLE);
  grVertexLayout (GR_PARAM_Q, offsetof(VERTEX,q), GR_PARAM_ENABLE);
  grVertexLayout (GR_PARAM_Z, offsetof(VERTEX,z), GR_PARAM_ENABLE);
  grVertexLayout (GR_PARAM_ST0, offsetof(VERTEX,coord[0]), GR_PARAM_ENABLE);
  grVertexLayout (GR_PARAM_ST1, offsetof(VERTEX,coord[2]), GR_PARAM_ENABLE);
  grVertexLayout (GR_PARAM_PARGB, offsetof(VERTEX,b), GR_PARAM_ENABLE);

  grCullMode(GR_CULL_NEGATIVE);

  if (settings.fog) //"FOGCOORD" extension
  {
     GrFog_t fog_t[64];
     guFogGenerateLinear (fog_t, 0.0f, 255.0f);//(float)rdp.fog_multiplier + (float)rdp.fog_offset);//256.0f);

     for (int i = 63; i > 0; i--)
     {
        if (fog_t[i] - fog_t[i-1] > 63)
        {
           fog_t[i-1] = fog_t[i] - 63;
        }
     }
     fog_t[0] = 0;
     grFogTable (fog_t);
     grVertexLayout (GR_PARAM_FOG_EXT, offsetof(VERTEX,f), GR_PARAM_ENABLE);
  }
  else //not supported
     settings.fog = FALSE;

  grDepthBufferMode (GR_DEPTHBUFFER_ZBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);

  settings.res_x = settings.scr_res_x;
  settings.res_y = settings.scr_res_y;
  ChangeSize ();

  guLoadTextures ();
  ClearCache ();

  grCullMode (GR_CULL_DISABLE);
  grDepthBufferMode (GR_DEPTHBUFFER_ZBUFFER);
  grDepthBufferFunction (GR_CMP_ALWAYS);
  grRenderBuffer(GR_BUFFER_BACKBUFFER);
  grColorMask (FXTRUE, FXTRUE);
  grDepthMask (FXTRUE);
  grBufferClear (0, 0, 0xFFFF);
  grBufferSwap (0);
  grBufferClear (0, 0, 0xFFFF);
  grDepthMask (FXFALSE);
  grTexFilterMode (0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
  grTexFilterMode (1, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
  grTexClampMode (0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
  grTexClampMode (1, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
  grClipWindow (0, 0, settings.scr_res_x, settings.scr_res_y);
  rdp.update |= UPDATE_SCISSOR | UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;

#ifdef TEXTURE_FILTER // Hiroshi Morii <koolsmoky@users.sourceforge.net>
  if (!settings.ghq_use)
  {
    settings.ghq_use = settings.ghq_fltr || settings.ghq_enht /*|| settings.ghq_cmpr*/ || settings.ghq_hirs;
    if (settings.ghq_use)
    {
      /* Plugin path */
      int options = texfltr[settings.ghq_fltr]|texenht[settings.ghq_enht]|texcmpr[settings.ghq_cmpr]|texhirs[settings.ghq_hirs];
      if (settings.ghq_enht_cmpr)
        options |= COMPRESS_TEX;
      if (settings.ghq_hirs_cmpr)
        options |= COMPRESS_HIRESTEX;
      //      if (settings.ghq_enht_tile)
      //        options |= TILE_TEX;
      if (settings.ghq_hirs_tile)
        options |= TILE_HIRESTEX;
      if (settings.ghq_enht_f16bpp)
        options |= FORCE16BPP_TEX;
      if (settings.ghq_hirs_f16bpp)
        options |= FORCE16BPP_HIRESTEX;
      if (settings.ghq_enht_gz)
        options |= GZ_TEXCACHE;
      if (settings.ghq_hirs_gz)
        options |= GZ_HIRESTEXCACHE;
      if (settings.ghq_cache_save)
        options |= (DUMP_TEXCACHE|DUMP_HIRESTEXCACHE);
      if (settings.ghq_hirs_let_texartists_fly)
        options |= LET_TEXARTISTS_FLY;
      if (settings.ghq_hirs_dump)
        options |= DUMP_TEX;

      ghq_dmptex_toggle_key = 0;

      swprintf(romname, 256, L"%hs", rdp.RomName);
      swprintf(foldername, sizeof(foldername), L"%hs", ConfigGetUserDataPath());
      swprintf(cachename, sizeof(cachename), L"%hs", ConfigGetUserCachePath());

      settings.ghq_use = (int)ext_ghq_init(voodoo.max_tex_size, // max texture width supported by hardware
        voodoo.max_tex_size, // max texture height supported by hardware
        32, // max texture bpp supported by hardware
        options,
        settings.ghq_cache_size * 1024*1024, // cache texture to system memory
        foldername,
        cachename,
        romname, // name of ROM. must be no longer than 256 characters
        DisplayLoadProgress);
    }
  }
#endif

  return TRUE;
}

void ReleaseGfx ()
{
  VLOG("ReleaseGfx ()\n");

  // Release graphics
  grSstWinClose (gfx_context);

  // Shutdown glide
  grGlideShutdown();

  fullscreen = FALSE;
  rdp.window_changed = TRUE;
}

// new API code begins here!

#ifdef __cplusplus
extern "C" {
#endif

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
#ifdef VISUAL_LOGGING
  VLOG("CALL ReadScreen2 ()\n");
#endif
  *width = settings.res_x;
  *height = settings.res_y;
  if (dest)
  {
    uint8_t * line = (uint8_t*)dest;

  GrLfbInfo_t info;
  info.size = sizeof(GrLfbInfo_t);
  if (grLfbLock (GR_LFB_READ_ONLY,
    GR_BUFFER_FRONTBUFFER,
    GR_LFBWRITEMODE_888,
    GR_ORIGIN_UPPER_LEFT,
    FXFALSE,
    &info))
  {
    // Copy the screen, let's hope this works.
      for (uint32_t y=0; y<settings.res_y; y++)
      {
        uint8_t *ptr = (uint8_t*) info.lfbPtr + (info.strideInBytes * y);
        for (uint32_t x=0; x<settings.res_x; x++)
        {
          line[x*3]   = ptr[2];  // red
          line[x*3+1] = ptr[1];  // green
          line[x*3+2] = ptr[0];  // blue
          ptr += 4;
        }
        line += settings.res_x * 3;
      }

      // Unlock the frontbuffer
      grLfbUnlock (GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER);
    }
#ifdef VISUAL_LOGGING
    VLOG ("ReadScreen. Success.\n");
#endif
  }
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
  VLOG("CALL PluginStartup ()\n");
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    const char *configDir = ConfigGetSharedDataFilepath("Glide64mk2.ini");
    if (configDir)
    {
       SetConfigDir(configDir);
       ReadSettings();
       return M64ERR_SUCCESS;
    }
    else
    {
        ERRLOG("Couldn't find Glide64mk2.ini");
        return M64ERR_FILES;
    }
}

EXPORT m64p_error CALL PluginShutdown(void)
{
  VLOG("CALL PluginShutdown ()\n");
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
  VLOG("CALL PluginGetVersion ()\n");
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_GFX;

    if (PluginVersion != NULL)
        *PluginVersion = 0x016304;

    if (APIVersion != NULL)
        *APIVersion = VIDEO_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Glide64mk2 Video Plugin";

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

/******************************************************************
Function: CaptureScreen
Purpose:  This function dumps the current frame to a file
input:    pointer to the directory to save the file to
output:   none
*******************************************************************/
EXPORT void CALL CaptureScreen ( char * Directory )
{
  capture_screen = 1;
  strcpy (capture_path, Directory);
}

/******************************************************************
Function: ChangeWindow
Purpose:  to change the window between fullscreen and window
mode. If the window was in fullscreen this should
change the screen to window mode and vice vesa.
input:    none
output:   none
*******************************************************************/
//#warning ChangeWindow unimplemented
EXPORT void CALL ChangeWindow (void)
{
  VLOG ("ChangeWindow()\n");
}

/******************************************************************
Function: CloseDLL
Purpose:  This function is called when the emulator is closing
down allowing the dll to de-initialise.
input:    none
output:   none
*******************************************************************/
void CALL CloseDLL (void)
{
  VLOG ("CloseDLL ()\n");

#ifdef TEXTURE_FILTER // Hiroshi Morii <koolsmoky@users.sourceforge.net>
  if (settings.ghq_use)
  {
    ext_ghq_shutdown();
    settings.ghq_use = 0;
  }
#endif
  if (fullscreen)
    ReleaseGfx ();
  ZLUT_release();
  ClearCache ();
}

/******************************************************************
Function: DrawScreen
Purpose:  This function is called when the emulator receives a
WM_PAINT message. This allows the gfx to fit in when
it is being used in the desktop.
input:    none
output:   none
*******************************************************************/
void CALL DrawScreen (void)
{
  VLOG ("DrawScreen ()\n");
}

/******************************************************************
Function: GetDllInfo
Purpose:  This function allows the emulator to gather information
about the dll by filling in the PluginInfo structure.
input:    a pointer to a PLUGIN_INFO stucture that needs to be
filled by the function. (see def above)
output:   none
*******************************************************************/
void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo )
{
  VLOG ("GetDllInfo ()\n");
  PluginInfo->Version = 0x0103;     // Set to 0x0103
  PluginInfo->Type  = PLUGIN_TYPE_GFX;  // Set to PLUGIN_TYPE_GFX
  sprintf (PluginInfo->Name, "Glide64mk2 "G64_VERSION RELTIME);  // Name of the DLL

  // If DLL supports memory these memory options then set them to TRUE or FALSE
  //  if it does not support it
  PluginInfo->NormalMemory = TRUE;  // a normal uint8_t array
  PluginInfo->MemoryBswaped = TRUE; // a normal uint8_t array where the memory has been pre
  // bswap on a dword (32 bits) boundry
}

/******************************************************************
Function: InitiateGFX
Purpose:  This function is called when the DLL is started to give
information from the emulator that the n64 graphics
uses. This is not called from the emulation thread.
Input:    Gfx_Info is passed to this function which is defined
above.
Output:   TRUE on success
FALSE on failure to initialise

** note on interrupts **:
To generate an interrupt set the appropriate bit in MI_INTR_REG
and then call the function CheckInterrupts to tell the emulator
that there is a waiting interrupt.
*******************************************************************/

EXPORT int CALL InitiateGFX (GFX_INFO Gfx_Info)
{
  VLOG ("InitiateGFX (*)\n");
  voodoo.num_tmu = 4;

  // Assume scale of 1 for debug purposes
  rdp.scale_x = 1.0f;
  rdp.scale_y = 1.0f;

  memset (&settings, 0, sizeof(SETTINGS));
  ReadSettings ();
  char name[21] = "DEFAULT";
  ReadSpecialSettings (name);
  settings.res_data_org = settings.res_data;

  gfx = Gfx_Info;

  util_init ();
  math_init ();
  TexCacheInit ();
  CRC_BuildTable();
  CountCombine();
  if (fb_depth_render_enabled)
    ZLUT_init();

  grGlideInit ();
  grSstSelect (0);
  const char *extensions = grGetString (GR_EXTENSION);
  grGlideShutdown ();
  if (strstr (extensions, "EVOODOO"))
    evoodoo = 1;
  else
    evoodoo = 0;

  return TRUE;
}

/******************************************************************
Function: MoveScreen
Purpose:  This function is called in response to the emulator
receiving a WM_MOVE passing the xpos and ypos passed
from that message.
input:    xpos - the x-coordinate of the upper-left corner of the
client area of the window.
ypos - y-coordinate of the upper-left corner of the
client area of the window.
output:   none
*******************************************************************/
EXPORT void CALL MoveScreen (int xpos, int ypos)
{
  rdp.window_changed = TRUE;
}

/******************************************************************
Function: RomClosed
Purpose:  This function is called when a rom is closed.
input:    none
output:   none
*******************************************************************/
EXPORT void CALL RomClosed (void)
{
  VLOG ("RomClosed ()\n");

  CLOSE_RDP_LOG ();
  CLOSE_RDP_E_LOG ();
  rdp.window_changed = TRUE;
  romopen = FALSE;
  if (fullscreen && evoodoo)
    ReleaseGfx ();
}

static void CheckDRAMSize()
{
  uint32_t test = gfx.RDRAM[0x007FFFFF] + 1;
  if (test)
    BMASK = 0x7FFFFF;
  else
    BMASK = WMASK;
#ifdef LOGGING
  sprintf (out_buf, "Detected RDRAM size: %08lx\n", BMASK);
  LOG (out_buf);
#endif
}

/******************************************************************
Function: RomOpen
Purpose:  This function is called when a rom is open. (from the
emulation thread)
input:    none
output:   none
*******************************************************************/
EXPORT int CALL RomOpen (void)
{
  VLOG ("RomOpen ()\n");
  no_dlist = true;
  romopen = TRUE;
  ucode_error_report = TRUE;	// allowed to report ucode errors
  rdp_reset ();

  // Get the country code & translate to NTSC(0) or PAL(1)
  uint16_t code = ((uint16_t*)gfx.HEADER)[0x1F^1];

  if (code == 0x4400) region = 1; // Germany (PAL)
  if (code == 0x4500) region = 0; // USA (NTSC)
  if (code == 0x4A00) region = 0; // Japan (NTSC)
  if (code == 0x5000) region = 1; // Europe (PAL)
  if (code == 0x5500) region = 0; // Australia (NTSC)

  char name[21] = "DEFAULT";
  ReadSpecialSettings (name);

  // get the name of the ROM
  for (int i=0; i<20; i++)
    name[i] = gfx.HEADER[(32+i)^3];
  name[20] = 0;

  // remove all trailing spaces
  while (name[strlen(name)-1] == ' ')
    name[strlen(name)-1] = 0;

  strncpy(rdp.RomName, name, sizeof(name));
  ReadSpecialSettings (name);
  ClearCache ();

  CheckDRAMSize();

  OPEN_RDP_LOG ();
  OPEN_RDP_E_LOG ();


  // ** EVOODOO EXTENSIONS **
  if (!fullscreen)
  {
    grGlideInit ();
    grSstSelect (0);
  }
  const char *extensions = grGetString (GR_EXTENSION);
  if (!fullscreen)
  {
    grGlideShutdown ();

    if (strstr (extensions, "EVOODOO"))
      evoodoo = 1;
    else
      evoodoo = 0;

    if (evoodoo)
      InitGfx ();
  }

  if (strstr (extensions, "ROMNAME"))
  {
    char strSetRomName[] = "grSetRomName";
    void (FX_CALL *grSetRomName)(char*);
    grSetRomName = (void (FX_CALL *)(char*))grGetProcAddress (strSetRomName);
    grSetRomName (name);
  }
  // **
  return true;
}

/******************************************************************
Function: ShowCFB
Purpose:  Useally once Dlists are started being displayed, cfb is
ignored. This function tells the dll to start displaying
them again.
input:    none
output:   none
*******************************************************************/
bool no_dlist = true;
EXPORT void CALL ShowCFB (void)
{
  no_dlist = true;
  VLOG ("ShowCFB ()\n");
}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
}

void drawViRegBG()
{
  LRDP("drawViRegBG\n");
  const uint32_t VIwidth = *gfx.VI_WIDTH_REG;
  FB_TO_SCREEN_INFO fb_info;
  fb_info.width  = VIwidth;
  fb_info.height = (uint32_t)rdp.vi_height;
  if (fb_info.height == 0)
  {
    LRDP("Image height = 0 - skipping\n");
    return;
  }
  fb_info.ul_x = 0;

  fb_info.lr_x = VIwidth - 1;
  //  fb_info.lr_x = (uint32_t)rdp.vi_width - 1;
  fb_info.ul_y = 0;
  fb_info.lr_y = fb_info.height - 1;
  fb_info.opaque = 1;
  fb_info.addr = *gfx.VI_ORIGIN_REG;
  fb_info.size = *gfx.VI_STATUS_REG & 3;
  rdp.last_bg = fb_info.addr;

  bool drawn = DrawFrameBufferToScreen(fb_info);
  if (settings.hacks&hack_Lego && drawn)
  {
    rdp.updatescreen = 1;
    newSwapBuffers ();
    DrawFrameBufferToScreen(fb_info);
  }
}

}

void DrawFrameBuffer ()
{
   grDepthMask (FXTRUE);
   grColorMask (FXTRUE, FXTRUE);
   grBufferClear (0, 0, 0xFFFF);
   drawViRegBG();
}

extern "C" {
/******************************************************************
Function: UpdateScreen
Purpose:  This function is called in response to a vsync of the
screen were the VI bit in MI_INTR_REG has already been
set
input:    none
output:   none
*******************************************************************/
uint32_t update_screen_count = 0;
EXPORT void CALL UpdateScreen (void)
{
#ifdef VISUAL_LOGGING
  char out_buf[128];
  sprintf (out_buf, "UpdateScreen (). Origin: %08x, Old origin: %08x, width: %d\n", *gfx.VI_ORIGIN_REG, rdp.vi_org_reg, *gfx.VI_WIDTH_REG);
  VLOG (out_buf);
  LRDP(out_buf);
#endif

  uint32_t width = (*gfx.VI_WIDTH_REG) << 1;
  if (*gfx.VI_ORIGIN_REG  > width)
    update_screen_count++;
  uint32_t limit = (settings.hacks&hack_Lego) ? 15 : 30;
  if ((settings.frame_buffer&fb_cpu_write_hack) && (update_screen_count > limit) && (rdp.last_bg == 0))
  {
    LRDP("DirectCPUWrite hack!\n");
    update_screen_count = 0;
    no_dlist = true;
    ClearCache ();
    UpdateScreen();
    return;
  }
  //*/
  //*
  if( no_dlist )
  {
    if( *gfx.VI_ORIGIN_REG  > width )
    {
      ChangeSize ();
      LRDP("ChangeSize done\n");
      DrawFrameBuffer();
      LRDP("DrawFrameBuffer done\n");
      rdp.updatescreen = 1;
      newSwapBuffers ();
    }
    return;
  }
  //*/
  if (settings.swapmode == 0)
    newSwapBuffers ();
}

static void DrawWholeFrameBufferToScreen()
{
  static uint32_t toScreenCI = 0;
  if (rdp.ci_width < 200)
    return;
  if (rdp.cimg == toScreenCI)
    return;
  toScreenCI = rdp.cimg;
  FB_TO_SCREEN_INFO fb_info;
  fb_info.addr   = rdp.cimg;
  fb_info.size   = rdp.ci_size;
  fb_info.width  = rdp.ci_width;
  fb_info.height = rdp.ci_height;
  if (fb_info.height == 0)
    return;
  fb_info.ul_x = 0;
  fb_info.lr_x = rdp.ci_width-1;
  fb_info.ul_y = 0;
  fb_info.lr_y = rdp.ci_height-1;
  fb_info.opaque = 0;
  DrawFrameBufferToScreen(fb_info);
  if (!(settings.frame_buffer & fb_ref))
    memset(gfx.RDRAM+rdp.cimg, 0, (rdp.ci_width*rdp.ci_height)<<rdp.ci_size>>1);
}

#ifdef __LIBRETRO__ // Core options
#include "../../../libretro/libretro.h"
extern retro_environment_t environ_cb;
extern void update_variables(void);
#endif
}

extern unsigned retro_filtering;

uint32_t curframe = 0;
void newSwapBuffers()
{
#ifdef __LIBRETRO__ // Core options
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   settings.filtering = retro_filtering;
#endif
  if (!rdp.updatescreen)
    return;

  rdp.updatescreen = 0;

  LRDP("swapped\n");

  rdp.update |= UPDATE_SCISSOR | UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;
  grClipWindow (0, 0, settings.scr_res_x, settings.scr_res_y);
  grDepthBufferFunction (GR_CMP_ALWAYS);
  grDepthMask (FXFALSE);
  grCullMode (GR_CULL_DISABLE);

  if (settings.frame_buffer & fb_read_back_to_screen)
    DrawWholeFrameBufferToScreen();

  {
    if (fb_hwfbe_enabled && !(settings.hacks&hack_RE2) && !evoodoo)
      grAuxBufferExt( GR_BUFFER_AUXBUFFER );
    grBufferSwap (settings.vsync);

    if  (settings.buff_clear || (settings.hacks&hack_PPL && settings.ucode == 6))
    {
      if (settings.hacks&hack_RE2 && fb_depth_render_enabled)
        grDepthMask (FXFALSE);
      else
        grDepthMask (FXTRUE);
      grBufferClear (0, 0, 0xFFFF);
    }
  }

  if (settings.frame_buffer & fb_read_back_to_screen2)
    DrawWholeFrameBufferToScreen();

  frame_count ++;
}

extern "C"
{

/******************************************************************
Function: ViStatusChanged
Purpose:  This function is called to notify the dll that the
ViStatus registers value has been changed.
input:    none
output:   none
*******************************************************************/
EXPORT void CALL ViStatusChanged (void)
{
}

/******************************************************************
Function: ViWidthChanged
Purpose:  This function is called to notify the dll that the
ViWidth registers value has been changed.
input:    none
output:   none
*******************************************************************/
EXPORT void CALL ViWidthChanged (void)
{
}

}
