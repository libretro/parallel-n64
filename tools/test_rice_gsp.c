/* Rice's RSP commands that Graphics/ dispatches
 * (mupen64plus-video-rice/src/gSP_rice.c), on a vertex pipeline of its own
 * here recording what it is asked:
 *
 *   * ModifyVertex: a vertex's colour, texture coordinates (by the texture
 *     scale), screen x and y (Tarzan's with hacks and a VI x scale) and z;
 *   * CIVertex (Perfect Dark): vertices through the world-projection
 *     matrix, projected, their fog, clipped; their colour from the colour
 *     index table, lit by the normal through the model view (normalized),
 *     the vertex's alpha kept, or the primitive's with shading off, or the
 *     table's; the fog factor; texture coordinates the vertex's or
 *     generated;
 *   * a light's colour, the number of lights;
 *   * DMA triangles: within RDRAM, textures and the combiner set up once,
 *     each triangle's texture coordinates, drawn if any;
 *   * a light: past 16 nothing; Majora's Mask's 16-bit direction and range;
 *     the ambient light or a light's colour and direction;
 *   * the viewport from scale and translation, within RDRAM. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "RenderBase.h"
#include "Video.h"
#include "../../Graphics/RSP/gSP_funcs_prot.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what Rice gives it ---- */
#define RAM 0x10000
static uint8_t ram[RAM];
static unsigned int vi_x_scale;
ALIGN(16, RDP_Options gRDP);
ALIGN(16, RSP_Options gRSP);
struct gDPInfo gDP;
struct gSPInfo gSP;
GFX_INFO gfx_info;
uint32_t g_dwRamSize = RAM;
PluginStatus status;
GlobalOptions options;
WindowSettingStruct windowSetting;
ALIGN(16, XVECTOR4 g_vtxTransformed[MAX_VERTS]);
ALIGN(16, XVECTOR4 g_vecProjected[MAX_VERTS]);
VECTOR2 g_fVtxTxtCoords[MAX_VERTS];
uint32_t g_dwVtxDifColor[MAX_VERTS];
float g_fFogCoord[MAX_VERTS];
Light gRSPlights[16];
N64Light gRSPn64lights[16];
ALIGN(16, Matrix gRSPworldProject);
ALIGN(16, Matrix gRSPmodelViewTop);
float gRSPfFogMin;
ALIGN(16, XVECTOR4 g_normal);
ALIGN(16, XVECTOR4 g_vtxNonTransformed[MAX_VERTS]);
FiddledVtx *g_pVtxBase;
uint32_t dwPDCIAddr;

/* ---- a vertex pipeline of its own ---- */
static int combined, clipped[MAX_VERTS], fogged[MAX_VERTS], prepared, constants, blended, drawn, ambients;
static uint32_t lit, ambient, tris[16][3];
static float xyz[4], dir[5];
static int viewport[5];
static XVECTOR4 litNormal;

void UpdateCombinedMatrix(void) { combined++; }
void RSP_Vtx_Clipping(int i) { clipped[i]++; }
uint32_t LightVert(XVECTOR4 *norm, int vidx) { (void)vidx; litNormal = *norm; return lit; }
void ReplaceAlphaWithFogFactor(int i) { fogged[i]++; }
void TexGen(float *s, float *t) { *s = g_normal.x + 100.0f; *t = g_normal.y + 200.0f; }
void InitVertexTextureConstants(void) { constants++; }
bool PrepareTriangle(uint32_t v0, uint32_t v1, uint32_t v2)
{ if (prepared < 16) { tris[prepared][0] = v0; tris[prepared][1] = v1; tris[prepared][2] = v2; } prepared++; return true; }
void SetVertexXYZ(uint32_t v, float x, float y, float z) { xyz[0] = (float)v; xyz[1] = x; xyz[2] = y; xyz[3] = z; }
void SetLightDirection(uint32_t l, float x, float y, float z, float range)
{ dir[0] = (float)l; dir[1] = x; dir[2] = y; dir[3] = z; dir[4] = range; }
void SetAmbientLight(uint32_t color) { ambient = color; ambients++; }
static int textures;
void PrepareTextures(void) { textures++; }
void RiceRender_SetCombinerAndBlender(void) { blended++; }
bool RiceRender_DrawTriangles(void) { drawn++; return true; }
void RiceRender_SetViewport(int l, int t, int r, int b, int z)
{ viewport[0] = l; viewport[1] = t; viewport[2] = r; viewport[3] = b; viewport[4] = z; }

