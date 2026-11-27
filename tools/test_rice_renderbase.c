/* Rice's vertex pipeline (mupen64plus-video-rice/src/RenderBase.c), on
 * a renderer, combiner and RDRAM of its own, held to what it did as C++:
 *
 *   * the state a renderer starts with;
 *   * the vertices the ucodes load, transformed, lit (by directional and
 *     by Zelda MM's point lights), coloured, fogged, clipped and with
 *     their texture coordinates or texgen, for each hack the loader
 *     knows; Diddy Kong Racing's, Conker's and Rogue Squadron's too;
 *   * the triangles' vertices readied for drawing: the clipper's screen
 *     positions and fog, the key and coverage alpha, one texture or two,
 *     from the cache or a render texture, the texgen correction, the
 *     texture scale hack and the LOD fraction; and which triangles are
 *     culled;
 *   * vertices on the view's edges, and just inside and outside them;
 *   * the small setters: the primitive colour and depth, a vertex's
 *     position, a light's direction, the ambient light, the main tile,
 *     the fog range, the depth hack and the combined matrix with its
 *     mirrors.
 *
 * Each part folds what it leaves behind into a hash; the table below is
 * what the C++ gave (built with -DRICE_RB_GOLDEN against the old file,
 * which prints the hashes instead). */
#include <stdio.h>
#include <string.h>

#ifdef RICE_RB_GOLDEN
#include "Render.h"
#else
#include "Config.h"
#include "RenderBase.h"
#include "RiceCombiner.h"
#include "RiceRender.h"
#endif

#include "ConvertImage.h"
#include "FrameBuffer.h"
#include "../../Graphics/GBI.h"
#include "../../Graphics/RSP/gSP_state.h"
#include "../../Graphics/RDP/gDP_state.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what the pipeline reads and writes outside itself ---- */
struct gSPInfo gSP;
struct gDPInfo gDP;
GFX_INFO gfx_info;
PluginStatus status;
WindowSettingStruct windowSetting;
GlobalOptions options;
GameSetting g_curRomInfo;
RiceCombiner rice_combiner;
SetImgInfo g_TI;
RenderTextureInfo gRenderTextureInfos[4];
FiddledVtx *g_pVtxBase;
uint32_t lastSetTile;
uint32_t dwConkerVtxZAddr;

static int blender_calls;     /* the combiner and blender set again */
static int mux_used;          /* the mux uses the LOD fraction */

#ifdef RICE_RB_GOLDEN
#define TEST_C_LINKAGE extern "C"
#else
#define TEST_C_LINKAGE
#endif

TEST_C_LINKAGE bool DecodedMux_IsUsed(const DecodedMux *mux, uint8_t val, uint8_t mask)
{
   (void)mux; (void)val; (void)mask;
   return mux_used != 0;
}

#ifdef RICE_RB_GOLDEN
/* the renderer of the C++: its texture coordinates as Render.cpp set
   them, the mux in it */
RiceRenderState rice_render;
CRender *CRender::g_pRender = NULL;
CRender::CRender() {}
CRender::~CRender() {}
static CRender test_render;
void CRender::SetCombinerAndBlender() { blender_calls++; }

void SetVertexTextureUVCoord(TexCord &dst, const TexCord &src, int tile, TxtrCacheEntry *pEntry)
{
    RenderTexture &txtr = g_textures[tile];
    RenderTextureInfo &info = gRenderTextureInfos[pEntry->txtrBufIdx-1];
    float s = src.u;
    float t = src.v;

    uint32_t addrOffset = g_TI.dwAddr-info.CI_Info.dwAddr;
    uint32_t extraTop = (addrOffset>>(info.CI_Info.dwSize-1)) /info.CI_Info.dwWidth;
    uint32_t extraLeft = (addrOffset>>(info.CI_Info.dwSize-1))%info.CI_Info.dwWidth;

    if( pEntry->txtrBufIdx > 0  )
    {
        s += (extraLeft+pEntry->ti.LeftToLoad)/txtr.m_fTexWidth;
        t += (extraTop+pEntry->ti.TopToLoad)/txtr.m_fTexHeight;

        s *= info.scaleX;
        t *= info.scaleY;
    }

    dst.u = s;
    dst.v = t;
}

