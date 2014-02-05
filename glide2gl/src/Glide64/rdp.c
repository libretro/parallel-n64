/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or * any later version.
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

#include <math.h>
#include "Gfx_1.3.h"
#include "m64p.h"
#include "3dmath.h"
#include "Util.h"
#include "Combine.h"
#include "TexCache.h"
#ifdef HAVE_HWFBE
#include "TexBuffer.h"
#endif
#include "FBtoScreen.h"
#include "CRC.h"
#include "GBI.h"
#include "../../libretro/SDL.h"

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

// positional and texel coordinate clipping
#define CCLIP(ux,lx,ut,lt,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			lx = lc; \
		}

#define CCLIP2(ux,lx,ut,lt,un,ln,uc,lc) \
		if (ux > lx || lx < uc || ux > lc) { rdp.tri_n += 2; return; } \
		if (ux < uc) { \
			float p = (uc-ux)/(lx-ux); \
			ut = p*(lt-ut)+ut; \
			un = p*(ln-un)+un; \
			ux = uc; \
		} \
		if (lx > lc) { \
			float p = (lc-ux)/(lx-ux); \
			lt = p*(lt-ut)+ut; \
			ln = p*(ln-un)+un; \
			lx = lc; \
		}

const char *ACmp[] = { "NONE", "THRESHOLD", "UNKNOWN", "DITHER" };

const char *Mode0[] = { "COMBINED",    "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "1",        "NOISE",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0" };

const char *Mode1[] = { "COMBINED",    "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "CENTER",     "K4",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0" };

const char *Mode2[] = { "COMBINED",    "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "SCALE",      "COMBINED_ALPHA",
            "T0_ALPHA",     "T1_ALPHA",
            "PRIM_ALPHA",   "SHADE_ALPHA",
            "ENV_ALPHA",    "LOD_FRACTION",
            "PRIM_LODFRAC",   "K5",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0",
            "0",        "0" };

const char *Mode3[] = { "COMBINED",    "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "1",        "0" };

const char *Alpha0[] = { "COMBINED",   "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "1",        "0" };

#define Alpha1 Alpha0
const char *Alpha2[] = { "LOD_FRACTION", "TEXEL0",
            "TEXEL1",     "PRIMITIVE",
            "SHADE",      "ENVIORNMENT",
            "PRIM_LODFRAC",   "0" };
#define Alpha3 Alpha0

const char *FBLa[] = { "G_BL_CLR_IN", "G_BL_CLR_MEM", "G_BL_CLR_BL", "G_BL_CLR_FOG" };
const char *FBLb[] = { "G_BL_A_IN", "G_BL_A_FOG", "G_BL_A_SHADE", "G_BL_0" };
const char *FBLc[] = { "G_BL_CLR_IN", "G_BL_CLR_MEM", "G_BL_CLR_BL", "G_BL_CLR_FOG"};
const char *FBLd[] = { "G_BL_1MA", "G_BL_A_MEM", "G_BL_1", "G_BL_0" };

const char *str_zs[] = { "G_ZS_PIXEL", "G_ZS_PRIM" };

const char *str_yn[] = { "NO", "YES" };
const char *str_offon[] = { "OFF", "ON" };

const char *str_cull[] = { "DISABLE", "FRONT", "BACK", "BOTH" };

// I=intensity probably
const char *str_format[] = { "RGBA", "YUV", "CI", "IA", "I", "?", "?", "?" };
const char *str_size[]   = { "4bit", "8bit", "16bit", "32bit" };
const char *str_cm[]     = { "WRAP/NO CLAMP", "MIRROR/NO CLAMP", "WRAP/CLAMP", "MIRROR/CLAMP" };
const char *str_lod[]    = { "1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1024", "2048" };
const char *str_aspect[] = { "1x8", "1x4", "1x2", "1x1", "2x1", "4x1", "8x1" };

const char *str_filter[] = { "Point Sampled", "Average (box)", "Bilinear" };

const char *str_tlut[]   = { "TT_NONE", "TT_UNKNOWN", "TT_RGBA_16", "TT_IA_16" };

const char *str_dither[] = { "Pattern", "~Pattern", "Noise", "None" };

const char *CIStatus[]   = { "ci_main", "ci_zimg", "ci_unknown",  "ci_useless",
                            "ci_old_copy", "ci_copy", "ci_copy_self",
                            "ci_zcopy", "ci_aux", "ci_aux_copy" };

//static variables

char out_buf[2048];

uint32_t frame_count;  // frame counter

int ucode_error_report = true;
int wrong_tile = -1;

uint8_t microcode[4096];
uint32_t uc_crc;
extern void microcheck(void);

//forward decls
static void CopyFrameBuffer (GrBuffer_t buffer);

void calc_light (VERTEX *v)
{
   uint32_t l;
   float light_intensity = 0.0f;
   float color[3];
   color[0] = rdp.light[rdp.num_lights].col[0];
   color[1] = rdp.light[rdp.num_lights].col[1];
   color[2] = rdp.light[rdp.num_lights].col[2];

   for (l = 0; l < rdp.num_lights; l++)
   {
      light_intensity = DotProduct (rdp.light_vector[l], v->vec);

      if (light_intensity > 0.0f) 
      {
         color[0] += rdp.light[l].col[0] * light_intensity;
         color[1] += rdp.light[l].col[1] * light_intensity;
         color[2] += rdp.light[l].col[2] * light_intensity;
      }
   }

   if (color[0] > 1.0f)
      color[0] = 1.0f;
   if (color[1] > 1.0f)
      color[1] = 1.0f;
   if (color[2] > 1.0f)
      color[2] = 1.0f;

   v->r = (uint8_t)(color[0]*255.0f);
   v->g = (uint8_t)(color[1]*255.0f);
   v->b = (uint8_t)(color[2]*255.0f);
}

void calc_linear (VERTEX *v)
{
   if (settings.force_calc_sphere)
   {
      calc_sphere(v);
      return;
   }
   DECLAREALIGN16VAR(vec[3]);

   TransformVector (v->vec, vec, rdp.model);
   //    TransformVector (v->vec, vec, rdp.combined);
   NormalizeVector (vec);
   float x, y;
   if (!rdp.use_lookat)
   {
      x = vec[0];
      y = vec[1];
   }
   else
   {
      x = DotProduct (rdp.lookat[0], vec);
      y = DotProduct (rdp.lookat[1], vec);
   }

   if (x > 1.0f)
      x = 1.0f;
   else if (x < -1.0f)
      x = -1.0f;
   if (y > 1.0f)
      y = 1.0f;
   else if (y < -1.0f)
      y = -1.0f;

   if (rdp.cur_cache[0])
   {
      // scale >> 6 is size to map to
      v->ou = (glide64_acos(x)/3.141592654f) * (rdp.tiles[rdp.cur_tile].org_s_scale >> 6);
      v->ov = (glide64_acos(y)/3.141592654f) * (rdp.tiles[rdp.cur_tile].org_t_scale >> 6);
   }
   v->uv_scaled = 1;
#ifdef EXTREME_LOGGING
   FRDP ("calc linear u: %f, v: %f\n", v->ou, v->ov);
#endif
}

void calc_sphere (VERTEX *v)
{
   //  LRDP("calc_sphere\n");
   DECLAREALIGN16VAR(vec[3]);
   int s_scale, t_scale;
   if (settings.hacks&hack_Chopper)
   {
      s_scale = min(rdp.tiles[rdp.cur_tile].org_s_scale >> 6, rdp.tiles[rdp.cur_tile].lr_s);
      t_scale = min(rdp.tiles[rdp.cur_tile].org_t_scale >> 6, rdp.tiles[rdp.cur_tile].lr_t);
   }
   else
   {
      s_scale = rdp.tiles[rdp.cur_tile].org_s_scale >> 6;
      t_scale = rdp.tiles[rdp.cur_tile].org_t_scale >> 6;
   }
   TransformVector (v->vec, vec, rdp.model);
   NormalizeVector (vec);
   float x, y;
   if (!rdp.use_lookat)
   {
      x = vec[0];
      y = vec[1];
   }
   else
   {
      x = DotProduct (rdp.lookat[0], vec);
      y = DotProduct (rdp.lookat[1], vec);
   }
   v->ou = (x * 0.5f + 0.5f) * s_scale;
   v->ov = (y * 0.5f + 0.5f) * t_scale;
   v->uv_scaled = 1;
#ifdef EXTREME_LOGGING
   FRDP ("calc sphere u: %f, v: %f\n", v->ou, v->ov);
#endif
}

// ** UCODE FUNCTIONS **
#include "ucode.h"
#include "glide64_gDP.h"
#include "glide64_gSP.h"
#include "ucode00.h"
#include "ucode01.h"
#include "ucode02.h"
#include "ucode03.h"
#include "ucode04.h"
#include "ucode05.h"
#include "ucode06.h"
#include "ucode07.h"
#include "ucode08.h"
#include "ucode09.h"
#include "ucode09rdp.h"
#include "turbo3D.h"

static int reset = 0;
static int old_ucode = -1;


void rdp_new(void)
{
   unsigned i, cpu;
   cpu = 0;
   rdp.vtx1 = (VERTEX*)malloc(256 * sizeof(VERTEX));
   rdp.vtx2 = (VERTEX*)malloc(256 * sizeof(VERTEX));
   rdp.vtx  = (VERTEX*)malloc(MAX_VTX * sizeof(VERTEX));
   rdp.frame_buffers = (COLOR_IMAGE*)malloc((NUMTEXBUF+2) * sizeof(COLOR_IMAGE));

   memset(rdp.vtx1, 0, 256 * sizeof(VERTEX));
   memset(rdp.vtx2, 0, 256 * sizeof(VERTEX));
   memset(rdp.vtx,  0, MAX_VTX * sizeof(VERTEX));

   rdp.vtxbuf = 0;
   rdp.vtxbuf2 = 0;
   rdp.vtx_buffer = 0;
   rdp.n_global = 0;
   rdp.v0 = 0;
   rdp.vn = 0;

   for (i = 0; i < MAX_TMU; i++)
   {
      rdp.cache[i] = (CACHE_LUT*)malloc(MAX_CACHE * sizeof(CACHE_LUT));
      rdp.cur_cache[i]   = 0;
      rdp.cur_cache_n[i] = 0;
   }

   if (perf_get_cpu_features_cb)
      cpu = perf_get_cpu_features_cb();

#if defined(HAVE_NEON)
   if (cpu & RETRO_SIMD_NEON)
      gfx_instruction[2][1] = uc2_vertex_neon;
#endif

}

void rdp_setfuncs(void)
{
   if (settings.hacks & hack_Makers)
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "Applying Mischief Makers function pointer table tweak...\n");
      gfx_instruction[0][191] = uc0_tri1_mischief;
   }
}

void rdp_free(void)
{
   int i;
   if (rdp.vtx1)
      free(rdp.vtx1);
   if (rdp.vtx2)
      free(rdp.vtx2);

   for (i = 0; i < MAX_TMU; i++)
   {
      if (rdp.cache[i])
         free(rdp.cache[i]);
   }

   if (rdp.vtx)
      free(rdp.vtx);
   if (rdp.frame_buffers)
      free(rdp.frame_buffers);
}

void rdp_reset(void)
{
   int i;
   reset = 1;

   // set all vertex numbers
   for (i = 0; i < MAX_VTX; i++)
      rdp.vtx[i].number = i;

   rdp.scissor_o.ul_x = 0;
   rdp.scissor_o.ul_y = 0;
   rdp.scissor_o.lr_x = 320;
   rdp.scissor_o.lr_y = 240;

   rdp.vi_org_reg = *gfx.VI_ORIGIN_REG;
   rdp.view_scale[2] = 32.0f * 511.0f;
   rdp.view_trans[2] = 32.0f * 511.0f;
   rdp.clip_ratio = 1.0f;

   rdp.lookat[0][0] = rdp.lookat[1][1] = 1.0f;

   rdp.cycle_mode = G_CYC_COPY;
   rdp.allow_combine = 1;
   rdp.update = UPDATE_SCISSOR | UPDATE_COMBINE | UPDATE_ZBUF_ENABLED | UPDATE_CULL_MODE;
   rdp.fog_mode = FOG_MODE_ENABLED;
   rdp.maincimg[0].addr = rdp.maincimg[1].addr = rdp.last_drawn_ci_addr = 0x7FFFFFFF;
}

