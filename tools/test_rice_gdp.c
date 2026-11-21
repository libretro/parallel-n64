/* Rice's RDP commands (mupen64plus-video-rice/src/gDP_rice.c), on a
 * renderer of its own here recording what it is asked:
 *
 *   * fill, fog and environment colours, the scissor;
 *   * a fill rectangle: copy and fill cycles one wider and taller; one and
 *     two cycles their memory coverage; into a depth image, the depth
 *     cleared (in a rectangle where it is not the screen's; GoldenEye's
 *     double depth buffer), the clear written to RDRAM where the game
 *     asks it emulated; into a render texture, its rendered bounds, the
 *     fill written in RDRAM (8 and 16 bits) when written directly or the
 *     whole width, filled with the fill colour or the primitive's;
 *     otherwise filled, the bounds kept with render textures or back
 *     buffers checked;
 *   * LoadBlock, LoadTile, LoadTLUT: what TMEM was loaded from (its map,
 *     the loads that filled it from 0 and 0x100, the lines a load started
 *     at), the tile's coordinates, and with full TMEM the texels in TMEM
 *     (LoadBlock's rows, every other one's words swapped); Rice's copy of
 *     TLUT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "ConvertImage.h"
#include "FrameBuffer.h"
#include "RenderBase.h"
#include "Timing.h"
#include "Video.h"
#include "../../Graphics/RDP/gDP_funcs_prot.h"
#include "../../Graphics/rdp_coverage.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what Rice gives it ---- */
#define RAM 0x20000
static uint8_t ram[RAM];
ALIGN(16, RDP_Options gRDP);
struct gDPInfo gDP;
GFX_INFO gfx_info;
uint32_t g_dwRamSize = RAM;
SetImgInfo g_CI, g_ZI, g_TI;
TmemType g_Tmem;
TMEMLoadMapInfo g_tmemLoadAddrMap[0x200];
TMEMLoadMapInfo g_tmemInfo0, g_tmemInfo1;
uint32_t g_TmemFlag[16];
uint32_t g_TxtLoadBy;
uint16_t g_wRDPTlut[0x200];
PluginStatus status;
GlobalOptions options;
FrameBufferOptions frameBufferOptions;
GameSetting g_curRomInfo;
WindowSettingStruct windowSetting;
RenderTextureInfo gRenderTextureInfos[20];
RenderTextureInfo *g_pRenderTextureInfo;

/* ---- a renderer of its own ---- */
static int fogs, clears, fills, actives, coverages, usedAsDI;
static uint32_t fog[4], fillColour;
static int clearRect;               /* 1 a rectangle, 0 all */
static COORDRECT cleared;
static int filled[4];

void RiceRender_SetFogColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{ fog[0] = r; fog[1] = g; fog[2] = b; fog[3] = a; fogs++; }
void RiceRender_ClearDepth(COORDRECT *rect)
{ clears++; clearRect = rect != NULL; if (rect) cleared = *rect; }
bool RiceRender_FillRect(int x0, int y0, int x1, int y1, uint32_t c)
{ filled[0] = x0; filled[1] = y0; filled[2] = x1; filled[3] = y1; fillColour = c; fills++; return true; }
void RiceFB_ActiveTextureBuffer(void) { actives++; }
bool IsUsedAsDI(uint32_t addr) { (void)addr; return usedAsDI != 0; }
static int32_t covered[4];
void rice_coverage_fill(int32_t a, int32_t b, int32_t c, int32_t d)
{ covered[0] = a; covered[1] = b; covered[2] = c; covered[3] = d; coverages++; }

static void reset(void)
{
   fogs = clears = fills = actives = coverages = 0;
   clearRect = -1;
   memset(filled, 0, sizeof(filled));
   fillColour = 0;
}

/* N64 byte a of RDRAM, as the CPU sees it */
static uint8_t n64(uint32_t a) { return ram[a ^ 3]; }

static void colours(void)
{
   ScissorType sc;

   gRDP.fillColor = 0;
   status.DPCycleCount = 0;
   ricegDPSetFillColor(0xF801);
   CHECK(gRDP.fillColor == Convert555ToRGBA(0xF801) && gRDP.fillColor != 0);
   CHECK(status.DPCycleCount == Timing_DLParser_SetFillColor);

   reset();
   ricegDPSetFogColor(1, 2, 3, 4);
   CHECK(fogs == 1 && fog[0] == 1 && fog[1] == 2 && fog[2] == 3 && fog[3] == 4);
   CHECK(status.DPCycleCount == Timing_DLParser_SetFillColor + Timing_DLParser_SetFogColor);

   gRDP.colorsAreReloaded = false;
   ricegDPSetEnvColor(255, 204, 51, 102);
   CHECK(gRDP.colorsAreReloaded);
   CHECK(gRDP.envColor == COLOR_RGBA(255, 204, 51, 102));
   CHECK(gRDP.fvEnvColor[0] == 1.0f && gRDP.fvEnvColor[1] == 204 / 255.0f);
   CHECK(gRDP.fvEnvColor[2] == 51 / 255.0f && gRDP.fvEnvColor[3] == 102 / 255.0f);

   ricegDPSetScissor(&sc, 2, 1.5f, 2.5f, 300.25f, 200.75f);
   CHECK(sc.mode == 2 && sc.x0 == 1 && sc.y0 == 2 && sc.x1 == 300 && sc.y1 == 200);
}

static void fill_mode(uint32_t cycle)
{
   memset(&gRDP.otherMode, 0, sizeof(gRDP.otherMode));
   gRDP.otherMode.cycle_type = cycle;
}

