/*
Copyright (C) 2002 Rice1964

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

/* Rice's render textures on GL. C89. */
#include <stdlib.h>

#include "OGLTexture.h"
#include "RenderTexture.h"

static bool ogl_set_as_render_target(CRenderTexture *rt, bool enable)
{
    (void)rt; (void)enable;
    return true;
}

static void ogl_load_texture(CRenderTexture *rt, TxtrCacheEntry *pEntry)
{
    (void)rt; (void)pEntry;
}

static void ogl_store_to_rdram(CRenderTexture *rt, int infoIdx)
{
    (void)rt; (void)infoIdx;
}

static const CRenderTextureOps ogl_ops = { ogl_set_as_render_target, ogl_load_texture, ogl_store_to_rdram };

CRenderTexture *CRenderTexture_New(int width, int height, RenderTextureInfo *pInfo, TextureUsage usage)
{
    CRenderTexture *rt = (CRenderTexture *)calloc(1, sizeof(*rt));
    if (!rt)
        return NULL;
    rt->ops      = &ogl_ops;
    rt->m_width  = width;
    rt->m_height = height;
    rt->m_usage  = usage;
    rt->m_pInfo  = pInfo;
    if (usage == AS_BACK_BUFFER_SAVE)
        rt->m_pTexture = COGLTexture_New(width, height, usage);
    return rt;
}

void CRenderTexture_Delete(CRenderTexture *rt)
{
    if (!rt)
        return;
    if (rt->m_beingRendered)
    {
        RiceFB_CloseRenderTexture(false);
        CRenderTexture_SetAsRenderTarget(rt, false);
    }
    /* the GL texture given back too */
    CTexture_Delete(rt->m_pTexture);
    free(rt);
}