void CRender::SetVertexTextureUVCoord(TLITVERTEX &v, const TexCord &fTex0)
{
    RenderTexture &txtr = g_textures[0];
    if( txtr.pTextureEntry && txtr.pTextureEntry->txtrBufIdx > 0 )
        ::SetVertexTextureUVCoord(v.tcord[0], fTex0, 0, txtr.pTextureEntry);
    else
        v.tcord[0] = fTex0;
}

void CRender::SetVertexTextureUVCoord(TLITVERTEX &v, float fTex0S, float fTex0T)
{
    TexCord t = { fTex0S, fTex0T };
    SetVertexTextureUVCoord(v, t);
}

void CRender::SetVertexTextureUVCoord(TLITVERTEX &v, const TexCord &fTex0_, const TexCord &fTex1_)
{
    TexCord fTex0 = fTex0_;
    TexCord fTex1 = fTex1_;

    if( (options.enableHackForGames == HACK_FOR_ZELDA||options.enableHackForGames == HACK_FOR_ZELDA_MM) && m_Mux == 0x00262a60150c937fLL && gRSP.curTile == 0 )
    {
        gDPTile *t0 = &gDP.tiles[0];
        gDPTile *t1 = &gDP.tiles[1];
        if( t0->format == G_IM_FMT_I && t0->size == G_IM_SIZ_8b && t0->width == 64 &&
            t1->format == G_IM_FMT_I && t1->size == G_IM_SIZ_8b && t1->width == 64 &&
            t0->height == t1->height )
        {
            fTex0.u /= 2;
            fTex0.v /= 2;
            fTex1.u /= 2;
            fTex1.v /= 2;
        }
    }

    RenderTexture &txtr0 = g_textures[0];
    if( txtr0.pTextureEntry && txtr0.pTextureEntry->txtrBufIdx > 0 )
        ::SetVertexTextureUVCoord(v.tcord[0], fTex0, 0, txtr0.pTextureEntry);
    else
        v.tcord[0] = fTex0;

    RenderTexture &txtr1 = g_textures[1];
    if( txtr1.pTextureEntry && txtr1.pTextureEntry->txtrBufIdx > 0 )
        ::SetVertexTextureUVCoord(v.tcord[1], fTex1, 1, txtr1.pTextureEntry);
    else
        v.tcord[1] = fTex1;
}

void CRender::SetVertexTextureUVCoord(TLITVERTEX &v, float fTex0S, float fTex0T, float fTex1S, float fTex1T)
{
    TexCord t0 = { fTex0S, fTex0T };
    TexCord t1 = { fTex1S, fTex1T };
    SetVertexTextureUVCoord(v, t0, t1);
}

XMATRIX reverseXY(-1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1);
XMATRIX reverseY(1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1);
extern bool bHalfTxtScale;
void HackZAll();
void ProcessVertexDataConker(uint32_t dwAddr, uint32_t dwV0, uint32_t dwNum);
void ProcessVertexData_Rogue_Squadron(uint32_t dwXYZAddr, uint32_t dwColorAddr, uint32_t dwXYZCmd, uint32_t dwColorCmd);
#define SET_MUX(m) (test_render.m_Mux = (m))
#define SET_UV2(v, a, b, c, d) CRender::g_pRender->SetVertexTextureUVCoord(v, a, b, c, d)
#define SET_UV1(v, a, b) CRender::g_pRender->SetVertexTextureUVCoord(v, a, b)
#else
RiceRenderState rice_render;
void RiceRender_SetCombinerAndBlender(void) { blender_calls++; }
#define SET_MUX(m) (rice_render.mux = (m))
#define SET_UV2(v, a, b, c, d) RiceRender_SetVertexUV2f(&(v), a, b, c, d)
#define SET_UV1(v, a, b) RiceRender_SetVertexUVf(&(v), a, b)
#endif

