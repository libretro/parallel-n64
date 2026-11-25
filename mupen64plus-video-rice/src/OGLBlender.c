/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - OGLBlender.c                                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2003 Rice1964                                           *
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
/* Rice's GL blender: the BLEND_ functions as GL's (OGLBlender.h). C89. */
#include <stdint.h>

#include "osal_opengl.h"

#include "OGLBlender.h"

/* by BLEND_; 0 is not one */
static const GLenum blend_funcs[] =
{
    GL_SRC_ALPHA,               /* nothing */
    GL_ZERO,                    /* BLEND_ZERO               = 1 */
    GL_ONE,                     /* BLEND_ONE                = 2 */
    GL_SRC_COLOR,               /* BLEND_SRCCOLOR           = 3 */
    GL_ONE_MINUS_SRC_COLOR,     /* BLEND_INVSRCCOLOR        = 4 */
    GL_SRC_ALPHA,               /* BLEND_SRCALPHA           = 5 */
    GL_ONE_MINUS_SRC_ALPHA,     /* BLEND_INVSRCALPHA        = 6 */
    GL_DST_ALPHA,               /* BLEND_DESTALPHA          = 7 */
    GL_ONE_MINUS_DST_ALPHA,     /* BLEND_INVDESTALPHA       = 8 */
    GL_DST_COLOR,               /* BLEND_DESTCOLOR          = 9 */
    GL_ONE_MINUS_DST_COLOR,     /* BLEND_INVDESTCOLOR       = 10 */
    GL_SRC_ALPHA_SATURATE,      /* BLEND_SRCALPHASAT        = 11 */
    GL_SRC_ALPHA_SATURATE,      /* BLEND_BOTHSRCALPHA       = 12 */
    GL_SRC_ALPHA_SATURATE       /* BLEND_BOTHINVSRCALPHA    = 13 */
};

static void oglblender_func(CBlender *b, uint32_t srcFunc, uint32_t desFunc)
{
    (void)b;
    glBlendFunc(blend_funcs[srcFunc], blend_funcs[desFunc]);
}

static void oglblender_enable(CBlender *b)
{
    (void)b;
    glEnable(GL_BLEND);
}

static void oglblender_disable(CBlender *b)
{
    (void)b;
    glDisable(GL_BLEND);
}

static const CBlenderOps oglblender_ops = { oglblender_func, oglblender_enable, oglblender_disable };
static CBlender oglblender = { &oglblender_ops };

CBlender *OGLBlender_Get(void)
{
    return &oglblender;
}