void microcheck(void)
{
   uint32_t i;
   uc_crc = 0;

   // Check first 3k of ucode, because the last 1k sometimes contains trash
   for (i = 0; i < 3072 >> 2; i++)
      uc_crc += ((uint32_t*)microcode)[i];

   FRDP_E ("crc: %08lx\n", uc_crc);

#ifdef LOG_UCODE
   std::ofstream ucf;
   ucf.open ("ucode.txt", std::ios::out | std::ios::binary);
   int8_t d;
   for (i=0; i<0x400000; i++)
   {
      d = ((int8_t*)gfx.RDRAM)[i^3];
      ucf.write (&d, 1);
   }
   ucf.close ();
#endif

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "Glide64 ucode = %08lx\n", uc_crc);

   {
      old_ucode = settings.ucode;

      if (
               uc_crc == 0x006bd77f
            || uc_crc == 0x07200895
            || uc_crc == UCODE_GOLDENEYE_007
            || uc_crc == UCODE_DUKE_NUKEM_64
            || uc_crc == UCODE_ROBOTECH_CRYSTAL_DREAMS_PROTO
            || uc_crc == UCODE_NBA_SHOWTIME
            || uc_crc == 0xbc03e969
            || uc_crc == 0xd5604971
            || uc_crc == UCODE_MORITA_SHOUGI_64
            || uc_crc == 0xd67c2f8b
            || uc_crc == UCODE_KILLER_INSTINCT_GOLD
            || uc_crc == UCODE_MISCHIEF_MAKERS
            || uc_crc == UCODE_MORTAL_KOMBAT_TRILOGY
            || uc_crc == 0x5182f610
            || uc_crc == UCODE_BLAST_CORPS
            || uc_crc == UCODE_PILOTWINGS_64
            || uc_crc == UCODE_CRUISN_USA
            || uc_crc == UCODE_SUPER_MARIO_64
            || uc_crc == UCODE_TETRISPHERE
            || uc_crc == 0x4165e1fd
            || uc_crc == UCODE_EIKU_NO_SAINT_ANDREWS
         )
      {
         settings.ucode = 0;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 0 - RSP SW 2.0X (Super Mario 64)\n");
      }
      else if (
               uc_crc == UCODE_CLAYFIGHTER_63
            || uc_crc == 0x05777c62
            || uc_crc == 0x057e7c62
            || uc_crc == 0x1118b3e0
            || uc_crc == UCODE_MINI_RACERS_CRC1
            || uc_crc == 0x1de712ff
            || uc_crc == 0x24cd885b
            || uc_crc == 0x26a7879a
            || uc_crc == 0xfb816260
            || uc_crc == 0x2c7975d6
            || uc_crc == 0x2d3fe3f1
            || uc_crc == UCODE_FIGHTING_FORCE_64_CRC1
            || uc_crc == 0x339872a6
            || uc_crc == 0x3ff1a4ca
            || uc_crc == 0x4340ac9b
            || uc_crc == 0x440cfad6
            || uc_crc == 0x4fe6df78
            || uc_crc == 0x5257cd2a
            || uc_crc == UCODE_MORTAL_KOMBAT_MYTHOLOGIES
            || uc_crc == 0x5414030d
            || uc_crc == 0x559ff7d4
            || uc_crc == UCODE_YOSHIS_STORY_CRC2
            || uc_crc == UCODE_IGGY_RECKIN_BALLS
            || uc_crc == 0x6075e9eb
            || uc_crc == UCODE_DEZAEMON3D
            || uc_crc == UCODE_1080_SNOWBOARDING
            || uc_crc == 0x66c0b10a
            || uc_crc == 0x6eaa1da8
            || uc_crc == 0x72a4f34e
            || uc_crc == 0x73999a23
            || uc_crc == 0x7df75834
            || uc_crc == UCODE_DOOM_64
            || uc_crc == UCODE_TUROK_1
            || uc_crc == 0x82f48073
            || uc_crc == UCODE_MINI_RACERS_CRC2
            || uc_crc == 0x841ce10f
            || uc_crc == 0x863e1ca7
            || uc_crc == UCODE_MARIO_KART_64
            || uc_crc == 0x8d5735b2
            || uc_crc == 0x8d5735b3
            || uc_crc == 0x97d1b58a
            || uc_crc == UCODE_QUAKE_64
            || uc_crc == UCODE_WETRIX
            || uc_crc == UCODE_STAR_FOX_64
            || uc_crc == UCODE_FIGHTING_FORCE_64_CRC2
            || uc_crc == 0xb4577b9c
            || uc_crc == 0xbe78677c
            || uc_crc == 0xbed8b069
            || uc_crc == 0xc3704e41
            || uc_crc == UCODE_EXTREME_G
            || uc_crc == 0xc99a4c6c
            || uc_crc == 0xcee7920f
            || uc_crc == 0xd1663234
            || uc_crc == 0xd2a9f59c
            || uc_crc == 0xd41db5f7
            || uc_crc == 0xd57049a5
            || uc_crc == UCODE_TAMIYA_RACING_64_PROTO
            || uc_crc == UCODE_WIPEOUT_64
            || uc_crc == 0xe9231df2
            || uc_crc == 0xec040469
            || uc_crc == UCODE_DUAL_HEROES
            || uc_crc == UCODE_HEXEN_64
            || uc_crc == UCODE_CHAMELEON_TWIST
            || uc_crc == UCODE_BANJO_KAZOOIE
            || uc_crc == UCODE_MACE_THE_DARK_AGE
            || uc_crc == 0xef54ee35
            )
      {
         settings.ucode = 1;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 1 - F3DEX 1.XX (Star Fox 64)\n");
      }
      else if (
               uc_crc == 0x03044b84
            || uc_crc == 0x030f4b84
            || uc_crc == 0x0ff79527
            || uc_crc == UCODE_COMMAND_AND_CONQUER
            || uc_crc == UCODE_KNIFE_EDGE
            || uc_crc == UCODE_EXTREME_G_2
            || uc_crc == UCODE_DONKEY_KONG_64
            || uc_crc == UCODE_TONIC_TROUBLE
            || uc_crc == UCODE_PAPER_MARIO
            || uc_crc == UCODE_ANIMAL_CROSSING
            || uc_crc == UCODE_ZELDA_MAJORAS_MASK
            || uc_crc == UCODE_ZELDA_OOT
            || uc_crc == 0x6124a508
            || uc_crc == 0x630a61fb
            || uc_crc == UCODE_CASTLEVANIA_64
            || uc_crc == UCODE_CASTLEVANIA_64
            || uc_crc == UCODE_KING_HILL_64
            || uc_crc == 0x679e1205
            || uc_crc == 0x6d8f8f8a
            || uc_crc == 0x753be4a5
            || uc_crc == 0xda13ab96
            || uc_crc == 0xe65cb4ad
            || uc_crc == 0xe1290fa2
            || uc_crc == UCODE_HEY_YOU_PIKACHU
            || uc_crc == UCODE_CRUISN_EXOTICA
            || uc_crc == UCODE_STARCRAFT_64
            || uc_crc == 0x2b291027
            || uc_crc == UCODE_POKEMON_SNAP
            || uc_crc == 0x2f7dd1d5
            || uc_crc == UCODE_CRUISN_EXOTICA
            || uc_crc == UCODE_GANBARE_GOEMON_2
            || uc_crc == 0x93d11ffb
            || uc_crc == 0x93d1ff7b
            || uc_crc == UCODE_FZERO_X
            || uc_crc == 0x955117fb
            || uc_crc == UCODE_BIOHAZARD_2
            || uc_crc == 0xa2d0f88e 
            || uc_crc == 0xaa86cb1d
            || uc_crc == 0xaae4a5b9
            || uc_crc == 0xad0a6292
            || uc_crc == 0xad0a6312
            || uc_crc == UCODE_NBA_SHOWTIME
            || uc_crc == 0xba65ea1e
            || uc_crc == UCODE_KIRBY_64_CRYSTAL_SHARDS
            || uc_crc == UCODE_SUPER_SMASH_BROS
            || uc_crc == UCODE_MARIO_TENNIS
            || uc_crc == UCODE_MEGA_MAN_64
            || uc_crc == UCODE_RIDGE_RACER_64
            || uc_crc == UCODE_40WINKS
            || uc_crc == 0xcb8c9b6c
            || uc_crc == UCODE_ARMORINES_PROJECT
            )
      {
         settings.ucode = 2;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 2 - F3DEX 2.XX (The Legend of Zelda: Ocarina of Time)\n");
      }
      else if (
               uc_crc == UCODE_WAVERACE_64
            )
      {
         settings.ucode = 3;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 3 - F3DEX ? (WaveRace)\n");
      }
      else if (
               uc_crc == UCODE_STAR_WARS_SHADOW_OF_THE_EMPIRE
            )
      {
         settings.ucode = 4;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 4 - RSP SW 2.0D EXT (Star Wars: Shadows of the Empire)\n");
      }
      else if (
                  uc_crc == UCODE_JET_FORCE_GEMINI
               || uc_crc == UCODE_MICKEYS_SPEEDWAY_USA
               || uc_crc == UCODE_DIDDY_KONG_RACING
               || uc_crc == 0x63be08b3
            )
      {
         settings.ucode = 5;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 5 - RSP SW 2.0 (Diddy Kong Racing)\n");
      }
      else if (
                  uc_crc == 0x1ea9e30f
               || uc_crc == 0x74af0a74
               || uc_crc == 0x794c3e28
               || uc_crc == UCODE_BANGAIOH
               || uc_crc == 0x2b5a89c2
               || uc_crc == UCODE_YOSHIS_STORY_CRC1
               || uc_crc == 0xd20dedbf
            )
      {
         settings.ucode = 6;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 6 - S2DEX 1.XX  (Yoshi's Story - SimCity 2000)\n");
      }
      else if (
               uc_crc == UCODE_PERFECT_DARK
            )
      {
         settings.ucode = 7;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 7 - RSP SW PD (Perfect Dark)\n");
      }
      else if (
               uc_crc == UCODE_CONKERS_BAD_FUR_DAY
            )
      {
         settings.ucode = 8;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 8 - F3DEXBG 2.08 (Conker's Bad Fur Day)\n");
      }
      else if (
               uc_crc == 0x0bf36d36
            )
      {
         settings.ucode = 9;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 9 - Star Wars: Battle for Naboo\n");
      }
      else if (
                  uc_crc == 0x1f120bbb
               || uc_crc == 0xf9893f70
               || uc_crc == UCODE_LAST_LEGION_UX
            )
      {
         settings.ucode = 21;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode 21 - ???.\n");
      }
      else if (
               uc_crc == 0x0d7bbffb
            || uc_crc == 0x0ff795bf
            || uc_crc == UCODE_STAR_WARS_ROGUE_SQUADRON
            || uc_crc == 0x844b55b5
            || uc_crc == 0x8ec3e124
            || uc_crc == 0xd5c4dc96
            )
      {
         settings.ucode = -1;
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "Microcode -1 - Unknown Microcode.\n");
      }

      if (log_cb)
         log_cb(RETRO_LOG_INFO, "microcheck: old ucode: %d,  new ucode: %d\n", old_ucode, settings.ucode);
      if (uc_crc == 0x8d5735b2 || uc_crc == 0xb1821ed3 || uc_crc == 0x1118b3e0) //F3DLP.Rej ucode. perspective texture correction is not implemented
      {
         rdp.Persp_en = 1;
         rdp.persp_supported = false;
      }
      else if (settings.texture_correction)
         rdp.persp_supported = true;
   }
}

static uint32_t d_ul_x, d_ul_y, d_lr_x, d_lr_y;

static void DrawPartFrameBufferToScreen(void)
{
   FB_TO_SCREEN_INFO fb_info;
   fb_info.addr   = rdp.cimg;
   fb_info.size   = rdp.ci_size;
   fb_info.width  = rdp.ci_width;
   fb_info.height = rdp.ci_height;
   fb_info.ul_x = d_ul_x;
   fb_info.lr_x = d_lr_x;
   fb_info.ul_y = d_ul_y;
   fb_info.lr_y = d_lr_y;
   fb_info.opaque = 0;
   DrawFrameBufferToScreen(&fb_info);
   memset(gfx.RDRAM+rdp.cimg, 0, (rdp.ci_width*rdp.ci_height)<<rdp.ci_size>>1);
}

#define RGBA16TO32(color) \
  ((color&1)?0xFF:0) | \
  ((uint32_t)(((color & 0xF800) >> 11)) << 24) | \
  ((uint32_t)(((color & 0x07C0) >> 6)) << 16) | \
  ((uint32_t)(((color & 0x003E) >> 1)) << 8)

static void CopyFrameBuffer (GrBuffer_t buffer)
{
   FRDP ("CopyFrameBuffer: %08lx... ", rdp.cimg);

   // don't bother to write the stuff in asm... the slow part is the read from video card,
   //   not the copy.

   uint32_t width = rdp.ci_width;//*gfx.VI_WIDTH_REG;
   uint32_t height;
   if (fb_emulation_enabled && !(settings.hacks&hack_PPL))
   {
      int ind = (rdp.ci_count > 0)?rdp.ci_count-1:0;
      height = rdp.frame_buffers[ind].height;
   }
   else
   {
      height = rdp.ci_lower_bound;
      if (settings.hacks&hack_PPL)
         height -= rdp.ci_upper_bound;
   }
   FRDP ("width: %d, height: %d...  ", width, height);

   if (rdp.scale_x < 1.1f)
   {
      uint16_t * ptr_src = (uint16_t*)malloc(width * height * sizeof(uint16_t));
      if (grLfbReadRegion(buffer,
               (uint32_t)rdp.offset_x,
               (uint32_t)rdp.offset_y,//rdp.ci_upper_bound,
               width,
               height,
               width<<1,
               ptr_src))
      {
         uint16_t *ptr_dst = (uint16_t*)(gfx.RDRAM+rdp.cimg);
         uint32_t *ptr_dst32 = (uint32_t*)(gfx.RDRAM+rdp.cimg);
         uint16_t c;
         uint32_t y, x;

         for (y = 0; y < height; y++)
         {
            for (x = 0; x < width; x++)
            {
               c = ptr_src[x + y * width];
               if ((settings.frame_buffer & fb_read_alpha) && c <= 0) {}
               else
                  c = (c&0xFFC0) | ((c&0x001F) << 1) | 1;
               if (rdp.ci_size == 2)
                  ptr_dst[(x + y * width)^1] = c;
               else
                  ptr_dst32[x + y * width] = RGBA16TO32(c);
            }
         }
         LRDP("ReadRegion.  Framebuffer copy complete.\n");
      }
      else
      {
         LRDP("Framebuffer copy failed.\n");
      }
      if (ptr_src)
         free(ptr_src);
   }
   else
   {
      if (rdp.motionblur && fb_hwfbe_enabled)
      {
         return;
      }
      else
      {
         float scale_x = (settings.scr_res_x - rdp.offset_x*2.0f)  / max(width, rdp.vi_width);
         float scale_y = (settings.scr_res_y - rdp.offset_y*2.0f) / max(height, rdp.vi_height);

         FRDP("width: %d, height: %d, ul_y: %d, lr_y: %d, scale_x: %f, scale_y: %f, ci_width: %d, ci_height: %d\n",width, height, rdp.ci_upper_bound, rdp.ci_lower_bound, scale_x, scale_y, rdp.ci_width, rdp.ci_height);
         GrLfbInfo_t info;
         info.size = sizeof(GrLfbInfo_t);

         if (grLfbLock (GR_LFB_READ_ONLY,
                  buffer,
                  GR_LFBWRITEMODE_565,
                  GR_ORIGIN_UPPER_LEFT,
                  FXFALSE,
                  &info))
         {
            uint16_t *ptr_src = (uint16_t*)info.lfbPtr;
            uint16_t *ptr_dst = (uint16_t*)(gfx.RDRAM+rdp.cimg);
            uint32_t *ptr_dst32 = (uint32_t*)(gfx.RDRAM+rdp.cimg);
            uint16_t c;
            uint32_t stride = info.strideInBytes>>1;
            int y, x;

            int read_alpha = settings.frame_buffer & fb_read_alpha;
            if ((settings.hacks&hack_PMario) && rdp.frame_buffers[rdp.ci_count-1].status != CI_AUX)
               read_alpha = false;
            int x_start = 0, y_start = 0, x_end = width, y_end = height;

            if (settings.hacks&hack_BAR)
               x_start = 80, y_start = 24, x_end = 240, y_end = 86;

            for (y = y_start; y < y_end; y++)
            {
               for (x = x_start; x < x_end; x++)
               {
                  c = ptr_src[(int)(x*scale_x + rdp.offset_x) + (int)(y * scale_y + rdp.offset_y) * stride];
                  c = (c&0xFFC0) | ((c&0x001F) << 1) | 1;
                  if (read_alpha && c == 1)
                     c = 0;
                  if (rdp.ci_size <= 2)
                     ptr_dst[(x + y * width)^1] = c;
                  else
                     ptr_dst32[x + y * width] = RGBA16TO32(c);
               }
            }

            // Unlock the backbuffer
            grLfbUnlock (GR_LFB_READ_ONLY, buffer);
            LRDP("LfbLock.  Framebuffer copy complete.\n");
         }
         else
         {
            LRDP("Framebuffer copy failed.\n");
         }
      }
   }
}

