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
/* Rice's textures on GL (OGLTexture.h). C89. */
#include <stdlib.h>

#include "OGLTexture.h"

/* RiceConfig.h's */
#define TXT_QUALITY_DEFAULT 0
#define TXT_QUALITY_32BIT   1
#define TXT_QUALITY_16BIT   2

static void ogl_upload(CTexture *tex)
{
    COGLTexture *t = (COGLTexture *)tex;

    glBindTexture(GL_TEXTURE_2D, t->m_dwTextureName);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (RiceConfig_Mipmapping())
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    /* from where the pixels were converted to the GPU */
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->m_dwCreatedTextureWidth, tex->m_dwCreatedTextureHeight,
          GL_RGBA, GL_UNSIGNED_BYTE, tex->m_pTexture);
}

static void ogl_release(CTexture *tex)
{
    COGLTexture *t = (COGLTexture *)tex;
    glDeleteTextures(1, &t->m_dwTextureName);
}

static const CTextureOps ogl_ops = { ogl_upload, ogl_release };

CTexture *COGLTexture_New(uint32_t dwWidth, uint32_t dwHeight, TextureUsage usage)
{
    COGLTexture *t = (COGLTexture *)malloc(sizeof(*t));
    CTexture *tex;
    uint32_t w;

    if (!t)
        return NULL;
    tex = &t->base;
    CTexture_Init(tex, &ogl_ops, dwWidth, dwHeight, usage);
    /* always 32 bits a texel to load */
    tex->m_dwTextureFmt = TEXTURE_FMT_A8R8G8B8;
    t->m_glFmt          = GL_RGBA;
    glGenTextures(1, &t->m_dwTextureName);

    /* a power of 2 each way */
    for (w = 1; w < dwWidth; w <<= 1);
    tex->m_dwCreatedTextureWidth = w;
    for (w = 1; w < dwHeight; w <<= 1);
    tex->m_dwCreatedTextureHeight = w;

    tex->m_fYScale = (float)tex->m_dwCreatedTextureHeight / (float)tex->m_dwHeight;
    tex->m_fXScale = (float)tex->m_dwCreatedTextureWidth / (float)tex->m_dwWidth;

    tex->m_pTexture = malloc(tex->m_dwCreatedTextureWidth * tex->m_dwCreatedTextureHeight
          * CTexture_GetPixelSize(tex));
    if (!tex->m_pTexture)
    {
        CTexture_Delete(tex);
        return NULL;
    }

    switch (RiceConfig_TextureQuality())
    {
        case TXT_QUALITY_DEFAULT:
            if (RiceConfig_ColorQuality() == TEXTURE_FMT_A4R4G4B4)
                t->m_glFmt = GL_RGBA4;
            break;
        case TXT_QUALITY_16BIT:
            t->m_glFmt = GL_RGBA4;
            break;
    }

    glBindTexture(GL_TEXTURE_2D, t->m_dwTextureName);
    /* storage only: the pixels come with the first update */
    glTexImage2D(GL_TEXTURE_2D, 0, t->m_glFmt, tex->m_dwCreatedTextureWidth, tex->m_dwCreatedTextureHeight, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    return tex;
}
