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
#include "Util.h"
#include "3dmath.h"
#include "Combine.h"
#include "TexCache.h"
#include "CRC.h"
#include "FBtoScreen.h"
#include "DepthBufferRender.h"
#include "../../libretro/libretro.h"
#include "../../libretro/SDL.h"

#if defined(__GNUC__)
#include <sys/time.h>
#elif defined(__MSC__)
#include <time.h>
#define PATH_MAX MAX_PATH
#endif

#define G64_VERSION "G64 Mk2"
#define RELTIME "Date: " __DATE__// " Time: " __TIME__

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

int romopen = false;
int exception = false;

uint32_t region = 0;

// ref rate
// 60=0x0, 70=0x1, 72=0x2, 75=0x3, 80=0x4, 90=0x5, 100=0x6, 85=0x7, 120=0x8, none=0xff

uint32_t BMASK = 0x7FFFFF;
// Reality display processor structure
struct RDP rdp;

SETTINGS settings = { false, 640, 480, GR_RESOLUTION_640x480, 0 };

VOODOO voodoo = {0, 0};

uint32_t   offset_textures = 0;
uint32_t   offset_texbuf1 = 0;

int    capture_screen = 0;
char    capture_path[256];

// SOME FUNCTION DEFINITIONS 

static void DrawFrameBuffer(void);
void glide_set_filtering(unsigned value);

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;

void _ChangeSize(void)
{

   float res_scl_y = (float)settings.res_y / 240.0f;
   uint32_t scale_x = 0;
   uint32_t scale_y = 0;
   uint32_t dwHStartReg = *gfx_info.VI_H_START_REG;
#if 0
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "VI_H_START_REG: %d\n", dwHStartReg);
#endif
   uint32_t dwVStartReg = *gfx_info.VI_V_START_REG;
   float fscale_x = 0.0;
    float fscale_y = 0.0;
	float aspect = 0.0;
   uint32_t hstart = dwHStartReg >> 16;
   uint32_t hend = dwHStartReg & 0xFFFF;
   uint32_t vstart = dwVStartReg >> 16;
   uint32_t vend = dwVStartReg & 0xFFFF;
   rdp.scale_1024 = settings.scr_res_x / 1024.0f;
   rdp.scale_768 = settings.scr_res_y / 768.0f;
   scale_x = *gfx_info.VI_X_SCALE_REG & 0xFFF;
   if (!scale_x) return;
   scale_y = *gfx_info.VI_Y_SCALE_REG & 0xFFF;
   if (!scale_y) return;
   fscale_x = (float)scale_x / 1024.0f;
   fscale_y = (float)scale_y / 2048.0f;

   //  float res_scl_x = (float)settings.res_x / 320.0f;
  
   // dunno... but sometimes this happens
   if (hend == hstart) hend = (int)(*gfx_info.VI_WIDTH_REG / fscale_x);

   rdp.vi_width = (hend - hstart) * fscale_x;
   rdp.vi_height = (vend - vstart) * fscale_y * 1.0126582f;
   aspect = (settings.adjust_aspect && (fscale_y > fscale_x) && (rdp.vi_width > rdp.vi_height)) ? fscale_x/fscale_y : 1.0f;

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
   if (((uint32_t)rdp.vi_width <= (*gfx_info.VI_WIDTH_REG)/2) && (rdp.vi_width > rdp.vi_height))
      rdp.scale_y *= 0.5f;

   rdp.scissor_o.ul_x = 0;
   rdp.scissor_o.ul_y = 0;
   rdp.scissor_o.lr_x = (uint32_t)rdp.vi_width;
   rdp.scissor_o.lr_y = (uint32_t)rdp.vi_height;

   rdp.update |= UPDATE_VIEWPORT | UPDATE_SCISSOR;
}