/******************************************************************
Function: ProcessDList
Purpose:  This function is called when there is a Dlist to be
processed. (High level GFX list)
input:    none
output:   none
*******************************************************************/
void DetectFrameBufferUsage(void);
uint32_t fbreads_front = 0;
uint32_t fbreads_back = 0;
int cpu_fb_read_called = false;
int cpu_fb_write_called = false;
int cpu_fb_write = false;
int cpu_fb_ignore = false;
int CI_SET = true;
uint32_t swapped_addr = 0;
uint32_t ucode5_texshiftaddr = 0;
uint32_t ucode5_texshiftcount = 0;
uint16_t ucode5_texshift = 0;
int depth_buffer_fog;

EXPORT void CALL ProcessDList(void)
{
  no_dlist = false;
  update_screen_count = 0;
  ChangeSize ();

#ifdef VISUAL_LOGGING
  VLOG ("ProcessDList ()\n");
#endif

  if (reset)
  {
    reset = 0;
    if (settings.autodetect_ucode)
    {
      // Thanks to ZeZu for ucode autodetection!!!
      uint32_t startUcode = *(uint32_t*)(gfx.DMEM+0xFD0);
      memcpy (microcode, gfx.RDRAM+startUcode, 4096);
      microcheck ();
    }
    else
      memset (microcode, 0, 4096);
  }
  else if ( ((old_ucode == ucode_S2DEX) && (settings.ucode == ucode_F3DEX)) || settings.force_microcheck)
  {
    uint32_t startUcode = *(uint32_t*)(gfx.DMEM+0xFD0);
    memcpy (microcode, gfx.RDRAM+startUcode, 4096);
    microcheck ();
  }

  if (exception)
    return;

  // Clear out the RDP log
#ifdef RDP_LOGGING
  if (settings.logging && settings.log_clear)
  {
    CLOSE_RDP_LOG ();
    OPEN_RDP_LOG ();
  }
#endif

#ifdef UNIMP_LOG
  if (settings.log_unk && settings.unk_clear)
  {
    std::ofstream unimp;
    unimp.open("unimp.txt");
    unimp.close();
  }
#endif

  //* Set states *//
  if (settings.swapmode > 0)
    SwapOK = true;
  rdp.updatescreen = 1;

  rdp.tri_n = 0;  // 0 triangles so far this frame
  rdp.debug_n = 0;

  rdp.model_i = 0; // 0 matrices so far in stack
  //stack_size can be less then 32! Important for Silicon Vally. Thanks Orkin!
  rdp.model_stack_size = min(32, (*(uint32_t*)(gfx.DMEM+0x0FE4))>>6);
  if (rdp.model_stack_size == 0)
    rdp.model_stack_size = 32;
  rdp.Persp_en = true;
  rdp.fb_drawn = rdp.fb_drawn_front = false;
  rdp.update = 0x7FFFFFFF;  // All but clear cache
  rdp.geom_mode = 0;
  rdp.acmp = 0;
  rdp.maincimg[1] = rdp.maincimg[0];
  rdp.skip_drawing = false;
  rdp.s2dex_tex_loaded = false;
  rdp.bg_image_height = 0xFFFF;
  fbreads_front = fbreads_back = 0;
  rdp.fog_multiplier = rdp.fog_offset = 0;
  rdp.zsrc = 0;
  if (rdp.vi_org_reg != *gfx.VI_ORIGIN_REG)
    rdp.tlut_mode = 0; //is it correct?
  rdp.scissor_set = false;
  ucode5_texshiftaddr = ucode5_texshiftcount = 0;
  cpu_fb_write = false;
  cpu_fb_read_called = false;
  cpu_fb_write_called = false;
  cpu_fb_ignore = false;
  d_ul_x = 0xffff;
  d_ul_y = 0xffff;
  d_lr_x = 0;
  d_lr_y = 0;
  depth_buffer_fog = true;

  //analyze possible frame buffer usage
  if (fb_emulation_enabled)
    DetectFrameBufferUsage();
  if (!(settings.hacks&hack_Lego) || rdp.num_of_ci > 1)
    rdp.last_bg = 0;
  //* End of set states *//

  // Get the start of the display list and the length of it
  uint32_t dlist_start = *(uint32_t*)(gfx.DMEM+0xFF0);
  uint32_t dlist_length = *(uint32_t*)(gfx.DMEM+0xFF4);
  FRDP("--- NEW DLIST --- crc: %08lx, ucode: %d, fbuf: %08lx, fbuf_width: %d, dlist start: %08lx, dlist_length: %d, x_scale: %f, y_scale: %f\n", uc_crc, settings.ucode, *gfx.VI_ORIGIN_REG, *gfx.VI_WIDTH_REG, dlist_start, dlist_length, (*gfx.VI_X_SCALE_REG & 0xFFF)/1024.0f, (*gfx.VI_Y_SCALE_REG & 0xFFF)/1024.0f);
  FRDP_E("--- NEW DLIST --- crc: %08lx, ucode: %d, fbuf: %08lx\n", uc_crc, settings.ucode, *gfx.VI_ORIGIN_REG);

  // Do nothing if dlist is empty
  if (dlist_start == 0)
     return;

  if (cpu_fb_write == true)
    DrawPartFrameBufferToScreen();
  if ((settings.hacks&hack_Tonic) && dlist_length < 16)
  {
    rdp_fullsync(rdp.cmd0, rdp.cmd1);
    FRDP_E("DLIST is too short!\n");
    return;
  }

  // Start executing at the start of the display list
  rdp.pc_i = 0;
  rdp.pc[rdp.pc_i] = dlist_start;
  rdp.dl_count = -1;
  rdp.halt = 0;
  uint32_t a;

  if (settings.ucode == ucode_Turbo3d)
     Turbo3D();
  else
  {
     // MAIN PROCESSING LOOP
     do {
        // Get the address of the next command
        a = rdp.pc[rdp.pc_i] & BMASK;

        // Load the next command and its input
        rdp.cmd0 = ((uint32_t*)gfx.RDRAM)[a>>2];   // \ Current command, 64 bit
        rdp.cmd1 = ((uint32_t*)gfx.RDRAM)[(a>>2)+1]; // /
        // cmd2 and cmd3 are filled only when needed, by the function that needs them

#ifdef LOG_COMMANDS
        // Output the address before the command
        FRDP ("%08lx (c0:%08lx, c1:%08lx): ", a, rdp.cmd0, rdp.cmd1);
#endif

        // Go to the next instruction
        rdp.pc[rdp.pc_i] = (a+8) & BMASK;

        // Process this instruction
        gfx_instruction[settings.ucode][rdp.cmd0>>24](rdp.cmd0, rdp.cmd1);

        // check DL counter
        if (rdp.dl_count != -1)
        {
           rdp.dl_count --;
           if (rdp.dl_count == 0)
           {
              rdp.dl_count = -1;

              LRDP("End of DL\n");
              rdp.pc_i --;
           }
        }
     } while (!rdp.halt);
  }

  if (fb_emulation_enabled)
  {
    rdp.scale_x = rdp.scale_x_bak;
    rdp.scale_y = rdp.scale_y_bak;
  }
  if (settings.frame_buffer & fb_ref)
    CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
#ifdef HAVE_HWFBE
  if (rdp.cur_image && fb_hwfbe_enabled)
    CloseTextureBuffer(rdp.read_whole_frame && ((settings.hacks&hack_PMario) || rdp.swap_ci_index >= 0));
#endif

  if ((settings.hacks&hack_TGR2) && rdp.vi_org_reg != *gfx.VI_ORIGIN_REG && CI_SET)
  {
    newSwapBuffers ();
    CI_SET = false;
  }
  LRDP("ProcessDList end\n");
}

// undef - undefined instruction, always ignore
static void undef(uint32_t w0, uint32_t w1)
{
#ifdef _ENDUSER_RELEASE_
  *gfx.MI_INTR_REG |= 0x20;
  gfx.CheckInterrupts();
  rdp.halt = 1;
#else
  FRDP_E("** undefined ** (%08lx)\n", w0);
  FRDP("** undefined ** (%08lx) - IGNORED\n", w0);
#endif
}

// spnoop - no operation, always ignore
static void spnoop(uint32_t w0, uint32_t w1)
{
  LRDP("spnoop\n");
}

// noop - no operation, always ignore
static void rdp_noop(uint32_t w0, uint32_t w1)
{
  LRDP("noop\n");
}

static void ys_memrect(uint32_t w0, uint32_t w1)
{
  uint32_t tile = (uint16_t)((w1 & 0x07000000) >> 24);

  uint32_t lr_x = (uint16_t)((w0 & 0x00FFF000) >> 14);
  uint32_t lr_y = (uint16_t)((w0 & 0x00000FFF) >> 2);
  uint32_t ul_x = (uint16_t)((w1 & 0x00FFF000) >> 14);
  uint32_t ul_y = (uint16_t)((w1 & 0x00000FFF) >> 2);

  if (lr_y > rdp.scissor_o.lr_y)
    lr_y = rdp.scissor_o.lr_y;
  uint32_t off_x = ((rdp.cmd2 & 0xFFFF0000) >> 16) >> 5;
  uint32_t off_y = (rdp.cmd2 & 0x0000FFFF) >> 5;

#ifndef NDEBUG
  FRDP ("memrect (%d, %d, %d, %d), ci_width: %d", ul_x, ul_y, lr_x, lr_y, rdp.ci_width);
  if (off_x > 0)
    FRDP ("  off_x: %d", off_x);
  if (off_y > 0)
    FRDP ("  off_y: %d", off_y);
  LRDP("\n");
#endif

  uint32_t y, width = lr_x - ul_x;
  uint32_t tex_width = rdp.tiles[tile].line << 3;
  uint8_t * texaddr = gfx.RDRAM + rdp.addr[rdp.tiles[tile].t_mem] + tex_width*off_y + off_x;
  uint8_t * fbaddr = gfx.RDRAM + rdp.cimg + ul_x;

  for (y = ul_y; y < lr_y; y++) {
    uint8_t *src = texaddr + (y - ul_y) * tex_width;
    uint8_t *dst = fbaddr + y * rdp.ci_width;
    memcpy (dst, src, width);
  }
}

static void pm_palette_mod(void)
{
   uint8_t envr, envg, envb;
   uint16_t env16, prmr, prmg, prmb, prim16, *dst;
   int8_t i;

   envr = (uint8_t)(((rdp.env_color >> 24)  & 0xFF) * 0.0039215689f * 31.0f);
   envg = (uint8_t)(((rdp.env_color >> 16)  & 0xFF) * 0.0039215689f * 31.0f);
   envb = (uint8_t)(((rdp.env_color >> 8 )  & 0xFF) * 0.0039215689f * 31.0f);
   env16 = (uint16_t)((envr<<11)|(envg<<6)|(envb<<1)|1);
   prmr = (uint8_t)(((rdp.prim_color >> 24) & 0xFF) * 0.0039215689f * 31.0f);
   prmg = (uint8_t)(((rdp.prim_color >> 16) & 0xFF) * 0.0039215689f * 31.0f);
   prmb = (uint8_t)(((rdp.prim_color >> 8 ) & 0xFF) * 0.0039215689f * 31.0f);
   prim16 = (uint16_t)((prmr<<11)|(prmg<<6)|(prmb<<1)|1);
   dst = (uint16_t*)(gfx.RDRAM+rdp.cimg);

   for (i = 0; i < 16; i++)
      dst[i^1] = (rdp.pal_8[i]&1) ? prim16 : env16;

   //LRDP("Texrect palette modification\n");
}

static void pd_zcopy(uint32_t w0, uint32_t w1)
{
   int x;
   uint16_t ul_x = (uint16_t)((w1 & 0x00FFF000) >> 14);
   uint16_t lr_x = (uint16_t)((w0 & 0x00FFF000) >> 14) + 1;
   uint16_t ul_u = (uint16_t)((rdp.cmd2 & 0xFFFF0000) >> 21) + 1;
   uint16_t *ptr_dst = (uint16_t*)(gfx.RDRAM+rdp.cimg);
   uint16_t width = lr_x - ul_x;
   uint16_t * ptr_src = ((uint16_t*)rdp.tmem)+ul_u;
   uint16_t c;
   for (x = 0; x < width; x++)
   {
      c = ptr_src[x];
      c = ((c<<8)&0xFF00) | (c >> 8);
      ptr_dst[(ul_x+x)^1] = c;
      //      FRDP("dst[%d]=%04lx \n", (x + ul_x)^1, c);
   }
}

static void DrawDepthBufferFog(void)
{
  if (rdp.zi_width < 200)
    return;

  FB_TO_SCREEN_INFO fb_info;
  fb_info.addr   = rdp.zimg;
  fb_info.size   = 2;
  fb_info.width  = rdp.zi_width;
  fb_info.height = rdp.ci_height;
  fb_info.ul_x = rdp.scissor_o.ul_x;
  fb_info.lr_x = rdp.scissor_o.lr_x;
  fb_info.ul_y = rdp.scissor_o.ul_y;
  fb_info.lr_y = rdp.scissor_o.lr_y;
  fb_info.opaque = 0;

  DrawDepthBufferToScreen(&fb_info);
}