static void w32(uint32_t a, uint32_t v) { *(uint32_t *)&ram[a] = v; }
static void w16(uint32_t a, uint16_t v) { *(uint16_t *)&ram[a ^ 2] = v; }   /* the N64's halfword at a */

static void modify_vertex(void)
{
   g_fVtxTxtCoords[3].x = g_fVtxTxtCoords[3].y = 9.0f;
   xyz[0] = -1.0f;
   ricegSPModifyVertex(3, G_MWO_POINT_RGBA, 0x11223344);
   CHECK(g_dwVtxDifColor[3] == COLOR_RGBA(0x11, 0x22, 0x33, 0x44));
   CHECK(g_fVtxTxtCoords[3].x == 9.0f && g_fVtxTxtCoords[3].y == 9.0f);
   gRSP.fTexScaleX = 2.0f;
   gRSP.fTexScaleY = 0.5f;
   ricegSPModifyVertex(4, G_MWO_POINT_ST, (uint32_t)(-64 & 0xFFFF) << 16 | 96);
   CHECK(g_fVtxTxtCoords[4].x == -1.0f && g_fVtxTxtCoords[4].y == 6.0f);
   CHECK(xyz[0] == -1.0f);                   /* nor its position */
   windowSetting.uViWidth = 320;
   windowSetting.uViHeight = 240;
   windowSetting.fViWidth = 320.0f;
   windowSetting.fViHeight = 240.0f;
   g_vecProjected[5].x = 7.0f;
   g_vecProjected[5].y = 8.0f;
   g_vecProjected[5].z = 0.25f;
   options.bEnableHacks = false;
   vi_x_scale = 0x200;
   ricegSPModifyVertex(5, G_MWO_POINT_XYSCREEN, (uint32_t)(400 << 16) | (uint32_t)(-40 & 0xFFFF));
   /* x 100 - 160, y 120 - -10: twice of the VI's size */
   CHECK(xyz[0] == 5.0f && xyz[1] == -60.0f * 2 / 320.0f && xyz[2] == 130.0f * 2 / 240.0f && xyz[3] == 0.25f);
   options.bEnableHacks = true;
   ricegSPModifyVertex(5, G_MWO_POINT_XYSCREEN, (uint32_t)(400 << 16) | (uint32_t)(-40 & 0xFFFF));
   CHECK(xyz[1] == -60.0f * 2 / 320.0f);     /* no VI x scale: as others */
   vi_x_scale = 0x201;
   ricegSPModifyVertex(5, G_MWO_POINT_XYSCREEN, (uint32_t)(400 << 16) | (uint32_t)(-40 & 0xFFFF));
   CHECK(xyz[1] == -60.0f / 320.0f && xyz[2] == 130.0f / 240.0f);   /* Tarzan's */
   options.bEnableHacks = false;
   ricegSPModifyVertex(5, G_MWO_POINT_ZSCREEN, 0x03FF << 16);
   CHECK(xyz[1] == 7.0f && xyz[2] == 8.0f && xyz[3] == 0.75f);
   ricegSPModifyVertex(5, G_MWO_POINT_ZSCREEN, 0);
   CHECK(xyz[3] == 0.25f);
   xyz[0] = -1.0f;
   ricegSPModifyVertex(5, 0x7F, 0);
   CHECK(xyz[0] == -1.0f);
}

static void set_matrix(Matrix *m, float scale)
{
   memset(m, 0, sizeof(*m));
   m->_11 = scale; m->_22 = scale * 2; m->_33 = scale * 4; m->_44 = 1.0f;
   m->_41 = 1.0f; m->_42 = -2.0f; m->_43 = 3.0f; m->_14 = 0.5f;
}