void ChangeSize(void)
{
   float offset_y;

   switch (settings.aspectmode)
   {
      case 0: //4:3
         if (settings.scr_res_x >= settings.scr_res_y * 4.0f / 3.0f)
         {
            settings.res_y = settings.scr_res_y;
            settings.res_x = (uint32_t)(settings.res_y * 4.0f / 3.0f);
         }
         else
         {
            settings.res_x = settings.scr_res_x;
            settings.res_y = (uint32_t)(settings.res_x / 4.0f * 3.0f);
         }
         break;
      case 1: //16:9
         if (settings.scr_res_x >= settings.scr_res_y * 16.0f / 9.0f)
         {
            settings.res_y = settings.scr_res_y;
            settings.res_x = (uint32_t)(settings.res_y * 16.0f / 9.0f);
         }
         else
         {
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
   offset_y = (settings.scr_res_y - settings.res_y) / 2.0f;
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
#ifdef DEBUG_GLIDE2GL
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
#endif
}

extern retro_environment_t environ_cb;

void ReadSettings(void)
{
   struct retro_variable var = { "mupen64-screensize", 0 };
   unsigned screen_width = 640;
   unsigned screen_height = 480;
   settings.card_id = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (sscanf(var.value ? var.value : "640x480", "%dx%d", &screen_width, &screen_height) != 2)
      {
         screen_width  = 640;
         screen_height = 480;
      }
   }

   settings.scr_res_x = screen_width;
   settings.scr_res_y = screen_height;
   settings.res_x = 320;
   settings.res_y = 240;


   settings.vsync = 1;
   settings.ssformat = 1;

   settings.wrpResolution = 0;
   settings.wrpVRAM = 0;
   settings.wrpFBO = 0;
   settings.wrpAnisotropic = 0;

   settings.autodetect_ucode = true;
   settings.ucode = 2;
   settings.fog = 1;
   settings.buff_clear = 1;
   settings.logging = false;
   settings.log_clear = false;
   settings.elogging = false;
   settings.filter_cache = false;
   settings.unk_as_red = false;
   settings.log_unk = false;
   settings.unk_clear = false;
}

extern uint8_t microcode[4096];

extern bool no_audio;
extern uint32_t gfx_plugin_accuracy;

extern void update_variables(void);

void ReadSpecialSettings (const char * name)
{
   int smart_read, hires, get_fbinfo, read_always, depth_render, fb_crc_mode,
       read_back_to_screen, cpu_write_hack, optimize_texrect, hires_buf_clear,
       read_alpha, ignore_aux_copy, useless_is_useless;
   uint32_t i, uc_crc;
   bool updated;
   struct retro_variable var;

   fprintf(stderr, "ReadSpecialSettings: %s\n", name);

   updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();
   
   if (strstr(name, (const char *)"DEFAULT"))
   {
      settings.filtering = 0;
      settings.buff_clear = 1;
      settings.swapmode = 1;
      settings.lodmode = 0;

      //frame buffer
      smart_read = 0;
      hires = 0;
      get_fbinfo = 0;
      read_always = 0;
      depth_render = 1;
      fb_crc_mode = 1;
      read_back_to_screen = 0; 
      cpu_write_hack = 0;
      optimize_texrect = 1;
      hires_buf_clear = 0;
      read_alpha = 0;
      ignore_aux_copy = 0;
      useless_is_useless = 0;

      settings.alt_tex_size = 0;
      settings.force_microcheck = 0;
      settings.force_quad3d = 0;
      settings.force_calc_sphere = 0;
      settings.texture_correction = 1;
      settings.depth_bias = 20;
      settings.increase_texrect_edge = 0;
      settings.decrease_fillrect_edge = 0;
      settings.stipple_mode = 2;
      settings.stipple_pattern = (uint32_t)1041204192;
      settings.clip_zmax = 1;
      settings.clip_zmin = 0;
      settings.adjust_aspect = 1;
      settings.correct_viewport = 0;
      settings.zmode_compare_less = 0;
      settings.old_style_adither = 0;
      settings.n64_z_scale = 0;
      settings.pal230 = 0;
   }

   settings.hacks = 0;

   // We might want to detect some games by ucode crc, so set
   // up uc_crc here
   uc_crc = 0;

   for (i = 0; i < 3072 >> 2; i++)
      uc_crc += ((uint32_t*)microcode)[i];

   // Glide64 mk2 INI config
   if (strstr(name, (const char *)"1080 SNOWBOARDING"))
   {
      //settings.alt_tex_size = 1;
      //depthmode = 0
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 1;
      hires = 1;
#endif
      //fb_clear = 1;
   }
   else if (strstr(name, (const char *)"A Bug's Life"))
   {
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"AERO FIGHTERS ASSAUL"))
   {
      settings.clip_zmin = 1;
   }
   else if (strstr(name, (const char *)"AIDYN CHRONICLES"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"All-Star Baseball 20"))
   {
      //force_depth_compare = 1
   }
   else if (
            strstr(name, (const char *)"All-Star Baseball 99")
         || strstr(name, (const char *)"All Star Baseball 99")
         )
   {
      //force_depth_compare = 1
      //depthmode = 1
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char *)"All-Star Baseball '0"))
   {
      //force_depth_compare = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"ARMYMENAIRCOMBAT"))
   {
      settings.increase_texrect_edge = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"BURABURA POYON"))
   {
      //fix_tex_coord = 1
      //depthmode = 0
   }
   // ;Bakushou Jinsei 64 - Mezease! Resort Ou.
   else if (strstr(name, (const char *)"ÊÞ¸¼®³¼ÞÝ¾²64"))
   {
      //fb_info_disable = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"BAKU-BOMBERMAN")
         || strstr(name, (const char *)"BOMBERMAN64E")
         || strstr(name, (const char *)"BOMBERMAN64U"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"BAKUBOMB2")
         || strstr(name, (const char *)"BOMBERMAN64U2"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"BANGAIOH"))
   {
      //depthmode = 1
   }
   else if (
            strstr(name, (const char *)"Banjo-Kazooie")
         || strstr(name, (const char *)"BANJO KAZOOIE 2")
         || strstr(name, (const char *)"BANJO TOOIE")
         )
   {
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#else
      read_always = 1;
#endif
   }
   else if (strstr(name, (const char *)"BASS HUNTER 64"))
   {
      //fix_tex_coord = 1
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char *)"BATTLEZONE"))
   {
      //force_depth_compare = 1
      //depthmode = 1
   }
   else if (
            strstr(name, (const char *)"BEETLE ADVENTURE JP")
         || strstr(name, (const char *)"Beetle Adventure Rac")
         )
   {
      //wrap_big_tex = 1
      settings.n64_z_scale = 1;
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"Bust A Move 3 DX")
         || strstr(name, (const char *)"Bust A Move '99")
         )
   {
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Bust A Move 2"))
   {
      //fix_tex_coord = 1
      settings.filtering = 2;
      //depthmode = 1
      settings.fog = 0;
   }
   else if (strstr(name, (const char *)"CARMAGEDDON64"))
   {
      //wrap_big_tex = 1
      settings. filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"HYDRO THUNDER"))
   {
      settings. filtering = 1;
   }
   else if (strstr(name, (const char *)"CENTRE COURT TENNIS"))
   {
      //soft_depth_compare = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"Chameleon Twist2"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"extreme_g")
         || strstr(name, (const char *)"extremeg"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"Extreme G 2"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"MICKEY USA")
         || strstr(name, (const char *)"MICKEY USA PAL")
         )
   {
      settings.alt_tex_size = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"MISCHIEF MAKERS")
         || strstr(name, (const char *)"TROUBLE MAKERS"))
   {
      //mischief_tex_hack = 0
      //tex_wrap_hack = 0
      //depthmode = 1
      settings.filtering = 1;
      settings.fog = 0;
   }
   else if (strstr(name, (const char*)"Tigger's Honey Hunt"))
   {
      settings.zmode_compare_less = 1;
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"TOM AND JERRY"))
   {
      settings.depth_bias = 2;
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"SPACE DYNAMITES"))
   {
      settings.force_microcheck = 1;
   }
   else if (strstr(name, (const char*)"SPIDERMAN"))
   {
   }
   else if (strstr(name, (const char*)"STARCRAFT 64"))
   {
      settings.force_microcheck = 1;
      settings.aspectmode = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"STAR SOLDIER"))
   {
      settings.force_microcheck = 1;
      settings.filtering = 1;
      //depthmode = 1
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"STAR WARS EP1 RACER"))
   {
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"TELEFOOT SOCCER 2000"))
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"TG RALLY 2"))
   {
      settings.filtering = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"Tonic Trouble"))
   {
      //depthmode = 0
      cpu_write_hack = 1;
   }
   else if (strstr(name, (const char*)"SUPERROBOTSPIRITS"))
   {
      settings.aspectmode = 2;
   }
   else if (strstr(name, (const char*)"THPS2")
         || strstr(name, (const char*)"THPS3")
         || strstr(name, (const char*)"TONY HAWK PRO SKATER")
         || strstr(name, (const char*)"TONY HAWK SKATEBOARD")
         )
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"TOP GEAR RALLY 2"))
   {
      settings.filtering = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"TRIPLE PLAY 2000"))
   {
      //wrap_big_tex = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"TSUWAMONO64"))
   {
      settings.force_microcheck = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"TSUMI TO BATSU"))
   {
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"MortalKombatTrilogy"))
   {
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Perfect Dark"))
   {
      useless_is_useless = 1;
      settings.decrease_fillrect_edge = 1;
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"Resident Evil II") || strstr(name, (const char *)"BioHazard II"))
   {
      cpu_write_hack = 1;
      settings.adjust_aspect = 0;
      settings.n64_z_scale = 1;
      //fix_tex_coord = 128
      //depthmode = 0
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"World Cup 98"))
   {
      //depthmode = 0
      settings.swapmode = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"EXCITEBIKE64"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"´¸½ÄØ°ÑG2"))
   {
      //depthmode = 0
      smart_read = 0;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"EARTHWORM JIM 3D"))
   {
      //increase_primdepth = 1
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char *)"Cruis'n USA"))
   {
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"CruisnExotica"))
   {
      settings.filtering = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"custom robo")
         || strstr(name, (const char *)"CUSTOMROBOV2"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"´²º³É¾ÝÄ±ÝÄÞØ­°½"))
   {
      //;Eikou no Saint Andrew
      settings.correct_viewport = 1;
   }
   else if (strstr(name, (const char *)"Eltail"))
   {
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"DeadlyArts"))
   {
      //soft_depth_compare = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      settings.clip_zmin = 1;
   }
   else if (strstr(name, (const char *)"Bottom of the 9th"))
   {
      settings.filtering = 1;
      //depthmode = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"BRUNSWICKBOWLING"))
   {
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"CHOPPER ATTACK"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"CITY TOUR GP"))
   {
      settings.force_microcheck = 1;
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Command&Conquer"))
   {
      //fix_tex_coord = 1
      settings.adjust_aspect = 2;
      settings.filtering = 1;
      //depthmode = 1
      settings.fog = 0;
   }
   else if (strstr(name, (const char *)"CONKER BFD"))
   {
      //ignore_previous = 1
      settings.lodmode = 1;
      settings.filtering = 1;
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 1;
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"DARK RIFT"))
   {
      settings.force_microcheck = 1;
   }
   else if (strstr(name, (const char*)"Donald Duck Goin' Qu")
         || strstr(name, (const char*)"Donald Duck Quack At"))
   {
      cpu_write_hack = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"ÄÞ×´ÓÝ3 ÉËÞÀÉÏÁSOS!"))
   {
      //;Doraemon 3 - Nobita no Machi SOS! (J)
      settings.clip_zmin = 1;
   }
   else if (strstr(name, (const char*)"DR.MARIO 64"))
   {
      //fix_tex_coord = 256
      //optimize_write = 1
      read_back_to_screen = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 0;
#endif
   }
   else if (strstr(name, (const char*)"F1 POLE POSITION 64"))
   {
      settings.clip_zmin = 1;
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"HUMAN GRAND PRIX"))
   {
      settings.filtering = 2;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"F1RacingChampionship"))
   {
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"F1 WORLD GRAND PRIX")
         || strstr(name, (const char*)"F1 WORLD GRAND PRIX2"))
   {
      //soft_depth_compare = 1
      //wrap_big_tex = 1
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"F3 Ì³×²É¼ÚÝ2"))
   {
      //;Fushigi no Dungeon - Furai no Shiren 2 (J) 
      settings.decrease_fillrect_edge = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"G.A.S.P!!Fighters'NE"))
   {
      //soft_depth_compare = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      settings.clip_zmin = 1;
   }
   else if (strstr(name, (const char*)"MS. PAC-MAN MM"))
   {
      cpu_write_hack = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"NBA Courtside 2")
         || strstr(name, (const char*)"NASCAR 2000")
         || strstr(name, (const char*)"NASCAR 99")
         )
   {
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"NBA JAM 2000")
         || strstr(name, (const char*)"NBA JAM 99")
         )
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"NBA LIVE 2000")
         )
   {
      settings.adjust_aspect = 0;
   }
   else if (strstr(name, (const char*)"NBA Live 99")
         )
   {
      settings.swapmode = 0;
      settings.adjust_aspect = 0;
   }
   else if (strstr(name, (const char*)"NINTAMAGAMEGALLERY64"))
   {
      settings.force_microcheck = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"NFL QBC 2000")
         || strstr(name, (const char*)"NFL Quarterback Club")
         )
   {
      //wrap_big_tex = 1
      //depthmode = 0
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"GANBAKE GOEMON") 
         //|| strstr(name, (const char*)"¶ÞÝÊÞÚ\ ºÞ´ÓÝ") */ TODO: illegal characters - find by ucode CRC */
         || strstr(name, (const char*)"MYSTICAL NINJA")
         || strstr(name, (const char*)"MYSTICAL NINJA2 SG")
         )
   {
      optimize_texrect = 0;
      settings.alt_tex_size = 1;
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"GAUNTLET LEGENDS"))
   {
      //depthmode = 1
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"Getter Love!!"))
   {
      settings.zmode_compare_less = 1;
      //texrect_compare_less = 1
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"GOEMON2 DERODERO")
         || strstr(name, (const char*)"GOEMONS GREAT ADV"))
   {
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"GOLDEN NUGGET 64"))
   {
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"GT64"))
   {
      settings.force_microcheck = 1;
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"ÊÑ½À°ÓÉ¶ÞÀØ64")
         )
   {
      settings.force_microcheck = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"HARVESTMOON64")
         || strstr(name, (const char*)"ÎÞ¸¼Þ®³ÓÉ¶ÞÀØ2")
         )
   {
      settings.zmode_compare_less = 1;
      //depthmode = 0
      settings.fog = 0;
   }
   else if (strstr(name, (const char*)"MGAH VOL1"))
   {
      settings.force_microcheck = 1;
      //depthmode = 1
      settings.zmode_compare_less = 1;
      smart_read = 1;
   }
   else if (strstr(name, (const char*)"MARIO STORY")
         || strstr(name, (const char*)"PAPER MARIO")
         )
   {
      useless_is_useless = 1;
      hires_buf_clear = 0;
      settings.filtering = 1;
      //depthmode = 1
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 1;
#endif
      read_alpha = 1;
   }
   else if (strstr(name, (const char*)"Mia Hamm Soccer 64"))
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"NITRO64"))
   {
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"NUCLEARSTRIKE64"))
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"NFL BLITZ")
         || strstr(name, (const char*)"NFL BLITZ 2001")
         || strstr(name, (const char*)"NFL BLITZ SPECIAL ED")
         )
   {
      settings.lodmode = 1;
   }
   else if (strstr(name, (const char*)"Monaco Grand Prix")
         || strstr(name, (const char*)"Monaco GP Racing 2")
         )
   {
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"ÓØÀ¼®³·Þ64"))
   {
      //;Morita Shogi 64
      settings.correct_viewport = 1;
   }
   else if (strstr(name, (const char*)"NEWTETRIS"))
   {
      settings.pal230 = 1;
      //fix_tex_coord = 1
      settings.increase_texrect_edge = 1;
      //depthmode = 0
      settings.fog = 0;
   }
   else if (strstr(name, (const char*)"MLB FEATURING K G JR"))
   {
      read_back_to_screen = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"HSV ADVENTURE RACING"))
   {
      //wrap_big_tex = 1
      settings.n64_z_scale = 1;
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"MarioGolf64"))
   {
      //fb_info_disable = 1
      ignore_aux_copy = 1;
      settings.buff_clear = 0;
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"Virtual Pool 64"))
   {
      //depthmode = 1
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"TWINE"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"V-RALLY"))
   {
      //fix_tex_coord = 3
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"Waialae Country Club"))
   {
      //wrap_big_tex = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"TWISTED EDGE"))
   {
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"STAR TWINS")
         || strstr(name, (const char*)"JET FORCE GEMINI")
         || strstr(name, (const char*)"J F G DISPLAY")
         )
   {
      read_back_to_screen = 1;
      settings.decrease_fillrect_edge = 1;
      //settings.alt_tex_size = 1;
      //depthmode = 1
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"ÄÞ×´ÓÝ Ð¯ÂÉ¾²Ú²¾·"))
   {
      //;Doraemon - Mittsu no Seireiseki (J)
      read_back_to_screen = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"HEIWA ÊßÁÝº Ü°ÙÄÞ64"))
   {
      //; Heiwa Pachinko World
      //depthmode = 0
      settings.fog = 0;
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"·×¯Ä¶²¹Â 64ÀÝÃ²ÀÞÝ"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"½°Êß°ÛÎÞ¯ÄÀ²¾Ý64"))
   {
      //;Super Robot Taisen 64 (J)
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"Supercross"))
   {
      //depthmode = 1
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"Top Gear Overdrive"))
   {
      //fb_info_disable = 1
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"TOP GEAR RALLY"))
   {
      no_audio = true;
   }
   else if (strstr(name, (const char*)"½½Ò!À²¾ÝÊß½ÞÙÀÞÏ"))
   {
      settings.force_microcheck = 1;
      //depthmode = 1
      settings.fog = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"ÐÝÅÃÞÀÏºÞ¯ÁÜ°ÙÄÞ"))
   {
      //;Tamagotchi World 64 (J) 
      //depthmode = 0
      settings.fog = 0;
   }
   else if (strstr(name, (const char*)"Taz Express"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"Top Gear Hyper Bike"))
   {
      //fb_info_disable = 1
      settings.swapmode = 2;
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"I S S 64"))
   {
      //depthmode = 1
      settings.swapmode = 2;
      settings.old_style_adither = 1;
   }
   else if (strstr(name, (const char*)"I.S.S.2000"))
   {
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"ITF 2000")
         || strstr(name, (const char*)"IT&F SUMMERGAMES")
         )
   {
      settings.filtering = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"J_league 1997"))
   {
      //fix_tex_coord = 1
      //depthmode = 1
      settings.swapmode = 0;
   }
