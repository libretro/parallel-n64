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
/* Rice's GL texture units (RiceGLTex.h). C89. */
#include <stdint.h>

#include "osal_opengl.h"

#include "Config.h"
#include "Debugger.h"
#include "OGLTexture.h"
#include "RenderBase.h"
#include "RiceGLTex.h"

RiceGLTex rice_gl_tex;

/* by TextureUVFlag; mirror and clamp made the real ones at Init */
static UVFlagMap OGLXUVFlagMaps[] =
{
    {TEXTURE_UV_FLAG_WRAP, GL_REPEAT},
    {TEXTURE_UV_FLAG_MIRROR, GL_MIRRORED_REPEAT},
    {TEXTURE_UV_FLAG_CLAMP, GL_CLAMP},
};

void RiceGLTex_Reset(void)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        rice_gl_tex.curBoundTex[i]    = 0;
        rice_gl_tex.texUnitEnabled[i] = false;
        rice_gl_tex.tileUFlags[i]     = TEXTURE_UV_FLAG_CLAMP;
        rice_gl_tex.tileVFlags[i]     = TEXTURE_UV_FLAG_CLAMP;
    }
    rice_gl_tex.enableMultiTexture = false;
}

void RiceGLTex_Init(void)
{
    int i;

    OGLXUVFlagMaps[TEXTURE_UV_FLAG_MIRROR].realFlag = GL_MIRRORED_REPEAT;
    OGLXUVFlagMaps[TEXTURE_UV_FLAG_CLAMP].realFlag  = GL_CLAMP_TO_EDGE;

    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &rice_gl_tex.maxTexUnits);

    for (i = 0; i < 8; i++)
        rice_gl_tex.textureUnitMap[i] = -1;
    rice_gl_tex.textureUnitMap[0] = 0;    /* T0 is usually using texture unit 0 */
    rice_gl_tex.textureUnitMap[1] = 1;    /* T1 is usually using texture unit 1 */
}

void RiceGLTex_EnableMultiTexture(void)
{
    rice_gl_tex.enableMultiTexture = true;
}

void RiceGLTex_EnableUnit(int unitno, bool flag)
{
    if (rice_gl_tex.texUnitEnabled[unitno] != flag)
    {
        rice_gl_tex.texUnitEnabled[unitno] = flag;
        glActiveTexture(GL_TEXTURE0 + unitno);
    }
}

void RiceGLTex_DisableMultiTexture(void)
{
    glActiveTexture(GL_TEXTURE1);
    RiceGLTex_EnableUnit(1, false);
    glActiveTexture(GL_TEXTURE0);
    RiceGLTex_EnableUnit(0, false);
    glActiveTexture(GL_TEXTURE0);
    RiceGLTex_EnableUnit(0, true);
}

void RiceGLTex_Bind(GLuint texture, int unitno)
{
    if (rice_gl_tex.enableMultiTexture)
    {
        if (unitno < rice_gl_tex.maxTexUnits && rice_gl_tex.curBoundTex[unitno] != texture)
        {
            glActiveTexture(GL_TEXTURE0 + unitno);
            glBindTexture(GL_TEXTURE_2D, texture);
            rice_gl_tex.curBoundTex[unitno] = texture;
        }
    }
    else if (rice_gl_tex.curBoundTex[0] != texture)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        rice_gl_tex.curBoundTex[0] = texture;
    }
}

