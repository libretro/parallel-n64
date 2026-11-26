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

#ifndef _RICE_RENDER_H
#define _RICE_RENDER_H

#include "osal_opengl.h"
#include "RiceGLState.h"
#include "RiceGLTex.h"
#include "RiceGLDraw.h"

#include "Blender.h"
#include "CombinerDefs.h"
#include "RiceCombiner.h"
#include "Config.h"
#include "Debugger.h"
#include "RenderBase.h"
#include "RSP_Parser.h"
#include "RSP_S2DEX.h"

enum TextureChannel 
{
    TXT_RGB,
    TXT_ALPHA,
    TXT_RGBA,
};

class CRender
{
    friend class OGLDeviceBuilder;

protected:
    CRender();

public:

    float m_fScreenViewportMultX;
    float m_fScreenViewportMultY;


    uint32_t  m_dwTexturePerspective;
    bool    m_bAlphaTestEnable;

    bool    m_bZUpdate;
    bool    m_bZCompare;
    uint32_t  m_dwZBias;

    TextureFilter   m_dwMinFilter;
    TextureFilter   m_dwMagFilter;

    uint32_t  m_dwAlpha;

    uint64_t      m_Mux;
    bool    m_bBlendModeValid;

    CBlender *m_pAlphaBlender;
    
    
    ~CRender();
    
    inline bool IsTexel0Enable() { return rice_combiner.tex0Enabled; }
    inline bool IsTexel1Enable() { return rice_combiner.tex1Enabled; }
    inline bool IsTextureEnabled() { return rice_combiner.tex0Enabled || rice_combiner.tex1Enabled; }

    inline RenderTexture& GetCurrentTexture() { return g_textures[gRSP.curTile]; }
    inline RenderTexture& GetTexture(uint32_t dwTile) { return g_textures[dwTile]; }
    void SetViewport(int nLeft, int nTop, int nRight, int nBottom, int maxZ);
    void SetClipRatio(uint32_t type, uint32_t value);
    void UpdateClipRectangle();
    void UpdateScissorWithClipRatio();

    void SetTextureEnableAndScale(int dwTile, bool enable, float fScaleX, float fScaleY);
    

    uint32_t GetFogColor() { return gRDP.fogColor; }

    void SetProjection(const Matrix & mat, bool bPush, bool bReplace);
    void SetWorldView(const Matrix & mat, bool bPush, bool bReplace);
    inline int GetProjectMatrixLevel(void) { return gRSP.projectionMtxTop; }
    inline int GetWorldViewMatrixLevel(void) { return gRSP.modelViewMtxTop; }

    inline void PopProjection()
    {
        if (gRSP.projectionMtxTop > 0)
            gRSP.projectionMtxTop--;
        else
            TRACE0("Popping past projection stack limits");
    }

    void PopWorldView();
    void SetWorldProjectMatrix(Matrix &mtx);
    
    void ResetMatrices();

    inline RenderShadeMode GetShadeMode() { return gRSP.shadeMode; }

    void SetVtxTextureCoord(uint32_t dwV, float tu, float tv)
    {
        g_fVtxTxtCoords[dwV].x = tu;
        g_fVtxTxtCoords[dwV].y = tv;
    }

    void SetCombinerAndBlender();
    void SetMux(uint32_t dwMux0, uint32_t dwMux1);

    void BeginRendering(void) {CRender::gRenderReferenceCount++;}       // For DirectX only

    void ClearBuffer(bool cbuffer, bool zbuffer) { RiceGL_ClearBuffer(cbuffer, zbuffer); }
    void ClearBuffer(bool cbuffer, bool zbuffer, COORDRECT &rect) 
    {
        ClearBuffer(cbuffer, zbuffer);
    }

    void SetTextureFilter(uint32_t dwFilter);
    


    void InitOtherModes(void);

    void SetVertexTextureUVCoord(TLITVERTEX &v, float fTex0S, float fTex0T, float fTex1S, float fTex1T);
    void SetVertexTextureUVCoord(TLITVERTEX &v, float fTex0S, float fTex0T);
    void SetVertexTextureUVCoord(TLITVERTEX &v, const TexCord &fTex0, const TexCord &fTex1);
    void SetVertexTextureUVCoord(TLITVERTEX &v, const TexCord &fTex0);
    
    bool DrawTriangles();

    bool TexRect(int nX0, int nY0, int nX1, int nY1, float fS0, float fT0, float fScaleS, float fScaleT, bool colorFlag, uint32_t difcolor);
    bool TexRectFlip(int nX0, int nY0, int nX1, int nY1, float fS0, float fT0, float fS1, float fT1);
    bool FillRect(int nX0, int nY0, int nX1, int nY1, uint32_t dwColor);
    bool Line3D(uint32_t dwV0, uint32_t dwV1, uint32_t dwWidth);

    void SetTexelRepeatFlags(uint32_t dwTile) { RiceGLTex_SetTileRepeat(dwTile); }
    void SetAllTexelRepeatFlag() { RiceCombiner_SetTexelRepeat(); }
    

    void CleanUp(void);
    

    void LoadSprite2D(Sprite2DInfo &info, uint32_t ucode);
    void LoadObjBGCopy(uObjBg &info);
    void LoadObjBG1CYC(uObjScaleBg &info);
    void LoadObjSprite(uObjTxSprite &info, bool useTIAddr);