#if 0
   // TODO: illegal characters - will have to find this game by ucode CRC
   // later
   else if (strstr(name, (const char*)"JØ°¸Þ\ ²ÚÌÞÝËÞ°Ä1997"))
   {
      //;J.League Eleven Beat 1997
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
#endif
   else if (strstr(name, (const char*)"J WORLD SOCCER3"))
   {
      //depthmode = 1
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"KEN GRIFFEY SLUGFEST"))
   {
      read_back_to_screen = 2;
      //depthmode = 1
      settings.swapmode = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"MASTERS'98"))
   {
      //wrap_big_tex = 1
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"MO WORLD LEAGUE SOCC"))
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"Ç¼ÂÞØ64"))
   {
      //; Nushi Zuri 64
      settings.force_microcheck = 1;
      //wrap_big_tex = 0
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"PACHINKO365NICHI"))
   {
      settings.correct_viewport = 1;
   }
   else if (strstr(name, (const char*)"PERFECT STRIKER"))
   {
      //depthmode = 1
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"ROCKETROBOTONWHEELS"))
   {
      settings.clip_zmin = 1;
   }
   else if (strstr(name, (const char*)"SD HIRYU STADIUM"))
   {
      settings.force_microcheck = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"Shadow of the Empire"))
   {
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"RUSH 2049"))
   {
      //force_texrect_zbuf = 1
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"SCARS"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"LEGORacers"))
   {
      cpu_write_hack = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      read_alpha = 1;
   }
   else if (strstr(name, (const char*)"Lode Runner 3D"))
   {
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char*)"Parlor PRO 64"))
   {
      settings.force_microcheck = 1;
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"PUZZLE LEAGUE N64")
         || strstr(name, (const char*)"PUZZLE LEAGUE"))
   {
      //PPL = 1
      settings.force_microcheck = 1;
      //fix_tex_coord = 1
      settings.filtering = 2;
      //depthmode = 1
      settings.fog = 0;
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 0;
#endif
      read_alpha = 1;
   }
   else if (strstr(name, (const char*)"POKEMON SNAP"))
   {
      //depthmode = 1
#ifdef HAVE_HWFBE
      hires = 0;
#else
      read_always = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"POKEMON STADIUM")
         || strstr(name, (const char*)"POKEMON STADIUM G&S")
         )
   {
      //depthmode = 1
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 0;
#endif
      read_alpha = 1;
      fb_crc_mode = 2;
   }
   else if (strstr(name, (const char*)"POKEMON STADIUM 2"))
   {
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 2;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 1;
#endif
      read_alpha = 1;
      fb_crc_mode = 2;
   }
   else if (strstr(name, (const char*)"RAINBOW SIX"))
   {
      settings.increase_texrect_edge = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"RALLY CHALLENGE")
         || strstr(name, (const char*)"Rally'99"))
   {
      settings.filtering = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"Rayman 2"))
   {
      //depthmode = 0
      cpu_write_hack = 1;
   }
   else if (strstr(name, (const char*)"quarterback_club_98"))
   {
      hires_buf_clear = 0;
      settings.filtering = 1;
      //depthmode = 1
      settings.swapmode = 0;
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      optimize_texrect = 0;
      hires = 1;
#endif
      read_alpha = 1;
   }
   else if (strstr(name, (const char*)"PowerLeague64"))
   {
      settings.force_quad3d = 1;
   }
   else if (strstr(name, (const char*)"Racing Simulation 2"))
   {
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"TOP GEAR RALLY"))
   {
      settings.depth_bias = 64;
      //fillcolor_fix = 1
      //depthmode = 0
   }