/* a Perfect Dark vertex at a: y, x, cidx, z, t, s (the N64's halfwords, swapped in pairs) */
static void pd_vertex(uint32_t a, short x, short y, short z, uint16_t cidx, short s, short t)
{
   N64VtxPD *v = (N64VtxPD *)&ram[a];
   v->x = x; v->y = y; v->z = z; v->cidx = cidx; v->s = s; v->t = t;
}

static void ci_vertex(void)
{
   uint32_t i;

   memset(clipped, 0, sizeof(clipped));
   memset(fogged, 0, sizeof(fogged));
   combined = 0;
   set_matrix(&gRSPworldProject, 1.0f);
   memset(&gRSPmodelViewTop, 0, sizeof(gRSPmodelViewTop));
   gRSPmodelViewTop._11 = 2.0f; gRSPmodelViewTop._12 = 1.0f; gRSPmodelViewTop._13 = 3.0f;
   gRSPmodelViewTop._21 = 5.0f; gRSPmodelViewTop._22 = 2.0f; gRSPmodelViewTop._23 = 7.0f;
   gRSPmodelViewTop._31 = 11.0f; gRSPmodelViewTop._32 = 13.0f; gRSPmodelViewTop._33 = 2.0f;
   gRSPfFogMin = 0.125f;
   dwPDCIAddr = 0x3000;
   ram[0x3000 + 0x10 + 0] = 0x80;          /* a */
   ram[0x3000 + 0x10 + 1] = 0x30;          /* b */
   ram[0x3000 + 0x10 + 2] = 0xF0;          /* g: -16 as a normal */
   ram[0x3000 + 0x10 + 3] = 0x40;          /* r */
   ram[0x3000 + 0x20 + 0] = 0x11;
   ram[0x3000 + 0x20 + 1] = 0x22;
   ram[0x3000 + 0x20 + 2] = 0x33;
   ram[0x3000 + 0x20 + 3] = 0x44;
   pd_vertex(0x1000, 2, 3, 4, 0x110, 5, 6);   /* cidx: its low byte */
   pd_vertex(0x100C, 2, 0, -8, 0x20, -5, 7);
   gRSP.bLightingEnable = false;
   gRSP.bTextureGen = false;
   gRSP.ucode = 5;
   gSP.geometryMode = 0;
   gRDP.primitiveColor = 0xDEADBEEF;
   options.bWinFrameMode = false;
   g_pVtxBase = NULL;

   ricegSPCIVertex(0x1000, 2, 10);
   CHECK(combined == 1 && g_pVtxBase == (FiddledVtx *)&ram[0x1000]);
   CHECK(g_vtxNonTransformed[10].x == 2.0f && g_vtxNonTransformed[10].y == 3.0f && g_vtxNonTransformed[10].z == 4.0f);
   /* (2, 3, 4, 1): x 2 + 1, y 6 - 2, z 16 + 3, w 1 + 1 */
   CHECK(g_vtxTransformed[10].x == 3.0f && g_vtxTransformed[10].y == 4.0f && g_vtxTransformed[10].z == 19.0f && g_vtxTransformed[10].w == 2.0f);
   CHECK(g_vecProjected[10].w == 0.5f && g_vecProjected[10].x == 1.5f && g_vecProjected[10].y == 2.0f && g_vecProjected[10].z == 9.5f);
   CHECK(g_fFogCoord[10] == 9.5f);
   /* (2, 0, -8, 1): z -32 + 3, w 2: z below 0 alone */
   CHECK(g_vecProjected[11].w > 0 && g_vecProjected[11].z < 0 && g_fFogCoord[11] == 0.125f);
   CHECK(clipped[10] == 1 && clipped[11] == 1 && clipped[9] == 0 && clipped[12] == 0);
   CHECK(fogged[10] == 1 && fogged[11] == 1);
   /* unlit, shaded: the table's */
   CHECK(g_dwVtxDifColor[10] == COLOR_RGBA(0x40, 0xF0, 0x30, 0x80));
   CHECK(g_dwVtxDifColor[11] == COLOR_RGBA(0x44, 0x33, 0x22, 0x11));
   CHECK(g_fVtxTxtCoords[10].x == 5.0f && g_fVtxTxtCoords[10].y == 6.0f);
   CHECK(g_fVtxTxtCoords[11].x == -5.0f && g_fVtxTxtCoords[11].y == 7.0f);
   /* shade off before ucode 5: the primitive's */
   gRSP.ucode = 4;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_dwVtxDifColor[10] == 0xDEADBEEF);
   gSP.geometryMode = G_SHADE;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_dwVtxDifColor[10] == COLOR_RGBA(0x40, 0xF0, 0x30, 0x80));
   gSP.geometryMode = 0;
   gRSP.ucode = 5;
   /* a fog coordinate below the least, and a w below 0 */
   gRSPfFogMin = 20.0f;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_fFogCoord[10] == 20.0f);
   gRSPfFogMin = 0.125f;
   gRSPworldProject._44 = -3.0f;            /* w -2, z -14: w below 0 alone */
   gRSPworldProject._43 = -30.0f;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_vecProjected[10].w < 0 && g_vecProjected[10].z > 0 && g_fFogCoord[10] == 0.125f);
   /* a z between 0 and 1 kept; the least below it */
   set_matrix(&gRSPworldProject, 1.0f);
   gRSPworldProject._43 = -15.0f;           /* z 1, w 2 */
   gRSPfFogMin = -1.0f;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_vecProjected[10].z == 0.5f && g_fFogCoord[10] == 0.5f);
   gRSPworldProject._43 = -16.0f;           /* z 0 */
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_vecProjected[10].z == 0.0f && g_fFogCoord[10] == 0.0f);
   gRSPfFogMin = 0.125f;
   set_matrix(&gRSPworldProject, 1.0f);

   /* lit: the normal (r, g, b as signed) through the model view, normalized */
   gRSP.bLightingEnable = true;
   lit = 0x00ABCDEF;
   ricegSPCIVertex(0x1000, 1, 10);
   {
      float nx = 0x40 * 2.0f + -16 * 5.0f + 0x30 * 11.0f;
      float ny = 0x40 * 1.0f + -16 * 2.0f + 0x30 * 13.0f;
      float nz = 0x40 * 3.0f + -16 * 7.0f + 0x30 * 2.0f;
      float n = (float)sqrt(nx * nx + ny * ny + nz * nz);
      CHECK(fabs(litNormal.x - nx / n) < 1e-6 && fabs(litNormal.y - ny / n) < 1e-6 && fabs(litNormal.z - nz / n) < 1e-6);
   }
   CHECK(g_dwVtxDifColor[10] == 0x80ABCDEF);   /* the vertex's alpha */
   /* a normal of 0 stays 0 */
   ram[0x3000 + 0x10 + 1] = ram[0x3000 + 0x10 + 2] = ram[0x3000 + 0x10 + 3] = 0;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(litNormal.x == 0.0f && litNormal.y == 0.0f && litNormal.z == 0.0f);
   ram[0x3000 + 0x10 + 1] = 0x30; ram[0x3000 + 0x10 + 2] = 0xF0; ram[0x3000 + 0x10 + 3] = 0x40;
   /* a normal the model view takes to 0: 0 */
   {
      Matrix keep = gRSPmodelViewTop;
      memset(&gRSPmodelViewTop, 0, sizeof(gRSPmodelViewTop));
      ricegSPCIVertex(0x1000, 1, 10);
      CHECK(litNormal.x == 0.0f && litNormal.y == 0.0f && litNormal.z == 0.0f);
      gRSPmodelViewTop = keep;
   }
   /* texture coordinates generated: lit and generating */
   gRSP.bTextureGen = true;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_fVtxTxtCoords[10].x == g_normal.x + 100.0f && g_fVtxTxtCoords[10].y == g_normal.y + 200.0f);
   gRSP.bLightingEnable = false;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_fVtxTxtCoords[10].x == 5.0f);
   gRSP.bTextureGen = false;
   /* wireframe: the table's, lit or not */
   options.bWinFrameMode = true;
   gRSP.bLightingEnable = true;
   ricegSPCIVertex(0x1000, 1, 10);
   CHECK(g_dwVtxDifColor[10] == COLOR_RGBA(0x40, 0xF0, 0x30, 0x80));
   options.bWinFrameMode = false;
   gRSP.bLightingEnable = false;
   for (i = 0; i < MAX_VERTS; i++)
      CHECK(i == 10 || i == 11 || clipped[i] == 0);
}