static void fill_rect(void)
{
   uint32_t i;

   windowSetting.uViWidth = 320;
   windowSetting.uViHeight = 240;
   windowSetting.fMultX = 2.0f;
   windowSetting.fMultY = 1.5f;
   gRDP.fillColor = 0x11223344;
   gRDP.primitiveColor = 0x55667788;
   gRDP.originalFillColor = 0xABCD;
   g_pRenderTextureInfo = &gRenderTextureInfos[0];
   status.leftRendered = status.topRendered = status.rightRendered = status.bottomRendered = -1;

   /* otherwise: filled, fill colour in a fill cycle, the primitive's else */
   usedAsDI = 0;
   fill_mode(G_CYC_FILL);
   reset();
   ricegDPFillRect(3, 4, 30, 40);
   CHECK(fills == 1 && filled[0] == 3 && filled[1] == 4 && filled[2] == 31 && filled[3] == 41);
   CHECK(fillColour == 0x11223344 && clears == 0 && coverages == 0 && actives == 0);
   CHECK(status.leftRendered == -1);        /* bounds kept only with render textures or back buffers */
   fill_mode(G_CYC_COPY);
   reset();
   ricegDPFillRect(3, 4, 30, 40);
   CHECK(fills == 1 && filled[2] == 31 && filled[3] == 41 && fillColour == 0x55667788);
   fill_mode(G_CYC_1CYCLE);
   reset();
   ricegDPFillRect(3, 4, 30, 40);
   CHECK(fills == 1 && filled[2] == 30 && filled[3] == 40 && fillColour == 0x55667788);
   CHECK(coverages == 0);                   /* no coverage in this mode */
   /* a mode that may draw coverage: its coverage asked for, unwidened */
   gRDP.otherMode.force_bl = 1;             /* forced blending reading memory */
   gRDP.otherMode.im_rd = 1;
   reset();
   ricegDPFillRect(3, 4, 30, 40);
   CHECK(RDPCVG_MAY_DRAW_COVERAGE(gRDP.otherMode._u32[0]));
   CHECK(coverages == 1 && fills == 1 && covered[0] == 3 && covered[1] == 4 && covered[2] == 30 && covered[3] == 40);
   fill_mode(G_CYC_FILL);
   gRDP.otherMode.force_bl = 1;
   gRDP.otherMode.im_rd = 1;
   reset();
   ricegDPFillRect(3, 4, 30, 40);
   CHECK(coverages == 0);                   /* a fill cycle draws none */

   /* bounds with render textures or back buffers, the render texture made active */
   fill_mode(G_CYC_FILL);
   frameBufferOptions.bSupportRenderTextures = true;
   status.bCIBufferIsRendered = false;
   status.leftRendered = status.topRendered = status.rightRendered = status.bottomRendered = -1;
   reset();
   ricegDPFillRect(10, 20, 30, 40);
   CHECK(actives == 1);
   CHECK(status.leftRendered == 10 && status.topRendered == 20 && status.rightRendered == 31 && status.bottomRendered == 41);
   status.bCIBufferIsRendered = true;
   reset();
   ricegDPFillRect(5, 25, 20, 50);
   CHECK(actives == 0);
   /* the least left and top so far, the greatest right and bottom */
   CHECK(status.leftRendered == 5 && status.topRendered == 20 && status.rightRendered == 31 && status.bottomRendered == 51);
   ricegDPFillRect(7, 21, 40, 30);
   CHECK(status.leftRendered == 5 && status.topRendered == 20 && status.rightRendered == 41 && status.bottomRendered == 51);
   status.rightRendered = status.bottomRendered = 0;            /* a bound of 0 one too */
   ricegDPFillRect(-6, -6, -3, -3);
   CHECK(status.rightRendered == 0 && status.bottomRendered == 0 && status.leftRendered == -6 && status.topRendered == -6);
   status.leftRendered = 5;
   status.topRendered = 20;
   frameBufferOptions.bSupportRenderTextures = false;
   frameBufferOptions.bCheckBackBufs = true;
   status.leftRendered = -1;
   ricegDPFillRect(8, 21, 40, 30);
   CHECK(status.leftRendered == 8);
   frameBufferOptions.bCheckBackBufs = false;

   /* into a depth image: cleared, in the rectangle unless it is the screen */
   usedAsDI = 1;
   reset();
   ricegDPFillRect(0, 0, 319, 239);
   CHECK(clears == 1 && clearRect == 0 && fills == 0);
   reset();
   ricegDPFillRect(0, 0, 317, 239);          /* 318 wide: 2 short */
   CHECK(clears == 1 && clearRect == 1);
   CHECK(cleared.x1 == 0 && cleared.y1 == 0 && cleared.x2 == 636 && cleared.y2 == 360);
   reset();
   ricegDPFillRect(0, 0, 318, 237);          /* 319 by 238: 2 short */
   CHECK(clears == 1 && clearRect == 1);
   reset();
   ricegDPFillRect(0, 0, 318, 238);          /* 319 by 239: whole */
   CHECK(clears == 1 && clearRect == 0);
   reset();
   ricegDPFillRect(1, 0, 319, 239);
   CHECK(clears == 1 && clearRect == 1 && cleared.x1 == 2);
   reset();
   ricegDPFillRect(0, 3, 319, 239);
   CHECK(clears == 1 && clearRect == 1 && cleared.y1 == 4);
   /* GoldenEye: the upper half where the colour image is the depth's, the lower its offset on */
   options.enableHackForGames = HACK_FOR_GOLDEN_EYE;
   g_CI.dwAddr = g_ZI.dwAddr = 0x8000;
   g_CI.dwWidth = 320;
   reset();
   ricegDPFillRect(2, 3, 9, 19);
   CHECK(clears == 1 && clearRect == 1 && cleared.x1 == 4 && cleared.y1 == 4 && cleared.x2 == 20 && cleared.y2 == 30);
   g_CI.dwAddr = 0x8000 + 10 * 320 * 2;
   reset();
   ricegDPFillRect(2, 3, 9, 19);
   CHECK(clears == 1 && clearRect == 1 && cleared.x1 == 4 && cleared.y1 == 19 && cleared.x2 == 20 && cleared.y2 == 45);
   options.enableHackForGames = NO_HACK_FOR_GAME;
   reset();
   ricegDPFillRect(2, 3, 9, 19);
   CHECK(cleared.y1 == 4 && cleared.y2 == 30);
   /* emulated: the fill colour written, rows from uly to lry, from ulx */
   g_curRomInfo.bEmulateClear = true;
   g_CI.dwAddr = 0x1000;
   g_CI.dwWidth = 16;
   memset(ram, 0, sizeof(ram));
   gfx_info.RDRAM = ram;
   reset();
   ricegDPFillRect(1, 2, 3, 4);              /* fill cycle: to 4 and 5 */
   for (i = 0; i < 16 * 8; i++)              /* pixel i of the image: the N64's halfword at 0x1000 + 2i */
   {
      uint32_t x = i % 16, y = i / 16;
      int in = x >= 1 && x < 4 && y >= 2 && y < 5;
      CHECK(*(uint16_t *)&ram[(0x1000 + 2 * i) ^ 2] == (in ? 0xABCD : 0));
   }
   g_curRomInfo.bEmulateClear = false;
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(1, 2, 3, 4);
   for (i = 0; i < RAM; i++)
      CHECK(ram[i] == 0);
   usedAsDI = 0;

   /* Banjo-Tooie: nothing into a render texture */
   status.bHandleN64RenderTexture = true;
   options.enableHackForGames = HACK_FOR_BANJO_TOOIE;
   reset();
   ricegDPFillRect(1, 2, 3, 4);
   CHECK(fills == 0 && actives == 0 && clears == 0);
   options.enableHackForGames = NO_HACK_FOR_GAME;

   /* into a render texture */
   g_pRenderTextureInfo->N64Width = 16;
   g_pRenderTextureInfo->maxUsedHeight = 100;
   g_pRenderTextureInfo->CI_Info.dwAddr = 0x2000;
   g_pRenderTextureInfo->CI_Info.dwSize = G_IM_SIZ_8b;
   status.leftRendered = status.topRendered = status.rightRendered = status.bottomRendered = -1;
   status.bCIBufferIsRendered = false;
   status.bDirectWriteIntoRDRAM = false;
   status.bFrameBufferIsDrawn = false;
   status.bFrameBufferDrawnByTriangles = false;
   memset(ram, 0, sizeof(ram));
   gRDP.originalFillColor = 0x5A;
   reset();
   ricegDPFillRect(2, 1, 5, 3);              /* fill: to 6, 4 */
   CHECK(actives == 1 && fills == 1 && fillColour == 0x11223344);
   CHECK(status.leftRendered == 2 && status.topRendered == 1 && status.rightRendered == 6 && status.bottomRendered == 4);
   CHECK(g_pRenderTextureInfo->maxUsedHeight == 100);   /* the highest row used: not less */
   CHECK(status.bFrameBufferIsDrawn && status.bFrameBufferDrawnByTriangles);
   for (i = 0; i < 0x1000; i++)
      CHECK(ram[0x2000 + i] == 0);           /* not the whole width: nothing in RDRAM */
   status.bCIBufferIsRendered = true;
   g_pRenderTextureInfo->maxUsedHeight = 2;
   reset();
   ricegDPFillRect(0, 0, 15, 2);             /* the whole width: written, 8 bits */
   CHECK(actives == 0 && fills == 1);
   CHECK(n64(0x2000 + 0) == 0x5A && n64(0x2000 + 15) == 0x5A && n64(0x2000 + 2 * 16 + 7) == 0x5A);
   CHECK(n64(0x2000 + 3 * 16) == 0);
   CHECK(g_pRenderTextureInfo->maxUsedHeight == 3);
   CHECK(status.leftRendered == 0 && status.topRendered == 0 && status.rightRendered == 16 && status.bottomRendered == 4);
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(0, 0, 14, 0);             /* the width less one: written too */
   CHECK(n64(0x2000 + 14) == 0x5A && n64(0x2000 + 15) == 0);
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(0, 0, 13, 0);             /* less two: not */
   CHECK(n64(0x2000) == 0);
   ricegDPFillRect(0, 1, 15, 1);             /* not from the top: not */
   CHECK(n64(0x2000 + 16) == 0);
   CHECK(status.topRendered == 0);
   ricegDPFillRect(1, 0, 15, 1);             /* not from the left: not */
   CHECK(n64(0x2000 + 1) == 0);
   CHECK(status.leftRendered == 0 && status.topRendered == 0);   /* a bound of 0 one */
   /* written directly: written wherever, and not filled */
   status.bDirectWriteIntoRDRAM = true;
   status.bFrameBufferIsDrawn = false;
   memset(ram, 0, sizeof(ram));
   reset();
   ricegDPFillRect(3, 1, 4, 1);
   CHECK(n64(0x2000 + 16 + 3) == 0x5A && n64(0x2000 + 16 + 4) == 0x5A && n64(0x2000 + 16 + 5) == 0);
   CHECK(n64(0x2000 + 16 + 2) == 0 && n64(0x2000 + 2 * 16 + 3) == 0);
   CHECK(fills == 0 && !status.bFrameBufferIsDrawn && status.bFrameBufferDrawnByTriangles);
   /* 16 bits: each pixel two bytes */
   g_pRenderTextureInfo->CI_Info.dwSize = G_IM_SIZ_16b;
   gRDP.originalFillColor = 0xBEEF;
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(2, 0, 4, 1);              /* to 5, 2 */
   for (i = 0; i < 16 * 4; i++)
   {
      uint32_t x = i % 16, y = i / 16;
      int in = x >= 2 && x < 5 && y < 2;
      CHECK(*(uint16_t *)&ram[(0x2000 + 2 * i) ^ 2] == (in ? 0xBEEF : 0));
   }
   /* nothing past RDRAM's end: 16 bits to its last halfword, 8 to its last byte */
   g_pRenderTextureInfo->CI_Info.dwAddr = RAM - 16 * 2 - 4;
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(0, 0, 15, 2);             /* rows 0..2: the second's 2 pixels, no more */
   CHECK(*(uint16_t *)&ram[(RAM - 2) ^ 2] == 0xBEEF && *(uint16_t *)&ram[(RAM - 36) ^ 2] == 0xBEEF);
   g_pRenderTextureInfo->CI_Info.dwSize = G_IM_SIZ_8b;
   g_pRenderTextureInfo->CI_Info.dwAddr = RAM - 16 - 3;
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(0, 0, 15, 2);
   CHECK(ram[(RAM - 1) ^ 3] == 0xEF && ram[(RAM - 19) ^ 3] == 0xEF && ram[(RAM - 20) ^ 3] == 0);
   /* a row past it and one back within, the width exceeded */
   g_pRenderTextureInfo->CI_Info.dwAddr = RAM - 18;
   memset(ram, 0, sizeof(ram));
   ricegDPFillRect(0, 0, 19, 0);             /* 20 wide from RAM - 18: 18 of them */
   CHECK(ram[(RAM - 1) ^ 3] == 0xEF && ram[(RAM - 18) ^ 3] == 0xEF && ram[(RAM - 19) ^ 3] == 0);
   g_pRenderTextureInfo->CI_Info.dwAddr = 0x2000;
   status.bDirectWriteIntoRDRAM = false;
   /* not a fill cycle: the primitive's colour */
   fill_mode(G_CYC_1CYCLE);
   reset();
   ricegDPFillRect(2, 1, 5, 3);
   CHECK(fills == 1 && fillColour == 0x55667788 && filled[2] == 5);
   status.bHandleN64RenderTexture = false;
}