void RiceGLTex_Unbind(int unitno)
{
    if (rice_gl_tex.enableMultiTexture)
    {
        glActiveTexture(GL_TEXTURE0 + unitno);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

/* the wrap mode set when it or the bound texture changed.  (The
 * texture remembered is unit 0's, whichever unit this is, as the C++
 * had it; S makes @unitno active, T relies on S having done it.) */
static void set_tex_wrap(int unitno, GLuint flag, GLenum pname, bool activate,
      GLuint *mflag, GLuint *mtex)
{
    if (rice_gl_tex.curBoundTex[unitno] != mtex[unitno] || mflag[unitno] != flag)
    {
        if (activate)
            glActiveTexture(GL_TEXTURE0 + unitno);
        mtex[unitno]  = rice_gl_tex.curBoundTex[0];
        mflag[unitno] = flag;
        glTexParameteri(GL_TEXTURE_2D, pname, flag);
    }
}

static GLuint wrap_s_flag[8], wrap_s_tex[8];
static GLuint wrap_t_flag[8], wrap_t_tex[8];

static void set_uv_flag(TextureUVFlag flag, uint32_t tile, bool is_v)
{
    GLuint mode = OGLXUVFlagMaps[flag].realFlag;
    GLenum pname = is_v ? GL_TEXTURE_WRAP_T : GL_TEXTURE_WRAP_S;
    GLuint *mflag = is_v ? wrap_t_flag : wrap_s_flag;
    GLuint *mtex  = is_v ? wrap_t_tex : wrap_s_tex;
    int tex, textureNo;

    if (is_v)
        rice_gl_tex.tileVFlags[tile] = flag;
    else
        rice_gl_tex.tileUFlags[tile] = flag;

    if (!rice_gl_tex.enableMultiTexture)
    {
        if (tile == gRSP.curTile)    /* for basic GL, only the 1st texel */
        {
            COGLTexture *pTexture = (COGLTexture *)g_textures[gRSP.curTile].m_pCTexture;
            if (pTexture)
            {
                RiceGLTex_EnableUnit(0, true);
                RiceGLTex_Bind(pTexture->m_dwTextureName, 0);
            }
            set_tex_wrap(0, mode, pname, !is_v, mflag, mtex);
        }
        return;
    }

    if (tile == gRSP.curTile)
        tex = 0;
    else if (tile == ((gRSP.curTile+1)&7))
        tex = 1;
    else if (tile == ((gRSP.curTile+2)&7))
        tex = 2;
    else if (tile == ((gRSP.curTile+3)&7))
        tex = 3;
    else
        return;

    for (textureNo = 0; textureNo < 8; textureNo++)
    {
        if (rice_gl_tex.textureUnitMap[textureNo] == tex)
        {
            COGLTexture *pTexture = (COGLTexture *)g_textures[(gRSP.curTile+tex)&7].m_pCTexture;
            if (pTexture)
            {
                RiceGLTex_EnableUnit(textureNo, true);
                RiceGLTex_Bind(pTexture->m_dwTextureName, textureNo);
            }
            set_tex_wrap(textureNo, mode, pname, !is_v, mflag, mtex);
        }
    }
}

void RiceGLTex_SetUFlag(TextureUVFlag flag, uint32_t tile)
{
    set_uv_flag(flag, tile, false);
}

void RiceGLTex_SetVFlag(TextureUVFlag flag, uint32_t tile)
{
    set_uv_flag(flag, tile, true);
}

void RiceGLTex_SetTileRepeat(uint32_t dwTile)
{
    TileAdditionalInfo *tile = &gRDP.tilesinfo[dwTile];
    struct gDPTile *tileinfo = &gDP.tiles[dwTile];

    if( tile->bForceClampS )
        RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_CLAMP, dwTile);
    else if( tile->bForceWrapS )
        RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_WRAP, dwTile);
    else if( tileinfo->masks == 0 || tileinfo->clamps )
    {
        if( gRDP.otherMode.cycle_type  >= G_CYC_COPY )
            RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_WRAP, dwTile);  /* can not clamp in COPY/FILL mode */
        else
            RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_CLAMP, dwTile);
    }
    else if (tileinfo->mirrors )
        RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_MIRROR, dwTile);
    else
        RiceGLTex_SetUFlag(TEXTURE_UV_FLAG_WRAP, dwTile);

    if( tile->bForceClampT )
        RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_CLAMP, dwTile);
    else if( tile->bForceWrapT )
        RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_WRAP, dwTile);
    else if( tileinfo->maskt == 0 || tileinfo->clampt)
    {
        if( gRDP.otherMode.cycle_type  >= G_CYC_COPY )
            RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_WRAP, dwTile);  /* can not clamp in COPY/FILL mode */
        else
            RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_CLAMP, dwTile);
    }
    else if (tileinfo->mirrort )
        RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_MIRROR, dwTile);
    else
        RiceGLTex_SetVFlag(TEXTURE_UV_FLAG_WRAP, dwTile);
}

void RiceGLTex_ApplyFilter(TextureFilter min_filter, TextureFilter mag_filter)
{
    static uint32_t minflag[32], magflag[32];
    static uint32_t mtex[32];
    int i;

    for (i = 0; i < rice_gl_tex.maxTexUnits; i++)
    {
        GLint iMinFilter, iMagFilter;

        if (min_filter == FILTER_LINEAR)   /* the texture is filtered */
        {
            iMagFilter = GL_LINEAR;
            switch (options.mipmapping)
            {
                case TEXTURE_BILINEAR_FILTER:
                    iMinFilter = GL_LINEAR_MIPMAP_NEAREST;
                    break;
                case TEXTURE_TRILINEAR_FILTER:
                    iMinFilter = GL_LINEAR_MIPMAP_LINEAR;
                    break;
                case TEXTURE_NO_FILTER:
                    iMinFilter = GL_NEAREST_MIPMAP_NEAREST;
                    break;
                case TEXTURE_NO_MIPMAP:
                default:
                    iMinFilter = GL_LINEAR;   /* bilinear without mipmaps */
                    break;
            }
        }
        else    /* no filtering, all nearest */
        {
            iMagFilter = GL_NEAREST;
            iMinFilter = options.mipmapping ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
        }

        if (!rice_gl_tex.texUnitEnabled[i])
            continue;

        if (mtex[i] != rice_gl_tex.curBoundTex[i])
        {
            mtex[i] = rice_gl_tex.curBoundTex[i];
            glActiveTexture(GL_TEXTURE0 + i);
            minflag[i] = min_filter;
            magflag[i] = mag_filter;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMagFilter);
        }
        else
        {
            if (minflag[i] != (unsigned int)min_filter)
            {
                minflag[i] = min_filter;
                glActiveTexture(GL_TEXTURE0 + i);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
            }
            if (magflag[i] != (unsigned int)mag_filter)
            {
                magflag[i] = mag_filter;
                glActiveTexture(GL_TEXTURE0 + i);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMagFilter);
            }
        }
    }
}