static void lights(void)
{
   ricegSPLightColor(2, 0x10203040);
   CHECK(gRSPlights[2].r == 0x10 && gRSPlights[2].g == 0x20 && gRSPlights[2].b == 0x30 && gRSPlights[2].a == 255);
   CHECK(gRSPlights[2].fr == 16.0f && gRSPlights[2].fg == 32.0f && gRSPlights[2].fb == 48.0f && gRSPlights[2].fa == 255.0f);
   ricegSPNumLights(5);
   CHECK(gSP.numLights == 5);

   /* a light: colour, and x, y, z the N64's bytes 8, 9, 10 */
   w32(0x2000, 0x11223300);
   w32(0x2004, 0x11223301);
   ram[0x2000 + (8 ^ 3)] = 0x7F;
   ram[0x2000 + (9 ^ 3)] = 0x81;
   ram[0x2000 + (10 ^ 3)] = 0x05;
   gRSP.ambientLightIndex = 4;
   ambients = 0;
   memset(dir, 0, sizeof(dir));
   ricegSPLight(0x2000, 3);
   CHECK(gRSPn64lights[3].dwRGBA == 0x11223300 && gRSPn64lights[3].dwRGBACopy == 0x11223301);
   CHECK(gRSPlights[3].r == 0x11 && gRSPlights[3].g == 0x22 && gRSPlights[3].b == 0x33);
   CHECK(dir[0] == 3.0f && dir[1] == 127.0f && dir[2] == -127.0f && dir[3] == 5.0f && dir[4] == 0.0f);
   CHECK(ambients == 0);
   /* the ambient one */
   ricegSPLight(0x2000, 4);
   CHECK(ambients == 1 && ambient == COLOR_RGBA(0x11, 0x22, 0x33, 0xFF) && dir[0] == 3.0f);
   CHECK(gRSPn64lights[4].dwRGBA == 0x11223300);
   /* past 16: nothing */
   ricegSPLight(0x2000, 16);
   CHECK(ambients == 1 && dir[0] == 3.0f);
   /* Majora's Mask's: x, y, z and range the N64's halfwords 4, 5, 7 and 6 */
   options.enableHackForGames = HACK_FOR_ZELDA_MM;
   w32(0x2100, 0x55667708);
   w32(0x2104, 0x556677FF);
   *(short *)&ram[0x2100 + 10] = 100;
   *(short *)&ram[0x2100 + 8] = -200;
   *(short *)&ram[0x2100 + 14] = 300;
   *(short *)&ram[0x2100 + 12] = 400;
   ricegSPLight(0x2100, 6);
   CHECK(dir[0] == 6.0f && dir[1] == 100.0f && dir[2] == -200.0f && dir[3] == 300.0f && dir[4] == 400.0f);
   CHECK(gRSPn64lights[6].dwRGBA == 0x55667708 && gRSPn64lights[6].dwRGBACopy == 0x556677FF);
   w32(0x2104, 0x556677FE);                  /* not its marks: the usual */
   ram[0x2100 + (8 ^ 3)] = 1; ram[0x2100 + (9 ^ 3)] = 2; ram[0x2100 + (10 ^ 3)] = 3;
   ricegSPLight(0x2100, 6);
   CHECK(dir[1] == 1.0f && dir[2] == 2.0f && dir[3] == 3.0f && dir[4] == 0.0f);
   w32(0x2104, 0x556677FF);
   w32(0x2100, 0x55667709);
   ricegSPLight(0x2100, 6);
   CHECK(dir[1] == 1.0f && dir[4] == 0.0f);
   options.enableHackForGames = NO_HACK_FOR_GAME;
   w32(0x2100, 0x55667708);
   ricegSPLight(0x2100, 6);
   CHECK(dir[1] == 1.0f && dir[4] == 0.0f);
}