/* the TMEM line flags, bit i of word w line w * 32 + i */
static int flag(uint32_t line) { return (g_TmemFlag[line >> 5] >> (line & 31)) & 1; }

static void load_block(void)
{
   uint32_t i;
   TMEMLoadMapInfo *info = &g_tmemLoadAddrMap[0];

   memset(ram, 0, sizeof(ram));
   for (i = 0; i < 0x4000; i++)
      ram[0x4000 + (i ^ 3)] = (uint8_t)(i * 5 + 3);      /* N64 byte 0x4000 + i */
   for (i = RAM - 0x100; i < RAM; i++)
      ram[i ^ 3] = (uint8_t)(i * 7 + 1);
   g_TI.dwAddr = 0x4000;
   g_TI.dwFormat = G_IM_FMT_RGBA;
   g_TI.dwSize = G_IM_SIZ_16b;
   g_TI.dwWidth = 32;
   g_TI.bpl = 64;
   memset(&gDP.tiles, 0, sizeof(gDP.tiles));
   memset(&gRDP.tilesinfo, 0, sizeof(gRDP.tilesinfo));
   for (i = 0; i < 8; i++)
      gDP.tiles[i].tmem = 0x80;
   gDP.tiles[7].tmem = 0;
   gDP.tiles[7].size = G_IM_SIZ_16b;
   gDP.tiles[7].line = 4;
   gRDP.tilesinfo[7].bForceWrapS = gRDP.tilesinfo[7].bForceClampT = true;
   gRDP.tilesinfo[7].bSizeIsValid = true;
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   memset(&g_tmemInfo0, 0, sizeof(g_tmemInfo0));
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   options.bUseFullTMEM = false;

   /* 2048 texels of 16 bits from 0: TMEM's whole, its lines flagged from 0 */
   memset(info, 0xEE, sizeof(*info));
   gDP.tiles[7].lrs = gDP.tiles[7].lrt = 77;
   gRDP.tilesinfo[7].hilite_sl = 77;
   ricegDPLoadBlock(7, 0, 0, 2047, 0);
   CHECK(!gRDP.tilesinfo[7].bForceWrapS && !gRDP.tilesinfo[7].bForceClampT && !gRDP.tilesinfo[7].bSizeIsValid);
   CHECK(gRDP.tilesinfo[7].lastTileCmd == CMD_LOADBLOCK && g_TxtLoadBy == CMD_LOADBLOCK);
   CHECK(info->dwLoadAddress == 0x4000 && info->bSetBy == CMD_LOADBLOCK && info->dxt == 0 && info->bSwapped);
   CHECK(info->dwFormat == G_IM_FMT_RGBA && info->dwSize == G_IM_SIZ_16b && info->dwWidth == 32);
   CHECK(info->dwTotalWords == 2048 && info->dwTmem == 0 && info->dwLine == 4);
   CHECK(info->sl == 0 && info->sh == 2047 && info->tl == 0 && info->th == 0);
   CHECK(gDP.tiles[7].lrs == 0 && gDP.tiles[7].uls == 2047 && gDP.tiles[7].lrt == 0 && gDP.tiles[7].ult == 0);
   CHECK(gRDP.tilesinfo[7].hilite_sl == 0 && gRDP.tilesinfo[7].hilite_sh == 2047);
   CHECK(g_tmemInfo0.dwTotalWords == 512 && g_tmemInfo0.dwLoadAddress == 0x4000);
   CHECK(g_tmemInfo1.dwTotalWords == 512 && g_tmemInfo1.bSetBy == CMD_LOADBLOCK);
   /* 512 lines: words 0..15 cleared, line 0 flagged */
   CHECK(g_TmemFlag[0] == 1);
   for (i = 1; i < 16; i++)
      CHECK(g_TmemFlag[i] == 0);
   /* 1024 of them: the lower half's only */
   memset(&g_tmemInfo0, 0, sizeof(g_tmemInfo0));
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   memset(info, 0xEE, sizeof(*info));
   g_TI.dwFormat = G_IM_FMT_CI;
   ricegDPLoadBlock(7, 5, 3, 1023, 0x80);
   CHECK(g_tmemInfo0.dwTotalWords == 256 && g_tmemInfo1.dwTotalWords == 0);
   CHECK(info->sl == 5 && info->tl == 3 && gDP.tiles[7].lrs == 5 && gDP.tiles[7].lrt == 3);
   CHECK(info->dwFormat == G_IM_FMT_CI && g_tmemInfo0.dwFormat == G_IM_FMT_CI);
   g_TI.dwFormat = G_IM_FMT_RGBA;
   CHECK(info->dxt == 0x80 && !info->bSwapped && info->th == 0x80 && gDP.tiles[7].ult == 0x80);
   ricegDPLoadBlock(7, 0, 0, 1022, 0);
   CHECK(g_tmemInfo0.dwTotalWords == 256);   /* not again */
   /* from 0x100: 1024 of them */
   gDP.tiles[7].tmem = 0x100;
   ricegDPLoadBlock(7, 0, 0, 1023, 0);
   CHECK(g_tmemInfo1.dwTotalWords == 256 && g_tmemLoadAddrMap[0x100].dwTmem == 0x100);
   CHECK(g_tmemInfo1.dwTmem == 0x100 && g_tmemInfo1.bSetBy == CMD_LOADBLOCK);
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   ricegDPLoadBlock(7, 0, 0, 2047, 0);
   CHECK(g_tmemInfo1.dwTotalWords == 0);
   /* 32 bits: twice the size */
   gDP.tiles[7].size = G_IM_SIZ_32b;
   gDP.tiles[7].tmem = 0;
   ricegDPLoadBlock(7, 0, 0, 1023, 0);
   CHECK(g_tmemLoadAddrMap[0].dwTotalWords == 2048 && g_tmemInfo1.dwTotalWords == 512);
   /* a flag part of a word: lines 40..47 of 32 texels' worth from line 40 */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   gDP.tiles[7].size = G_IM_SIZ_16b;
   gDP.tiles[7].tmem = 40;
   ricegDPLoadBlock(7, 0, 0, 31, 0);         /* 32 texels: 8 lines */
   CHECK(flag(40) && !flag(41) && !flag(47) && flag(48) && flag(39) && flag(32));
   CHECK(g_TmemFlag[0] == 0xFFFFFFFF);
   /* to a word's last line: lines 40..62 */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 91, 0);         /* 23 lines */
   CHECK(flag(40) && !flag(41) && !flag(62) && flag(63) && flag(64) && flag(32));
   /* past it: 128 lines from 40, words 2..4 whole, 5 in part */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 511, 0);
   CHECK(flag(40) && !flag(63) && !flag(64) && !flag(100) && !flag(160) && !flag(167) && flag(168) && flag(32));
   /* 33 lines on: the next word's first */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 227, 0);        /* 57 lines: 24, 32 and 1 */
   CHECK(flag(40) && !flag(64) && !flag(95) && !flag(96) && flag(97));
   /* past a word's end: from line 60, 16 lines, into the next */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   gDP.tiles[7].tmem = 60;
   ricegDPLoadBlock(7, 0, 0, 63, 0);
   CHECK(flag(60) && !flag(61) && !flag(63) && !flag(64) && !flag(75) && flag(76) && flag(59) && flag(32));
   /* from a word's start, part of one: lines 64..69 */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   gDP.tiles[7].tmem = 64;
   ricegDPLoadBlock(7, 0, 0, 23, 0);
   CHECK(flag(64) && !flag(65) && !flag(69) && flag(70) && flag(63));
   /* from a word's start, past it: 33 lines, the next word's first too */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 131, 0);
   CHECK(flag(64) && !flag(65) && !flag(95) && !flag(96) && flag(97) && flag(63));
   /* TMEM's last word, from its start: 64 lines and 40 */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   gDP.tiles[7].tmem = 0x1E0;
   ricegDPLoadBlock(7, 0, 0, 255, 0);
   CHECK(flag(0x1E0) && !flag(0x1FF) && flag(0x1DF));
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 159, 0);
   CHECK(flag(0x1E0) && !flag(0x1FF) && flag(0x1DF));
   /* the end of TMEM: nothing past its flags */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   gDP.tiles[7].tmem = 0x1F0;
   ricegDPLoadBlock(7, 0, 0, 2047, 0);
   CHECK(flag(0x1F0) && !flag(0x1FF) && flag(0x1EF));
   gDP.tiles[7].tmem = 0x1E8;
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 2047, 0);
   CHECK(flag(0x1E8) && !flag(0x1FF) && flag(0x1E7));
   gDP.tiles[7].tmem = 0x1C8;                /* into the last word and past it */
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   ricegDPLoadBlock(7, 0, 0, 2047, 0);
   CHECK(flag(0x1C8) && !flag(0x1E0) && !flag(0x1FF) && flag(0x1C7) && flag(0x1C0));
   /* lastTileCmd of the tile loaded only */
   gRDP.tilesinfo[3].lastTileCmd = 0;
   ricegDPLoadBlock(7, 0, 0, 3, 0);
   CHECK(gRDP.tilesinfo[3].lastTileCmd == 0);
   /* every tile at its TMEM: its own too */
   for (i = 0; i < 8; i++)
      gDP.tiles[i].tmem = 0x1C8;
   gRDP.tilesinfo[7].lastTileCmd = 0;
   ricegDPLoadBlock(7, 0, 0, 3, 0);
   CHECK(gRDP.tilesinfo[7].lastTileCmd == CMD_LOADBLOCK);
   for (i = 0; i < 7; i++)
      gDP.tiles[i].tmem = 0x80;

   /* full TMEM: the texels, N64 byte order, from uls of row ult */
   options.bUseFullTMEM = true;
   gDP.tiles[7].tmem = 2;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 4, 1, 63, 0);        /* 64 of 16 bits: 128 bytes from 0x4000 + 64 + 8 */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[2];
      int ok = 1;
      for (i = 0; i < 128; i++)
         if (t[i] != n64(0x4000 + 64 + 8 + i)) ok = 0;
      CHECK(ok);
      CHECK(t[128] == 0 && ((const uint8_t *)&g_Tmem.g_Tmem64bit[2])[-1] == 0);
   }
   /* unaligned in RDRAM: 3 leading bytes, 1 trailing */
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   gDP.tiles[7].size = G_IM_SIZ_8b;
   g_TI.dwSize = G_IM_SIZ_8b;
   ricegDPLoadBlock(7, 1, 0, 3, 0);          /* 4 bytes from 0x4001: 3 leading, 1 trailing */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[2];
      int ok = 1;
      for (i = 0; i < 4; i++)
         if (t[i] != n64(0x4001 + i)) ok = 0;
      CHECK(ok && t[4] == 0);
   }
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 1, 0, 9, 0);          /* 10 bytes from 0x4001: 3, a word, 3 */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[2];
      int ok = 1;
      for (i = 0; i < 10; i++)
         if (t[i] != n64(0x4001 + i)) ok = 0;
      CHECK(ok && t[10] == 0);
   }
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 3, 0, 1, 0);          /* 2 bytes from 0x4003: past its leading 1 */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[2];
      CHECK(t[0] == n64(0x4003) && t[1] == n64(0x4004) && t[2] == 0);
   }
   g_TI.dwSize = G_IM_SIZ_16b;
   gDP.tiles[7].size = G_IM_SIZ_16b;
   /* dxt: rows of (2047 + dxt) / dxt words, every other one's words swapped */
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   gDP.tiles[7].tmem = 0;
   ricegDPLoadBlock(7, 0, 0, 127, 1024);     /* rows of 2 words (16 bytes), 256 bytes */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[0];
      int ok = 1;
      for (i = 0; i < 256; i++)
      {
         uint32_t row = i / 16;
         uint32_t k = (row & 1) ? (i ^ 4) : i;
         if (t[i] != n64(0x4000 + k)) ok = 0;
      }
      CHECK(ok && t[256] == 0);
   }
   /* 32 bits: odd rows' 64-bit halves swapped, nothing past the rows */
   for (i = 0; i < sizeof(g_Tmem); i++)
      ((uint8_t *)&g_Tmem)[i] = (uint8_t)(i * 3 + 1);
   gDP.tiles[7].size = G_IM_SIZ_32b;
   g_TI.dwSize = G_IM_SIZ_32b;
   ricegDPLoadBlock(7, 0, 0, 47, 512);       /* 192 bytes, rows of 4 words: 6 of them */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[0];
      int ok = 1;
      for (i = 0; i < 192; i++)
      {
         uint32_t row = i / 32;
         uint32_t k = (row & 1) ? (i ^ 8) : i;
         if (t[i] != n64(0x4000 + k)) ok = 0;
      }
      for (i = 192; i < 256; i++)
         if (t[i] != (uint8_t)(i * 3 + 1)) ok = 0;
      CHECK(ok);
   }
   /* 16 bits, rows of 4 words: the odd ones' 32-bit halves of each */
   for (i = 0; i < sizeof(g_Tmem); i++)
      ((uint8_t *)&g_Tmem)[i] = (uint8_t)(i * 3 + 1);
   gDP.tiles[7].size = G_IM_SIZ_16b;
   g_TI.dwSize = G_IM_SIZ_16b;
   ricegDPLoadBlock(7, 0, 0, 47, 512);       /* 96 bytes: 3 rows */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[0];
      int ok = 1;
      for (i = 0; i < 96; i++)
      {
         uint32_t row = i / 32;
         uint32_t k = (row & 1) ? (i ^ 4) : i;
         if (t[i] != n64(0x4000 + k)) ok = 0;
      }
      for (i = 96; i < 128; i++)
         if (t[i] != (uint8_t)(i * 3 + 1)) ok = 0;
      CHECK(ok);
   }
   /* a dxt of 1: rows of 2048 words, none whole, nothing copied */
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 0, 0, 63, 1);
   CHECK(g_Tmem.g_Tmem64bit[0] == 0);
   g_TI.dwSize = G_IM_SIZ_16b;
   gDP.tiles[7].size = G_IM_SIZ_16b;
   /* nothing read past RDRAM, nor written past TMEM */
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   g_TI.dwAddr = RAM - 64;
   ricegDPLoadBlock(7, 0, 0, 31, 0);         /* 64 bytes: to RDRAM's end */
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[63] == n64(RAM - 1));
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 0, 0, 32, 0);         /* 66: past it */
   CHECK(g_Tmem.g_Tmem64bit[0] == 0);
   g_TI.dwAddr = 0x4000;
   gDP.tiles[7].tmem = 0x1F8;
   ricegDPLoadBlock(7, 0, 0, 31, 0);         /* 64 bytes at 0x1F8 * 8: to TMEM's end */
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0x1F8])[63] == n64(0x4000 + 63));
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 0, 0, 32, 0);         /* past it */
   CHECK(g_Tmem.g_Tmem64bit[0x1F8] == 0);
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   gDP.tiles[7].tmem = 0;
   ricegDPLoadBlock(7, 0, 0, 0, 0);          /* (0 + 1) << 2 >> 1: 2 bytes */
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[1] == n64(0x4001));
   /* 8 bits: a byte, and an odd count to one past TMEM's end */
   gDP.tiles[7].size = G_IM_SIZ_8b;
   g_TI.dwSize = G_IM_SIZ_8b;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 0, 0, 0, 0);
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[0] == n64(0x4000) && ((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[1] == 0);
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadBlock(7, 1, 0, 0, 0);          /* a byte at 0x4001: fewer than its leading 3 */
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[0] == n64(0x4001) && ((const uint8_t *)&g_Tmem.g_Tmem64bit[0])[1] == 0);
   gDP.tiles[7].tmem = 0x1F8;
   ricegDPLoadBlock(7, 0, 0, 64, 0);         /* 65 bytes: 4097 */
   CHECK(g_Tmem.g_Tmem64bit[0x1F8] == 0);
   gDP.tiles[7].tmem = 0;
   gDP.tiles[7].size = G_IM_SIZ_16b;
   g_TI.dwSize = G_IM_SIZ_16b;
   options.bUseFullTMEM = false;
}