/* ---- random numbers and the hash ---- */
static uint32_t seed = 0x2545F491u;
static uint32_t rnd(void)
{
   seed ^= seed << 13;
   seed ^= seed >> 17;
   seed ^= seed << 5;
   return seed;
}
/* a float from -range to range, in 1/256ths */
static float rndf(float range)
{
   return ((float)(int)(rnd() % 65537u) - 32768.0f) / 32768.0f * range;
}

/* random draws for one call's arguments, drawn in order (the order a
   call's arguments are worked out in is the compiler's) */
static uint32_t R[6];
static float F[6];
static void draw(int n, float range)
{
   int i;
   for (i = 0; i < n; i++)
   {
      R[i] = rnd();
      F[i] = rndf(range);
   }
}

static uint32_t hash;
static void fold(const void *p, size_t n)
{
   const uint8_t *b = (const uint8_t *)p;
   size_t i;
   for (i = 0; i < n; i++)
      hash = (hash ^ b[i]) * 16777619u;
}
static void fold32(uint32_t v) { fold(&v, sizeof(v)); }

static uint32_t rdram_words[0x4000];   /* 64 KB, word aligned */

static void fill_rdram(void)
{
   unsigned i;
   for (i = 0; i < sizeof(rdram_words) / 4; i++)
      rdram_words[i] = rnd();
}

/* modest vertices: small positions and texture coordinates, so most of
   them land inside the view */
static void fill_vertices(uint32_t addr, int n)
{
   FiddledVtx *v = (FiddledVtx *)((uint8_t *)rdram_words + addr);
   int i;
   for (i = 0; i < n; i++)
   {
      v[i].x = (short)(rndf(300.0f));
      v[i].y = (short)(rndf(300.0f));
      v[i].z = (short)(rndf(300.0f));
      v[i].tu = (short)(rndf(2048.0f));
      v[i].tv = (short)(rndf(2048.0f));
      if (rnd() % 8 == 0)
      {
         /* a zero normal, the transform's other case */
         v[i].norma.nx = v[i].norma.ny = v[i].norma.nz = 0;
      }
   }
}

static void rnd_matrix(Matrix *m, int perspective)
{
   int r, c;
   for (r = 0; r < 4; r++)
      for (c = 0; c < 4; c++)
         m->m[r][c] = rndf(1.5f);
   if (perspective)
   {
      m->m[3][3] = 400.0f + rndf(200.0f);
      m->m[2][3] = rndf(1.0f);
   }
}

/* a light's colour channel, of 0 to 8 bits */
static float rnd_colour(void)
{
   uint32_t bits = rnd() % 9;
   return (float)(rnd() % (1u << bits));
}

static void rnd_lights(void)
{
   unsigned l;
   gSP.numLights = rnd() % 8;
   for (l = 0; l < 16; l++)
   {
      gRSPlights[l].x = rndf(1.0f);
      gRSPlights[l].y = rndf(1.0f);
      gRSPlights[l].z = rndf(1.0f);
      gRSPlights[l].range = (rnd() % 2) ? 0.0f : 100.0f + rndf(50.0f);
      gRSPlights[l].col = rnd();
      gRSPlights[l].fr = rnd_colour();
      gRSPlights[l].fg = rnd_colour();
      gRSPlights[l].fb = rnd_colour();
   }
   SetAmbientLight(rnd());
}

static const HACK_FOR_GAMES hacks[] = {
   NO_HACK_FOR_GAME, HACK_FOR_ZELDA_MM, HACK_FOR_NASCAR,
   HACK_REVERSE_XY_COOR, HACK_REVERSE_Y_COOR, HACK_FOR_ZELDA
};

static void rnd_modes(void)
{
   gRSP.bLightingEnable = rnd() % 2;
   gRSP.bTextureGen = rnd() % 2;
   gRSP.bFogEnabled = rnd() % 2;
   gRSP.ucode = rnd() % 8;
   gSP.geometryMode = rnd() & (G_SHADE | G_FOG | G_TEXTURE_GEN_LINEAR);
   options.enableHackForGames = hacks[rnd() % (sizeof(hacks) / sizeof(hacks[0]))];
   options.bWinFrameMode = (rnd() % 6) == 0;
   g_curRomInfo.bPrimaryDepthHack = rnd() % 2;
   draw(2, 1.0f);
   gRDP.otherMode._u64 = ((uint64_t)R[0] << 32) | R[1];
   gRDP.fPrimitiveDepth = rndf(1.0f);
   gRDP.primitiveColor = rnd();
   draw(2, 1000.0f);
   SetFogMinMax(F[0] + 500.0f, F[1] + 600.0f, 0.0f, 0.0f);
}