static void dma_triangles(void)
{
   uint32_t i;

   memset(&gSP.segment, 0, sizeof(gSP.segment));
   gSP.segment[1] = 0x4000;
   for (i = 0; i < 3; i++)
   {
      w32(0x4000 + i * 16 + 0, (i + 1) << 16 | (i + 2) << 8 | (i + 3) | 0xE0E0E0);  /* 5 bits each */
      w32(0x4000 + i * 16 + 4, (uint32_t)((10 + i) << 16) | (uint16_t)(-1 - (int)i));
      w32(0x4000 + i * 16 + 8, (uint32_t)((20 + i) << 16) | 21);
      w32(0x4000 + i * 16 + 12, (uint32_t)(0xFFFF0000u) | 31);
   }
   prepared = constants = blended = drawn = textures = 0;
   status.primitiveType = PRIM_TRI1;
   ricegSPDMATriangles(0x01000000, 3);
   CHECK(status.primitiveType == PRIM_DMA_TRI);
   CHECK(prepared == 3 && textures == 1 && constants == 1 && blended == 1 && drawn == 1);
   CHECK(tris[0][0] == 1 && tris[0][1] == 2 && tris[0][2] == 3 && tris[2][0] == 3 && tris[2][1] == 4 && tris[2][2] == 5);
   /* the last triangle's coordinates on its vertices */
   CHECK(g_fVtxTxtCoords[3].x == 12.0f && g_fVtxTxtCoords[3].y == -3.0f);
   CHECK(g_fVtxTxtCoords[4].x == 22.0f && g_fVtxTxtCoords[4].y == 21.0f);
   CHECK(g_fVtxTxtCoords[5].x == -1.0f && g_fVtxTxtCoords[5].y == 31.0f);
   /* none: nothing set up or drawn */
   prepared = constants = blended = drawn = textures = 0;
   ricegSPDMATriangles(0x01000000, 0);
   CHECK(prepared == 0 && textures == 0 && blended == 0 && drawn == 0 && status.primitiveType == PRIM_DMA_TRI);
   /* past RDRAM: nothing */
   status.primitiveType = PRIM_TRI1;
   gSP.segment[1] = RAM - 32;
   ricegSPDMATriangles(0x01000000, 2);
   CHECK(prepared == 0 && drawn == 0 && status.primitiveType == PRIM_TRI1);
   gSP.segment[1] = RAM - 33;                /* 32 bytes, to a byte short of its end */
   w32(RAM - 36, 0); w32(RAM - 20, 0);
   ricegSPDMATriangles(0x01000000, 2);
   CHECK(prepared == 2 && drawn == 1);
}