static void rdp_texrect(uint32_t w0, uint32_t w1)
{
  if (!rdp.LLE)
  {
    uint32_t a = rdp.pc[rdp.pc_i];
    uint8_t cmdHalf1 = gfx.RDRAM[a+3];
    uint8_t cmdHalf2 = gfx.RDRAM[a+11];
    a >>= 2;
    if ((cmdHalf1 == 0xE1 && cmdHalf2 == 0xF1) || (cmdHalf1 == 0xB4 && cmdHalf2 == 0xB3) || (cmdHalf1 == 0xB3 && cmdHalf2 == 0xB2))
    {
      //gSPTextureRectangle
      rdp.cmd2 = ((uint32_t*)gfx.RDRAM)[a+1];
      rdp.cmd3 = ((uint32_t*)gfx.RDRAM)[a+3];
      rdp.pc[rdp.pc_i] += 16;
    }
    else
    {
      //gDPTextureRectangle
      if (settings.hacks&hack_ASB)
        rdp.cmd2 = 0;
      else
        rdp.cmd2 = ((uint32_t*)gfx.RDRAM)[a+0];
      rdp.cmd3 = ((uint32_t*)gfx.RDRAM)[a+1];
      rdp.pc[rdp.pc_i] += 8;
    }
  }
  if ((settings.hacks&hack_Yoshi) && settings.ucode == ucode_S2DEX)
  {
    ys_memrect(w0, w1);
    return;
  }

  if (rdp.skip_drawing || (!fb_emulation_enabled && (rdp.cimg == rdp.zimg)))
  {
    if ((settings.hacks&hack_PMario) && rdp.ci_status == CI_USELESS)
    {
      pm_palette_mod ();
    }
    else
    {
      LRDP("Texrect skipped\n");
    }
    return;
  }

#ifdef HAVE_HWFBE
  if ((settings.ucode == ucode_CBFD) && rdp.cur_image && rdp.cur_image->format)
  {
    //FRDP("Wrong Texrect. texaddr: %08lx, cimg: %08lx, cimg_end: %08lx\n", rdp.timg.addr, rdp.maincimg[1].addr, rdp.maincimg[1].addr+rdp.ci_width*rdp.ci_height*rdp.ci_size);
    LRDP("Shadow texrect is skipped.\n");
    rdp.tri_n += 2;
    return;
  }
#endif

  if ((settings.ucode == ucode_PerfectDark) && (rdp.frame_buffers[rdp.ci_count-1].status == CI_ZCOPY))
  {
    pd_zcopy(w0, w1);
    LRDP("Depth buffer copied.\n");
    rdp.tri_n += 2;
    return;
  }

  if ((rdp.othermode_l >> 16) == 0x3c18 && rdp.cycle1 == 0x03ffffff && rdp.cycle2 == 0x01ff1fff) //depth image based fog
  {
    if (!depth_buffer_fog)
      return;
    if (settings.fog)
      DrawDepthBufferFog();
    depth_buffer_fog = false;
    return;
  }

  //  FRDP ("rdp.cycle1 %08lx, rdp.cycle2 %08lx\n", rdp.cycle1, rdp.cycle2);

  float ul_x, ul_y, lr_x, lr_y;
  if (rdp.cycle_mode == G_CYC_COPY)
  {
    ul_x = max(0.0f, (int16_t)((w1 & 0x00FFF000) >> 14));
    ul_y = max(0.0f, (int16_t)((w1 & 0x00000FFF) >> 2));
    lr_x = max(0.0f, (int16_t)((w0 & 0x00FFF000) >> 14));
    lr_y = max(0.0f, (int16_t)((w0 & 0x00000FFF) >> 2));
  }
  else
  {
    ul_x = max(0.0f, ((int16_t)((w1 & 0x00FFF000) >> 12)) / 4.0f);
    ul_y = max(0.0f, ((int16_t)(w1 & 0x00000FFF)) / 4.0f);
    lr_x = max(0.0f, ((int16_t)((w0 & 0x00FFF000) >> 12)) / 4.0f);
    lr_y = max(0.0f, ((int16_t)(w0 & 0x00000FFF)) / 4.0f);
  }

  if (ul_x >= lr_x)
  {
    FRDP("Wrong Texrect: ul_x: %f, lr_x: %f\n", ul_x, lr_x);
    return;
  }

  if (rdp.cycle_mode > G_CYC_2CYCLE)
  {
    lr_x += 1.0f;
    lr_y += 1.0f;
  } else if (lr_y - ul_y < 1.0f)
    lr_y = ceil(lr_y);

  if (settings.increase_texrect_edge)
  {
    if (glide64_floor(lr_x) != lr_x)
      lr_x = ceil(lr_x);
    if (glide64_floor(lr_y) != lr_y)
      lr_y = ceil(lr_y);
  }

  //*
#ifdef HAVE_HWFBE
  if (rdp.tbuff_tex && (settings.frame_buffer & fb_optimize_texrect))
  {
    LRDP("Attempt to optimize texrect\n");
    if (!rdp.tbuff_tex->drawn)
    {
      DRAWIMAGE d;
      d.imageX  = 0;
      d.imageW  = (uint16_t)rdp.tbuff_tex->width;
      d.frameX  = (uint16_t)ul_x;
      d.frameW  = (uint16_t)(rdp.tbuff_tex->width);

      d.imageY  = 0;
      d.imageH  = (uint16_t)rdp.tbuff_tex->height;
      d.frameY  = (uint16_t)ul_y;
      d.frameH  = (uint16_t)(rdp.tbuff_tex->height);
      FRDP("texrect. ul_x: %d, ul_y: %d, lr_x: %d, lr_y: %d, width: %d, height: %d\n", ul_x, ul_y, lr_x, lr_y, rdp.tbuff_tex->width, rdp.tbuff_tex->height);
      d.scaleX  = 1.0f;
      d.scaleY  = 1.0f;
      DrawHiresImage(&d, rdp.tbuff_tex->width == rdp.ci_width);
      rdp.tbuff_tex->drawn = true;
    }
    return;
  }
#endif
  //*/
  // framebuffer workaround for Zelda: MM LOT
  if ((rdp.othermode_l & 0xFFFF0000) == 0x0f5a0000)
    return;

  /*Gonetz*/
  //hack for Zelda MM. it removes black texrects which cover all geometry in "Link meets Zelda" cut scene
  if ((settings.hacks&hack_Zelda) && rdp.timg.addr >= rdp.cimg && rdp.timg.addr < rdp.ci_end)
  {
    FRDP("Wrong Texrect. texaddr: %08lx, cimg: %08lx, cimg_end: %08lx\n", rdp.cur_cache[0]->addr, rdp.cimg, rdp.cimg+rdp.ci_width*rdp.ci_height*2);
    rdp.tri_n += 2;
    return;
  }
  //*
  //hack for Banjo2. it removes black texrects under Banjo
  if (!fb_hwfbe_enabled && ((rdp.cycle1 << 16) | (rdp.cycle2 & 0xFFFF)) == 0xFFFFFFFF && (rdp.othermode_l & 0xFFFF0000) == 0x00500000)
  {
    rdp.tri_n += 2;
    return;
  }
  //*/
  //*
  //remove motion blur in night vision
  if ((settings.ucode == ucode_PerfectDark) && (rdp.maincimg[1].addr != rdp.maincimg[0].addr) && (rdp.timg.addr >= rdp.maincimg[1].addr) && (rdp.timg.addr < (rdp.maincimg[1].addr+rdp.ci_width*rdp.ci_height*rdp.ci_size)))
  {
    if (fb_emulation_enabled)
      if (rdp.frame_buffers[rdp.ci_count-1].status == CI_COPY_SELF)
      {
        //FRDP("Wrong Texrect. texaddr: %08lx, cimg: %08lx, cimg_end: %08lx\n", rdp.timg.addr, rdp.maincimg[1], rdp.maincimg[1]+rdp.ci_width*rdp.ci_height*rdp.ci_size);
        LRDP("Wrong Texrect.\n");
        rdp.tri_n += 2;
        return;
      }
  }
  //*/

  int i;

  uint32_t tile = (uint16_t)((w1 & 0x07000000) >> 24);

  rdp.texrecting = 1;

  uint32_t prev_tile = rdp.cur_tile;
  rdp.cur_tile = tile;

  const float Z = set_sprite_combine_mode ();

  rdp.texrecting = 0;

  if (!rdp.cur_cache[0])
  {
    rdp.cur_tile = prev_tile;
    rdp.tri_n += 2;
    return;
  }
  // ****
  // ** Texrect offset by Gugaman **
  //
  //integer representation of texture coordinate.
  //needed to detect and avoid overflow after shifting
  int32_t off_x_i = (rdp.cmd2 >> 16) & 0xFFFF;
  int32_t off_y_i = rdp.cmd2 & 0xFFFF;
  float dsdx = (float)((int16_t)((rdp.cmd3 & 0xFFFF0000) >> 16)) / 1024.0f;
  float dtdy = (float)((int16_t)(rdp.cmd3 & 0x0000FFFF)) / 1024.0f;
  if (off_x_i & 0x8000) //check for sign bit
    off_x_i |= ~0xffff; //make it negative
  //the same as for off_x_i
  if (off_y_i & 0x8000)
    off_y_i |= ~0xffff;

  if (rdp.cycle_mode == G_CYC_COPY)
    dsdx /= 4.0f;

  float s_ul_x = ul_x * rdp.scale_x + rdp.offset_x;
  float s_lr_x = lr_x * rdp.scale_x + rdp.offset_x;
  float s_ul_y = ul_y * rdp.scale_y + rdp.offset_y;
  float s_lr_y = lr_y * rdp.scale_y + rdp.offset_y;

  FRDP("texrect (%.2f, %.2f, %.2f, %.2f), tile: %d, #%d, #%d\n", ul_x, ul_y, lr_x, lr_y, tile, rdp.tri_n, rdp.tri_n+1);
  FRDP ("(%f, %f) -> (%f, %f), s: (%d, %d) -> (%d, %d)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y, rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);
  FRDP("\toff_x: %f, off_y: %f, dsdx: %f, dtdy: %f\n", off_x_i/32.0f, off_y_i/32.0f, dsdx, dtdy);

  float off_size_x;
  float off_size_y;

  if ( ((w0 >> 24)&0xFF) == 0xE5 ) //texrectflip
  {
    {
      off_size_x = (lr_y - ul_y - 1) * dsdx;
      off_size_y = (lr_x - ul_x - 1) * dtdy;
    }
  }
  else
  {
    {
      off_size_x = (lr_x - ul_x - 1) * dsdx;
      off_size_y = (lr_y - ul_y - 1) * dtdy;
    }
  }

  struct {
    float ul_u, ul_v, lr_u, lr_v;
  } texUV[2]; //struct for texture coordinates
  //angrylion's macro, helps to cut overflowed values.
  #define SIGN16(x) (((x) & 0x8000) ? ((x) | ~0xffff) : ((x) & 0xffff))

  //calculate texture coordinates
  for (i = 0; i < 2; i++)
  {
    if (rdp.cur_cache[i] && (rdp.tex & (i+1)))
    {
      float sx = 1, sy = 1;
      int x_i = off_x_i, y_i = off_y_i;
      TILE *tile = &rdp.tiles[rdp.cur_tile + i];
      //shifting
      if (tile->shift_s)
      {
        if (tile->shift_s > 10)
        {
          uint8_t iShift = (16 - tile->shift_s);
          x_i <<= iShift;
          sx = (float)(1 << iShift);
        }
        else
        {
          uint8_t iShift = tile->shift_s;
          x_i >>= iShift;
          sx = 1.0f/(float)(1 << iShift);
        }
      }
      if (tile->shift_t)
      {
        if (tile->shift_t > 10)
        {
          uint8_t iShift = (16 - tile->shift_t);
          y_i <<= iShift;
          sy = (float)(1 << iShift);
        }
        else
        {
          uint8_t iShift = tile->shift_t;
          y_i >>= iShift;
          sy = 1.0f/(float)(1 << iShift);
        }
      }

#ifdef HAVE_HWFBE
      if (rdp.aTBuffTex[i]) //hwfbe texture
      {
        float t0_off_x;
        float t0_off_y;
        if (off_x_i + off_y_i == 0)
        {
          t0_off_x = tile->ul_s;
          t0_off_y = tile->ul_t;
        }
        else
        {
          t0_off_x = off_x_i/32.0f;
          t0_off_y = off_y_i/32.0f;
        }
        t0_off_x += rdp.aTBuffTex[i]->u_shift;// + tile->ul_s; //commented for Paper Mario motion blur
        t0_off_y += rdp.aTBuffTex[i]->v_shift;// + tile->ul_t;
        texUV[i].ul_u = t0_off_x * sx;
        texUV[i].ul_v = t0_off_y * sy;

        texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
        texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

        texUV[i].ul_u *= rdp.aTBuffTex[i]->u_scale;
        texUV[i].ul_v *= rdp.aTBuffTex[i]->v_scale;
        texUV[i].lr_u *= rdp.aTBuffTex[i]->u_scale;
        texUV[i].lr_v *= rdp.aTBuffTex[i]->v_scale;
        FRDP("tbuff_tex[%d] ul_u: %f, ul_v: %f, lr_u: %f, lr_v: %f\n",
          i, texUV[i].ul_u, texUV[i].ul_v, texUV[i].lr_u, texUV[i].lr_v);
      }
      else //common case
#endif
      {
        //kill 10.5 format overflow by SIGN16 macro
        texUV[i].ul_u = SIGN16(x_i) / 32.0f;
        texUV[i].ul_v = SIGN16(y_i) / 32.0f;

        texUV[i].ul_u -= tile->f_ul_s;
        texUV[i].ul_v -= tile->f_ul_t;

        texUV[i].lr_u = texUV[i].ul_u + off_size_x * sx;
        texUV[i].lr_v = texUV[i].ul_v + off_size_y * sy;

        texUV[i].ul_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].ul_u;
        texUV[i].lr_u = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_x * texUV[i].lr_u;
        texUV[i].ul_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].ul_v;
        texUV[i].lr_v = rdp.cur_cache[i]->c_off + rdp.cur_cache[i]->c_scl_y * texUV[i].lr_v;
      }
    }
    else
    {
      texUV[i].ul_u = texUV[i].ul_v = texUV[i].lr_u = texUV[i].lr_v = 0;
    }
  }
  rdp.cur_tile = prev_tile;

  // ****

  FRDP ("  scissor: (%d, %d) -> (%d, %d)\n", rdp.scissor.ul_x, rdp.scissor.ul_y, rdp.scissor.lr_x, rdp.scissor.lr_y);

  CCLIP2 (s_ul_x, s_lr_x, texUV[0].ul_u, texUV[0].lr_u, texUV[1].ul_u, texUV[1].lr_u, (float)rdp.scissor.ul_x, (float)rdp.scissor.lr_x);
  CCLIP2 (s_ul_y, s_lr_y, texUV[0].ul_v, texUV[0].lr_v, texUV[1].ul_v, texUV[1].lr_v, (float)rdp.scissor.ul_y, (float)rdp.scissor.lr_y);

  FRDP ("  draw at: (%f, %f) -> (%f, %f)\n", s_ul_x, s_ul_y, s_lr_x, s_lr_y);

  VERTEX vstd[4] = {
    { s_ul_x, s_ul_y, Z, 1.0f, texUV[0].ul_u, texUV[0].ul_v, texUV[1].ul_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
    { s_lr_x, s_ul_y, Z, 1.0f, texUV[0].lr_u, texUV[0].ul_v, texUV[1].lr_u, texUV[1].ul_v, {0, 0, 0, 0}, 255 },
    { s_ul_x, s_lr_y, Z, 1.0f, texUV[0].ul_u, texUV[0].lr_v, texUV[1].ul_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 },
    { s_lr_x, s_lr_y, Z, 1.0f, texUV[0].lr_u, texUV[0].lr_v, texUV[1].lr_u, texUV[1].lr_v, {0, 0, 0, 0}, 255 } };

    if ( ((w0 >> 24)&0xFF) == 0xE5 ) //texrectflip
    {
      vstd[1].u0 = texUV[0].ul_u;
      vstd[1].v0 = texUV[0].lr_v;
      vstd[1].u1 = texUV[1].ul_u;
      vstd[1].v1 = texUV[1].lr_v;

      vstd[2].u0 = texUV[0].lr_u;
      vstd[2].v0 = texUV[0].ul_v;
      vstd[2].u1 = texUV[1].lr_u;
      vstd[2].v1 = texUV[1].ul_v;
    }

    VERTEX *vptr = vstd;
    int n_vertices = 4;

    AllowShadeMods (vptr, n_vertices);
    for (i=0; i<n_vertices; i++)
    {
      apply_shade_mods (&vptr[i]);
    }

    {
      if (rdp.fog_mode >= FOG_MODE_BLEND)
      {
        float fog;
        if (rdp.fog_mode == FOG_MODE_BLEND)
          fog = 1.0f/max(1, rdp.fog_color&0xFF);
        else
          fog = 1.0f/max(1, (~rdp.fog_color)&0xFF);
        for (i = 0; i < n_vertices; i++)
        {
          vptr[i].f = fog;
        }
        grFogMode (GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT);
      }

      ConvertCoordsConvert (vptr, n_vertices);

      grDrawVertexArrayContiguous (GR_TRIANGLE_STRIP, n_vertices, vptr, sizeof(VERTEX));

      rdp.tri_n += 2;
    }
}