static void rnd_stacks(void)
{
   gRSP.modelViewMtxTop = rnd() % RICE_MATRIX_STACK;
   gRSP.projectionMtxTop = rnd() % RICE_MATRIX_STACK;
   rnd_matrix(&gRSP.modelviewMtxs[gRSP.modelViewMtxTop], 0);
   rnd_matrix(&gRSP.projectionMtxs[gRSP.projectionMtxTop], 1);
   rnd_matrix(&gRSPmodelViewTop, 0);
   if (rnd() % 3 == 0)
   {
      /* vertices near the point lights, inside their reach */
      int r, c;
      for (r = 0; r < 4; r++)
         for (c = 0; c < 4; c++)
            gRSPmodelViewTop.m[r][c] *= 0.01f;
   }
   memset(g_clipFlag, 0x77, sizeof(g_clipFlag));
   gRSP.bMatrixIsUpdated = rnd() % 4 != 0;
   gRSP.bCombinedMatrixIsUpdated = rnd() % 2;
}

static void fold_vertices(void)
{
   fold(g_vtxNonTransformed, sizeof(g_vtxNonTransformed));
   fold(g_vtxTransformed, sizeof(g_vtxTransformed));
   fold(g_vecProjected, sizeof(g_vecProjected));
   fold(g_dwVtxDifColor, sizeof(g_dwVtxDifColor));
   fold(g_fVtxTxtCoords, sizeof(g_fVtxTxtCoords));
   fold(g_fFogCoord, sizeof(g_fFogCoord));
   fold(g_clipFlag, sizeof(g_clipFlag));
   fold(g_clipFlag2, sizeof(g_clipFlag2));
   fold(&g_normal, sizeof(g_normal));
   fold(&gRSPworldProject, sizeof(gRSPworldProject));
   fold32(gRSP.bMatrixIsUpdated);
   fold32(gRSP.bCombinedMatrixIsUpdated);
   fold32(status.SPCycleCount);
   fold32((uint32_t)((uint8_t *)g_pVtxBase - (uint8_t *)rdram_words));
}

/* ---- the parts ---- */
static void reset_state(void)
{
   memset(&gRSP, 0x5A, sizeof(gRSP));
   memset(&gRDP, 0xA5, sizeof(gRDP));
   memset(&windowSetting, 0x3C, sizeof(windowSetting));
   memset(&gSP, 0, sizeof(gSP));
   memset(&gDP, 0, sizeof(gDP));
   memset(&status, 0, sizeof(status));
   memset(&options, 0, sizeof(options));
   memset(&g_curRomInfo, 0, sizeof(g_curRomInfo));
   InitRenderBase();
}

static uint32_t run_init(void)
{
   hash = 2166136261u;
   reset_state();
   /* what InitRenderBase clears, dirtied */
   memset(&gSP, 0x33, sizeof(gSP));
   memset(&gDP, 0x44, sizeof(gDP));
   memset(gRSPn64lights, 0x55, sizeof(gRSPn64lights));
   memset(g_clipFlag, 0x66, sizeof(g_clipFlag));
   gRSPfFogMin = gRSPfFogMax = 3.0f;
   memset(&gRSP, 0x5A, sizeof(gRSP));
   InitRenderBase();
   fold(&gRSP, sizeof(gRSP));
   fold(&gRDP, sizeof(gRDP));
   fold(&windowSetting, sizeof(windowSetting));
   fold(&gSP, sizeof(gSP));
   fold(&gDP, sizeof(gDP));
   fold(g_clipFlag, sizeof(g_clipFlag));
   fold(g_vtxNonTransformed, sizeof(g_vtxNonTransformed));
   fold(gRSPn64lights, sizeof(gRSPn64lights));
   fold(&gRSPfFogMin, sizeof(gRSPfFogMin));
   fold(&gRSPfFogMax, sizeof(gRSPfFogMax));
   CHECK(gRSP.ucode == 1 && gRSP.bCullBack && !gRSP.bCullFront);
   CHECK(gRDP.fillColor == 0xFFFFFFFFu && windowSetting.clipping.width == 640);
   return hash;
}