static void viewport_(void)
{
   /* scale 640, 480 (quarters); translation 640, 480: centre 160, 120 */
   w16(0x5000 + 0, 640);
   w16(0x5000 + 2, (uint16_t)-480);
   w16(0x5000 + 4, 511);
   w16(0x5000 + 6, 0);
   w16(0x5000 + 8, 644);
   w16(0x5000 + 10, 484);
   w16(0x5000 + 12, 511);
   w16(0x5000 + 14, 0);
   ricegSPViewport(0x5000);
   CHECK(viewport[0] == 1 && viewport[1] == 1 && viewport[2] == 321 && viewport[3] == 241 && viewport[4] == 0x3FF);
   w16(0x5000 + 0, (uint16_t)-640);
   w16(0x5000 + 2, 480);
   viewport[0] = 99;
   ricegSPViewport(0x5000);
   CHECK(viewport[0] == 1 && viewport[2] == 321 && viewport[1] == 1 && viewport[3] == 241);
   /* within RDRAM */
   viewport[0] = 99;
   ricegSPViewport(RAM - 16);
   CHECK(viewport[0] == 99);
   memcpy(&ram[RAM - 18], &ram[0x5000], 16);
   ricegSPViewport(RAM - 18);
   CHECK(viewport[0] != 99);
}

int main(void)
{
   gfx_info.RDRAM = ram;
   gfx_info.VI_X_SCALE_REG = &vi_x_scale;
   modify_vertex();
   ci_vertex();
   lights();
   dma_triangles();
   viewport_();
   printf("rice RSP commands: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
