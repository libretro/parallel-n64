/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - OGLCombiner.cpp                                         *
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

#include "osal_opengl.h"

#include "OGLCombiner.h"
#include "OGLRender.h"
#include "OGLGraphicsContext.h"
#include "OGLTexture.h"

//========================================================================
uint32_t DirectX_OGL_BlendFuncMaps [] =
{
    GL_SRC_ALPHA,               //Nothing
    GL_ZERO,                    //BLEND_ZERO               = 1,
    GL_ONE,                     //BLEND_ONE                = 2,
    GL_SRC_COLOR,               //BLEND_SRCCOLOR           = 3,
    GL_ONE_MINUS_SRC_COLOR,     //BLEND_INVSRCCOLOR        = 4,
    GL_SRC_ALPHA,               //BLEND_SRCALPHA           = 5,
    GL_ONE_MINUS_SRC_ALPHA,     //BLEND_INVSRCALPHA        = 6,
    GL_DST_ALPHA,               //BLEND_DESTALPHA          = 7,
    GL_ONE_MINUS_DST_ALPHA,     //BLEND_INVDESTALPHA       = 8,
    GL_DST_COLOR,               //BLEND_DESTCOLOR          = 9,
    GL_ONE_MINUS_DST_COLOR,     //BLEND_INVDESTCOLOR       = 10,
    GL_SRC_ALPHA_SATURATE,      //BLEND_SRCALPHASAT        = 11,
    GL_SRC_ALPHA_SATURATE,      //BLEND_BOTHSRCALPHA       = 12,    
    GL_SRC_ALPHA_SATURATE,      //BLEND_BOTHINVSRCALPHA    = 13,
};

static void OGLBlender_BlendFunc(CBlender *b, uint32_t srcFunc, uint32_t desFunc)
{
    (void)b;
    glBlendFunc(DirectX_OGL_BlendFuncMaps[srcFunc], DirectX_OGL_BlendFuncMaps[desFunc]);
}

static void OGLBlender_Enable(CBlender *b)
{
    (void)b;
    glEnable(GL_BLEND);
}

static void OGLBlender_Disable(CBlender *b)
{
    (void)b;
    glDisable(GL_BLEND);
}

static const CBlenderOps oglBlenderOps = { OGLBlender_BlendFunc, OGLBlender_Enable, OGLBlender_Disable };
static CBlender oglBlender = { &oglBlenderOps };

CBlender *OGLBlender_Get(void)
{
    return &oglBlender;
}