static void rdp_loadsync(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("loadsync - ignored\n");
#endif
}

static void rdp_pipesync(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("pipesync - ignored\n");
#endif
}

static void rdp_tilesync(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("tilesync - ignored\n");
#endif
}

static void rdp_fullsync(uint32_t w0, uint32_t w1)
{
   gDPFullSync();
}

static void rdp_setkeygb(uint32_t w0, uint32_t w1)
{
   uint32_t wG = 0; //stub
   uint32_t wB = 0; //stub
   uint32_t sB = w1 & 0xFF;
   uint32_t cB = (w1 >> 8) & 0xFF;
   uint32_t sG = (w1 >> 16) & 0xFF;
   uint32_t cG = (w1 >> 24) & 0xFF;

   gDPSetKeyGB(cG, sG, wG, cB, sB, wB);
}

static void rdp_setkeyr(uint32_t w0, uint32_t w1)
{
   gDPSetKeyR(
         (w1 >> 8) & 0xFF,    /* cR */
         w1 & 0xFF,           /* sR */
         0                    /* wR */
         );
}

static void rdp_setconvert(uint32_t w0, uint32_t w1)
{
   gDPSetConvert(
         0,                    /* k0 */
         0,                    /* k1 */
         0,                    /* k2 */
         0,                    /* k3 */
         (w1 >> 9) & 0x1FF,    /* k4 */
         (w1 & 0x1FF)          /* k5 */
         );
}

static void rdp_setscissor(uint32_t w0, uint32_t w1)
{
   gDPSetScissor(0,
         /*min(*/(uint32_t)(((w0 & 0x00FFF000) >> 14))/*, 320)*/,
         /*min(*/(uint32_t)(((w0 & 0x00000FFF) >> 2))/*, 240)*/,
         /*min(*/(uint32_t)(((w1 & 0x00FFF000) >> 14))/*, 320)*/,
         /*min(*/(uint32_t)(((w1 & 0x00000FFF) >> 2))/*, 240)*/
         );
}

static void rdp_setprimdepth(uint32_t w0, uint32_t w1)
{
   gDPSetPrimDepth(0 /* stub */, 0 /* stub */);
}

#define F3DEX2_SETOTHERMODE(cmd,sft,len,data) { \
  rdp.cmd0 = (cmd<<24) | ((32-(sft)-(len))<<8) | (((len)-1)); \
  rdp.cmd1 = data; \
  gfx_instruction[settings.ucode][cmd](rdp.cmd0, rdp.cmd1); \
}
#define SETOTHERMODE(cmd,sft,len,data) { \
  rdp.cmd0 = (cmd<<24) | ((sft)<<8) | (len); \
  rdp.cmd1 = data; \
  gfx_instruction[settings.ucode][cmd](rdp.cmd0, rdp.cmd1); \
}

static void rdp_setothermode(uint32_t w0, uint32_t w1)
{
  SETOTHERMODE(0xB9, 0, 32, w1);            // SETOTHERMODE_L
  SETOTHERMODE(0xBA, 0, 32, w0 & 0x00FFFFFF);       // SETOTHERMODE_H

  //LRDP("rdp_setothermode\n");
}

static void rdp_uc2_setothermode(uint32_t w0, uint32_t w1)
{
   F3DEX2_SETOTHERMODE(0xE2, 0, 32, w1);         // SETOTHERMODE_L
   F3DEX2_SETOTHERMODE(0xE3, 0, 32, w0 & 0x00FFFFFF);    // SETOTHERMODE_H

  //LRDP("rdp_uc2_setothermode\n");
}

void load_palette (uint32_t addr, uint16_t start, uint16_t count)
{
   uint16_t *dpal, end, i, p;
   dpal = (uint16_t*)(rdp.pal_8 + start);
   end = start+count;

   for (i=start; i<end; i++)
   {
      *(dpal++) = *(uint16_t *)(gfx.RDRAM + (addr^2));
      addr += 2;
      //FRDP ("%d: %08lx\n", i, *(uint16_t *)(gfx.RDRAM + (addr^2)));
   }
   start >>= 4;
   end = start + (count >> 4);
   if (end == start) // it can be if count < 16
      end = start + 1;
   for (p = start; p < end; p++)
      rdp.pal_8_crc[p] = CRC32( 0xFFFFFFFF, &rdp.pal_8[(p << 4)], 32 );
   rdp.pal_256_crc = CRC32( 0xFFFFFFFF, rdp.pal_8_crc, 64 );
}

static void rdp_loadtlut(uint32_t w0, uint32_t w1)
{
   int32_t i, j;
   uint32_t tile;
   uint16_t start, count;

   tile = (w1 >> 24) & 0x07;
   start = rdp.tiles[tile].t_mem - 256; // starting location in the palettes
   //start = ((uint16_t)(w1 >> 2) & 0x3FF) + 1;
   count = ((uint16_t)(w1 >> 14) & 0x3FF) + 1;    // number to copy

   if (rdp.timg.addr + (count<<1) > BMASK)
      count = (uint16_t)((BMASK - rdp.timg.addr) >> 1);

   if (start+count > 256)
      count = 256-start;

   load_palette (rdp.timg.addr, start, count);

   rdp.timg.addr += count << 1;

#ifdef HAVE_HWFBE
   if (!rdp.tbuff_tex) //paranoid check.
      return;

   //the buffer is definitely wrong, as there must be no CI frame buffers
   //find and remove it
   for (i = 0; i < NUM_TMU; i++)
   {
      for (j = 0; j < rdp.texbufs[i].count; j++)
      {
         if (&(rdp.texbufs[i].images[j]) == rdp.tbuff_tex)
         {
            rdp.texbufs[i].count--;
            if (j < rdp.texbufs[i].count)
               memcpy(&(rdp.texbufs[i].images[j]), &(rdp.texbufs[i].images[j+1]), sizeof(TBUFF_COLOR_IMAGE)*(rdp.texbufs[i].count-j));
            return;
         }
      }
   }
#endif
   //FRDP("loadtlut: tile: %d, start: %d, count: %d, from: %08lx\n", tile, start, count, rdp.timg.addr);
}

int tile_set = 0;
static void rdp_settilesize(uint32_t w0, uint32_t w1)
{
   gDPSetTileSize((w1 >> 24) & 0x07,
         (((w0 >> 14)) & 0x03ff),
         (((w0 >> 2 )) & 0x03ff),
         (((w1 >> 14)) & 0x03ff),
         (((w1 >> 2 )) & 0x03ff)
         );
}

#ifdef HAVE_HWFBE
static void setTBufTex(uint16_t t_mem, uint32_t cnt)
{
   int i;
   FRDP("setTBufTex t_mem=%d, cnt=%d\n", t_mem, cnt);
   TBUFF_COLOR_IMAGE * pTbufTex = rdp.tbuff_tex;
   for (i = 0; i < 2; i++)
   {
      LRDP("Before: ");
      if (rdp.aTBuffTex[i])
      {
         FRDP("rdp.aTBuffTex[%d]: tmu=%d t_mem=%d tile=%d\n", i, rdp.aTBuffTex[i]->tmu, rdp.aTBuffTex[i]->t_mem, rdp.aTBuffTex[i]->tile);
      }
      else
      {
         FRDP("rdp.aTBuffTex[%d]=0\n", i);
      }

      if ((rdp.aTBuffTex[i] == 0 && rdp.aTBuffTex[i^1] != pTbufTex) || (rdp.aTBuffTex[i] && rdp.aTBuffTex[i]->t_mem >= t_mem && rdp.aTBuffTex[i]->t_mem < t_mem + cnt))
      {
         if (pTbufTex)
         {
            rdp.aTBuffTex[i] = pTbufTex;
            rdp.aTBuffTex[i]->t_mem = t_mem;
            pTbufTex = 0;
            FRDP("rdp.aTBuffTex[%d] tmu=%d t_mem=%d\n", i, rdp.aTBuffTex[i]->tmu, rdp.aTBuffTex[i]->t_mem);
         }
         else
         {
            rdp.aTBuffTex[i] = 0;
            FRDP("rdp.aTBuffTex[%d]=0\n", i);
         }
      }
   }
}
#endif

static void rdp_loadblock(uint32_t w0, uint32_t w1)
{
  if (rdp.skip_drawing)
    return;

  // lr_s specifies number of 64-bit words to copy
  // 10.2 format
  gDPLoadBlock(
        ((w1 >> 24) & 0x07), 
        (w0 >> 14) & 0x3FF, /* ul_s */
        (w0 >>  2) & 0x3FF, /* ul_t */
        (w1 >> 14) & 0x3FF, /* lr_s */
        (w1 & 0x0FFF) /* dxt */
        );
}

static void rdp_loadtile(uint32_t w0, uint32_t w1)
{
   gDPLoadTile(
         ((w1 >> 24) & 0x07), /* tile */
         ((w0 >> 14) & 0x03FF), /* ul_s */
         ((w0 >> 2 ) & 0x03FF), /*ul_t */
         ((w1 >> 14) & 0x03FF), /* lr_s */
         ((w1 >> 2 ) & 0x03FF) /* lr_t */
         );
}

static void rdp_settile(uint32_t w0, uint32_t w1)
{
   gDPSetTile(
         ((w0 >> 21) & 0x07),       /* format */
         ((w0 >> 19) & 0x03),       /* size   */
         ((w0 >> 9) & 0x01FF),      /* line   */
         (w0 & 0x1FF),              /* tmem   */
         ((w1 >> 24) & 0x07),       /* tile index no */
         ((w1 >> 20) & 0x0F),       /* palette */
         ((w1 >> 19) & 0x01),       /* clamp_t */
         ((w1 >> 9) & 0x01),        /* clamp_s */
         ((w1 >> 14) & 0x0F),       /* mask_t */
         ((w1 >> 4) & 0x0F),        /* mask_s */
         ((w1 >> 10) & 0x0F),       /* shift_t */
         (w1 & 0x0F),               /* shift_s */
         ((w1 >> 18) & 0x01),       /* mirror_t */
         ((w1 >> 8) & 0x01)         /* mirror_s */
         );
}

static void rdp_fillrect(uint32_t w0, uint32_t w1)
{
  uint32_t ul_x = ((w1 & 0x00FFF000) >> 14);
  uint32_t ul_y = (w1 & 0x00000FFF) >> 2;
  uint32_t lr_x = ((w0 & 0x00FFF000) >> 14) + 1;
  uint32_t lr_y = ((w0 & 0x00000FFF) >> 2) + 1;
  gDPFillRectangle(ul_x, ul_y, lr_x, lr_y);
}

static void rdp_setfillcolor(uint32_t w0, uint32_t w1)
{
   gDPSetFillColor(w1);
}

static void rdp_setfogcolor(uint32_t w0, uint32_t w1)
{
   gDPSetFogColor(0 /*stub */, 0 /* stub */, 0 /* stub */, 0 /* stub */);
}

static void rdp_setblendcolor(uint32_t w0, uint32_t w1)
{
   gDPSetBlendColor(0 /*stub */, 0 /* stub */, 0 /* stub */, 0 /* stub */);
}

static void rdp_setprimcolor(uint32_t w0, uint32_t w1)
{
   gDPSetPrimColor(0 /* stub */, 0 /* stub */, 0 /* stub */, 0 /* stub */, 0 /* stub */, 0 /* stub */);
}

static void rdp_setenvcolor(uint32_t w0, uint32_t w1)
{
   gDPSetEnvColor( _SHIFTR( w1, 24, 8 ),       // r
         _SHIFTR( w1, 16, 8 ),       // g
         _SHIFTR( w1,  8, 8 ),       // b
         _SHIFTR( w1,  0, 8 ) );     // a
}

static void rdp_setcombine(uint32_t w0, uint32_t w1)
{
   gDP_SetCombine(w0, w1);
}

static void rdp_settextureimage(uint32_t w0, uint32_t w1)
{
   gDPSetTextureImage(
         ((w0 >> 21) & 0x07),    /* format */
         ((w0 >> 19) & 0x03),    /* size   */
         (1 + (w0 & 0x00000FFF)),/* width  */
         segoffset(w1)           /* address */
         );
}

static void rdp_setdepthimage(uint32_t w0, uint32_t w1)
{
   gDPSetDepthImage(segoffset(w1) & BMASK);
}

int SwapOK = true;

