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
/* The renderer's state and what it asks of GL for it: the depth bias,
 * the alpha reference, the tiles' textures, the matrices reset and the
 * simple textured rectangle. C89. */
#ifndef _RICE_RENDER_STATE_H_
#define _RICE_RENDER_STATE_H_

#include <stdint.h>
#include <boolean.h>

#include "typedefs.h"
#include "Texture.h"
#include "TextureManager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RiceRenderState
{
    uint32_t zBias;    /* the depth bias the triangles are drawn with */
    uint32_t alpha;    /* the alpha reference, from the RDP's blend colour */
} RiceRenderState;

extern RiceRenderState rice_render;

/* the state as a new renderer has it: no bias, alpha reference 0xFF */
void RiceRender_Reset(void);
/* the GL side readied: the display viewport, the vertex arrays and the
   texture units */
void RiceRender_Initialize(void);

void RiceRender_SetZBias(int bias);
void RiceRender_SetAlphaRef(uint32_t alpha);
void RiceRender_SetFogEnable(bool enable);

/* tile drawn with handler, a tile of width by height texels; nothing
   changes when the tile has that texture already */
void RiceRender_SetCurrentTexture(int tile, CTexture *handler,
      uint32_t width, uint32_t height, TxtrCacheEntry *entry);
/* tile drawn with entry's texture, or with none (64 by 64) when there
   is no texture */
void RiceRender_SetCurrentTextureEntry(int tile, TxtrCacheEntry *entry);

/* the clip rectangle, the matrices, the depth bias and the vertices as
   a display list starts with them */
void RiceRender_RenderReset(void);
/* the RE2 scissor, or else the scissor with the clip ratio */
void RiceRender_UpdateScissor(void);

/* a rectangle in N64 pixels drawn with texture 0, its corners' texture
   coordinates (u0, v0) and (u1, v1) */
void RiceRender_DrawSimple2DTexture(float x0, float y0, float x1, float y1,
      float u0, float v0, float u1, float v1, COLOR dif, COLOR spe,
      float z, float rhw);

#ifdef __cplusplus
}
#endif

#endif