#if 0
   else if (strstr(name, (const char*)"POLARISSNOCROSS"))
   {
      //fix_tex_coord = 5
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"READY 2 RUMBLE"))
   {
      //fix_tex_coord = 64
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"Ready to Rumble"))
   {
      //fix_tex_coord = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"LT DUCK DODGERS"))
   {
      //wrap_big_tex = 1
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"LET'S SMASH"))
   {
      //soft_depth_compare = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"LCARS - WT_Riker"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"RUGRATS IN PARIS"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"Shadowman"))
   {
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"J LEAGUE LIVE 64"))
   {
      //wrap_big_tex = 1
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"Iggy's Reckin' Balls"))
   {
      //fix_tex_coord = 512
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"Ultraman Battle JAPA"))
   {
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"D K DISPLAY"))
   {
      //depthmode = 1
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"MarioParty3"))
   {
      //fix_tex_coord = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"MK_MYTHOLOGIES"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"NFL QBC '99"))
   {
      //force_depth_compare = 1
      //wrap_big_tex = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"OgreBattle64"))
   {
      //fb_info_disable = 1
      //force_depth_compare = 1
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"MICROMACHINES64TURBO"))
   {
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"Fighting Force"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"D K DISPLAY"))
   {
      //depthmode = 1
      //fb_clear = 1
   }
   else if (strstr(name, (const char *)"DAFFY DUCK STARRING"))
   {
      //depthmode = 1
      //wrap_big_tex = 1
   }
   else if (strstr(name, (const char *)"CyberTiger"))
   {
      //fix_tex_coord = 16
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"F-Zero X") || strstr(name, (const char *)"F-ZERO X"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"DERBYSTALLION64"))
   {
      //fix_tex_coord = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"DUKE NUKEM"))
   {
      //increase_primdepth = 1
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"EVANGELION"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Big Mountain 2000"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"YAKOUTYUU2"))
   {
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"WRESTLEMANIA 2000"))
   {
      //depthmode = 0
   }
#endif
   else if (strstr(name, (const char *)"Pilot Wings64"))
   {
      settings.depth_bias = 10;
      //depthmode = 1
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char *)"DRACULA MOKUSHIROKU")
         || strstr(name, (const char *)"DRACULA MOKUSHIROKU2")
         )
   {
      //depthmode = 0
      //fb_clear = 1
      settings.old_style_adither = 1;
   }
   else if (strstr(name, (const char *)"CASTLEVANIA")
         || strstr(name, (const char *)"CASTLEVANIA2"))
   {
      //depthmode = 0
      //fb_clear = 1
#ifndef HAVE_HWFBE
      read_always = 1;
#endif
   }
   else if (strstr(name, (const char *)"Dual heroes JAPAN")
         || strstr(name, (const char *)"Dual heroes PAL")
         || strstr(name, (const char *)"Dual heroes USA"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"JEREMY MCGRATH SUPER"))
   {
      //depthmode = 0
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"Kirby64"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"GOLDENEYE"))
   {
      settings.lodmode = 1;
      settings.depth_bias = 40;
      settings.filtering = 1;
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"DONKEY KONG 64"))
   {
      settings.lodmode = 1;
      settings.depth_bias = 64;
      //depthmode = 1
      //fb_clear = 1
      read_always = 1;
   }
   else if (strstr(name, (const char *)"Glover"))
   {
      settings.filtering = 1;
      //depthmode = 0;
   }
   else if (strstr(name, (const char *)"GEX: ENTER THE GECKO")
         || strstr(name, (const char *)"Gex 3 Deep Cover Gec"))
   {
      settings.filtering = 1;
      //depthmode = 0;
   }
   else if (strstr(name, (const char *)"WAVE RACE 64"))
   {
      settings.lodmode = 1;
      settings.pal230 = 1;
   }
   else if (strstr(name, (const char *)"WILD CHOPPERS"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char *)"Wipeout 64"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"WONDER PROJECT J2"))
   {
      //depthmode = 0
      settings.buff_clear = 0;
      settings.swapmode = 0;
   }
   else if (strstr(name, (const char *)"Doom64"))
   {
      //fillcolor_fix = 1
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"HEXEN"))
   {
      cpu_write_hack = 1;
      settings.filtering = 1;
      //depthmode = 1
      settings.buff_clear = 0;
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char *)"ZELDA MAJORA'S MASK") || strstr(name, (const char *)"THE MASK OF MUJURA"))
   {
      //wrap_big_tex = 1
      settings.filtering = 1;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
      fb_crc_mode = 0;
   }
   else if (strstr(name, (const char *)"THE LEGEND OF ZELDA") || strstr(name, (const char *)"ZELDA MASTER QUEST"))
   {
      settings.filtering = 1;
      //depthmode = 1
      settings.lodmode = 1;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
   }
   else if (strstr(name, (const char*)"Re-Volt"))
   {
      settings.texture_correction = 0;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"RIDGE RACER 64"))
   {
      settings.force_calc_sphere = 1;
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#else
      read_always = 1;
#endif
   }
   else if (strstr(name, (const char*)"ROAD RASH 64"))
   {
      //depthmode = 0
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"Robopon64"))
   {
      //depthmode = 0
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char*)"RONALDINHO SOCCER"))
   {
      //depthmode = 1
      settings.swapmode = 2;
      settings.old_style_adither = 1;
   }
   else if (strstr(name, (const char*)"RTL WLS2000"))
   {
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"BIOFREAKS"))
   {
      //depthmode = 0
      settings.buff_clear = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"Blast Corps") || strstr(name, (const char *)"Blastdozer"))
   {
      //depthmode = 1
      settings.swapmode = 0;
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      read_alpha = 1;
   }
   else if (strstr(name, (const char *)"blitz2k"))
   {
      settings.lodmode = 1;
   }
   else if (strstr(name, (const char *)"Body Harvest"))
   {
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"Killer Instinct Gold") || strstr(name, (const char *)"KILLER INSTINCT GOLD"))
   {
      settings.filtering = 1;
      //depthmode = 0
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"KNIFE EDGE"))
   {
      //wrap_big_tex = 1
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"Knockout Kings 2000"))
   {
      //fb_info_disable = 1
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      //fb_clear = 1
      read_alpha = 1;
   }
   else if (strstr(name, (const char *)"MACE"))
   {
#if 1
      // Not in original INI - fixes black stripes on big textures
      // TODO: check for regressions
      settings.increase_texrect_edge = 1;
#endif
      //fix_tex_coord = 8
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Quake"))
   {
      settings.force_microcheck = 1;
      settings.buff_clear = 0;
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char *)"QUAKE II"))
   {
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"Holy Magic Century"))
   {
      settings.filtering = 2;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"Quest 64"))
   {
      //depthmode = 1
   }
   else if (strstr(name, (const char*)"Silicon Valley"))
   {
      settings.filtering = 1;
      //depthmode = 0
   }
   else if (strstr(name, (const char*)"SNOWBOARD KIDS2")
         || strstr(name, (const char*)"Snobow Kids 2"))
   {
      settings.swapmode = 0;
      settings.filtering = 1;
   }
   else if (strstr(name, (const char*)"South Park Chef's Lu")
         || strstr(name, (const char*)"South Park: Chef's L"))
   {
      //fix_tex_coord = 4
      settings.filtering = 1;
      //depthmode = 1
      settings.fog = 0;
      settings.buff_clear = 0;
   }
   else if (strstr(name, (const char*)"LAMBORGHINI"))
   {
   }
   else if (strstr(name, (const char*)"MAGICAL TETRIS"))
   {
      settings.force_microcheck = 1;
      //depthmode = 1
      settings.fog = 0;
   }
   else if (strstr(name, (const char *)"MarioParty"))
   {
      settings.clip_zmin = 1;
      //depthmode = 0
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char*)"MarioParty2"))
   {
      //depthmode = 0
      settings.swapmode = 2;
   }
   else if (strstr(name, (const char *)"Mega Man 64")
         || strstr(name, (const char *)"RockMan Dash"))
   {
      settings.increase_texrect_edge = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
   }
   else if (strstr(name, (const char *)"TUROK_DINOSAUR_HUNTE"))
   {
      settings.depth_bias = 1;
      settings.lodmode = 1;
   }
   else if (strstr(name, (const char *)"SUPER MARIO 64")
         || strstr(name, (const char *)"SUPERMARIO64"))
   {
      settings.depth_bias = 64;
      settings.lodmode = 1;
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"SM64 Star Road"))
   {
      settings.depth_bias = 1;
      settings.lodmode = 1;
      settings.filtering = 1;
      //depthmode = 1
   }
   else if (strstr(name, (const char *)"SUPERMAN"))
   {
      cpu_write_hack = 1;
   }
   else if (strstr(name, (const char *)"TETRISPHERE"))
   {
      settings.alt_tex_size = 1;
      settings.increase_texrect_edge = 1;
      //depthmode = 1
      smart_read = 1;
#ifdef HAVE_HWFBE
      hires = 1;
#endif
      fb_crc_mode = 2;
   }
   else if (strstr(name, (const char *)"MARIOKART64"))
   {
      settings.depth_bias = 30;
      settings.stipple_mode = 1;
      settings.stipple_pattern = (uint32_t)4286595040UL;
      //depthmode = 1
#ifndef HAVE_HWFBE
      read_always = 1;
#endif
   }
   else if (strstr(name, (const char *)"YOSHI STORY"))
   {
      //fix_tex_coord = 32
      //depthmode = 1
      settings.filtering = 1;
      settings.fog = 0;
   }
   else
   {
      if (strstr(name, (const char*)"Mini Racers") ||
            uc_crc == UCODE_MINI_RACERS_CRC1 || uc_crc == UCODE_MINI_RACERS_CRC2)
      {
         /* Mini Racers (prototype ROM ) does not have a valid name, so detect by ucode crc */
         settings.force_microcheck = 1;
         settings.buff_clear = 0;
         smart_read = 1;
#ifdef HAVE_HWFBE
         hires = 1;
#endif
         settings.swapmode = 0;
      }
   }

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
   //else if (strstr(name, (const char *)"PUZZLE LEAGUE"))
      //settings.hacks |= hack_PPL;

   switch (gfx_plugin_accuracy)
   {
      case 2: /* HIGH */

         break;
      case 1: /* MEDIUM */
         if (read_always > 0) // turn off read_always
         {
            read_always = 0;
            if (smart_read == 0) // turn on smart_read instead
               smart_read = 1;
         }
         break;
      case 0: /* LOW */
         if (read_always > 0)
            read_always = 0;
         if (smart_read > 0)
            smart_read = 0;
         break; 
   }

   if (settings.n64_z_scale)
      ZLUT_init();

   //frame buffer

   if (optimize_texrect > 0)
      settings.frame_buffer |= fb_optimize_texrect;
   else if (optimize_texrect == 0)
      settings.frame_buffer &= ~fb_optimize_texrect;

   if (ignore_aux_copy > 0)
      settings.frame_buffer |= fb_ignore_aux_copy;
   else if (ignore_aux_copy == 0)
      settings.frame_buffer &= ~fb_ignore_aux_copy;

   if (hires_buf_clear > 0)
      settings.frame_buffer |= fb_hwfbe_buf_clear;
   else if (hires_buf_clear == 0)
      settings.frame_buffer &= ~fb_hwfbe_buf_clear;

   if (read_alpha > 0)
      settings.frame_buffer |= fb_read_alpha;
   else if (read_alpha == 0)
      settings.frame_buffer &= ~fb_read_alpha;
   if (useless_is_useless > 0)
      settings.frame_buffer |= fb_useless_is_useless;
   else
      settings.frame_buffer &= ~fb_useless_is_useless;

   if (fb_crc_mode >= 0)
      settings.fb_crc_mode = fb_crc_mode;

   if (smart_read > 0)
      settings.frame_buffer |= fb_emulation;
   else if (smart_read == 0)
      settings.frame_buffer &= ~fb_emulation;

   if (hires > 0)
      settings.frame_buffer |= fb_hwfbe;
   else if (hires == 0)
      settings.frame_buffer &= ~fb_hwfbe;
   if (read_always > 0)
      settings.frame_buffer |= fb_ref;
   else if (read_always == 0)
      settings.frame_buffer &= ~fb_ref;

   if (read_back_to_screen == 1)
      settings.frame_buffer |= fb_read_back_to_screen;
   else if (read_back_to_screen == 2)
      settings.frame_buffer |= fb_read_back_to_screen2;
   else if (read_back_to_screen == 0)
      settings.frame_buffer &= ~(fb_read_back_to_screen|fb_read_back_to_screen2);

   if (cpu_write_hack > 0)
      settings.frame_buffer |= fb_cpu_write_hack;
   else if (cpu_write_hack == 0)
      settings.frame_buffer &= ~fb_cpu_write_hack;

   if (get_fbinfo > 0)
      settings.frame_buffer |= fb_get_info;
   else if (get_fbinfo == 0)
      settings.frame_buffer &= ~fb_get_info;

   if (depth_render > 0)
      settings.frame_buffer |= fb_depth_render;
   else if (depth_render == 0)
      settings.frame_buffer &= ~fb_depth_render;



   settings.frame_buffer |= fb_motionblur;

   settings.flame_corona = (settings.hacks & hack_Zelda) && !fb_depth_render_enabled;

   var.key = "mupen64-filtering";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	  if (strcmp(var.value, "N64 3-point") == 0)
