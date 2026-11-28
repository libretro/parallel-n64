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
/* Rice's GL texture units: what is bound and enabled on each, the tiles'
 * wrap modes, and the min/mag filters. C89. */
#ifndef _RICE_GL_TEX_H_
#define _RICE_GL_TEX_H_

#include <stdint.h>
#include <boolean.h>

#include "osal_opengl.h"
#include "typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RiceGLTex
{
    GLuint curBoundTex[8];
    bool   texUnitEnabled[8];
    bool   enableMultiTexture;
    GLint  maxTexUnits;
    /* the texel (0 = the current tile's, 1 = the next's ...) each GL unit
     * samples, or -1 */
    int    textureUnitMap[8];
    /* each tile's wrap mode as the N64 set it */
    TextureUVFlag tileUFlags[8];
    TextureUVFlag tileVFlags[8];
} RiceGLTex;

extern RiceGLTex rice_gl_tex;

/* nothing bound or enabled, single texture, every tile clamped (the
 * renderer's construction) */
void RiceGLTex_Reset(void);
/* the GL context's unit count, units 0 and 1 for the two texels, the
 * real mirror and clamp modes (the renderer's Initialize) */
void RiceGLTex_Init(void);

void RiceGLTex_EnableMultiTexture(void);
/* units 0 and 1 off, then 0 on, unit 0 active */
void RiceGLTex_DisableMultiTexture(void);

void RiceGLTex_Bind(GLuint texture, int unitno);
/* nothing bound on @unitno (multitexture only) */
void RiceGLTex_Unbind(int unitno);
void RiceGLTex_EnableUnit(int unitno, bool flag);

/* @tile's horizontal / vertical wrap mode, and on each unit sampling it
 * the texture bound and the mode set */
void RiceGLTex_SetUFlag(TextureUVFlag flag, uint32_t tile);
void RiceGLTex_SetVFlag(TextureUVFlag flag, uint32_t tile);
/* @tile's wrap modes from its mask, clamp and mirror bits (and the
 * ROM's forced clamps and wraps) */
void RiceGLTex_SetTileRepeat(uint32_t tile);

/* each enabled unit's min and mag filters for @filter (and the
 * mipmapping option), set when it or the bound texture changed */
void RiceGLTex_ApplyFilter(TextureFilter filter);

#ifdef __cplusplus
}
#endif

#endif
