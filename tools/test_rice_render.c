/* Rice's renderer state (mupen64plus-video-rice/src/RiceRender.c), on a
 * GL side of its own that records what it is asked:
 *
 *   * a new renderer has no depth bias and alpha reference 0xFF;
 *   * the depth bias is kept and given to GL, the alpha reference kept;
 *   * fog is the ROM's, or on whatever the ROM says when it is forced;
 *   * a tile's texture: the entry always kept, the rest only for a new
 *     texture, an enhanced texture at its entry's size; an entry with no
 *     texture leaves the tile with none;
 *   * the matrices reset to the identity, and a display list's start;
 *   * the RE2 scissor, or else the clip-ratio one;
 *   * the simple textured rectangle's corners, a render texture's
 *     coordinates scaled, the frame updated at the first primitive only
 *     when the ROM asks for that and the origin changed. */
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "OGLBlender.h"
#include "RenderBase.h"
#include "RiceCombiner.h"
#include "RiceGLDraw.h"
#include "RiceGLState.h"
#include "RiceGLTex.h"
#include "RiceRender.h"
#include "Video.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what Rice gives the renderer ---- */
ALIGN(16, RSP_Options gRSP);
RenderTexture g_textures[MAX_TEXTURES];
TLITVERTEX g_texRectTVtx[4];
RenderTextureInfo gRenderTextureInfos[20];
PluginStatus status;
WindowSettingStruct windowSetting;
GlobalOptions options;
RomOptions currentRomOptions;
RiceCombiner rice_combiner;

/* ---- a GL side of its own ---- */
static int n_viewport, n_arrays, n_texinit, n_combined, n_clip, n_ratio;
static int n_frame, n_draw, re2;
static int last_bias = -1;

void RiceGL_ViewportDisplay(void) { n_viewport++; }
void RiceGL_InitVertexArrays(void) { n_arrays++; }
void RiceGLTex_Init(void) { n_texinit++; }
void RiceGL_ApplyZBias(int bias) { last_bias = bias; }
void UpdateCombinedMatrix(void) { n_combined++; }
void RiceGL_UpdateClipRectangle(void) { n_clip++; }
void RiceGL_UpdateScissorWithClipRatio(void) { n_ratio++; }
bool RiceGL_RE2Scissor(void) { return re2 != 0; }
void RiceGC_UpdateFrame(void) { n_frame++; }
void RiceGL_DrawSimple2DTexture(void) { n_draw++; }

static CBlender the_blender;
CBlender *OGLBlender_Get(void) { return &the_blender; }

static void test_state(void)
{
   rice_render.zBias = 7;
   rice_render.alpha = 3;
   rice_render.blender = NULL;
   rice_render.filter = FILTER_LINEAR;
   RiceRender_Reset();
   CHECK(rice_render.zBias == 0);
   CHECK(rice_render.alpha == 0xFF);
   CHECK(rice_render.blender == &the_blender);
   CHECK(rice_render.filter == FILTER_POINT);

   RiceRender_SetZBias(2);
   CHECK(rice_render.zBias == 2);
   CHECK(last_bias == 2);
   RiceRender_SetAlphaRef(0x40);
   CHECK(rice_render.alpha == 0x40);

   RiceRender_Initialize();
   CHECK(n_viewport == 1 && n_arrays == 1 && n_texinit == 1);
}

static void test_fog(void)
{
   options.fogMethod = 1;
   RiceRender_SetFogEnable(true);
   CHECK(gRSP.bFogEnabled && rice_combiner.fog);
   RiceRender_SetFogEnable(false);
   CHECK(!gRSP.bFogEnabled && !rice_combiner.fog);
   options.fogMethod = 2;
   RiceRender_SetFogEnable(false);
   CHECK(gRSP.bFogEnabled && rice_combiner.fog);
   options.fogMethod = 0;
}