#ifdef DISABLE_3POINT
		 glide_set_filtering(3);
#else
		 glide_set_filtering(1);
#endif
	  else if (strcmp(var.value, "nearest") == 0)
		 glide_set_filtering(2);
	  else if (strcmp(var.value, "bilinear") == 0)
		 glide_set_filtering(3);
   }
}

int GetTexAddrUMA(int tmu, int texsize)
{
   int addr;

   addr = voodoo.tmem_ptr[0];
   voodoo.tmem_ptr[0] += texsize;
   voodoo.tmem_ptr[1] = voodoo.tmem_ptr[0];
   return addr;
}

void guLoadTextures(void)
{
   int tbuf_size = 0;

   bool log2_2048 = (settings.scr_res_x > 1024) ? true : false;

   tbuf_size = grTexCalcMemRequired(log2_2048 ? GR_LOD_LOG2_2048 : GR_LOD_LOG2_1024,
         GR_ASPECT_LOG2_1x1, GR_TEXFMT_RGB_565);

   rdp.texbufs[0].tmu = GR_TMU0;
   rdp.texbufs[0].begin = 0;
   rdp.texbufs[0].end = rdp.texbufs[0].begin+tbuf_size;
   rdp.texbufs[0].count = 0;
   rdp.texbufs[0].clear_allowed = true;
   rdp.texbufs[1].tmu = GR_TMU1;
   rdp.texbufs[1].begin = rdp.texbufs[0].end;
   rdp.texbufs[1].end = rdp.texbufs[1].begin+tbuf_size;
   rdp.texbufs[1].count = 0;
   rdp.texbufs[1].clear_allowed = true;
   offset_textures = tbuf_size + 16;
}