static uint32_t run_vertices(void)
{
   int round;
   hash = 2166136261u;
   reset_state();
   for (round = 0; round < 400; round++)
   {
      uint32_t v0 = rnd() % 40, n = 1 + rnd() % 40;
      uint32_t addr = (rnd() % 0x300) * 16;
      fill_rdram();
      fill_vertices(addr, (int)n);
      rnd_modes();
      rnd_lights();
      rnd_stacks();
      ProcessVertexData(addr, v0, n);
      fold_vertices();
   }
   return hash;
}

static uint32_t run_special(void)
{
   int round;
   hash = 2166136261u;
   reset_state();
   for (round = 0; round < 900; round++)
   {
      uint32_t v0 = rnd() % 40, n = 1 + rnd() % 40;
      uint32_t addr = (rnd() % 0x300) * 16;
      int k;
      fill_rdram();
      fill_vertices(addr, (int)n);
      rnd_modes();
      rnd_lights();
      rnd_stacks();
      switch (round % 3)
      {
         case 0:
            /* DKR: its own matrices, the billboard's base vertex */
            for (k = 0; k < 4; k++)
               rnd_matrix(&gRSP.DKRMatrixes[k], k & 1);
            gRSP.DKRCMatrixIndex = rnd() % 4;
            gRSP.DKRBillBoard = rnd() % 2;
            gRSP.DKRVtxCount = (rnd() % 3) ? 0 : 1 + rnd() % 4;
            if (rnd() % 3 == 0)
               n = 1 + rnd() % 2;
            ProcessVertexDataDKR(addr, v0, n);
            fold(&gRSP.DKRBaseVec, sizeof(gRSP.DKRBaseVec));
            fold32((uint32_t)gRSP.DKRVtxCount);
            break;
         case 1:
            /* Conker: the texgen normals elsewhere */
            dwConkerVtxZAddr = (rnd() % 0x100) * 16;
            ProcessVertexDataConker(addr, v0, n);
            break;
         default:
            /* Rogue Squadron: positions and colours apart */
            draw(3, 1.0f);
            ProcessVertexData_Rogue_Squadron(addr, (R[0] % 0x300) * 16,
                  (R[1] & 0xFFFF00FFu) | ((n << 10) & 0xFF00), R[2]);
            break;
      }
      fold_vertices();
   }
   return hash;
}

static TxtrCacheEntry entries[2];

