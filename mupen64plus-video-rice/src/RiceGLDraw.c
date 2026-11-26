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
/* Rice's GL draws (RiceGLDraw.h). C89. */
#include <stdint.h>

#include "osal_opengl.h"

#include "m64p_plugin.h"
#include "Config.h"
#include "RenderBase.h"
#include "RiceGLDraw.h"
#include "RSP_Parser.h"
#include "Video.h"

void RiceGL_PointVertexArrays(void)
{
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]));
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][0]));
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[0].u));
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[1].u));
}

void RiceGL_InitVertexArrays(void)
{
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][0]));
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[0].u));
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[1].u));
    glVertexAttribPointer(VS_FOG, 1, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][4]));
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]));
}

void RiceGL_ViewportDisplay(void)
{
    /* Always set: glsm re-applies its own tracked viewport every frame,
     * so a cache here would skip a needed glViewport; glsm itself drops a
     * call that changes nothing. */
    glViewport(0, 0, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight);
}

void RiceGL_ViewportN64(void)
{
    glViewport(windowSetting.vpLeftW,
          windowSetting.uDisplayHeight - windowSetting.vpTopW - windowSetting.vpHeightW,
          windowSetting.vpWidthW, windowSetting.vpHeightW);
}

void RiceGL_DrawTris(void)
{
    RiceGL_ViewportN64();

    /* Enable and point the vertex attribute arrays for the geometry draw.
     * They are otherwise only set up on a shader-combiner change, but the
     * libretro frontend resets GL state each frame, so on a frame where the
     * combiner did not change they would be left disabled and the draw
     * rejected. */
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][0]));
    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]));
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[0].u));
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(TLITVERTEX), &(g_vtxBuffer[0].tcord[1].u));
    glEnableVertexAttribArray(VS_FOG);
    glVertexAttribPointer(VS_FOG, 1, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][4]));

    glDrawElements(GL_TRIANGLES, gRSP.numVertices, GL_UNSIGNED_SHORT, g_vtxIndex);

    /* Fog is only used by this geometry draw. Disable it here so it is not
     * left enabled -- still pointing at g_vtxProjected5 with a 5-float
     * stride -- for the rect/blit draws, which use a different vertex
     * layout and would read fog out of bounds and crash. */
    glDisableVertexAttribArray(VS_FOG);
}

void RiceGL_DrawTexRect(void)
{
    const TLITVERTEX *v = g_texRectTVtx;
    GLboolean cullface;
    float depth, w, h;
    GLfloat colour[16], tex[8], tex2[8], vertices[16];

    RiceGL_ViewportDisplay();

    cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    depth = -(v[3].z*2-1);
    w = windowSetting.uDisplayWidth / 2.0f;
    h = windowSetting.uDisplayHeight / 2.0f;

    /* the fan from the last vertex back to the first */
#define TEXRECT_VTX(i, s) \
    colour[i*4+0] = (GLfloat)v[s].r; colour[i*4+1] = (GLfloat)v[s].g; \
    colour[i*4+2] = (GLfloat)v[s].b; colour[i*4+3] = (GLfloat)v[s].a; \
    tex[i*2+0]  = v[s].tcord[0].u; tex[i*2+1]  = v[s].tcord[0].v; \
    tex2[i*2+0] = v[s].tcord[1].u; tex2[i*2+1] = v[s].tcord[1].v; \
    vertices[i*4+0] = -1.0f + v[s].x / w; vertices[i*4+1] = 1.0f - v[s].y / h; \
    vertices[i*4+2] = depth; vertices[i*4+3] = 1
    TEXRECT_VTX(0, 3);
    TEXRECT_VTX(1, 2);
    TEXRECT_VTX(2, 1);
    TEXRECT_VTX(3, 0);
#undef TEXRECT_VTX

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT, GL_TRUE, 0, colour);
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 0, tex);
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, 0, tex2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    RiceGL_PointVertexArrays();

    if (cullface)
        glEnable(GL_CULL_FACE);
}

void RiceGL_DrawFillRect(float x0, float y0, float x1, float y1, uint32_t color, float depth)
{
    float a = (color>>24)/255.0f;
    float r = ((color>>16)&0xFF)/255.0f;
    float g = ((color>>8)&0xFF)/255.0f;
    float b = (color&0xFF)/255.0f;
    float w, h;
    GLboolean cullface;
    GLfloat colour[16], vertices[16];

    RiceGL_ViewportDisplay();

    cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    w = windowSetting.uDisplayWidth / 2.0f;
    h = windowSetting.uDisplayHeight / 2.0f;

    colour[0] = r; colour[1] = g; colour[2] = b; colour[3] = a;
    colour[4] = r; colour[5] = g; colour[6] = b; colour[7] = a;
    colour[8] = r; colour[9] = g; colour[10] = b; colour[11] = a;
    colour[12] = r; colour[13] = g; colour[14] = b; colour[15] = a;
    vertices[2] = vertices[6] = vertices[10] = vertices[14] = depth;
    vertices[3] = vertices[7] = vertices[11] = vertices[15] = 1;
    /* (x0,y1) (x1,y1) (x1,y0) (x0,y0) */
    vertices[0]  = -1.0f + x0 / w; vertices[1]  = 1.0f - y1 / h;
    vertices[4]  = -1.0f + x1 / w; vertices[5]  = 1.0f - y1 / h;
    vertices[8]  = -1.0f + x1 / w; vertices[9]  = 1.0f - y0 / h;
    vertices[12] = -1.0f + x0 / w; vertices[13] = 1.0f - y0 / h;

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT, GL_FALSE, 0, colour);
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, 0, vertices);
    glDisableVertexAttribArray(VS_TEXCOORD0);
    glDisableVertexAttribArray(VS_TEXCOORD1);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glVertexAttribPointer(VS_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(uint8_t)*4, &(g_oglVtxColors[0][0]));
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(float)*5, &(g_vtxProjected5[0][0]));
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glEnableVertexAttribArray(VS_TEXCOORD1);

    if (cullface)
        glEnable(GL_CULL_FACE);
}