void RestoreScale(void)
{
  FRDP("Return to original scale: x = %f, y = %f\n", rdp.scale_x_bak, rdp.scale_y_bak);
  rdp.scale_x = rdp.scale_x_bak;
  rdp.scale_y = rdp.scale_y_bak;
  //    update_scissor();
  rdp.view_scale[0] *= rdp.scale_x;
  rdp.view_scale[1] *= rdp.scale_y;
  rdp.view_trans[0] *= rdp.scale_x;
  rdp.view_trans[1] *= rdp.scale_y;
  rdp.update |= UPDATE_VIEWPORT | UPDATE_SCISSOR;
  //*
  {
    grDepthMask (FXFALSE);
    grBufferClear (0, 0, 0xFFFF);
    grDepthMask (FXTRUE);
  }
  //*/
}


static void rdp_setcolorimage(uint32_t w0, uint32_t w1)
{
   gDPSetColorImage(
         (w0 >> 21) & 0x7,       /* fmt */
         (w0 >> 19) & 0x3,       /* siz */
         (w0 & 0xFFF) + 1,       /* width */
         segoffset(w1) & BMASK   /* img */
         );
}

static void rsp_reserved0(uint32_t w0, uint32_t w1)
{
}

static void rsp_uc5_reserved0(uint32_t w0, uint32_t w1)
{
  ucode5_texshiftaddr = segoffset(w1);
  ucode5_texshiftcount = 0;

#ifdef EXTREME_LOGGING
  FRDP("uc5_texshift. addr: %08lx\n", ucode5_texshiftaddr);
#endif
}

static void rsp_reserved1(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("reserved1 - ignored\n");
#endif
}

static void rsp_reserved2(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("reserved2\n");
#endif
}

static void rsp_reserved3(uint32_t w0, uint32_t w1)
{
#ifdef EXTREME_LOGGING
  LRDP("reserved3 - ignored\n");
#endif
}

/******************************************************************
Function: FrameBufferRead
Purpose:  This function is called to notify the dll that the
frame buffer memory is beening read at the given address.
DLL should copy content from its render buffer to the frame buffer
in N64 RDRAM
DLL is responsible to maintain its own frame buffer memory addr list
DLL should copy 4KB block content back to RDRAM frame buffer.
Emulator should not call this function again if other memory
is read within the same 4KB range
input:    addr          rdram address
val                     val
size            1 = uint8_t, 2 = uint16_t, 4 = uint32_t
output:   none
*******************************************************************/

EXPORT void CALL FBRead(uint32_t addr)
{
  if (cpu_fb_ignore)
    return;
  if (cpu_fb_write_called)
  {
    cpu_fb_ignore = true;
    cpu_fb_write = false;
    return;
  }
  cpu_fb_read_called = true;
  uint32_t a = segoffset(addr);

#ifdef EXTREME_LOGGING
  FRDP("FBRead. addr: %08lx\n", a);
#endif

  if (!rdp.fb_drawn && (a >= rdp.cimg) && (a < rdp.ci_end))
  {
    fbreads_back++;
    //if (fbreads_back > 2) //&& (rdp.ci_width <= 320))
    {
       CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
      rdp.fb_drawn = true;
    }
  }
  if (!rdp.fb_drawn_front && (a >= rdp.maincimg[1].addr) && (a < rdp.maincimg[1].addr + rdp.ci_width*rdp.ci_height*2))
  {
    fbreads_front++;
    //if (fbreads_front > 2)//&& (rdp.ci_width <= 320))
    {
      uint32_t cimg = rdp.cimg;
      rdp.cimg = rdp.maincimg[1].addr;
      if (fb_emulation_enabled)
      {
        rdp.ci_width = rdp.maincimg[1].width;
        rdp.ci_count = 0;
        uint32_t h = rdp.frame_buffers[0].height;
        rdp.frame_buffers[0].height = rdp.maincimg[1].height;
        CopyFrameBuffer(GR_BUFFER_FRONTBUFFER);
        rdp.frame_buffers[0].height = h;
      }
      else
      {
        CopyFrameBuffer(GR_BUFFER_FRONTBUFFER);
      }
      rdp.cimg = cimg;
      rdp.fb_drawn_front = true;
    }
  }
}


/******************************************************************
Function: FrameBufferWrite
Purpose:  This function is called to notify the dll that the
frame buffer has been modified by CPU at the given address.
input:    addr          rdram address
val                     val
size            1 = uint8_t, 2 = uint16_t, 4 = uint32_t
output:   none
*******************************************************************/
EXPORT void CALL FBWrite(uint32_t addr, uint32_t size)
{
  if (cpu_fb_ignore)
    return;
  if (cpu_fb_read_called)
  {
    cpu_fb_ignore = true;
    cpu_fb_write = false;
    return;
  }
  cpu_fb_write_called = true;
  uint32_t a = segoffset(addr);

#ifdef EXTREME_LOGGING
  FRDP("FBWrite. addr: %08lx\n", a);
#endif

  if (a < rdp.cimg || a > rdp.ci_end)
    return;
  cpu_fb_write = true;
  uint32_t shift_l = (a-rdp.cimg) >> 1;
  uint32_t shift_r = shift_l+2;

  d_ul_x = min(d_ul_x, shift_l%rdp.ci_width);
  d_ul_y = min(d_ul_y, shift_l/rdp.ci_width);
  d_lr_x = max(d_lr_x, shift_r%rdp.ci_width);
  d_lr_y = max(d_lr_y, shift_r/rdp.ci_width);
}


/************************************************************************
Function: FBGetFrameBufferInfo
Purpose:  This function is called by the emulator core to retrieve frame
buffer information from the video plugin in order to be able
to notify the video plugin about CPU frame buffer read/write
operations

size:
= 1           byte
= 2           word (16 bit) <-- this is N64 default depth buffer format
= 4           dword (32 bit)

when frame buffer information is not available yet, set all values
in the FrameBufferInfo structure to 0

input:    FrameBufferInfo pinfo[6]
pinfo is pointed to a FrameBufferInfo structure which to be
filled in by this function
output:   Values are return in the FrameBufferInfo structure
Plugin can return up to 6 frame buffer info
************************************************************************/
///*
EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
   int i;
   FrameBufferInfo * pinfo = (FrameBufferInfo *)p;
   memset(pinfo,0,sizeof(FrameBufferInfo)*6);
   if (!(settings.frame_buffer&fb_get_info))
      return;

#ifdef EXTREME_LOGGING
   LRDP("FBGetFrameBufferInfo ()\n");
#endif
   //*
   if (fb_emulation_enabled)
   {
      pinfo[0].addr   = rdp.maincimg[1].addr;
      pinfo[0].size   = rdp.maincimg[1].size;
      pinfo[0].width  = rdp.maincimg[1].width;
      pinfo[0].height = rdp.maincimg[1].height;
      int info_index = 1;
      for (i = 0; i < rdp.num_of_ci && info_index < 6; i++)
      {
         COLOR_IMAGE *cur_fb = &rdp.frame_buffers[i];
         if (cur_fb->status == CI_MAIN || cur_fb->status == CI_COPY_SELF ||
               cur_fb->status == CI_OLD_COPY)
         {
            pinfo[info_index].addr   = cur_fb->addr;
            pinfo[info_index].size   = cur_fb->size;
            pinfo[info_index].width  = cur_fb->width;
            pinfo[info_index].height = cur_fb->height;
            info_index++;
         }
      }
   }
   else
   {
      pinfo[0].addr   = rdp.maincimg[0].addr;
      pinfo[0].size   = rdp.ci_size;
      pinfo[0].width  = rdp.ci_width;
      pinfo[0].height = rdp.ci_width*3/4;
      pinfo[1].addr   = rdp.maincimg[1].addr;
      pinfo[1].size   = rdp.ci_size;
      pinfo[1].width  = rdp.ci_width;
      pinfo[1].height = rdp.ci_width*3/4;
   }
   //*/
}

#include "ucodeFB.h"

void DetectFrameBufferUsage(void)
{
   int i;
   LRDP("DetectFrameBufferUsage\n");

   uint32_t dlist_start = *(uint32_t*)(gfx.DMEM+0xFF0);
   uint32_t a;

   // Do nothing if dlist is empty
   if (dlist_start == 0)
      return;

   int tidal = false;
   if ((settings.hacks&hack_PMario) && (rdp.copy_ci_index || rdp.frame_buffers[rdp.copy_ci_index].status == CI_COPY_SELF))
      tidal = true;
   uint32_t ci = rdp.cimg, zi = rdp.zimg;
   uint32_t ci_height = rdp.frame_buffers[(rdp.ci_count > 0)?rdp.ci_count-1:0].height;
   rdp.main_ci = rdp.main_ci_end = rdp.main_ci_bg = rdp.ci_count = 0;
   rdp.main_ci_index = rdp.copy_ci_index = rdp.copy_zi_index = 0;
   rdp.zimg_end = 0;
   rdp.tmpzimg = 0;
   rdp.motionblur = false;
   rdp.main_ci_last_tex_addr = 0;
   int previous_ci_was_read = rdp.read_previous_ci;
   rdp.read_previous_ci = false;
   rdp.read_whole_frame = false;
   rdp.swap_ci_index = rdp.black_ci_index = -1;
   SwapOK = true;

   // Start executing at the start of the display list
   rdp.pc_i = 0;
   rdp.pc[rdp.pc_i] = dlist_start;
   rdp.dl_count = -1;
   rdp.halt = 0;
   rdp.scale_x_bak = rdp.scale_x;
   rdp.scale_y_bak = rdp.scale_y;

   // MAIN PROCESSING LOOP
   do {

      // Get the address of the next command
      a = rdp.pc[rdp.pc_i] & BMASK;

      // Load the next command and its input
      rdp.cmd0 = ((uint32_t*)gfx.RDRAM)[a>>2];   // \ Current command, 64 bit
      rdp.cmd1 = ((uint32_t*)gfx.RDRAM)[(a>>2)+1]; // /

      // Output the address before the command

      // Go to the next instruction
      rdp.pc[rdp.pc_i] = (a+8) & BMASK;

      if ((uintptr_t)((void*)(gfx_instruction_lite[settings.ucode][rdp.cmd0>>24])))
         gfx_instruction_lite[settings.ucode][rdp.cmd0>>24](rdp.cmd0, rdp.cmd1);

      // check DL counter
      if (rdp.dl_count != -1)
      {
         rdp.dl_count --;
         if (rdp.dl_count == 0)
         {
            rdp.dl_count = -1;

            LRDP("End of DL\n");
            rdp.pc_i --;
         }
      }

   } while (!rdp.halt);
   SwapOK = true;
   if (rdp.ci_count > NUMTEXBUF) //overflow
   {
      rdp.cimg = ci;
      rdp.zimg = zi;
      rdp.num_of_ci = rdp.ci_count;
      rdp.scale_x = rdp.scale_x_bak;
      rdp.scale_y = rdp.scale_y_bak;
      return;
   }

   if (rdp.black_ci_index > 0 && rdp.black_ci_index < rdp.copy_ci_index)
      rdp.frame_buffers[rdp.black_ci_index].status = CI_MAIN;

   if (rdp.frame_buffers[rdp.ci_count-1].status == CI_UNKNOWN)
   {
      if (rdp.ci_count > 1)
         rdp.frame_buffers[rdp.ci_count-1].status = CI_AUX;
      else
         rdp.frame_buffers[rdp.ci_count-1].status = CI_MAIN;
   }

   if ((rdp.frame_buffers[rdp.ci_count-1].status == CI_AUX) &&
         (rdp.frame_buffers[rdp.main_ci_index].width < 320) &&
         (rdp.frame_buffers[rdp.ci_count-1].width > rdp.frame_buffers[rdp.main_ci_index].width))
   {
      for (i = 0; i < rdp.ci_count; i++)
      {
         if (rdp.frame_buffers[i].status == CI_MAIN)
            rdp.frame_buffers[i].status = CI_AUX;
         else if (rdp.frame_buffers[i].addr == rdp.frame_buffers[rdp.ci_count-1].addr)
            rdp.frame_buffers[i].status = CI_MAIN;
         //                        FRDP("rdp.frame_buffers[%d].status = %d\n", i, rdp.frame_buffers[i].status);
      }
      rdp.main_ci_index = rdp.ci_count-1;
   }

   int all_zimg = true;
   for (i = 0; i < rdp.ci_count; i++)
   {
      if (rdp.frame_buffers[i].status != CI_ZIMG)
      {
         all_zimg = false;
         break;
      }
   }
   if (all_zimg)
   {
      for (i = 0; i < rdp.ci_count; i++)
         rdp.frame_buffers[i].status = CI_MAIN;
   }

#ifndef NDEBUG
   LRDP("detect fb final results: \n");
   for (i = 0; i < rdp.ci_count; i++)
   {
      FRDP("rdp.frame_buffers[%d].status = %s, addr: %08lx, height: %d\n", i, CIStatus[rdp.frame_buffers[i].status], rdp.frame_buffers[i].addr, rdp.frame_buffers[i].height);
   }
#endif

   rdp.cimg = ci;
   rdp.zimg = zi;
   rdp.num_of_ci = rdp.ci_count;
   if (rdp.read_previous_ci && previous_ci_was_read)
   {
      if (!fb_hwfbe_enabled || !rdp.copy_ci_index)
         rdp.motionblur = true;
   }
   if (rdp.motionblur || fb_hwfbe_enabled || (rdp.frame_buffers[rdp.copy_ci_index].status == CI_AUX_COPY))
   {
      rdp.scale_x = rdp.scale_x_bak;
      rdp.scale_y = rdp.scale_y_bak;
   }

   if ((rdp.read_previous_ci || previous_ci_was_read) && !rdp.copy_ci_index)
      rdp.read_whole_frame = true;
   if (rdp.read_whole_frame)
   {
#ifdef HAVE_HWFBE
      if (fb_hwfbe_enabled)
      {
         if (rdp.read_previous_ci && !previous_ci_was_read && (settings.swapmode != 2) && (settings.ucode != ucode_PerfectDark))
         {
            int ind = (rdp.ci_count > 0)?rdp.ci_count-1:0;
            uint32_t height = rdp.frame_buffers[ind].height;
            rdp.frame_buffers[ind].height = ci_height;
            CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
            rdp.frame_buffers[ind].height = height;
         }
         if (rdp.swap_ci_index < 0)
         {
            rdp.texbufs[0].clear_allowed = rdp.texbufs[1].clear_allowed = true;
            OpenTextureBuffer(&rdp.frame_buffers[rdp.main_ci_index]);
         }
      }
      else
#endif
      {
         if (rdp.motionblur)
         {
            if (settings.frame_buffer&fb_motionblur)
               CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
            else
               memset(gfx.RDRAM+rdp.cimg, 0, rdp.ci_width*rdp.ci_height*rdp.ci_size);
         }
         else //if (ci_width == rdp.frame_buffers[rdp.main_ci_index].width)
         {
            if (rdp.maincimg[0].height > 65) //for 1080
            {
               rdp.cimg = rdp.maincimg[0].addr;
               rdp.ci_width = rdp.maincimg[0].width;
               rdp.ci_count = 0;
               uint32_t h = rdp.frame_buffers[0].height;
               rdp.frame_buffers[0].height = rdp.maincimg[0].height;
               CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
               rdp.frame_buffers[0].height = h;
            }
            else //conker
            {
               CopyFrameBuffer (GR_BUFFER_BACKBUFFER);
            }
         }
      }
   }

#ifdef HAVE_HWFBE
   if (fb_hwfbe_enabled)
   {
      for (i = 0; i < NUM_TMU; i++)
      {
         int j;
         rdp.texbufs[i].clear_allowed = true;
         for (j = 0; j < 256; j++)
         {
            rdp.texbufs[i].images[j].drawn = false;
            rdp.texbufs[i].images[j].clear = true;
         }
      }
      if (tidal)
      {
         //LRDP("Tidal wave!\n");
         rdp.copy_ci_index = rdp.main_ci_index;
      }
   }
#endif
   rdp.ci_count = 0;
#ifdef HAVE_HWFBE
   if (settings.hacks&hack_Banjo2)
      rdp.cur_tex_buf = 0;
#endif
   rdp.maincimg[0] = rdp.frame_buffers[rdp.main_ci_index];
   //    rdp.scale_x = rdp.scale_x_bak;
   //    rdp.scale_y = rdp.scale_y_bak;
  
   //LRDP("DetectFrameBufferUsage End\n");
}