    void LoadFrameBuffer(bool useVIreg, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
    void DrawSprite2D(Sprite2DInfo &info, uint32_t ucode);
    void DrawSpriteR(uObjTxSprite &sprite, bool initCombiner, uint32_t tile, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
    void DrawSprite(uObjTxSprite &sprite, bool rectR);
    void DrawObjBGCopy(uObjBg &info);
    void DrawFrameBuffer(bool useVIreg, uint32_t left, uint32_t top, uint32_t width, uint32_t height);
    void DrawObjBG1CYC(uObjScaleBg &bg, bool scaled);

    static CRender * g_pRender;
    static int gRenderReferenceCount;
    static CRender * GetRender(void);


protected:
    bool            m_savedZBufferFlag;
    uint32_t          m_savedMinFilter;
    uint32_t          m_savedMagFilter;

    // FillRect
    VECTOR2         m_fillRectVtx[2];
    
    // Line3D

    LITVERTEX       m_line3DVtx[2];
    VECTOR2         m_line3DVector[4];
    
    // TexRect

    TexCord         m_texRectTex1UV[2];
    TexCord         m_texRectTex2UV[2];

    // DrawSimple2DTexture
    void    StartDrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw);

    // DrawSimpleRect
    void    StartDrawSimpleRect(int nX0, int nY0, int nX1, int nY1, uint32_t dwColor, float depth, float rhw);
    VECTOR2         m_simpleRectVtx[2];

    bool            RemapTextureCoordinate(float s0, float s1, uint32_t tileWidth, uint32_t mask, float textureWidth,
                                            float &u0, float &u1);

    /* the GL renderer (once OGLRender) */
public:
    void Initialize(void);

    bool InitDeviceObjects();
    bool ClearDeviceObjects();

    void ApplyTextureFilter() { RiceGLTex_ApplyFilter(m_dwMinFilter, m_dwMagFilter); }

    void ZBufferEnable(bool bZBuffer) { RiceGL_ZBufferEnable(bZBuffer); }
    void ClearZBuffer(float depth) { RiceGL_ClearZBuffer(depth); }
    void SetZCompare(bool bZCompare) { RiceGL_SetZCompare(bZCompare); }
    void SetZUpdate(bool bZUpdate) { RiceGL_SetZUpdate(bZUpdate); }
    void SetZBias(int bias);
    void ApplyZBias(int bias) { RiceGL_ApplyZBias(bias); }
    void SetAlphaRef(uint32_t dwAlpha);
    void ForceAlphaRef(uint32_t dwAlpha);
    void SetViewportRender() { RiceGL_ViewportN64(); }
    void RenderReset();
    void SetCullMode(bool bCullFront, bool bCullBack) { RiceGL_SetCullMode(bCullFront, bCullBack); }
    void SetAlphaTestEnable(bool bAlphaTestEnable) { rice_combiner.alphaTest = bAlphaTestEnable; }
    void UpdateScissor();
    void ApplyRDPScissor(bool force) { RiceGL_ApplyRDPScissor(force); }
    void ApplyScissorWithClipRatio(bool force) { RiceGL_ApplyScissorWithClipRatio(force); }

    bool SetCurrentTexture(int tile, CTexture *handler,uint32_t dwTileWidth, uint32_t dwTileHeight, TxtrCacheEntry *pTextureEntry);
    bool SetCurrentTexture(int tile, TxtrCacheEntry *pTextureEntry);
    void SetAddressUAllStages(uint32_t dwTile, TextureUVFlag dwFlag) { RiceGLTex_SetUFlag(dwFlag, dwTile); }
    void SetAddressVAllStages(uint32_t dwTile, TextureUVFlag dwFlag) { RiceGLTex_SetVFlag(dwFlag, dwTile); }
    void SetTextureUFlag(TextureUVFlag dwFlag, uint32_t tile) { RiceGLTex_SetUFlag(dwFlag, tile); }
    void SetTextureVFlag(TextureUVFlag dwFlag, uint32_t tile) { RiceGLTex_SetVFlag(dwFlag, tile); }
    void BindTexture(GLuint texture, int unitno) { RiceGLTex_Bind(texture, unitno); }

    void DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw);
    void InitCombinerBlenderForSimpleRectDraw(uint32_t tile);
    void DrawSpriteR_Render() { RiceGL_DrawSpriteR(gRDP.fvPrimitiveColor); }

    void SetFogEnable(bool bEnable);
    void TurnFogOnOff(bool flag) { rice_combiner.fog = flag; }
    void SetFogColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { RiceGL_SetFogColor(r, g, b, a); }

    void DisableMultiTexture() { RiceGLTex_DisableMultiTexture(); }
    void EnableMultiTexture() { RiceGLTex_EnableMultiTexture(); }
    void EndRendering(void);

    void EnableTexUnit(int unitno, bool flag) { RiceGLTex_EnableUnit(unitno, flag); }

protected:

    // Basic render drawing functions
    bool RenderFlushTris()
    {
        /* fog off for the draw when the blender does not take it */
        bool fogOff = !gRDP.bFogEnableInBlender && gRSP.bFogEnabled;
        if (fogOff)
            TurnFogOnOff(false);
        ApplyZBias(m_dwZBias);
        RiceGL_DrawTris();
        if (fogOff)
            TurnFogOnOff(true);
        return true;
    }
    bool RenderTexRect() { RiceGL_DrawTexRect(); return true; }
    bool RenderFillRect(uint32_t dwColor, float depth)
    {
        RiceGL_DrawFillRect(m_fillRectVtx[0].x, m_fillRectVtx[0].y,
              m_fillRectVtx[1].x, m_fillRectVtx[1].y, dwColor, depth);
        return true;
    }
    bool RenderLine3D();

};

#define ffloor(a) (((int(a))<=(a))?(float)(int(a)):((float)(int(a))-1))

#endif  //_RICE_RENDER_H