static void test_textures(void)
{
   CTexture a, b, big;
   TxtrCacheEntry ea, eb;
   int pa, pb;
   RenderTexture *t = &g_textures[1];

   memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b)); memset(&big, 0, sizeof(big));
   memset(&ea, 0, sizeof(ea)); memset(&eb, 0, sizeof(eb));
   a.m_pTexture = &pa;  a.m_dwCreatedTextureWidth = 32;  a.m_dwCreatedTextureHeight = 16;
   b.m_pTexture = &pb;  b.m_dwCreatedTextureWidth = 8;   b.m_dwCreatedTextureHeight = 4;
   big.m_dwCreatedTextureWidth = 128; big.m_dwCreatedTextureHeight = 64;
   ea.pTexture = &a; ea.ti.WidthToCreate = 30; ea.ti.HeightToCreate = 15;

   memset(t, 0, sizeof(*t));
   RiceRender_SetCurrentTexture(1, &a, 30, 15, &ea);
   CHECK(t->pTextureEntry == &ea);
   CHECK(t->m_pCTexture == &a && t->m_lpsTexturePtr == &pa);
   CHECK(t->m_dwTileWidth == 30 && t->m_dwTileHeight == 15);
   CHECK(t->m_fTexWidth == 32.0f && t->m_fTexHeight == 16.0f);

   /* the same texture again: only the entry is taken */
   RiceRender_SetCurrentTexture(1, &a, 5, 6, &eb);
   CHECK(t->pTextureEntry == &eb);
   CHECK(t->m_dwTileWidth == 30 && t->m_dwTileHeight == 15);

   /* no texture: only the entry */
   RiceRender_SetCurrentTexture(1, NULL, 5, 6, NULL);
   CHECK(t->pTextureEntry == NULL);
   CHECK(t->m_pCTexture == &a && t->m_dwTileWidth == 30);

   /* enhanced: drawn at the entry's texture's size */
   b.m_bIsEnhancedTexture = true;
   eb.pTexture = &big;
   RiceRender_SetCurrentTexture(1, &b, 9, 10, &eb);
   CHECK(t->m_pCTexture == &b && t->m_lpsTexturePtr == &pb);
   CHECK(t->m_dwTileWidth == 9 && t->m_dwTileHeight == 10);
   CHECK(t->m_fTexWidth == 128.0f && t->m_fTexHeight == 64.0f);

   /* by entry: its texture at the size to create */
   RiceRender_SetCurrentTextureEntry(1, &ea);
   CHECK(t->pTextureEntry == &ea && t->m_pCTexture == &a);
   CHECK(t->m_dwTileWidth == 30 && t->m_dwTileHeight == 15);
   CHECK(t->m_fTexWidth == 32.0f);

   /* an entry with no texture leaves none recorded */
   eb.pTexture = NULL;
   RiceRender_SetCurrentTextureEntry(1, &eb);
   CHECK(t->pTextureEntry == NULL && t->m_pCTexture == &a);
   RiceRender_SetCurrentTextureEntry(1, &ea);
   CHECK(t->pTextureEntry == &ea);
   RiceRender_SetCurrentTextureEntry(1, NULL);
   CHECK(t->pTextureEntry == NULL);
}

static void test_reset(void)
{
   int i, j, ok = 1;
   int c0 = n_combined, clip0 = n_clip;

   memset(&gRSP.projectionMtxs[0], 0x55, sizeof(gRSP.projectionMtxs[0]));
   memset(&gRSP.modelviewMtxs[0], 0x55, sizeof(gRSP.modelviewMtxs[0]));
   gRSP.projectionMtxTop = 3;
   gRSP.modelViewMtxTop = 4;
   gRSP.bMatrixIsUpdated = false;
   gRSP.bWorldMatrixIsUpdated = false;
   gRSP.numVertices = 9;
   gRSP.maxVertexID = 9;
   gRSP.curTile = 5;
   gRSP.fTexScaleX = gRSP.fTexScaleY = 3.0f;
   rice_render.zBias = 2;

   RiceRender_RenderReset();
   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
      {
         float want = i == j ? 1.0f : 0.0f;
         if (gRSP.projectionMtxs[0].m[i][j] != want || gRSP.modelviewMtxs[0].m[i][j] != want)
            ok = 0;
      }
   CHECK(ok);
   CHECK(gRSP.projectionMtxTop == 0 && gRSP.modelViewMtxTop == 0);
   CHECK(gRSP.bMatrixIsUpdated && gRSP.bWorldMatrixIsUpdated);
   CHECK(n_combined == c0 + 1);
   CHECK(n_clip == clip0 + 1);
   CHECK(rice_render.zBias == 0 && last_bias == 0);
   CHECK(gRSP.numVertices == 0 && gRSP.maxVertexID == 0 && gRSP.curTile == 0);
   CHECK(gRSP.fTexScaleX == 1/32.0f && gRSP.fTexScaleY == 1/32.0f);
}

