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

#ifndef _BLENDER_H_
#define _BLENDER_H_

#include <stdint.h>

/* Rice's blender: what the RDP's blender asks, as the renderer's blend
 * functions (Blender.c); each renderer's three of them its ops. */

typedef struct CBlender CBlender;

typedef struct CBlenderOps
{
    void (*BlendFunc)(CBlender *b, uint32_t srcFunc, uint32_t desFunc);   /* BLEND_ each */
    void (*Enable)(CBlender *b);
    void (*Disable)(CBlender *b);
} CBlenderOps;

struct CBlender
{
    const CBlenderOps *ops;
};

typedef enum _BLEND 
{
    BLEND_ZERO            = 1,
    BLEND_ONE             = 2,
    BLEND_SRCCOLOR        = 3,
    BLEND_INVSRCCOLOR     = 4,
    BLEND_SRCALPHA        = 5,
    BLEND_INVSRCALPHA     = 6,
    BLEND_DESTALPHA       = 7,
    BLEND_INVDESTALPHA    = 8,
    BLEND_DESTCOLOR       = 9,
    BLEND_INVDESTCOLOR    = 10,
    BLEND_SRCALPHASAT     = 11,
    BLEND_BOTHSRCALPHA    = 12,
    BLEND_BOTHINVSRCALPHA = 13,
    BLEND_BLENDFACTOR     = 14,
    BLEND_INVBLENDFACTOR  = 15,
    BLEND_FORCE_DWORD     = 0x7fffffff
} BLEND;

#ifdef __cplusplus
extern "C" {
#endif

/* the blend of the RDP's other modes (gRDP.otherMode, gDP.otherMode) */
void Blender_InitBlenderMode(CBlender *b);
/* source alpha over the colour image */
void Blender_NormalAlphaBlender(CBlender *b);
/* the source as it is */
void Blender_DisableAlphaBlender(CBlender *b);

/* Render.cpp's: the renderer's alpha test on or off */
void RiceRender_SetAlphaTestEnable(int enable);

#ifdef __cplusplus
}
#endif

#endif