int InitGfx(void)
{
   OPEN_RDP_LOG ();  // doesn't matter if opens again; it will check for it
   OPEN_RDP_E_LOG ();
   VLOG ("InitGfx ()\n");

   rdp_reset ();

   if (!grSstWinOpen (
         0,
         GR_REFRESH_60Hz,
         GR_COLORFORMAT_RGBA,
         GR_ORIGIN_UPPER_LEFT,
         2,
         1))
   {
      ERRLOG("Error setting display mode");
      return false;
   }

   // get the # of TMUs available
   voodoo.tex_max_addr = grTexMaxAddress(GR_TMU0);

   grStipplePattern(settings.stipple_pattern);

   InitCombine();

   if (settings.fog) //"FOGCOORD" extension
   {
      guFogGenerateLinear (0.0f, 255.0f);//(float)rdp.fog_multiplier + (float)rdp.fog_offset);//256.0f);
   }
   else //not supported
      settings.fog = false;

   grDepthBufferMode (GR_DEPTHBUFFER_ZBUFFER);
   grDepthBufferFunction(GR_CMP_LESS);
   grDepthMask(FXTRUE);

   settings.res_x = settings.scr_res_x;
   settings.res_y = settings.scr_res_y;
   ChangeSize ();

   guLoadTextures ();
   ClearCache ();

   return true;
}