static void load_tile(void)
{
   uint32_t i;
   TMEMLoadMapInfo *info = &g_tmemLoadAddrMap[0x40];

   memset(&gDP.tiles, 0, sizeof(gDP.tiles));
   memset(&gRDP.tilesinfo, 0, sizeof(gRDP.tilesinfo));
   gDP.tiles[2].tmem = 0x40;
   gDP.tiles[5].tmem = 0x40;
   gDP.tiles[6].tmem = 0x41;
   gDP.tiles[2].size = G_IM_SIZ_16b;
   gDP.tiles[2].line = 4;
   g_TI.dwAddr = 0x4000;
   g_TI.dwFormat = G_IM_FMT_IA;
   g_TI.dwSize = G_IM_SIZ_16b;
   g_TI.dwWidth = 64;
   g_TI.bpl = 128;
   gRDP.tilesinfo[2].bForceClampS = true;
   memset(g_TmemFlag, 0xFF, sizeof(g_TmemFlag));
   memset(&g_tmemInfo0, 0, sizeof(g_tmemInfo0));

   /* coordinates swapped into order */
   gDP.tiles[0].tmem = 0x40;
   memset(info, 0xEE, sizeof(*info));
   ricegDPLoadTile(2, 15, 9, 0, 2);          /* 16 by 8 from (0, 2) */
   CHECK(!gRDP.tilesinfo[2].bForceClampS && gRDP.tilesinfo[2].bSizeIsValid);
   CHECK(gDP.tiles[2].lrs == 0 && gDP.tiles[2].lrt == 2 && gDP.tiles[2].uls == 15 && gDP.tiles[2].ult == 9);
   CHECK(gRDP.tilesinfo[2].hilite_sl == 0 && gRDP.tilesinfo[2].hilite_tl == 2);
   CHECK(gRDP.tilesinfo[2].hilite_sh == 15 && gRDP.tilesinfo[2].hilite_th == 9);
   CHECK(info->sl == 0 && info->tl == 2 && info->sh == 15 && info->th == 9);
   CHECK(info->dwLoadAddress == 0x4000 && info->dwFormat == G_IM_FMT_IA && info->dwSize == G_IM_SIZ_16b && info->dwWidth == 64);
   CHECK(info->dxt == 0 && !info->bSwapped && info->bSetBy == CMD_LOADTILE && info->dwLine == 4 && info->dwTmem == 0x40);
   CHECK(info->dwTotalWords == (4 * 8) << 2);
   CHECK(g_TxtLoadBy == CMD_LOADTILE);
   /* each tile at the same TMEM its last command a LoadTile */
   CHECK(gRDP.tilesinfo[2].lastTileCmd == CMD_LOADTILE && gRDP.tilesinfo[5].lastTileCmd == CMD_LOADTILE);
   CHECK(gRDP.tilesinfo[0].lastTileCmd == CMD_LOADTILE);
   CHECK(gRDP.tilesinfo[6].lastTileCmd == 0);
   /* 32 lines from 0x40 flagged */
   CHECK(flag(0x40) && !flag(0x41) && !flag(0x5F) && flag(0x60) && flag(0x3F));
   CHECK(g_tmemInfo0.dwTotalWords == 0);
   /* 32 bits: the line twice */
   gDP.tiles[2].size = G_IM_SIZ_32b;
   ricegDPLoadTile(2, 0, 0, 7, 3);
   CHECK(info->dwTotalWords == (8 * 4) << 2);
   gDP.tiles[2].size = G_IM_SIZ_16b;
   /* from 0: 256 lines or more, the load from 0; 512, from 0x100 too */
   gDP.tiles[2].tmem = 0;
   gDP.tiles[2].line = 32;
   ricegDPLoadTile(2, 0, 0, 255, 7);         /* 32 * 8 */
   CHECK(g_tmemInfo0.dwTotalWords == 256 && g_tmemInfo0.bSetBy == CMD_LOADTILE);
   memset(&g_tmemInfo0, 0, sizeof(g_tmemInfo0));
   ricegDPLoadTile(2, 0, 0, 255, 6);         /* 32 * 7 */
   CHECK(g_tmemInfo0.dwTotalWords == 0);
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   ricegDPLoadTile(2, 0, 0, 255, 15);
   CHECK(g_tmemInfo0.dwTotalWords == 512 && g_tmemInfo1.dwTotalWords == 512);
   CHECK(g_tmemInfo1.bSetBy == CMD_LOADTILE && g_tmemInfo1.dwTmem == 0);
   gDP.tiles[2].tmem = 0x100;
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   ricegDPLoadTile(2, 0, 0, 255, 7);
   CHECK(g_tmemInfo1.dwTotalWords == 256 && g_tmemInfo1.dwTmem == 0x100 && g_tmemInfo1.bSetBy == CMD_LOADTILE);
   memset(&g_tmemInfo1, 0, sizeof(g_tmemInfo1));
   ricegDPLoadTile(2, 0, 0, 255, 15);
   CHECK(g_tmemInfo1.dwTotalWords == 0);
   /* past TMEM's end (tmem * 8 + line * height over 4K): nothing */
   gDP.tiles[2].tmem = 0x1FF;
   gDP.tiles[2].line = 4;
   info = &g_tmemLoadAddrMap[0x1FF];
   memset(info, 0, sizeof(*info));
   ricegDPLoadTile(2, 0, 0, 15, 1);          /* 0xFF8 + 8 */
   CHECK(info->bSetBy == CMD_LOADTILE);
   memset(info, 0, sizeof(*info));
   gRDP.tilesinfo[2].bSizeIsValid = false;
   ricegDPLoadTile(2, 0, 0, 15, 2);          /* 0xFF8 + 12 */
   CHECK(info->bSetBy == 0 && gRDP.tilesinfo[2].bSizeIsValid);
   gDP.tiles[2].line = 9;                    /* 0xFF8 + 9 */
   ricegDPLoadTile(2, 0, 0, 15, 0);
   CHECK(info->bSetBy == 0);

   /* full TMEM: the first row where it goes, Bust-A-Move's pitch */
   options.bUseFullTMEM = true;
   gDP.tiles[2].tmem = 0x10;
   gDP.tiles[2].line = 4;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadTile(2, 2, 1, 17, 1);          /* one row of 16 from (2, 1) */
   {
      const uint8_t *t = (const uint8_t *)&g_Tmem.g_Tmem64bit[0x10];
      int ok = 1;
      for (i = 0; i < 32; i++)
         if (t[i] != n64(0x4000 + 128 + 4 + i)) ok = 0;
      CHECK(ok && t[32] == 0);
   }
   g_TI.bpl = 0;
   options.enableHackForGames = HACK_FOR_BUST_A_MOVE;
   ricegDPLoadTile(2, 0, 0, 15, 0);
   CHECK(g_TI.bpl == 1024);
   g_TI.bpl = 0;
   options.enableHackForGames = NO_HACK_FOR_GAME;
   ricegDPLoadTile(2, 0, 0, 15, 0);
   CHECK(g_TI.bpl == 0);
   g_TI.bpl = 128;
   /* a line of 0: nothing copied, but its loads recorded */
   gDP.tiles[2].line = 0;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   memset(&g_tmemLoadAddrMap[0x10], 0, sizeof(g_tmemLoadAddrMap[0x10]));
   ricegDPLoadTile(2, 0, 0, 15, 0);
   CHECK(g_Tmem.g_Tmem64bit[0x10] == 0 && g_tmemLoadAddrMap[0x10].bSetBy == 0);
   /* past RDRAM: nothing */
   gDP.tiles[2].line = 4;
   g_TI.dwAddr = RAM - 32;
   ricegDPLoadTile(2, 0, 0, 15, 0);          /* 32 bytes: to its end */
   CHECK(((const uint8_t *)&g_Tmem.g_Tmem64bit[0x10])[31] == n64(RAM - 1));
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadTile(2, 0, 0, 16, 0);          /* 34: past it */
   CHECK(g_Tmem.g_Tmem64bit[0x10] == 0);
   g_TI.dwAddr = 0x4000;
   options.bUseFullTMEM = false;
}

