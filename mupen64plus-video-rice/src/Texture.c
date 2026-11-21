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
/* Rice's textures (Texture.h). C89. */
#include <stdlib.h>

#include "Texture.h"

void CTexture_Init(CTexture *tex, const CTextureOps *ops, uint32_t dwWidth, uint32_t dwHeight,
      TextureUsage usage)
{
    tex->ops                      = ops;
    tex->m_dwWidth                = dwWidth;
    tex->m_dwHeight               = dwHeight;
    tex->m_dwCreatedTextureWidth  = dwWidth;
    tex->m_dwCreatedTextureHeight = dwHeight;
    tex->m_fXScale                = 1.0f;
    tex->m_fYScale                = 1.0f;
    tex->m_bScaledS               = false;
    tex->m_bScaledT               = false;
    tex->m_bClampedS              = false;
    tex->m_bClampedT              = false;
    tex->m_bIsEnhancedTexture     = false;
    tex->m_Usage                  = usage;
    tex->m_bHoldUpload            = false;
    tex->m_bUploadPending         = false;
    tex->m_pTexture               = NULL;
    tex->m_dwTextureFmt           = TEXTURE_FMT_A8R8G8B8;
}

void CTexture_Delete(CTexture *tex)
{
    if (!tex)
        return;
    tex->ops->release(tex);
    free(tex->m_pTexture);
    free(tex);
}

TextureFmt CTexture_GetSurfaceFormat(const CTexture *tex)
{
    if (tex->m_pTexture == NULL)
        return TEXTURE_FMT_UNKNOWN;
    return tex->m_dwTextureFmt;
}

uint32_t CTexture_GetPixelSize(const CTexture *tex)
{
    return tex->m_dwTextureFmt == TEXTURE_FMT_A8R8G8B8 ? 4 : 2;
}

bool CTexture_StartUpdate(CTexture *tex, DrawInfo *di)
{
    if (tex->m_pTexture == NULL)
        return false;

    di->dwHeight        = (uint16_t)tex->m_dwHeight;
    di->dwWidth         = (uint16_t)tex->m_dwWidth;
    di->dwCreatedHeight = (unsigned short)tex->m_dwCreatedTextureHeight;
    di->dwCreatedWidth  = (unsigned short)tex->m_dwCreatedTextureWidth;
    di->lpSurface       = tex->m_pTexture;
    di->lPitch          = (int)(CTexture_GetPixelSize(tex) * tex->m_dwCreatedTextureWidth);
    return true;
}

void CTexture_EndUpdate(CTexture *tex)
{
    if (tex->m_bHoldUpload)
    {
        tex->m_bUploadPending = true;
        return;
    }
    tex->ops->upload(tex);
}

void CTexture_HoldUpload(CTexture *tex, bool hold)
{
    tex->m_bHoldUpload = hold;
    if (!hold && tex->m_bUploadPending)
    {
        tex->m_bUploadPending = false;
        tex->ops->upload(tex);
    }
}