/*******************************************
 *          ProcessRDPList                 *
 *******************************************
 *    based on sources of ziggy's z64      *
 *******************************************/

static uint32_t rdp_cmd_ptr = 0;
static uint32_t rdp_cmd_cur = 0;
static uint32_t rdp_cmd_data[0x1000];

#define XSCALE(x) ((float)(x)/(1<<18))
#define YSCALE(y) ((float)(y)/(1<<2))
#define ZSCALE(z) ((rdp.zsrc == 1)? (float)(rdp.prim_depth) : (float)((uint32_t)(z))/0xffff0000)
  //#define WSCALE(w) (rdp.Persp_en? (float(uint32_t(w) + 0x10000)/0xffff0000) : 1.0f)
  //#define WSCALE(w) (rdp.Persp_en? 4294901760.0/(w + 65536) : 1.0f)
#define WSCALE(w) (rdp.Persp_en? 65536.0f/ (float)((w+ 0xffff)>>16) : 1.0f)
#define CSCALE(c) (((c)>0x3ff0000? 0x3ff0000:((c)<0? 0 : (c)))>>18)
#define _PERSP(w) ( w )
#define PERSP(s, w) ( ((int64_t)(s) << 20) / (_PERSP(w)? _PERSP(w):1) )
#define SSCALE(s, _w) (rdp.Persp_en? (float)(PERSP(s, _w))/(1 << 10) : (float)(s)/(1<<21))
#define TSCALE(s, w) (rdp.Persp_en? (float)(PERSP(s, w))/(1 << 10) : (float)(s)/(1<<21))

void lle_triangle(uint32_t w1, uint32_t w2, int shade, int texture, int zbuffer,
                  uint32_t * rdp_cmd)
{
  rdp.cur_tile = (w1 >> 16) & 0x7;
  int j;
  int xleft, xright, xleft_inc, xright_inc;
  int r, g, b, a, z, s, t, w;
  int drdx = 0, dgdx = 0, dbdx = 0, dadx = 0, dzdx = 0, dsdx = 0, dtdx = 0, dwdx = 0;
  int drde = 0, dgde = 0, dbde = 0, dade = 0, dzde = 0, dsde = 0, dtde = 0, dwde = 0;
  int flip = (w1 & 0x800000) ? 1 : 0;

  int32_t yl, ym, yh;
  int32_t xl, xm, xh;
  int32_t dxldy, dxhdy, dxmdy;
  uint32_t w3, w4, w5, w6, w7, w8;

  uint32_t * shade_base = rdp_cmd + 8;
  uint32_t * texture_base = rdp_cmd + 8;
  uint32_t * zbuffer_base = rdp_cmd + 8;

  if (shade)
  {
    texture_base += 16;
    zbuffer_base += 16;
  }
  if (texture)
  {
    zbuffer_base += 16;
  }

  w3 = rdp_cmd[2];
  w4 = rdp_cmd[3];
  w5 = rdp_cmd[4];
  w6 = rdp_cmd[5];
  w7 = rdp_cmd[6];
  w8 = rdp_cmd[7];

  yl = (w1 & 0x3fff);
  ym = ((w2 >> 16) & 0x3fff);
  yh = ((w2 >>  0) & 0x3fff);
  xl = (int32_t)(w3);
  xh = (int32_t)(w5);
  xm = (int32_t)(w7);
  dxldy = (int32_t)(w4);
  dxhdy = (int32_t)(w6);
  dxmdy = (int32_t)(w8);

  //TODO: negative overflows - commenting them out seems to cause no regressions
  //if experiencing any regressions to do with triangles, check back here
  //if (yl & (0x800<<2)) yl |= 0xfffff000<<2;
  //if (ym & (0x800<<2)) ym |= 0xfffff000<<2;
  //if (yh & (0x800<<2)) yh |= 0xfffff000<<2;

  yh &= ~3;

  r = 0xff; g = 0xff; b = 0xff; a = 0xff; z = 0xffff0000; s = 0;  t = 0;  w = 0x30000;

  if (shade)
  {
    r    = (shade_base[0] & 0xffff0000) | ((shade_base[+4 ] >> 16) & 0x0000ffff);
    g    = ((shade_base[0 ] << 16) & 0xffff0000) | (shade_base[4 ] & 0x0000ffff);
    b    = (shade_base[1 ] & 0xffff0000) | ((shade_base[5 ] >> 16) & 0x0000ffff);
    a    = ((shade_base[1 ] << 16) & 0xffff0000) | (shade_base[5 ] & 0x0000ffff);
    drdx = (shade_base[2 ] & 0xffff0000) | ((shade_base[6 ] >> 16) & 0x0000ffff);
    dgdx = ((shade_base[2 ] << 16) & 0xffff0000) | (shade_base[6 ] & 0x0000ffff);
    dbdx = (shade_base[3 ] & 0xffff0000) | ((shade_base[7 ] >> 16) & 0x0000ffff);
    dadx = ((shade_base[3 ] << 16) & 0xffff0000) | (shade_base[7 ] & 0x0000ffff);
    drde = (shade_base[8 ] & 0xffff0000) | ((shade_base[12] >> 16) & 0x0000ffff);
    dgde = ((shade_base[8 ] << 16) & 0xffff0000) | (shade_base[12] & 0x0000ffff);
    dbde = (shade_base[9 ] & 0xffff0000) | ((shade_base[13] >> 16) & 0x0000ffff);
    dade = ((shade_base[9 ] << 16) & 0xffff0000) | (shade_base[13] & 0x0000ffff);
  }
  if (texture)
  {
    s    = (texture_base[0 ] & 0xffff0000) | ((texture_base[4 ] >> 16) & 0x0000ffff);
    t    = ((texture_base[0 ] << 16) & 0xffff0000)      | (texture_base[4 ] & 0x0000ffff);
    w    = (texture_base[1 ] & 0xffff0000) | ((texture_base[5 ] >> 16) & 0x0000ffff);
    //    w = abs(w);
    dsdx = (texture_base[2 ] & 0xffff0000) | ((texture_base[6 ] >> 16) & 0x0000ffff);
    dtdx = ((texture_base[2 ] << 16) & 0xffff0000)      | (texture_base[6 ] & 0x0000ffff);
    dwdx = (texture_base[3 ] & 0xffff0000) | ((texture_base[7 ] >> 16) & 0x0000ffff);
    dsde = (texture_base[8 ] & 0xffff0000) | ((texture_base[12] >> 16) & 0x0000ffff);
    dtde = ((texture_base[8 ] << 16) & 0xffff0000)      | (texture_base[12] & 0x0000ffff);
    dwde = (texture_base[9 ] & 0xffff0000) | ((texture_base[13] >> 16) & 0x0000ffff);
  }
  if (zbuffer)
  {
    z    = zbuffer_base[0];
    dzdx = zbuffer_base[1];
    dzde = zbuffer_base[2];
  }

  xh <<= 2;  xm <<= 2;  xl <<= 2;
  r <<= 2;  g <<= 2;  b <<= 2;  a <<= 2;
  dsde >>= 2;  dtde >>= 2;  dsdx >>= 2;  dtdx >>= 2;
  dzdx >>= 2;  dzde >>= 2;
  dwdx >>= 2;  dwde >>= 2;


  int nbVtxs = 0;
  VERTEX vtxbuf[12];
  VERTEX * vtx = &vtxbuf[nbVtxs++];

  xleft = xm;
  xright = xh;
  xleft_inc = dxmdy;
  xright_inc = dxhdy;

  while (yh<ym &&
    !((!flip && xleft < xright+0x10000) ||
    (flip && xleft > xright-0x10000))) {
      xleft += xleft_inc;
      xright += xright_inc;
      s += dsde;    t += dtde;    w += dwde;
      r += drde;    g += dgde;    b += dbde;    a += dade;
      z += dzde;
      yh++;
  }

  j = ym-yh;
  if (j > 0)
  {
    int dx = (xleft-xright)>>16;
    if ((!flip && xleft < xright) ||
      (flip/* && xleft > xright*/))
    {
      if (shade) {
        vtx->r = CSCALE(r+drdx*dx);
        vtx->g = CSCALE(g+dgdx*dx);
        vtx->b = CSCALE(b+dbdx*dx);
        vtx->a = CSCALE(a+dadx*dx);
      }
      if (texture) {
        vtx->ou = SSCALE(s+dsdx*dx, w+dwdx*dx);
        vtx->ov = TSCALE(t+dtdx*dx, w+dwdx*dx);
      }
      vtx->x = XSCALE(xleft);
      vtx->y = YSCALE(yh);
      vtx->z = ZSCALE(z+dzdx*dx);
      vtx->w = WSCALE(w+dwdx*dx);
      vtx = &vtxbuf[nbVtxs++];
    }
    if ((!flip/* && xleft < xright*/) ||
      (flip && xleft > xright))
    {
      if (shade) {
        vtx->r = CSCALE(r);
        vtx->g = CSCALE(g);
        vtx->b = CSCALE(b);
        vtx->a = CSCALE(a);
      }
      if (texture) {
        vtx->ou = SSCALE(s, w);
        vtx->ov = TSCALE(t, w);
      }
      vtx->x = XSCALE(xright);
      vtx->y = YSCALE(yh);
      vtx->z = ZSCALE(z);
      vtx->w = WSCALE(w);
      vtx = &vtxbuf[nbVtxs++];
    }
    xleft += xleft_inc*j;  xright += xright_inc*j;
    s += dsde*j;  t += dtde*j;
    if (w + dwde*j) w += dwde*j;
    else w += dwde*(j-1);
    r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
    z += dzde*j;
    // render ...
  }

  if (xl != xh)
    xleft = xl;

  //if (yl-ym > 0)
  {
    int dx = (xleft-xright)>>16;
    if ((!flip && xleft <= xright) ||
      (flip/* && xleft >= xright*/))
    {
      if (shade) {
        vtx->r = CSCALE(r+drdx*dx);
        vtx->g = CSCALE(g+dgdx*dx);
        vtx->b = CSCALE(b+dbdx*dx);
        vtx->a = CSCALE(a+dadx*dx);
      }
      if (texture) {
        vtx->ou = SSCALE(s+dsdx*dx, w+dwdx*dx);
        vtx->ov = TSCALE(t+dtdx*dx, w+dwdx*dx);
      }
      vtx->x = XSCALE(xleft);
      vtx->y = YSCALE(ym);
      vtx->z = ZSCALE(z+dzdx*dx);
      vtx->w = WSCALE(w+dwdx*dx);
      vtx = &vtxbuf[nbVtxs++];
    }
    if ((!flip/* && xleft <= xright*/) ||
      (flip && xleft >= xright))
    {
      if (shade) {
        vtx->r = CSCALE(r);
        vtx->g = CSCALE(g);
        vtx->b = CSCALE(b);
        vtx->a = CSCALE(a);
      }
      if (texture) {
        vtx->ou = SSCALE(s, w);
        vtx->ov = TSCALE(t, w);
      }
      vtx->x = XSCALE(xright);
      vtx->y = YSCALE(ym);
      vtx->z = ZSCALE(z);
      vtx->w = WSCALE(w);
      vtx = &vtxbuf[nbVtxs++];
    }
  }
  xleft_inc = dxldy;
  xright_inc = dxhdy;

  j = yl-ym;
  //j--; // ?
  xleft += xleft_inc*j;  xright += xright_inc*j;
  s += dsde*j;  t += dtde*j;  w += dwde*j;
  r += drde*j;  g += dgde*j;  b += dbde*j;  a += dade*j;
  z += dzde*j;

  while (yl>ym &&
    !((!flip && xleft < xright+0x10000) ||
    (flip && xleft > xright-0x10000))) {
      xleft -= xleft_inc;    xright -= xright_inc;
      s -= dsde;    t -= dtde;    w -= dwde;
      r -= drde;    g -= dgde;    b -= dbde;    a -= dade;
      z -= dzde;
      j--;
      yl--;
  }

  // render ...
  if (j >= 0) {
    int dx = (xleft-xright)>>16;
    if ((!flip && xleft <= xright) ||
      (flip/* && xleft >= xright*/))
    {
      if (shade) {
        vtx->r = CSCALE(r+drdx*dx);
        vtx->g = CSCALE(g+dgdx*dx);
        vtx->b = CSCALE(b+dbdx*dx);
        vtx->a = CSCALE(a+dadx*dx);
      }
      if (texture) {
        vtx->ou = SSCALE(s+dsdx*dx, w+dwdx*dx);
        vtx->ov = TSCALE(t+dtdx*dx, w+dwdx*dx);
      }
      vtx->x = XSCALE(xleft);
      vtx->y = YSCALE(yl);
      vtx->z = ZSCALE(z+dzdx*dx);
      vtx->w = WSCALE(w+dwdx*dx);
      vtx = &vtxbuf[nbVtxs++];
    }
    if ((!flip/* && xleft <= xright*/) ||
      (flip && xleft >= xright))
    {
      if (shade) {
        vtx->r = CSCALE(r);
        vtx->g = CSCALE(g);
        vtx->b = CSCALE(b);
        vtx->a = CSCALE(a);
      }
      if (texture) {
        vtx->ou = SSCALE(s, w);
        vtx->ov = TSCALE(t, w);
      }
      vtx->x = XSCALE(xright);
      vtx->y = YSCALE(yl);
      vtx->z = ZSCALE(z);
      vtx->w = WSCALE(w);
      vtx = &vtxbuf[nbVtxs++];
    }
  }

  {
     int k;
    update ();
    for (k = 0; k < nbVtxs-1; k++)
    {
      VERTEX * v = &vtxbuf[k];
      v->x = v->x * rdp.scale_x + rdp.offset_x;
      v->y = v->y * rdp.scale_y + rdp.offset_y;
      //    v->z = 1.0f;///v->w;
      v->q = 1.0f/v->w;
      v->u1 = v->u0 = v->ou;
      v->v1 = v->v0 = v->ov;
      if (rdp.tex >= 1 && rdp.cur_cache[0])
      {
        if (rdp.tiles[rdp.cur_tile].shift_s)
        {
          if (rdp.tiles[rdp.cur_tile].shift_s > 10)
            v->u0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_s));
          else
            v->u0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_s);
        }
        if (rdp.tiles[rdp.cur_tile].shift_t)
        {
          if (rdp.tiles[rdp.cur_tile].shift_t > 10)
            v->v0 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile].shift_t));
          else
            v->v0 /= (float)(1 << rdp.tiles[rdp.cur_tile].shift_t);
        }

        v->u0 -= rdp.tiles[rdp.cur_tile].f_ul_s;
        v->v0 -= rdp.tiles[rdp.cur_tile].f_ul_t;
        v->u0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_x * v->u0;
        v->v0 = rdp.cur_cache[0]->c_off + rdp.cur_cache[0]->c_scl_y * v->v0;
        v->u0 /= v->w;
        v->v0 /= v->w;
      }

      if (rdp.tex >= 2 && rdp.cur_cache[1])
      {
        if (rdp.tiles[rdp.cur_tile+1].shift_s)
        {
          if (rdp.tiles[rdp.cur_tile+1].shift_s > 10)
            v->u1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_s));
          else
            v->u1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_s);
        }
        if (rdp.tiles[rdp.cur_tile+1].shift_t)
        {
          if (rdp.tiles[rdp.cur_tile+1].shift_t > 10)
            v->v1 *= (float)(1 << (16 - rdp.tiles[rdp.cur_tile+1].shift_t));
          else
            v->v1 /= (float)(1 << rdp.tiles[rdp.cur_tile+1].shift_t);
        }

        v->u1 -= rdp.tiles[rdp.cur_tile+1].f_ul_s;
        v->v1 -= rdp.tiles[rdp.cur_tile+1].f_ul_t;
        v->u1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_x * v->u1;
        v->v1 = rdp.cur_cache[1]->c_off + rdp.cur_cache[1]->c_scl_y * v->v1;
        v->u1 /= v->w;
        v->v1 /= v->w;
      }
      apply_shade_mods (v);
    }
    ConvertCoordsConvert (vtxbuf, nbVtxs);
    grCullMode (GR_CULL_DISABLE);
    grDrawVertexArrayContiguous (GR_TRIANGLE_STRIP, nbVtxs-1, vtxbuf, sizeof(VERTEX));
  }
}