static void load_tlut(void)
{
   uint32_t i;
   uint16_t *pal = (uint16_t *)&ram[0x6000];

   for (i = 0; i < 0x200; i++)
      pal[i] = (uint16_t)(0x1000 + i);       /* the CPU's halfword i: (i ^ 1) to the N64 */
   memset(g_wRDPTlut, 0, sizeof(g_wRDPTlut));
   memset(&gDP.tiles, 0, sizeof(gDP.tiles));
   memset(&gRDP.tilesinfo, 0, sizeof(gRDP.tilesinfo));
   gDP.tiles[7].tmem = 0x100 + 0x10;
   g_TI.dwAddr = 0x6000;
   g_TI.dwWidth = 8;
   gRDP.tilesinfo[7].bForceWrapT = true;
   options.bUseFullTMEM = false;

   /* 16 entries from (2, 1): RDRAM's halfwords 10.., to TLUT's 0x10.. */
   ricegDPLoadTLUT(0, 7, 2, 1, 17, 1);
   CHECK(!gRDP.tilesinfo[7].bForceWrapT && gRDP.tilesinfo[7].bSizeIsValid);
   CHECK(gRDP.tilesinfo[7].lastTileCmd == CMD_LOADTLUT && g_TxtLoadBy == CMD_LOADTLUT);
   CHECK(gDP.tiles[7].lrs == 2 && gDP.tiles[7].lrt == 1 && gDP.tiles[7].uls == 17 && gDP.tiles[7].ult == 1);
   CHECK(gRDP.tilesinfo[7].hilite_sl == 2 && gRDP.tilesinfo[7].hilite_tl == 1);
   {
      int ok = 1;
      for (i = 0; i < 16; i++)
         if (g_wRDPTlut[(0x10 + i) ^ 1] != pal[(10 + i) ^ 1 ^ 0]) ok = 0;
      CHECK(ok);
   }
   /* TLUT's entry n is the N64's halfword n: RDRAM's (n ^ 1) */
   CHECK(g_wRDPTlut[0x10 ^ 1] == pal[(10 + 0) ^ 1]);
   CHECK(g_wRDPTlut[0x0F ^ 1] == 0 && g_wRDPTlut[0x20 ^ 1] == 0);
   /* no more than 256, from RDRAM's start where it wraps */
   memset(g_wRDPTlut, 0, sizeof(g_wRDPTlut));
   gDP.tiles[7].tmem = 0x100;
   ricegDPLoadTLUT(0, 7, 0, 0, 511, 0);
   CHECK(g_wRDPTlut[255 ^ 1] == pal[255 ^ 1] && g_wRDPTlut[256 ^ 1] == 0);
   g_TI.dwAddr = 0x6000 + RAM;
   memset(g_wRDPTlut, 0, sizeof(g_wRDPTlut));
   ricegDPLoadTLUT(0, 7, 0, 0, 3, 0);
   CHECK(g_wRDPTlut[0 ^ 1] == pal[0 ^ 1] && g_wRDPTlut[3 ^ 1] == pal[3 ^ 1]);
   g_TI.dwAddr = 0x6000;
   /* full TMEM: each entry the first halfword of a TMEM word, to TMEM's end */
   options.bUseFullTMEM = true;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   gDP.tiles[7].tmem = 0x1FC;
   ricegDPLoadTLUT(0, 7, 0, 0, 7, 0);
   CHECK(*(uint16_t *)&g_Tmem.g_Tmem64bit[0x1FC] == pal[0 ^ 1]);
   CHECK(*(uint16_t *)&g_Tmem.g_Tmem64bit[0x1FF] == pal[3 ^ 1]);
   CHECK(((uint16_t *)&g_Tmem.g_Tmem64bit[0x1FC])[1] == 0);
   options.bUseFullTMEM = false;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   ricegDPLoadTLUT(0, 7, 0, 0, 7, 0);
   CHECK(g_Tmem.g_Tmem64bit[0x1FC] == 0);
}

int main(void)
{
   gfx_info.RDRAM = ram;
   colours();
   fill_rect();
   load_block();
   load_tile();
   load_tlut();
   printf("rice RDP commands: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