static void test_scissor(void)
{
   int r0 = n_ratio;
   re2 = 1;
   RiceRender_UpdateScissor();
   CHECK(n_ratio == r0);
   re2 = 0;
   RiceRender_UpdateScissor();
   CHECK(n_ratio == r0 + 1);
}

static void test_rect(void)
{
   TxtrCacheEntry e;
   const float xs[4] = { 10, 30, 30, 10 }, ys[4] = { 20, 20, 60, 60 };
   const float us[4] = { 0.25f, 0.75f, 0.75f, 0.25f }, vs[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
   int i, ok = 1, d0, f0;

   memset(&e, 0, sizeof(e));
   windowSetting.fMultX = 2.0f;
   windowSetting.fMultY = 3.0f;
   g_textures[0].pTextureEntry = NULL;
   status.bVIOriginIsUpdated = false;
   currentRomOptions.screenUpdateSetting = SCREEN_UPDATE_AT_1ST_PRIMITIVE;

   d0 = n_draw; f0 = n_frame;
   RiceRender_DrawSimple2DTexture(10, 20, 30, 60, 0.25f, 0.5f, 0.75f, 1.0f,
         0x11223344, 0x55667788, 0.5f, 0.25f);
   for (i = 0; i < 4; i++)
   {
      const TLITVERTEX *v = &g_texRectTVtx[i];
      if (v->x != xs[i] * 2.0f || v->y != ys[i] * 3.0f || v->z != 0.5f || v->rhw != 0.25f
            || v->dcDiffuse != 0x11223344 || v->dcSpecular != 0x55667788
            || v->tcord[0].u != us[i] || v->tcord[0].v != vs[i])
         ok = 0;
   }
   CHECK(ok);
   CHECK(n_draw == d0 + 1 && n_frame == f0);

   /* a render texture's coordinates by its scale; slot 0 is not one */
   e.txtrBufIdx = 3;
   gRenderTextureInfos[2].scaleX = 2.0f;
   gRenderTextureInfos[2].scaleY = 4.0f;
   g_textures[0].pTextureEntry = &e;
   RiceRender_DrawSimple2DTexture(10, 20, 30, 60, 0.25f, 0.5f, 0.75f, 1.0f, 0, 0, 0, 1);
   ok = 1;
   for (i = 0; i < 4; i++)
      if (g_texRectTVtx[i].tcord[0].u != us[i] * 2.0f || g_texRectTVtx[i].tcord[0].v != vs[i] * 4.0f)
         ok = 0;
   CHECK(ok);
   e.txtrBufIdx = 1;
   gRenderTextureInfos[0].scaleX = 0.5f;
   gRenderTextureInfos[0].scaleY = 0.25f;
   RiceRender_DrawSimple2DTexture(10, 20, 30, 60, 0.25f, 0.5f, 0.75f, 1.0f, 0, 0, 0, 1);
   CHECK(g_texRectTVtx[1].tcord[0].u == 0.375f && g_texRectTVtx[2].tcord[0].v == 0.25f);
   e.txtrBufIdx = 0;
   RiceRender_DrawSimple2DTexture(10, 20, 30, 60, 0.25f, 0.5f, 0.75f, 1.0f, 0, 0, 0, 1);
   CHECK(g_texRectTVtx[1].tcord[0].u == 0.75f && g_texRectTVtx[2].tcord[0].v == 1.0f);

   /* the frame updated at the first primitive after the origin changed */
   status.bVIOriginIsUpdated = true;
   RiceRender_DrawSimple2DTexture(0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1);
   CHECK(n_frame == f0 + 1 && status.bVIOriginIsUpdated == false);
   RiceRender_DrawSimple2DTexture(0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1);
   CHECK(n_frame == f0 + 1);
   status.bVIOriginIsUpdated = true;
   currentRomOptions.screenUpdateSetting = SCREEN_UPDATE_AT_1ST_PRIMITIVE + 1;
   RiceRender_DrawSimple2DTexture(0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1);
   CHECK(n_frame == f0 + 1 && status.bVIOriginIsUpdated == true);
}

int main(void)
{
   test_state();
   test_fog();
   test_textures();
   test_reset();
   test_scissor();
   test_rect();
   if (failures)
   {
      printf("test_rice_render: %d failed\n", failures);
      return 1;
   }
   printf("test_rice_render: ok\n");
   return 0;
}
