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
#include "Debugger.h"

GLIDE64_DEBUGGER _debugger;

#define SX(x) ((x)*rdp.scale_1024)
#define SY(x) ((x)*rdp.scale_768)

#ifdef COLORED_DEBUGGER
#define COL_CATEGORY()  grConstantColorValue(0xD288F400)
#define COL_UCC()   grConstantColorValue(0xFF000000)
#define COL_CC()    grConstantColorValue(0x88C3F400)
#define COL_UAC()   grConstantColorValue(0xFF808000)
#define COL_AC()    grConstantColorValue(0x3CEE5E00)
#define COL_TEXT()    grConstantColorValue(0xFFFFFF00)
#define COL_SEL(x)    grConstantColorValue((x)?0x00FF00FF:0x800000FF)
#else
#define COL_CATEGORY()
#define COL_UCC()
#define COL_CC()
#define COL_UAC()
#define COL_AC()
#define COL_TEXT()
#define COL_SEL(x)
#endif

#define COL_GRID    0xFFFFFF80

int  grid = 0;
static const char *tri_type[4] = { "TRIANGLE", "TEXRECT", "FILLRECT", "BACKGROUND" };

//Platform-specific stuff
#ifndef WIN32
typedef struct dbgPOINT {
   int x;
   int y;
} POINT;
#endif
void DbgCursorPos(POINT * pt)
{
#ifdef __WINDOWS__
  GetCursorPos (pt);
#else //!todo find a way to get cursor position on Unix
  pt->x = pt->y = 0;
#endif
}

//
// debug_init - initialize the debugger
//

void debug_init ()
{
  _debugger.capture = 0;
  _debugger.selected = SELECTED_TRI;
  _debugger.screen = NULL;
  _debugger.tri_list = NULL;
  _debugger.tri_last = NULL;
  _debugger.tri_sel = NULL;
  _debugger.tmu = 0;

  _debugger.tex_scroll = 0;
  _debugger.tex_sel = 0;

  _debugger.draw_mode = 0;
}

//
// debug_cacheviewer - views the debugger's cache
//

void debug_cacheviewer ()
{
  grCullMode (GR_CULL_DISABLE);

  int i;
  for (i=0; i<2; i++)
  {
    grTexFilterMode (i,
      (settings.filter_cache)?GR_TEXTUREFILTER_BILINEAR:GR_TEXTUREFILTER_POINT_SAMPLED,
      (settings.filter_cache)?GR_TEXTUREFILTER_BILINEAR:GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexClampMode (i,
      GR_TEXTURECLAMP_CLAMP,
      GR_TEXTURECLAMP_CLAMP);
  }

  switch (_debugger.draw_mode)
  {
  case 0:
    grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE,
      FXFALSE);
    grAlphaCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE,
      FXFALSE);
    break;
  case 1:
    grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE,
      FXFALSE);
    grAlphaCombine (GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_NONE,
      FXFALSE);
    grConstantColorValue (0xFFFFFFFF);
    break;
  case 2:
    grColorCombine (GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_LOCAL_CONSTANT,
      GR_COMBINE_OTHER_NONE,
      FXFALSE);
    grAlphaCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_LOCAL_NONE,
      GR_COMBINE_OTHER_TEXTURE,
      FXFALSE);
    grConstantColorValue (0xFFFFFFFF);
  }

  if (_debugger.tmu == 1)
  {
    grTexCombine (GR_TMU1,
      GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      FXFALSE,
      FXFALSE);

    grTexCombine (GR_TMU0,
      GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      GR_COMBINE_FUNCTION_SCALE_OTHER,
      GR_COMBINE_FACTOR_ONE,
      FXFALSE,
      FXFALSE);
  }
  else
  {
    grTexCombine (GR_TMU0,
      GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_FUNCTION_LOCAL,
      GR_COMBINE_FACTOR_NONE,
      FXFALSE,
      FXFALSE);
  }

  grAlphaBlendFunction (GR_BLEND_SRC_ALPHA,
    GR_BLEND_ONE_MINUS_SRC_ALPHA,
    GR_BLEND_ONE,
    GR_BLEND_ZERO);

  // Draw texture memory
  for (i=0; i<4; i++)
  {
    for (uint32_t x=0; x<16; x++)
    {
      uint32_t y = i+_debugger.tex_scroll;
      if (x+y*16 >= (uint32_t)rdp.n_cached[_debugger.tmu]) break;
      CACHE_LUT * cache = voodoo.tex_UMA?rdp.cache[0]:rdp.cache[_debugger.tmu];

      VERTEX v[4] = {
          { SX(x*64.0f), SY(512+64.0f*i), 1, 1,       0, 0, 0, 0, {0, 0, 0, 0} },
          { SX(x*64.0f+64.0f*cache[x+y*16].scale_x), SY(512+64.0f*i), 1, 1,    255*cache[x+y*16].scale_x, 0, 0, 0, {0, 0, 0, 0} },
          { SX(x*64.0f), SY(512+64.0f*i+64.0f*cache[x+y*16].scale_y), 1, 1,    0, 255*cache[x+y*16].scale_y, 0, 0, {0, 0, 0, 0} },
          { SX(x*64.0f+64.0f*cache[x+y*16].scale_x), SY(512+64.0f*i+64.0f*cache[x+y*16].scale_y), 1, 1, 255*cache[x+y*16].scale_x, 255*cache[x+y*16].scale_y, 0, 0, {0, 0, 0, 0} }
          };
      for
      (int i=0; i<4; i++)
      {
        v[i].u1 = v[i].u0;
        v[i].v1 = v[i].v0;
      }

      ConvertCoordsConvert (v, 4);

      grTexSource(_debugger.tmu,
        voodoo.tex_min_addr[_debugger.tmu] + cache[x+y*16].tmem_addr,
        GR_MIPMAPLEVELMASK_BOTH,
        &cache[x+y*16].t_info);

      grDrawTriangle (&v[2], &v[1], &v[0]);
      grDrawTriangle (&v[2], &v[3], &v[1]);
    }
  }

}