static void rdp_triangle(int shade, int texture, int zbuffer)
{
  lle_triangle(rdp.cmd0, rdp.cmd1, shade, texture, zbuffer, rdp_cmd_data + rdp_cmd_cur);
}

static void rdp_trifill(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 0, 0, 0, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("trifill\n");
#endif
}

static void rdp_trishade(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 1, 0, 0, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("trishade\n");
#endif
}

static void rdp_tritxtr(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 0, 1, 0, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("tritxtr\n");
#endif
}

static void rdp_trishadetxtr(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 1, 1, 0, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("trishadetxtr\n");
#endif
}

static void rdp_trifillz(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 0, 0, 1, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("trifillz\n");
#endif
}

static void rdp_trishadez(uint32_t w0, uint32_t w1)
{
  lle_triangle(w0, w1, 1, 0, 1, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
  LRDP("trishadez\n");
#endif
}

static void rdp_tritxtrz(uint32_t w0, uint32_t w1)
{
   lle_triangle(w0, w1, 0, 1, 1, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
   LRDP("tritxtrz\n");
#endif
}

static void rdp_trishadetxtrz(uint32_t w0, uint32_t w1)
{
   lle_triangle(w0, w1, 1, 1, 1, rdp_cmd_data + rdp_cmd_cur);
#ifdef EXTREME_LOGGING
   LRDP("trishadetxtrz\n");
#endif
}

static rdp_instr rdp_command_table[64] =
{
   /* 0x00 */
   spnoop,             undef,                  undef,                  undef,
   undef,              undef,                  undef,                  undef,
   rdp_trifill,        rdp_trifillz,           rdp_tritxtr,            rdp_tritxtrz,
   rdp_trishade,       rdp_trishadez,          rdp_trishadetxtr,       rdp_trishadetxtrz,
   /* 0x10 */
   undef,              undef,                  undef,                  undef,
   undef,              undef,                  undef,                  undef,
   undef,              undef,                  undef,                  undef,
   undef,              undef,                  undef,                  undef,
   /* 0x20 */
   undef,              undef,                  undef,                  undef,
   rdp_texrect,        rdp_texrect,            rdp_loadsync,           rdp_pipesync,
   rdp_tilesync,       rdp_fullsync,           rdp_setkeygb,           rdp_setkeyr,
   rdp_setconvert,     rdp_setscissor,         rdp_setprimdepth,       rdp_setothermode,
   /* 0x30 */
   rdp_loadtlut,           undef,                  rdp_settilesize,        rdp_loadblock,
   rdp_loadtile,           rdp_settile,            rdp_fillrect,           rdp_setfillcolor,
   rdp_setfogcolor,        rdp_setblendcolor,      rdp_setprimcolor,       rdp_setenvcolor,
   rdp_setcombine,         rdp_settextureimage,    rdp_setdepthimage,      rdp_setcolorimage
};

static const uint32_t rdp_command_length[64] =
{
   8,                      // 0x00, No Op
   8,                      // 0x01, ???
   8,                      // 0x02, ???
   8,                      // 0x03, ???
   8,                      // 0x04, ???
   8,                      // 0x05, ???
   8,                      // 0x06, ???
   8,                      // 0x07, ???
   32,                     // 0x08, Non-Shaded Triangle
   32+16,                  // 0x09, Non-Shaded, Z-Buffered Triangle
   32+64,                  // 0x0a, Textured Triangle
   32+64+16,               // 0x0b, Textured, Z-Buffered Triangle
   32+64,                  // 0x0c, Shaded Triangle
   32+64+16,               // 0x0d, Shaded, Z-Buffered Triangle
   32+64+64,               // 0x0e, Shaded+Textured Triangle
   32+64+64+16,            // 0x0f, Shaded+Textured, Z-Buffered Triangle
   8,                      // 0x10, ???
   8,                      // 0x11, ???
   8,                      // 0x12, ???
   8,                      // 0x13, ???
   8,                      // 0x14, ???
   8,                      // 0x15, ???
   8,                      // 0x16, ???
   8,                      // 0x17, ???
   8,                      // 0x18, ???
   8,                      // 0x19, ???
   8,                      // 0x1a, ???
   8,                      // 0x1b, ???
   8,                      // 0x1c, ???
   8,                      // 0x1d, ???
   8,                      // 0x1e, ???
   8,                      // 0x1f, ???
   8,                      // 0x20, ???
   8,                      // 0x21, ???
   8,                      // 0x22, ???
   8,                      // 0x23, ???
   16,                     // 0x24, Texture_Rectangle
   16,                     // 0x25, Texture_Rectangle_Flip
   8,                      // 0x26, Sync_Load
   8,                      // 0x27, Sync_Pipe
   8,                      // 0x28, Sync_Tile
   8,                      // 0x29, Sync_Full
   8,                      // 0x2a, Set_Key_GB
   8,                      // 0x2b, Set_Key_R
   8,                      // 0x2c, Set_Convert
   8,                      // 0x2d, Set_Scissor
   8,                      // 0x2e, Set_Prim_Depth
   8,                      // 0x2f, Set_Other_Modes
   8,                      // 0x30, Load_TLUT
   8,                      // 0x31, ???
   8,                      // 0x32, Set_Tile_Size
   8,                      // 0x33, Load_Block
   8,                      // 0x34, Load_Tile
   8,                      // 0x35, Set_Tile
   8,                      // 0x36, Fill_Rectangle
   8,                      // 0x37, Set_Fill_Color
   8,                      // 0x38, Set_Fog_Color
   8,                      // 0x39, Set_Blend_Color
   8,                      // 0x3a, Set_Prim_Color
   8,                      // 0x3b, Set_Env_Color
   8,                      // 0x3c, Set_Combine
   8,                      // 0x3d, Set_Texture_Image
   8,                      // 0x3e, Set_Mask_Image
   8                       // 0x3f, Set_Color_Image
};

#define rdram ((uint32_t*)gfx.RDRAM)
#define rsp_dmem ((uint32_t*)gfx.DMEM)

#define dp_start (*(uint32_t*)gfx.DPC_START_REG)
#define dp_end (*(uint32_t*)gfx.DPC_END_REG)
#define dp_current (*(uint32_t*)gfx.DPC_CURRENT_REG)
#define dp_status (*(uint32_t*)gfx.DPC_STATUS_REG)

// dp_status & 0x1 = XBUS_DMEM_DMA enabled
#define READ_RDP_DATA(address) ((dp_status & 0x1) ? (rsp_dmem[((address) & 0xfff)>>2]) : (rdram[(address) >> 2]))

static void rdphalf_1(uint32_t w0, uint32_t w1)
{
   uint32_t cmd = rdp.cmd1 >> 24;
   if (cmd >= G_TRI_FILL && cmd <= G_TRI_SHADE_TXTR_ZBUFF) //triangle command
   {
      LRDP("rdphalf_1 - lle triangle\n");
      rdp_cmd_ptr = 0;
      rdp_cmd_cur = 0;
      uint32_t a;

      do
      {
         rdp_cmd_data[rdp_cmd_ptr++] = rdp.cmd1;
         // check DL counter
         if (rdp.dl_count != -1)
         {
            rdp.dl_count --;
            if (rdp.dl_count == 0)
            {
               rdp.dl_count = -1;

               LRDP("End of DL\n");
               rdp.pc_i --;
            }
         }

         // Get the address of the next command
         a = rdp.pc[rdp.pc_i] & BMASK;

         // Load the next command and its input
         rdp.cmd0 = ((uint32_t*)gfx.RDRAM)[a>>2];   // \ Current command, 64 bit
         rdp.cmd1 = ((uint32_t*)gfx.RDRAM)[(a>>2)+1]; // /

         // Go to the next instruction
         rdp.pc[rdp.pc_i] = (a+8) & BMASK;

      }while ((rdp.cmd0 >> 24) != 0xb3);
      rdp_cmd_data[rdp_cmd_ptr++] = rdp.cmd1;
      cmd = (rdp_cmd_data[rdp_cmd_cur] >> 24) & 0x3f;
      rdp.cmd0 = rdp_cmd_data[rdp_cmd_cur+0];
      rdp.cmd1 = rdp_cmd_data[rdp_cmd_cur+1];
      /*
         uint32_t cmd3 = ((uint32_t*)gfx.RDRAM)[(a>>2)+2];
         if ((cmd3>>24) == 0xb4)
         rglSingleTriangle = true;
         else
         rglSingleTriangle = false;
         */
      rdp_command_table[cmd](rdp.cmd0, rdp.cmd1);
   }
   else
      LRDP("rdphalf_1 - IGNORED\n");
}

static void rdphalf_2(uint32_t w0, uint32_t w1)
{
   RDP_E("rdphalf_2 - IGNORED\n");
   LRDP("rdphalf_2 - IGNORED\n");
}

static void rdphalf_cont(uint32_t w0, uint32_t w1)
{
   RDP_E("rdphalf_cont - IGNORED\n");
   LRDP("rdphalf_cont - IGNORED\n");
}

/******************************************************************
Function: ProcessRDPList
Purpose:  This function is called when there is a Dlist to be
processed. (Low level GFX list)
input:    none
output:   none
*******************************************************************/
EXPORT void CALL ProcessRDPList(void)
{
#ifdef VISUAL_LOGGING
   VLOG ("ProcessRDPList ()\n");
   LRDP("ProcessRDPList ()\n");
#endif

   uint32_t i;
   uint32_t cmd, length, cmd_length;
   rdp_cmd_ptr = 0;
   rdp_cmd_cur = 0;

   if (dp_end <= dp_current) return;
   length = dp_end - dp_current;

   // load command data
   for (i=0; i < length; i += 4)
   {
      rdp_cmd_data[rdp_cmd_ptr++] = READ_RDP_DATA(dp_current + i);
#ifdef VISUAL_LOGGING
      if (rdp_cmd_ptr >= 0x1000)
      {
         FRDP("rdp_process_list: rdp_cmd_ptr overflow %x %x --> %x\n", length, dp_current, dp_end);
      }
#endif
   }

   dp_current = dp_end;

   cmd = (rdp_cmd_data[0] >> 24) & 0x3f;
   cmd_length = (rdp_cmd_ptr + 1) * 4;

   // check if more data is needed
   if (cmd_length < rdp_command_length[cmd])
      return;
   rdp.LLE = true;
   while (rdp_cmd_cur < rdp_cmd_ptr)
   {
      cmd = (rdp_cmd_data[rdp_cmd_cur] >> 24) & 0x3f;

      if (((rdp_cmd_ptr-rdp_cmd_cur) * 4) < rdp_command_length[cmd])
         return;

      // execute the command
      rdp.cmd0 = rdp_cmd_data[rdp_cmd_cur+0];
      rdp.cmd1 = rdp_cmd_data[rdp_cmd_cur+1];
      rdp.cmd2 = rdp_cmd_data[rdp_cmd_cur+2];
      rdp.cmd3 = rdp_cmd_data[rdp_cmd_cur+3];
      rdp_command_table[cmd](rdp.cmd0, rdp.cmd1);

      rdp_cmd_cur += rdp_command_length[cmd] / 4;
   };

   rdp.LLE = false;
   dp_start = dp_end;
   dp_status &= ~0x0002;
}