static void rnd_textures(void)
{
   int t;
   for (t = 0; t < MAX_TEXTURES; t++)
   {
      g_textures[t].m_fTexWidth = (float)(8 << (rnd() % 5));
      g_textures[t].m_fTexHeight = (float)(8 << (rnd() % 5));
      g_textures[t].pTextureEntry = NULL;
   }
   for (t = 0; t < 2; t++)
   {
      if (rnd() % 3 == 0)
      {
         entries[t].txtrBufIdx = (rnd() % 2) ? 1 + rnd() % 4 : 0;
         entries[t].ti.LeftToLoad = rnd() % 64;
         entries[t].ti.TopToLoad = rnd() % 64;
         g_textures[t].pTextureEntry = &entries[t];
      }
   }
   for (t = 0; t < 4; t++)
   {
      gRenderTextureInfos[t].CI_Info.dwAddr = rnd() % 0x10000;
      gRenderTextureInfos[t].CI_Info.dwSize = 1 + rnd() % 3;
      gRenderTextureInfos[t].CI_Info.dwWidth = 1 + rnd() % 320;
      gRenderTextureInfos[t].scaleX = 0.5f + (float)(rnd() % 4) * 0.5f;
      gRenderTextureInfos[t].scaleY = 0.5f + (float)(rnd() % 4) * 0.5f;
   }
   draw(2, 1.0f);
   g_TI.dwAddr = gRenderTextureInfos[R[0] % 4].CI_Info.dwAddr + R[1] % 0x2000;
   for (t = 0; t < 8; t++)
   {
      gRDP.tilesinfo[t].fShiftScaleS = (float)(1 << (rnd() % 3)) / 2.0f;
      gRDP.tilesinfo[t].fShiftScaleT = (float)(1 << (rnd() % 3)) / 2.0f;
      gRDP.tilesinfo[t].fhilite_sl = (float)(rnd() % 32);
      gRDP.tilesinfo[t].fhilite_tl = (float)(rnd() % 32);
      gDP.tiles[t].format = (rnd() % 2) ? G_IM_FMT_I : G_IM_FMT_RGBA;
      gDP.tiles[t].size = rnd() % 4;
      gDP.tiles[t].width = (rnd() % 2) ? 64 : 32;
      gDP.tiles[t].height = 32;
      gDP.tiles[t].uls = rnd() % 64;
      gDP.tiles[t].ult = rnd() % 64;
      gDP.tiles[t].lrs = rnd() % 64;
      gDP.tiles[t].lrt = rnd() % 64;
   }
   gRSP.curTile = rnd() % 8;
   gRSP.fTexScaleX = (float)(1 + rnd() % 64) / 64.0f;
   gRSP.fTexScaleY = (float)(1 + rnd() % 64) / 64.0f;
   rice_combiner.tex0Enabled = rnd() % 2;
   rice_combiner.tex1Enabled = rnd() % 2;
   SET_MUX((rnd() % 2) ? 0x00262a60150c937fULL : 0);
   lastSetTile = rnd() % 8;
}

static void fold_triangles(void)
{
   fold(g_vtxBuffer, sizeof(TLITVERTEX) * 120);
   fold(g_vtxProjected5, sizeof(float) * 5 * 120);
   fold(g_oglVtxColors, 4 * 120);
   fold(g_vtxIndex, sizeof(unsigned short) * 120);
   fold32(gRSP.numVertices);
   fold32(gRDP.LODFrac);
   fold32(status.dwNumTrisRendered);
   fold32(status.dwNumTrisClipped);
   fold32(status.SPCycleCount);
   fold32(bHalfTxtScale);
   fold32((uint32_t)blender_calls);
   fold(&gRSP.tex0scaleX, sizeof(float) * 10);
}

static uint32_t run_triangles(void)
{
   int round;
   hash = 2166136261u;
   reset_state();
   for (round = 0; round < 300; round++)
   {
      uint32_t n = 3 + rnd() % 37;
      uint32_t addr = (rnd() % 0x300) * 16;
      int tri;
      fill_rdram();
      fill_vertices(addr, (int)n);
      rnd_modes();
      rnd_lights();
      rnd_stacks();
      ProcessVertexData(addr, 0, n);

      rnd_textures();
      InitVertexTextureConstants();
      options.bOGLVertexClipper = rnd() % 2;
      gRSP.vtxXMul = rndf(400.0f);
      gRSP.vtxXAdd = rndf(400.0f);
      gRSP.vtxYMul = rndf(300.0f);
      gRSP.vtxYAdd = rndf(300.0f);
      gRDP.keyA = rnd() % 256;
      g_curRomInfo.bTextureScaleHack = rnd() % 2;
      g_curRomInfo.bEnableTxtLOD = rnd() % 2;
      bHalfTxtScale = (rnd() % 4) == 0;
      mux_used = rnd() % 2;
      windowSetting.vpWidthW = 320 + rnd() % 640;
      windowSetting.vpHeightW = 240 + rnd() % 480;
      windowSetting.fMultX = 1.0f + (float)(rnd() % 4);
      windowSetting.fMultY = 1.0f + (float)(rnd() % 4);
      gRSP.bCullFront = rnd() % 2;
      gRSP.bCullBack = rnd() % 2;
      if (rnd() % 3 == 0)
      {
         /* texture coordinates at the last tile's width or height, the
            texture scale hack's cue */
         const struct gDPTile *t = &gDP.tiles[lastSetTile];
         int w = ((t->uls - t->lrs + 1) << 1), h = ((t->ult - t->lrt + 1) << 1);
         uint32_t i, by_height = rnd() % 2;
         gRSP.fTexScaleX = 0.5f;
         gRSP.fTexScaleY = 0.25f;
         for (i = 0; i < n; i++)
         {
            if (rnd() % 4 != 0)
               continue;
            /* the width's or the height's alone, for the whole round */
            if (by_height)
               g_fVtxTxtCoords[i].y = (float)h / gRSP.fTexScaleY;
            else
               g_fVtxTxtCoords[i].x = (float)w / gRSP.fTexScaleX;
         }
      }
      if (rnd() % 2)
      {
         /* the LOD fraction worked out: the first triangle of two
            textures with a mux that takes it, its fog coordinates (what
            the positions are divided by) not zero */
         uint32_t i;
         for (i = 0; i < n; i++)
            g_fFogCoord[i] = 1.0f + (float)(rnd() % 4);
         gRDP.otherMode.text_lod = 1;
         g_curRomInfo.bEnableTxtLOD = true;
         rice_combiner.tex1Enabled = true;
         mux_used = 1;
      }
      gRSP.numVertices = 0;
      for (tri = 0; tri < 12; tri++)
      {
         uint32_t a = rnd() % n, b = rnd() % n, c = rnd() % n;
         fold32(IsTriangleVisible(a, b, c));
         if (gRSP.numVertices + 3 <= 120)
            CHECK(PrepareTriangle(a, b, c));
      }
      fold_triangles();
   }
   return hash;
}

