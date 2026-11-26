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
/* Rice's GL draws: the triangle list, the texture and fill rectangles,
 * the simple 2D texture, the rotated sprite, and the vertex arrays they
 * share. C89. */
#ifndef _RICE_GL_DRAW_H_
#define _RICE_GL_DRAW_H_

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the vertex arrays pointed at Rice's vertex buffers (g_vtxProjected5,
 * g_oglVtxColors, g_vtxBuffer) */
void RiceGL_PointVertexArrays(void);
/* as RiceGL_PointVertexArrays, and fog at g_vtxProjected5's fifth float */
void RiceGL_InitVertexArrays(void);
/* the whole display as the viewport */
void RiceGL_ViewportDisplay(void);
/* the N64 viewport, in the display */
void RiceGL_ViewportN64(void);

/* gRSP.numVertices of g_vtxIndex as triangles, in the N64 viewport */
void RiceGL_DrawTris(void);
/* g_texRectTVtx as a fan, with its colours and both texture coordinates */
void RiceGL_DrawTexRect(void);
/* (@x0,@y0)-(@x1,@y1), in display pixels, filled with @color at @depth */
void RiceGL_DrawFillRect(float x0, float y0, float x1, float y1, uint32_t color, float depth);
/* g_texRectTVtx as two triangles, in its first vertex's colour */
void RiceGL_DrawSimple2DTexture(void);
/* g_texRectTVtx as two triangles, in @color (RGBA, 0 to 1) */
void RiceGL_DrawSpriteR(const float color[4]);

#ifdef __cplusplus
}
#endif

#endif