void RiceGL_DrawSimple2DTexture(void)
{
    const TLITVERTEX *v = g_texRectTVtx;
    float a, r, g, b, w, h;
    GLboolean cullface;
    GLfloat colour[24], tex[12], tex2[12], vertices[24];

    cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    RiceGL_ViewportDisplay();

    a = (v[0].dcDiffuse>>24)/255.0f;
    r = ((v[0].dcDiffuse>>16)&0xFF)/255.0f;
    g = ((v[0].dcDiffuse>>8)&0xFF)/255.0f;
    b = (v[0].dcDiffuse&0xFF)/255.0f;
    w = windowSetting.uDisplayWidth / 2.0f;
    h = windowSetting.uDisplayHeight / 2.0f;

#define SIMPLE2D_VTX(i, s) \
    colour[i*4+0] = r; colour[i*4+1] = g; colour[i*4+2] = b; colour[i*4+3] = a; \
    tex[i*2+0]  = v[s].tcord[0].u; tex[i*2+1]  = v[s].tcord[0].v; \
    tex2[i*2+0] = v[s].tcord[1].u; tex2[i*2+1] = v[s].tcord[1].v; \
    vertices[i*4+0] = -1.0f + v[s].x / w; vertices[i*4+1] = 1.0f - v[s].y / h; \
    vertices[i*4+2] = -v[s].z; vertices[i*4+3] = 1
    /* vertices 0 1 2 and 0 2 3 */
    SIMPLE2D_VTX(0, 0);
    SIMPLE2D_VTX(1, 1);
    SIMPLE2D_VTX(2, 2);
    SIMPLE2D_VTX(3, 0);
    SIMPLE2D_VTX(4, 2);
    SIMPLE2D_VTX(5, 3);
#undef SIMPLE2D_VTX

    glEnableVertexAttribArray(VS_COLOR);
    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT, GL_FALSE, 0, colour);
    glEnableVertexAttribArray(VS_POSITION);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, 0, vertices);
    glDisableVertexAttribArray(VS_FOG);
    glEnableVertexAttribArray(VS_TEXCOORD0);
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 0, tex);
    glEnableVertexAttribArray(VS_TEXCOORD1);
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, 0, tex2);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    RiceGL_PointVertexArrays();

    if (cullface)
        glEnable(GL_CULL_FACE);
}

void RiceGL_DrawSpriteR(const float color[4])
{
    const TLITVERTEX *v = g_texRectTVtx;
    float w, h;
    GLboolean cullface;
    GLfloat colour[24], tex[12], tex2[12], vertices[24];

    RiceGL_ViewportDisplay();

    cullface = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    w = windowSetting.uDisplayWidth / 2.0f;
    h = windowSetting.uDisplayHeight / 2.0f;

#define SPRITER_VTX(i, s) \
    colour[i*4+0] = color[0]; colour[i*4+1] = color[1]; \
    colour[i*4+2] = color[2]; colour[i*4+3] = color[3]; \
    tex[i*2+0]  = v[s].tcord[0].u; tex[i*2+1]  = v[s].tcord[0].v; \
    tex2[i*2+0] = v[s].tcord[1].u; tex2[i*2+1] = v[s].tcord[1].v; \
    vertices[i*4+0] = -1.0f + v[s].x / w; vertices[i*4+1] = 1.0f - v[s].y / h; \
    vertices[i*4+2] = -v[s].z; vertices[i*4+3] = 1
    /* vertices 0 1 2 and 0 2 3 */
    SPRITER_VTX(0, 0);
    SPRITER_VTX(1, 1);
    SPRITER_VTX(2, 2);
    SPRITER_VTX(3, 0);
    SPRITER_VTX(4, 2);
    SPRITER_VTX(5, 3);
#undef SPRITER_VTX

    glVertexAttribPointer(VS_COLOR, 4, GL_FLOAT, GL_FALSE, 0, colour);
    glVertexAttribPointer(VS_POSITION, 4, GL_FLOAT, GL_FALSE, 0, vertices);
    glVertexAttribPointer(VS_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 0, tex);
    glVertexAttribPointer(VS_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, 0, tex2);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    RiceGL_PointVertexArrays();

    if (cullface)
        glEnable(GL_CULL_FACE);
}