/* vertices projected onto the view's edges exactly, and just past them */
static uint32_t run_edges(void)
{
   int round;
   hash = 2166136261u;
   reset_state();
   for (round = 0; round < 50; round++)
   {
      FiddledVtx *v = (FiddledVtx *)rdram_words;
      int i;
      fill_rdram();
      for (i = 0; i < 32; i++)
      {
         static const short edge[] = { -257, -256, -255, 0, 255, 256, 257 };
         v[i].x = edge[rnd() % 7];
         v[i].y = edge[rnd() % 7];
         v[i].z = edge[rnd() % 7];
      }
      rnd_modes();
      rnd_lights();
      options.enableHackForGames = NO_HACK_FOR_GAME;
      memset(&gRSPworldProject, 0, sizeof(gRSPworldProject));
      gRSPworldProject._11 = gRSPworldProject._22 = gRSPworldProject._33 = 1.0f / 256.0f;
      gRSPworldProject._44 = (rnd() % 4) ? 1.0f : -1.0f;
      gRSP.bMatrixIsUpdated = false;
      gRSP.bCombinedMatrixIsUpdated = false;
      ProcessVertexData(0, 0, 32);
      fold_vertices();
      gRSP.bCullFront = rnd() % 2;
      gRSP.bCullBack = rnd() % 2;
      for (i = 0; i < 16; i++)
      {
         draw(3, 1.0f);
         fold32(IsTriangleVisible(R[0] % 32, R[1] % 32, R[2] % 32));
      }
   }
   return hash;
}