//
// debug_capture - does a frame capture event (for debugging)
//

void debug_capture ()
{
  uint32_t i,j;

  if (_debugger.tri_list == NULL) goto END;
  _debugger.tri_sel = _debugger.tri_list;
  _debugger.selected = SELECTED_TRI;

  // Connect the list
  _debugger.tri_last->pNext = _debugger.tri_list;

END:
  // Release all data
  delete [] _debugger.screen;
  TRI_INFO *tri;
  for (tri=_debugger.tri_list; tri != _debugger.tri_last;)
  {
    TRI_INFO *tmp = tri;
    tri = tri->pNext;
    delete [] tmp->v;
    delete tmp;
  }
  delete [] tri->v;
  delete tri;

  // Reset all values
  _debugger.capture = 0;
  _debugger.selected = SELECTED_TRI;
  _debugger.screen = NULL;
  _debugger.tri_list = NULL;
  _debugger.tri_last = NULL;
  _debugger.tri_sel = NULL;
  _debugger.tex_sel = 0;
}

//
// debug_mouse - draws the debugger mouse
//

void debug_mouse ()
{
  grColorCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE,
    FXFALSE);

  grAlphaCombine (GR_COMBINE_FUNCTION_SCALE_OTHER,
    GR_COMBINE_FACTOR_ONE,
    GR_COMBINE_LOCAL_NONE,
    GR_COMBINE_OTHER_TEXTURE,
    FXFALSE);

  // Draw the cursor
  POINT pt;
  DbgCursorPos(&pt);
  float cx = (float)pt.x;
  float cy = (float)pt.y;

  VERTEX v[4] = {
    { cx,       cy, 1, 1,   0,   0,   0, 0, {0, 0, 0, 0} },
    { cx+32,    cy, 1, 1, 255,   0,   0, 0, {0, 0, 0, 0} },
    { cx,    cy+32, 1, 1,   0, 255,   0, 0, {0, 0, 0, 0} },
    { cx+32, cy+32, 1, 1, 255, 255,   0, 0, {0, 0, 0, 0} }
    };

  ConvertCoordsKeep (v, 4);

  grTexSource(GR_TMU0,
    voodoo.tex_min_addr[GR_TMU0] + offset_cursor,
    GR_MIPMAPLEVELMASK_BOTH,
    &cursorTex);

  if (voodoo.num_tmu >= 3)
    grTexCombine (GR_TMU2,
      GR_COMBINE_FUNCTION_NONE,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_FUNCTION_NONE,
      GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
  if (voodoo.num_tmu >= 2)
    grTexCombine (GR_TMU1,
      GR_COMBINE_FUNCTION_NONE,
      GR_COMBINE_FACTOR_NONE,
      GR_COMBINE_FUNCTION_NONE,
      GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
  grTexCombine (GR_TMU0,
    GR_COMBINE_FUNCTION_LOCAL,
    GR_COMBINE_FACTOR_NONE,
    GR_COMBINE_FUNCTION_LOCAL,
    GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

  grDrawTriangle (&v[0], &v[1], &v[2]);
  grDrawTriangle (&v[1], &v[3], &v[2]);
}

//
// debug_keys - receives debugger key input
//

void debug_keys ()
{
}

//
// output - output debugger text
//

void output (float x, float y, int scale, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  vsprintf(out_buf, fmt, ap);
  va_end(ap);

  uint8_t c,r;
  for (uint32_t i=0; i<strlen(out_buf); i++)
  {
    c = ((out_buf[i]-32) & 0x1F) * 8;//<< 3;
    r = (((out_buf[i]-32) & 0xE0) >> 5) * 16;//<< 4;
    VERTEX v[4] = { { SX(x), SY(768-y), 1, 1,   (float)c, r+16.0f, 0, 0, {0, 0, 0, 0} },
      { SX(x+8), SY(768-y), 1, 1,   c+8.0f, r+16.0f, 0, 0, {0, 0, 0, 0} },
      { SX(x), SY(768-y-16), 1, 1,  (float)c, (float)r, 0, 0, {0, 0, 0, 0} },
      { SX(x+8), SY(768-y-16), 1, 1,  c+8.0f, (float)r, 0, 0, {0, 0, 0, 0} }
      };
    if (!scale)
    {
      v[0].x = x;
      v[0].y = y;
      v[1].x = x+8;
      v[1].y = y;
      v[2].x = x;
      v[2].y = y-16;
      v[3].x = x+8;
      v[3].y = y-16;
    }

    ConvertCoordsKeep (v, 4);

    grDrawTriangle (&v[0], &v[1], &v[2]);
    grDrawTriangle (&v[1], &v[3], &v[2]);

    x+=8;
  }
}
