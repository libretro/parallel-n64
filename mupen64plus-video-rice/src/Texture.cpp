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

#include <stdlib.h>

#include "Config.h"
#include "Debugger.h"
#include "OGLDebug.h"
#include "OGLGraphicsContext.h"
#include "Texture.h"
#include "TextureManager.h"

#include "osal_opengl.h"

//////////////////////////////////////////
// Constructors / Deconstructors

// Probably shouldn't need more than 4096 * 4096

CTexture::CTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage) :
    m_pTexture(NULL),
    m_Usage(usage),
    m_dwWidth(dwWidth),
    m_dwHeight(dwHeight),
    m_dwCreatedTextureWidth(dwWidth),
    m_dwCreatedTextureHeight(dwHeight),
    m_fXScale(1.0f),
    m_fYScale(1.0f),
    m_dwTextureName(0)
{
    // FIXME: If usage is AS_RENDER_TARGET, we need to create pbuffer instead of regular texture
    glGenTextures(1, &m_dwTextureName);
    OPENGL_CHECK_ERRORS;

    // Make the width and height be the power of 2
    uint32 w;
    for (w = 1; w < dwWidth; w <<= 1);
    m_dwCreatedTextureWidth = w;
    for (w = 1; w < dwHeight; w <<= 1);
    m_dwCreatedTextureHeight = w;

    m_fXScale = (float)m_dwCreatedTextureWidth / (float)m_dwWidth;        
    m_fYScale = (float)m_dwCreatedTextureHeight / (float)m_dwHeight;

    m_pTexture = malloc(m_dwCreatedTextureWidth * m_dwCreatedTextureHeight * 4);

    LOG_TEXTURE(TRACE2("New texture: (%d, %d)", dwWidth, dwHeight));
}

CTexture::~CTexture()
{
    // FIXME: If usage is AS_RENDER_TARGET, we need to destroy the pbuffer

    glDeleteTextures(1, &m_dwTextureName);
    OPENGL_CHECK_ERRORS;

    free(m_pTexture);
}

bool CTexture::StartUpdate(DrawInfo *di)
{
    if (m_pTexture == NULL)
        return false;

    di->dwHeight = (uint16)m_dwHeight;
    di->dwWidth = (uint16)m_dwWidth;
    di->dwCreatedHeight = m_dwCreatedTextureHeight;
    di->dwCreatedWidth = m_dwCreatedTextureWidth;
    di->lpSurface = m_pTexture;
    di->lPitch = m_dwCreatedTextureWidth * 4;

    return true;
}

void CTexture::EndUpdate(DrawInfo *di)
{
    glBindTexture(GL_TEXTURE_2D, m_dwTextureName);
    OPENGL_CHECK_ERRORS;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    OPENGL_CHECK_ERRORS;

    // mipmap support
    if(options.mipmapping)
    {
        // Set Mipmap
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        OPENGL_CHECK_ERRORS;

        // __LIBRETRO__ TODO: 
        //glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glGenerateMipmap(GL_TEXTURE_2D);
        OPENGL_CHECK_ERRORS;
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        OPENGL_CHECK_ERRORS;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_dwCreatedTextureWidth, m_dwCreatedTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pTexture);
    OPENGL_CHECK_ERRORS;
}
