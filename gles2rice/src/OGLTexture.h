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
/* A texture of Rice's on GL (Texture.h). C89. */
#ifndef _OGL_TEXTURE_H_
#define _OGL_TEXTURE_H_

#include "osal_opengl.h"
#include "Texture.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct COGLTexture
{
    CTexture base;
    GLuint   m_dwTextureName;
    GLuint   m_glFmt;
} COGLTexture;

/* made @dwWidth x @dwHeight for @usage, its size made a power of 2 each
 * way; NULL where its pixels could not be had */
CTexture *COGLTexture_New(uint32_t dwWidth, uint32_t dwHeight, TextureUsage usage);

#define COGLTexture_Name(tex) (((COGLTexture *)(tex))->m_dwTextureName)

/* from RiceConfig.cpp: the options a texture is made and put on the GPU
 * by */
uint32_t RiceConfig_TextureQuality(void);
uint32_t RiceConfig_ColorQuality(void);
uint32_t RiceConfig_Mipmapping(void);

#ifdef __cplusplus
}
#endif

#endif