void ReleaseGfx(void)
{
   VLOG("ReleaseGfx ()\n");

   grSstWinClose (0);

   rdp_free();
}

// new API code begins here!

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
         uint32_t y, x;
         // Copy the screen, let's hope this works.
         for (y = 0; y < settings.res_y; y++)
         {
            uint8_t *ptr = (uint8_t*) info.lfbPtr + (info.strideInBytes * y);
            for (x = 0; x < settings.res_x; x++)
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

   ReadSettings();
   return M64ERR_SUCCESS;
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
      *Capabilities = 0;

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
   PluginInfo->NormalMemory = true;  // a normal uint8_t array
   PluginInfo->MemoryBswaped = true; // a normal uint8_t array where the memory has been pre
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
   char name[21] = "DEFAULT";

   VLOG ("InitiateGFX (*)\n");
   rdp_new();

   // Assume scale of 1 for debug purposes
   rdp.scale_x = 1.0f;
   rdp.scale_y = 1.0f;

   memset (&settings, 0, sizeof(SETTINGS));
   ReadSettings ();
   ReadSpecialSettings (name);
   settings.res_data_org = settings.res_data;

   util_init ();
   math_init ();
   TexCacheInit ();
   CRC_BuildTable();
   CountCombine();
   if (fb_depth_render_enabled)
      ZLUT_init();

   return true;
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
   romopen = false;
   ReleaseGfx ();
}