static uint32_t run_setters(void)
{
   int round;
   hash = 2166136261u;
   reset_state();
   for (round = 0; round < 500; round++)
   {
      uint32_t v = rnd() % MAX_VERTS;
      TLITVERTEX tv;
      memset(&tv, 0, sizeof(tv));

      draw(3, 1.0f);
      SetPrimitiveColor(R[0], R[1] % 256, R[2] % 256);
      fold32(gRDP.primitiveColor);
      fold32(gRDP.primLODMin);
      fold32(gRDP.primLODFrac);
      fold(gRDP.fvPrimitiveColor, sizeof(gRDP.fvPrimitiveColor));
      fold32(gRDP.colorsAreReloaded);

      draw(2, 1.0f);
      SetPrimitiveDepth(R[0], R[1]);
      fold32(gRDP.primitiveDepth);
      fold(&gRDP.fPrimitiveDepth, sizeof(float));

      g_vtxTransformed[v].w = rndf(4.0f);
      draw(3, 2.0f);
      SetVertexXYZ(v, F[0], F[1], F[2]);
      fold(&g_vecProjected[v], sizeof(XVECTOR4));
      fold(&g_vtxTransformed[v], sizeof(XVECTOR4));

      draw(5, 100.0f);
      SetLightDirection(R[0] % 16, F[1], F[2], F[3], (R[4] % 2) ? 0.0f : F[4] / 2.0f);
      fold(gRSPlights, sizeof(gRSPlights));

      SetAmbientLight(rnd());
      fold(&gRSP.fAmbientLightR, sizeof(float) * 3);
      fold32(gRSP.ambientLightColor);

      rice_combiner.tex0Enabled = rnd() % 2;
      rice_combiner.tex1Enabled = rnd() % 2;
      ForceMainTextureIndex(rnd() % 8);
      fold32(gRSP.curTile);

      draw(4, 2000.0f);
      SetFogMinMax(F[0], F[1], F[2] / 2000.0f, F[3] / 2000.0f);
      fold(&gRSPfFogMin, sizeof(float));
      fold(&gRSPfFogMax, sizeof(float));
      fold(&gRSPfFogDivider, sizeof(float));

      g_normal.x = rndf(1.0f);
      g_normal.y = rndf(1.0f);
      gSP.geometryMode = rnd() & G_TEXTURE_GEN_LINEAR;
      {
         float s, t;
         TexGen(&s, &t);
         fold(&s, sizeof(s));
         fold(&t, sizeof(t));
      }

      gRSP.numVertices = rnd() % 60;
      {
         uint32_t i;
         for (i = 0; i < gRSP.numVertices; i++)
         {
            g_vtxProjected5[i][2] = rndf(2.0f);
            g_vtxProjected5[i][3] = 0.5f + (float)(rnd() % 8);
         }
      }
      HackZAll();
      fold(g_vtxProjected5, sizeof(float) * 5 * 60);

      options.enableHackForGames = hacks[rnd() % (sizeof(hacks) / sizeof(hacks[0]))];
      rnd_stacks();
      UpdateCombinedMatrix();
      fold(&gRSPworldProject, sizeof(gRSPworldProject));

      rnd_textures();
      options.enableHackForGames = (rnd() % 2) ? HACK_FOR_ZELDA : HACK_FOR_ZELDA_MM;
      gRSP.curTile = rnd() % 2;
      gDP.tiles[0].size = gDP.tiles[1].size = G_IM_SIZ_8b;
      draw(4, 2.0f);
      SET_UV2(tv, F[0], F[1], F[2], F[3]);
      fold(tv.tcord, sizeof(tv.tcord));
      draw(2, 2.0f);
      SET_UV1(tv, F[0], F[1]);
      fold(tv.tcord, sizeof(tv.tcord));

      fold32(RiceRDP_TextTlut());
   }
   fold(&reverseXY, sizeof(reverseXY));
   fold(&reverseY, sizeof(reverseY));
   return hash;
}

int main(void)
{
   static uint32_t (*const parts[])(void) = {
      run_init, run_vertices, run_special, run_triangles, run_edges, run_setters
   };
   static const uint32_t golden[] = {
      0xa0c9a6c6u, 0xc9efc4a0u, 0x93306c48u, 0xa0391b5au, 0x26635b2fu,
      0x0507f023u
   };
   unsigned i;

   gfx_info.RDRAM = (unsigned char *)rdram_words;
#ifdef RICE_RB_GOLDEN
   CRender::g_pRender = &test_render;
#endif
   for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
   {
      uint32_t h = parts[i]();
#ifdef RICE_RB_GOLDEN
      printf("0x%08xu,\n", h);
      (void)golden;
#else
      if (h != golden[i])
      {
         printf("FAILED part %u: hash 0x%08x, the C++ gave 0x%08x\n", i, h, golden[i]);
         failures++;
      }
#endif
   }
   if (failures)
   {
      printf("test_rice_renderbase: %d failed\n", failures);
      return 1;
   }
   printf("test_rice_renderbase: ok\n");
   return 0;
}