static void CheckDRAMSize()
{
   uint32_t test = gfx_info.RDRAM[0x007FFFFF] + 1;
   if (test)
      BMASK = 0x7FFFFF;
   else
      BMASK = WMASK;

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "Detected RDRAM size: %08lx\n", BMASK);
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
   int i;
   uint16_t code;
   char name[21] = "DEFAULT";

   VLOG ("RomOpen ()\n");
   no_dlist = true;
   romopen = true;
   ucode_error_report = true;	// allowed to report ucode errors
   rdp_reset ();

   // Get the country code & translate to NTSC(0) or PAL(1)
   code = ((uint16_t*)gfx_info.HEADER)[0x1F^1];

   if (code == 0x4400) region = 1; // Germany (PAL)
   if (code == 0x4500) region = 0; // USA (NTSC)
   if (code == 0x4A00) region = 0; // Japan (NTSC)
   if (code == 0x5000) region = 1; // Europe (PAL)
   if (code == 0x5500) region = 0; // Australia (NTSC)

   ReadSpecialSettings (name);

   // get the name of the ROM
   for (i = 0; i < 20; i++)
      name[i] = gfx_info.HEADER[(32+i)^3];
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


   InitGfx ();
   rdp_setfuncs();

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

void drawViRegBG(void)
{
   bool drawn;
   uint32_t VIwidth;
   FB_TO_SCREEN_INFO fb_info;

   LRDP("drawViRegBG\n");
   if (rdp.vi_height == 0)
      return;

   VIwidth = *gfx_info.VI_WIDTH_REG;

   fb_info.width  = VIwidth;
   fb_info.height = (uint32_t)rdp.vi_height;
   fb_info.ul_x = 0;
   fb_info.lr_x = VIwidth - 1;
   fb_info.ul_y = 0;
   fb_info.lr_y = fb_info.height - 1;
   fb_info.opaque = 1;
   fb_info.addr = *gfx_info.VI_ORIGIN_REG;
   fb_info.size = *gfx_info.VI_STATUS_REG & 3;

   rdp.last_bg = fb_info.addr;

   drawn = DrawFrameBufferToScreen(&fb_info);
   if (settings.hacks&hack_Lego && drawn)
   {
      rdp.updatescreen = 1;
      newSwapBuffers ();
      DrawFrameBufferToScreen(&fb_info);
   }
}

void DrawFrameBuffer(void)
{
   drawViRegBG();
}

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
   uint32_t width, limit;
#ifdef VISUAL_LOGGING
   char out_buf[128];
   sprintf (out_buf, "UpdateScreen (). Origin: %08x, Old origin: %08x, width: %d\n", *gfx_info.VI_ORIGIN_REG, rdp.vi_org_reg, *gfx_info.VI_WIDTH_REG);
   VLOG (out_buf);
   LRDP(out_buf);
#endif

   width = (*gfx_info.VI_WIDTH_REG) << 1;
   if (*gfx_info.VI_ORIGIN_REG  > width)
      update_screen_count++;
   limit = (settings.hacks&hack_Lego) ? 15 : 30;

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
      if( *gfx_info.VI_ORIGIN_REG  > width )
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

static void DrawWholeFrameBufferToScreen(void)
{
  FB_TO_SCREEN_INFO fb_info;
  static uint32_t toScreenCI = 0;

  if (rdp.ci_width < 200)
    return;
  if (rdp.cimg == toScreenCI)
    return;
  if (rdp.ci_height == 0)
     return;
  toScreenCI = rdp.cimg;

  fb_info.addr   = rdp.cimg;
  fb_info.size   = rdp.ci_size;
  fb_info.width  = rdp.ci_width;
  fb_info.height = rdp.ci_height;
  fb_info.ul_x = 0;
  fb_info.lr_x = rdp.ci_width-1;
  fb_info.ul_y = 0;
  fb_info.lr_y = rdp.ci_height-1;
  fb_info.opaque = 0;
  DrawFrameBufferToScreen(&fb_info);
  if (!(settings.frame_buffer & fb_ref))
    memset(gfx_info.RDRAM+rdp.cimg, 0, (rdp.ci_width*rdp.ci_height)<<rdp.ci_size>>1);
}

uint32_t curframe = 0;
extern int need_to_compile;
void glide_set_filtering(unsigned value)
{
	if(settings.filtering != value){
		need_to_compile = 1;
		settings.filtering = value;
	}
}

void newSwapBuffers(void)
{
   if (!rdp.updatescreen)
      return;

   rdp.updatescreen = 0;

   LRDP("swapped\n");

   rdp.update |= UPDATE_SCISSOR | UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;
   grClipWindow (0, 0, settings.scr_res_x, settings.scr_res_y);
   grDepthBufferFunction (GR_CMP_ALWAYS);
   grDepthMask (FXFALSE);

   if (settings.frame_buffer & fb_read_back_to_screen)
      DrawWholeFrameBufferToScreen();

   {
      grBufferSwap (settings.vsync);

      if  (settings.buff_clear)
      {
         grDepthMask (FXTRUE);
         grBufferClear (0, 0, 0xFFFF);
      }
   }

   if (settings.frame_buffer & fb_read_back_to_screen2)
      DrawWholeFrameBufferToScreen();

   frame_count ++;
}

/******************************************************************
Function: ViStatusChanged
Purpose:  This function is called to notify the dll that the
ViStatus registers value has been changed.
input:    none
output:   none
*******************************************************************/
EXPORT void CALL ViStatusChanged(void)
{
}

/******************************************************************
Function: ViWidthChanged
Purpose:  This function is called to notify the dll that the
ViWidth registers value has been changed.
input:    none
output:   none
*******************************************************************/
EXPORT void CALL ViWidthChanged(void)
{
}
