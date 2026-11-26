/* Rice's frame-buffer manager (mupen64plus-video-rice/src/FrameBuffer.c),
 * on a renderer of its own that records what it is asked:
 *
 *   * random runs of colour images set (at a few addresses, the depth
 *     image's among them, of each size and width, with the commands after
 *     them in RDRAM), render textures activated and closed, origins shown,
 *     RDRAM written over, the texture cache's questions, CPU reads, frames
 *     and settings changed: every step's state and every call to the
 *     renderer held to what FrameBuffer.cpp did as C++;
 *   * at their edges: the depth image found drawn into as a texture, a
 *     CI found a frame buffer by the VI's origins, a CI's height, small
 *     textures doubled, render textures next to a new CI kept, a CPU
 *     read saving a CI, the oldest slot reused (these held to the C++
 *     too);
 *   * a back buffer saved as a render texture holds zeros where it is not
 *     set, not what was on the stack;
 *   * render textures made and deleted pair up, and nothing is left at
 *     close.
 *
 * Built with RICE_FB_GOLDEN against the C++ manager it prints the runs'
 * hashes instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"
#include "FrameBuffer.h"
#include "GraphicsContext.h"
#include "RenderBase.h"
#include "RiceGLState.h"
#include "RSP_Parser.h"
#include "Video.h"
#include "../../Graphics/GBI.h"
#include "../../Graphics/RSP/RSP_state.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

#define RAM_SIZE 0x400000
static uint8_t ram[RAM_SIZE];

static uint32_t seed;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static uint32_t h;
static void fold(uint32_t v)
{
   int i;
   for (i = 0; i < 4; i++)
      h = (h ^ ((v >> (i * 8)) & 0xFF)) * 16777619u;
}
static void foldf(float f) { uint32_t v; memcpy(&v, &f, 4); fold(v); }

/* ---- what Rice gives the manager ---- */
ALIGN(16, RDP_Options gRDP);
PluginStatus status;
WindowSettingStruct windowSetting;
GlobalOptions options;
FrameBufferOptions frameBufferOptions;
RomOptions currentRomOptions;
RomOptions defaultRomOptions;
GFX_INFO gfx_info;
uint32_t g_dwRamSize = RAM_SIZE;
SetImgInfo g_CI;
SetImgInfo g_ZI;
RSPInfo __RSP;
static uint32_t vi_width_reg;

/* ---- a renderer of its own ---- */
#define MAX_RT 4096
static CRenderTexture rts[MAX_RT];
static CTexture rt_tex[MAX_RT];
static int rt_live[MAX_RT];
static int rt_made, rt_deleted, rt_fail_target;

static int rt_id(const CRenderTexture *rt) { return rt ? (int)(rt - rts) : -1; }

static bool rt_set_as_render_target(CRenderTexture *rt, bool enable)
{
   fold(0x5E7); fold(rt_id(rt)); fold(enable);
   CHECK(rt_live[rt_id(rt)]);
   if (enable && rt_fail_target)
      return false;
   rt->m_beingRendered = enable;
   return true;
}
static void rt_load_texture(CRenderTexture *rt, TxtrCacheEntry *pEntry)
{
   fold(0x10AD); fold(rt_id(rt)); fold(pEntry->ti.Address);
   CHECK(rt_live[rt_id(rt)]);
}
static void rt_store_to_rdram(CRenderTexture *rt, int infoIdx)
{
   fold(0x570E); fold(rt_id(rt)); fold(infoIdx);
}
static const CRenderTextureOps rt_ops = { rt_set_as_render_target, rt_load_texture, rt_store_to_rdram };

CRenderTexture *CRenderTexture_New(int width, int height, RenderTextureInfo *pInfo, TextureUsage usage)
{
   CRenderTexture *rt;
   CHECK(rt_made < MAX_RT);
   if (rt_made >= MAX_RT)
      exit(1);
   rt = &rts[rt_made];
   fold(0x4E3); fold(rt_made); fold(width); fold(height); fold((uint32_t)(pInfo - gRenderTextureInfos)); fold(usage);
   memset(rt, 0, sizeof(*rt));
   rt->ops = &rt_ops;
   rt->m_width = width;
   rt->m_height = height;
   rt->m_usage = usage;
   rt->m_pInfo = pInfo;
   rt->m_pTexture = &rt_tex[rt_made];
   rt_live[rt_made++] = 1;
   return rt;
}

void CRenderTexture_Delete(CRenderTexture *rt)
{
   fold(0xDE1); fold(rt_id(rt));
   CHECK(rt_live[rt_id(rt)]);
   rt_live[rt_id(rt)] = 0;
   rt_deleted++;
}

void RiceGC_UpdateFrame(void) { fold(0xF4A); }
void RiceGC_Clear(unsigned flags, uint32_t color, float depth) { fold(0xC1E); fold(flags); fold(color); foldf(depth); }
void RiceGL_UpdateClipRectangle(void)
{
   fold(0xC11); foldf(windowSetting.fMultX); foldf(windowSetting.fMultY); fold(status.bHandleN64RenderTexture);
   fold(newRenderTextureInfo.bufferWidth); fold(newRenderTextureInfo.bufferHeight);
}
void RiceGL_ApplyScissorWithClipRatio(bool force) { fold(0x5C1); fold(force); }

uint32_t CalculateRDRAMCRC(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
      uint32_t size, uint32_t pitchInBytes)
{
   const uint8_t *p = (const uint8_t *)pPhysicalAddress;
   uint32_t crc = 2166136261u, y, x, row = (width << size) >> 1;
   fold(0xC2C); fold((uint32_t)(p - ram)); fold(left); fold(top); fold(width); fold(height); fold(size); fold(pitchInBytes);
   for (y = 0; y < height; y += 7)
      for (x = 0; x < row; x += 13)
         if ((size_t)(p - ram) + y * pitchInBytes + x < RAM_SIZE)
            crc = (crc ^ p[y * pitchInBytes + x]) * 16777619u;
   return crc;
}

/* ---- the state the manager keeps ---- */
static void fold_info(const RenderTextureInfo *info)
{
   fold(rt_id(info->pRenderTexture));
   fold(info->CI_Info.dwFormat); fold(info->CI_Info.dwSize); fold(info->CI_Info.dwWidth); fold(info->CI_Info.dwAddr);
   fold(info->bufferWidth); fold(info->bufferHeight); fold(info->N64Width); fold(info->N64Height);
   foldf(info->scaleX); foldf(info->scaleY);
   fold(info->maxUsedHeight); fold(info->updateAtFrame); fold(info->updateAtUcodeCount);
   fold(info->isUsed); fold(info->knownHeight); fold(info->crcInRDRAM); fold(info->crcCheckedAtFrame);
   fold(info->txtEntry.pTexture ? (uint32_t)(info->txtEntry.pTexture - rt_tex) : 0xFFFFFFFFu);
   fold(info->txtEntry.txtrBufIdx);
}

static void fold_state(void)
{
   int i;
   for (i = 0; i < numOfTxtBufInfos; i++)
      fold_info(&gRenderTextureInfos[i]);
   fold_info(&newRenderTextureInfo);
   fold(g_pRenderTextureInfo ? (uint32_t)(g_pRenderTextureInfo - gRenderTextureInfos) : 0xFFFFFFFFu);
   for (i = 0; i < 5; i++)
   {
      const RecentCIInfo *c = &g_RecentCIInfo[i];
      fold((uint32_t)(g_uRecentCIInfoPtrs[i] - g_RecentCIInfo));
      fold(c->dwFormat); fold(c->dwSize); fold(c->dwWidth); fold(c->dwAddr); fold(c->dwLastWidth); fold(c->dwLastHeight);
      fold(c->dwHeight); fold(c->dwMemSize); fold(c->bCopied); fold(c->dwCopiedAtFrame); fold(c->lastUsedFrame);
      fold(c->bUsedByVIAtFrame); fold(c->lastSetAtUcode);
      fold(g_RecentVIOriginInfo[i].addr); fold(g_RecentVIOriginInfo[i].FrameCount);
   }
   fold(status.bCIBufferIsRendered); fold(status.bN64IsDrawingTextureBuffer); fold(status.bHandleN64RenderTexture);
   fold(status.bN64FrameBufferIsUsed); fold(status.bDirectWriteIntoRDRAM); fold(status.bFrameBufferIsDrawn);
   fold(status.bFrameBufferDrawnByTriangles); fold(status.curRenderBuffer); fold(status.curDisplayBuffer);
   fold(status.leftRendered); fold(status.topRendered); fold(status.rightRendered); fold(status.bottomRendered);
   fold(g_CI.dwAddr); fold(g_CI.dwFormat); fold(g_CI.dwSize); fold(g_CI.dwWidth); fold(g_CI.bpl);
   fold(frameBufferOptions.bFillRectNextTextureBuffer);
   foldf(windowSetting.fMultX); foldf(windowSetting.fMultY);
}

/* the stack below a call filled, for what is left in it unset */
static void dirty_stack(void)
{
   volatile uint8_t junk[16384];
   unsigned i;
   for (i = 0; i < sizeof(junk); i++)
      junk[i] = (uint8_t)(0xA5 ^ i);
}

/* ---- the runs ---- */
static const uint32_t addrs[] = { 0x100000, 0x125800, 0x200000, 0x300000, 0x3DA800, 0x080000, 0x100280, 0x3E0000 };
static const uint32_t widths[] = { 320, 64, 128, 512, 100, 240, 32, 640, 48, 200, 33, 65, 300 };
#define ZI_ADDR 0x300000
#define DL_PC   0x3F0000

/* mostly a few, now and then one of many, for every slot to fill */
static uint32_t pick_addr(void)
{
   if ((rnd() % 4) == 0)
      return 0x40000 + 0x8000 * (rnd() % 80);
   return addrs[rnd() % (sizeof(addrs) / sizeof(addrs[0]))];
}

/* at or by an edge of [base, base + size): one before, the first, the
   last, one past; or somewhere in it */
static uint32_t edge(uint32_t base, uint32_t size)
{
   switch (rnd() % 5)
   {
      case 0:  return base - 1;
      case 1:  return base;
      case 2:  return base + size - 1;
      case 3:  return base + size;
      default: return base + (size ? rnd() % size : 0);
   }
}

/* an address by a render texture's edges, by a recent CI's, or anywhere */
static uint32_t probe_addr(void)
{
   uint32_t k = rnd() % 3;
   if (k == 0)
   {
      const RenderTextureInfo *info = &gRenderTextureInfos[rnd() % numOfTxtBufInfos];
      uint32_t height = info->knownHeight ? info->N64Height : (uint32_t)info->maxUsedHeight;
      return edge(info->CI_Info.dwAddr, info->CI_Info.dwSize * info->N64Width * height);
   }
   if (k == 1)
   {
      const RecentCIInfo *c = g_uRecentCIInfoPtrs[rnd() % 5];
      return edge(c->dwAddr, c->dwMemSize);
   }
   return pick_addr() + rnd() % 0x10000;
}

/* a CI by a render texture's edges, for the ones it covers */
static uint32_t ci_addr(uint32_t width, uint32_t size)
{
   if ((rnd() % 3) == 0)
   {
      const RenderTextureInfo *info = &gRenderTextureInfos[rnd() % numOfTxtBufInfos];
      uint32_t memsize2 = ((info->N64Height * info->N64Width) >> 1) << info->CI_Info.dwSize;
      uint32_t memsize = ((240 * width) >> 1) << size;
      uint32_t a;
      switch (rnd() % 4)
      {
         case 0:  a = edge(info->CI_Info.dwAddr, memsize2); break;
         case 1:  a = edge(info->CI_Info.dwAddr - memsize, 1); break;
         case 2:  a = edge(info->CI_Info.dwAddr + memsize2 - memsize, 1); break;
         default: a = info->CI_Info.dwAddr; break;
      }
      return a & ~1u;
   }
   return (rnd() % 32) ? pick_addr() : 0;
}

/* the ten commands after the CI's, and two past them */
static void write_dl(uint32_t width)
{
   int i, none = (rnd() % 6) == 0;   /* none of those looked for */
   for (i = 0; i < 12; i++)
   {
      uint32_t *w = (uint32_t *)(ram + DL_PC + i * 8), x1, y1;
      if (none && i < 10)
      {
         w[0] = w[1] = 0;
         continue;
      }
      x1 = (rnd() & 1) ? width : width - 1;
      y1 = rnd() % 300;
      switch (rnd() % 8)
      {
         case 0:  w[0] = (uint32_t)G_SETSCISSOR << 24;  w[1] = (rnd() % 1200); break;
         case 1:  w[0] = (uint32_t)G_SETFILLCOLOR << 24; w[1] = (rnd() & 1) ? 0xFFFCFFFC : rnd(); break;
         case 2:
         case 3:  w[0] = ((uint32_t)G_FILLRECT << 24) | ((x1 * 4) << 12) | (y1 * 4);
                  w[1] = (rnd() % 4) ? 0 : ((rnd() & 1) ? (((rnd() % 8) * 4) << 12) : (rnd() % 8) * 4); break;
         case 4:  w[0] = (uint32_t)G_TEXRECT << 24; w[1] = 0; break;
         case 5:  w[0] = (uint32_t)G_SETCIMG << 24; w[1] = 0; break;
         default: w[0] = 0; w[1] = 0; break;
      }
   }
}

static void settings(void)
{
   static const int hacks[] = { NO_HACK_FOR_GAME, HACK_FOR_MARIO_TENNIS, HACK_FOR_CONKER, HACK_FOR_BANJO_TOOIE };
   currentRomOptions.N64RenderToTextureEmuType = (rnd() % 4) ? TXT_BUF_IGNORE : TXT_BUF_NONE;
   currentRomOptions.screenUpdateSetting = (rnd() & 1) ? SCREEN_UPDATE_AT_CI_CHANGE : SCREEN_UPDATE_AT_VI_UPDATE;
   options.enableHackForGames = hacks[rnd() % 4];
   defaultRomOptions.bInN64Resolution = (rnd() % 3) == 0;
   defaultRomOptions.bDoubleSizeForSmallTxtrBuf = rnd() & 1;
   frameBufferOptions.bWriteBackBufToRDRAM = (rnd() % 4) == 0;
   frameBufferOptions.bRenderTextureWriteBack = (rnd() % 3) == 0;
   frameBufferOptions.bAtEachFrameUpdate = rnd() & 1;
   frameBufferOptions.bProcessCPURead = rnd() & 1;
   status.dwTvSystem = (rnd() & 1) ? TV_SYSTEM_PAL : TV_SYSTEM_NTSC;
   rt_fail_target = (rnd() % 8) == 0;
}

static uint32_t run(uint32_t s, int steps)
{
   int n;

   seed = s;
   h = 2166136261u;
   memset(ram, 0, sizeof(ram));
   for (n = 0; n < 0x10000; n++)
      ram[rnd() % RAM_SIZE] = (uint8_t)rnd();
   memset(&status, 0, sizeof(status));
   memset(&g_CI, 0, sizeof(g_CI));
   memset(g_RecentCIInfo, 0, sizeof(RecentCIInfo) * 5);
   memset(g_RecentVIOriginInfo, 0, sizeof(RecentViOriginInfo) * 5);
   memset(&newRenderTextureInfo, 0, sizeof(newRenderTextureInfo));
   g_pRenderTextureInfo = NULL;
   g_ZI.dwAddr = ZI_ADDR; g_ZI.dwFormat = G_IM_FMT_RGBA; g_ZI.dwSize = G_IM_SIZ_16b; g_ZI.dwWidth = 320;
   windowSetting.uViWidth = 320; windowSetting.uViHeight = 240;
   windowSetting.fViWidth = 320; windowSetting.fViHeight = 240;
   windowSetting.uDisplayWidth = 640; windowSetting.uDisplayHeight = 480;
   windowSetting.fMultX = windowSetting.fMultY = 2.0f;
   vi_width_reg = 320;
   status.gDlistCount = 1;
   settings();
   RiceFB_Initialize();

   for (n = 0; n < steps; n++)
   {
      uint32_t op = rnd() % 32;
      if (op < 7)
      {
         SetImgInfo ci;
         ci.dwFormat = (rnd() % 6) ? G_IM_FMT_RGBA : G_IM_FMT_I;
         ci.dwSize   = (rnd() % 5) ? G_IM_SIZ_16b : ((rnd() & 1) ? G_IM_SIZ_8b : G_IM_SIZ_32b);
         ci.dwWidth  = widths[rnd() % (sizeof(widths) / sizeof(widths[0]))];
         ci.dwAddr   = ci_addr(ci.dwWidth, ci.dwSize);
         ci.bpl      = ci.dwWidth << ci.dwSize >> 1;
         write_dl(ci.dwWidth);
         gRDP.scissor.left   = (rnd() % 4) ? 0 : 8;
         gRDP.scissor.top    = (rnd() % 4) ? 0 : 8;
         gRDP.scissor.right  = (rnd() & 1) ? (int)ci.dwWidth : 320;
         gRDP.scissor.bottom = (rnd() % 5) ? 240 : ((rnd() & 1) ? (int)(ci.dwWidth * 3 / 4) : (int)(rnd() % 300));
         gRDP.fillColor      = (rnd() & 1) ? 0x00FFFFF7 : rnd();
         if (rnd() & 1)
            status.bCIBufferIsRendered = true;
         RiceFB_SetCIAddr(&ci);
      }
      else if (op < 12)
      {
         RiceFB_ActiveTextureBuffer();
         /* drawn into, as far down as the draws reach */
         if (g_pRenderTextureInfo && (rnd() & 1))
            g_pRenderTextureInfo->maxUsedHeight = rnd() % 300;
         status.bFrameBufferIsDrawn = (rnd() % 4) != 0;
         status.bFrameBufferDrawnByTriangles = (rnd() % 4) != 0;
      }
      else if (op < 14)
         RiceFB_CloseRenderTexture(rnd() & 1);
      else if (op < 16)
      {
         /* by a recent CI: 2 widths on, within or by its first 4KB, or
            some widths on */
         const RecentCIInfo *c = g_uRecentCIInfoPtrs[rnd() % 5];
         uint32_t a;
         switch (rnd() % 4)
         {
            case 0:  a = c->dwAddr + 2 * vi_width_reg; break;
            case 1:  a = edge(c->dwAddr, 0x1000); break;
            case 2:  a = c->dwAddr + (1 + rnd() % 6) * c->dwWidth; break;
            default: a = pick_addr() + rnd() % 0x2000; break;
         }
         RiceFB_SetAddrBeDisplayed(a);
      }
      else if (op < 18)
      {
         int i;
         for (i = 0; i < 64; i++)
            ram[pick_addr() + rnd() % 0x20000] = (uint8_t)rnd();
         RiceFB_CheckRenderTextureCRCInRDRAM();
      }
      else if (op < 20)
         fold(RiceFB_CheckAddrInRenderTextures(probe_addr()));
      else if (op < 22)
         fold(RiceFB_CheckAddrInBackBuffers(probe_addr(), rnd() % 0x1000));
      else if (op < 23)
         RiceFB_FrameBufferReadByCPU((rnd() & 1) ? ZI_ADDR + rnd() % 0x10000 : probe_addr());
      else if (op < 24)
      {
         TxtrCacheEntry e;
         memset(&e, 0, sizeof(e));
         e.ti.Address = probe_addr();
         RiceFB_LoadTextureFromRenderTexture(&e, (int)(rnd() % 24) - 2);
      }
      else if (op < 25)
      {
         TxtrInfo ti;
         int idx = rnd() % 20;
         TxtrCacheEntry *e;
         memset(&ti, 0, sizeof(ti));
         ti.Format = (rnd() % 4) ? G_IM_FMT_RGBA : G_IM_FMT_I;
         ti.Size = (rnd() % 4) ? G_IM_SIZ_16b : G_IM_SIZ_8b;
         e = RiceFB_RenderTextureEntry(idx, &ti);
         fold(e ? (uint32_t)(e - &gRenderTextureInfos[idx].txtEntry) : 0xFFFFFFFFu);
         if (g_pRenderTextureInfo)
            fold(RiceFB_CIFormat());
         if (e)
         {
            fold(e->ti.Format); fold(e->ti.Size);
            fold(RiceFB_RenderTextureCRC(idx));
            fold(RiceFB_RenderTextureUpdatedAt(idx));
         }
      }
      else if (op < 26)
         settings();
      else if (op < 27)
      {
         /* to a frame some frames on from one recorded: by the 3 and 20
            the manager counts to */
         static const uint32_t ons[] = { 3, 4, 19, 20, 21 };
         uint32_t from = (rnd() & 1) ? g_uRecentCIInfoPtrs[rnd() % 5]->lastUsedFrame
                       : (rnd() & 1) ? g_uRecentCIInfoPtrs[rnd() % 5]->bUsedByVIAtFrame
                       : g_RecentVIOriginInfo[rnd() % 5].FrameCount;
         from += ons[rnd() % 5];
         if (from > status.gDlistCount)
            status.gDlistCount = from;
      }
      else if (op < 30)
      {
         status.gDlistCount += 1 + (rnd() % 2) * (rnd() % 30);
         status.gUcodeCount += 1 + rnd() % 3;
         status.bDirectWriteIntoRDRAM = (rnd() % 16) == 0;
      }
      else if (op < 31)
         status.gUcodeCount++;
      else if ((rnd() % 16) == 0)
      {
         RiceFB_CloseUp();
         RiceFB_Initialize();
      }
      fold_state();
   }

   RiceFB_CloseUp();
   fold(rt_made);
   return h;
}


/* ---- directed ---- */

/* the commands after the CI: up to four, NOOPs past them */
static void set_dl(uint32_t w0a, uint32_t w1a, uint32_t w0b, uint32_t w1b, uint32_t w0c, uint32_t w1c,
      uint32_t w0d, uint32_t w1d)
{
   uint32_t *w = (uint32_t *)(ram + DL_PC);
   memset(ram + DL_PC, 0, 12 * 8);
   w[0] = w0a; w[1] = w1a; w[2] = w0b; w[3] = w1b; w[4] = w0c; w[5] = w1c; w[6] = w0d; w[7] = w1d;
}
#define FILLRECT_W0(x1, y1) (((uint32_t)G_FILLRECT << 24) | (((x1) * 4) << 12) | ((y1) * 4))
#define FILLRECT_W1(x0, y0) ((((x0) * 4) << 12) | ((y0) * 4))
#define CMD(c)              ((uint32_t)(c) << 24)

/* a frame well on, nothing recorded, render textures on (the commands
   after the CI left as they are) */
static void fresh(void)
{
   seed = 11;
   memset(&status, 0, sizeof(status));
   memset(g_RecentCIInfo, 0, sizeof(RecentCIInfo) * 5);
   memset(g_RecentVIOriginInfo, 0, sizeof(RecentViOriginInfo) * 5);
   memset(&g_CI, 0, sizeof(g_CI));
   status.gDlistCount = 100;
   status.dwTvSystem = TV_SYSTEM_NTSC;
   currentRomOptions.N64RenderToTextureEmuType = TXT_BUF_IGNORE;
   currentRomOptions.screenUpdateSetting = SCREEN_UPDATE_AT_VI_UPDATE;
   options.enableHackForGames = NO_HACK_FOR_GAME;
   defaultRomOptions.bInN64Resolution = false;
   defaultRomOptions.bDoubleSizeForSmallTxtrBuf = false;
   frameBufferOptions.bWriteBackBufToRDRAM = false;
   frameBufferOptions.bRenderTextureWriteBack = false;
   frameBufferOptions.bAtEachFrameUpdate = false;
   frameBufferOptions.bProcessCPURead = false;
   rt_fail_target = 0;
   gRDP.scissor.left = gRDP.scissor.top = 0;
   gRDP.scissor.right = 320;
   gRDP.scissor.bottom = 240;
   gRDP.fillColor = 0;
   RiceFB_Initialize();
}

static void set_ci(uint32_t addr, uint32_t width)
{
   SetImgInfo ci;
   ci.dwAddr = addr;
   ci.dwFormat = G_IM_FMT_RGBA;
   ci.dwSize = G_IM_SIZ_16b;
   ci.dwWidth = width;
   ci.bpl = width * 2;
   RiceFB_SetCIAddr(&ci);
}

/* the depth image set as the CI: drawn into as a texture, from the
   commands after it */
static bool di_is_texture(uint32_t fill)
{
   fresh();
   gRDP.fillColor = fill;
   g_CI.dwAddr = 0x100000;
   set_ci(ZI_ADDR, 320);
   return status.bN64IsDrawingTextureBuffer;
}

static void directed(void)
{
   /* the depth image as a texture: from a fill colour, a full fill and
      the next CI, in the first ten commands */
   set_dl(0, 0, 0, 0, 0, 0, 0, 0);
   CHECK(di_is_texture(0));
   set_dl(CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0, 0, 0);
   CHECK(!di_is_texture(0));
   set_dl(CMD(G_SETSCISSOR), 0, CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(!di_is_texture(0));
   set_dl(CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_TEXRECT), 0, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(di_is_texture(0));
   set_dl(CMD(G_SETCIMG), 0, CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(di_is_texture(0));
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
   CHECK(!di_is_texture(0x00FFFFF7));
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
   CHECK(di_is_texture(0x12345678));
   set_dl(FILLRECT_W0(319, 240), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
   CHECK(!di_is_texture(0x00FFFFF7));
   set_dl(FILLRECT_W0(318, 240), FILLRECT_W1(0, 0), CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(!di_is_texture(0x12345678));
   set_dl(CMD(G_SETFILLCOLOR), 0x1234, FILLRECT_W0(320, 240), FILLRECT_W1(0, 0), 0, 0, 0, 0);
   CHECK(di_is_texture(0x00FFFFF7));
   set_dl(CMD(G_SETFILLCOLOR), 0xFFFCFFFC, FILLRECT_W0(320, 240), FILLRECT_W1(0, 0), 0, 0, 0, 0);
   CHECK(!di_is_texture(0x12345678));
   /* not a full fill: from x 1 or y 1, so the fill colour and CI decide */
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(1, 0), CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(!di_is_texture(0x12345678));
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(0, 1), CMD(G_SETFILLCOLOR), 0xFFFCFFFC, CMD(G_SETCIMG), 0, 0, 0);
   CHECK(!di_is_texture(0x12345678));
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(1, 0), 0, 0, 0, 0, 0, 0);
   CHECK(di_is_texture(0x00FFFFF7));
   set_dl(FILLRECT_W0(320, 240), FILLRECT_W1(0, 1), 0, 0, 0, 0, 0, 0);
   CHECK(di_is_texture(0x00FFFFF7));

   /* a CI shown by the VI up to four of its widths on is a frame buffer,
      not five on; nor once 20 frames have gone */
   {
      static const struct { uint32_t widths, frames; bool texture; } c[] = {
         { 4, 19, false }, { 5, 19, true }, { 4, 20, true }, { 1, 0, false } };
      unsigned k;
      set_dl(0, 0, 0, 0, 0, 0, 0, 0);
      for (k = 0; k < sizeof(c) / sizeof(c[0]); k++)
      {
         fresh();
         RiceFB_SetAddrBeDisplayed(0x100000 + c[k].widths * 320);
         status.gDlistCount += c[k].frames;
         set_ci(0x100000, 320);
         CHECK(status.bN64IsDrawingTextureBuffer == c[k].texture);
      }
      /* a CI seen: shown less than 20 frames ago */
      for (k = 19; k <= 20; k++)
      {
         fresh();
         RiceFB_SetAddrBeDisplayed(0x100000 + 640);   /* a frame buffer */
         set_ci(0x100000, 320);
         RiceFB_ActiveTextureBuffer();     /* into the recent CIs */
         RiceFB_SetAddrBeDisplayed(0x100000);
         status.gDlistCount += k;
         set_ci(0x100000, 320);
         CHECK(status.bN64IsDrawingTextureBuffer == (k == 20));
      }
   }

   /* the CI's height: from a full fill, the scissor, or a guess for
      NTSC and PAL, held to the scissor and to the end of RDRAM */
   {
      static const struct { uint32_t addr, w, pal, fill, sl, sb, height; } c[] = {
         { 0x100000, 64, 0, 64,  0, 240, 64 },
         { 0x100000, 64, 0, 0,   0, 40,  40 },
         { 0x100000, 64, 0, 0,   8, 240, 48 },
         { 0x100000, 64, 1, 0,   8, 240, 52 },
         { 0x100000, 64, 0, 0,   8, 30,  30 },
         { 0x100000, 64, 0, 0,   8, 1,   1 },
         /* past the end: what is left over the width, in pixels */
         { RAM_SIZE - 64 * 2 * 20, 64, 0, 0, 8, 240, 40 },
         { RAM_SIZE - 64 * 2 * 30, 64, 1, 0, 8, 240, 60 },
         { RAM_SIZE - 64 * 2 * 60, 64, 1, 0, 8, 240, 52 },
      };
      unsigned k;
      for (k = 0; k < sizeof(c) / sizeof(c[0]); k++)
      {
         fresh();
         status.dwTvSystem = c[k].pal ? TV_SYSTEM_PAL : TV_SYSTEM_NTSC;
         gRDP.scissor.left = c[k].sl;
         gRDP.scissor.right = 64;
         gRDP.scissor.bottom = c[k].sb;
         set_dl(c[k].fill ? FILLRECT_W0(64, c[k].fill) : 0, 0, 0, 0, 0, 0, 0, 0);
         set_ci(c[k].addr, c[k].w);
         CHECK(status.bHandleN64RenderTexture);
         CHECK(newRenderTextureInfo.N64Height == c[k].height);
      }
      /* small ones doubled, up to 128 by 128 */
      for (k = 127; k <= 129; k++)
      {
         fresh();
         defaultRomOptions.bDoubleSizeForSmallTxtrBuf = true;
         set_dl(FILLRECT_W0(128, k), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
         set_ci(0x100000, 128);
         CHECK(newRenderTextureInfo.bufferWidth == (k <= 128 ? 256u : 128u));
         fresh();
         defaultRomOptions.bDoubleSizeForSmallTxtrBuf = true;
         set_dl(FILLRECT_W0(k, 64), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
         set_ci(0x100000, k);
         CHECK(newRenderTextureInfo.bufferHeight == (k <= 128 ? 128u : 64u));
      }
   }

   /* a render texture next to a new CI, before or after it, is kept; one
      it overlaps by a byte is not */
   {
      static const struct { int32_t at; bool kept; } c[] = {
         { 64 * 64 * 2, true }, { -64 * 64 * 2, true }, { 64 * 64 * 2 - 2, false }, { -64 * 64 * 2 + 2, false } };
      unsigned k;
      for (k = 0; k < sizeof(c) / sizeof(c[0]); k++)
      {
         fresh();
         set_dl(FILLRECT_W0(64, 64), FILLRECT_W1(0, 0), 0, 0, 0, 0, 0, 0);
         set_ci(0x100000, 64);
         RiceFB_ActiveTextureBuffer();
         status.bFrameBufferIsDrawn = status.bFrameBufferDrawnByTriangles = true;
         CHECK(gRenderTextureInfos[0].isUsed);
         set_ci(0x100000 + c[k].at, 64);
         RiceFB_ActiveTextureBuffer();
         CHECK(gRenderTextureInfos[0].isUsed == c[k].kept);
         RiceFB_CloseUp();
      }
   }

   /* a CPU read of a CI used up to three frames ago saves it, not four */
   {
      uint32_t k;
      set_dl(0, 0, 0, 0, 0, 0, 0, 0);
      for (k = 3; k <= 4; k++)
      {
         fresh();
         frameBufferOptions.bProcessCPURead = true;
         frameBufferOptions.bWriteBackBufToRDRAM = true;
         RiceFB_SetAddrBeDisplayed(0x100000 + 640);   /* a frame buffer */
         set_ci(0x100000, 320);
         RiceFB_ActiveTextureBuffer();
         status.gDlistCount += k;
         RiceFB_FrameBufferReadByCPU(0x100000 + 0x100);
         CHECK(g_uRecentCIInfoPtrs[0]->bCopied == (k == 3));
      }
   }
}

#define RUNS 24

#ifdef RICE_FB_GOLDEN
int main(void)
{
   int r;
   gfx_info.RDRAM = ram;
   gfx_info.VI_WIDTH_REG = &vi_width_reg;
   __RSP.PCi = 0;
   __RSP.PC[0] = DL_PC;
   for (r = 0; r < RUNS; r++)
   {
      memset(rt_live, 0, sizeof(rt_live));
      rt_made = rt_deleted = 0;
      printf("   0x%08xu,%s", (unsigned)run(0x9E3779B9u * (r + 1), 600), (r % 4 == 3) ? "\n" : "");
   }
   directed();
   return failures != 0;
}
#else

static const uint32_t golden[RUNS] = {
  0xa0ad05c2u,   0xc0060a9bu,   0xade0d3fbu,   0x7dae231du,
  0x053c6d6du,   0x209a3799u,   0xaf4231fau,   0xbebe48a0u,
  0xfc7008cfu,   0xdb230fe1u,   0xcf7bd02bu,   0xf4783fdfu,
  0x345adb48u,   0x02b2b40cu,   0xdb5ae084u,   0x496b1341u,
  0x58eb89efu,   0xdfdf5f4eu,   0x1a96c3fdu,   0x6c0135cdu,
  0xdf3df7a0u,   0x25438c2du,   0xebe01619u,   0xe4e4072bu
};

int main(void)
{
   int r, i, live;

   gfx_info.RDRAM = ram;
   gfx_info.VI_WIDTH_REG = &vi_width_reg;
   __RSP.PCi = 0;
   __RSP.PC[0] = DL_PC;

   for (r = 0; r < RUNS; r++)
   {
      uint32_t got;
      memset(rt_live, 0, sizeof(rt_live));
      rt_made = rt_deleted = 0;
      got = run(0x9E3779B9u * (r + 1), 600);
      if (got != golden[r])
      {
         printf("FAILED run %d: 0x%08x, the C++ gave 0x%08x\n", r, (unsigned)got, (unsigned)golden[r]);
         failures++;
      }
      /* made and deleted pair up, nothing left at close */
      for (live = 0, i = 0; i < rt_made; i++)
         live += rt_live[i];
      CHECK(live == 0);
      CHECK(rt_made == rt_deleted);
      CHECK(rt_made > 0);
   }

   /* every slot used in one frame: the oldest given to the next (the first
      of two as old), its texture deleted */
   {
    int oldest;
    for (oldest = 5; oldest >= 0; oldest -= 5)
    {
      SetImgInfo ci;
      int k, before;
      seed = 3;
      memset(&status, 0, sizeof(status));
      status.gDlistCount = 50;
      settings();
      currentRomOptions.N64RenderToTextureEmuType = TXT_BUF_IGNORE;
      options.enableHackForGames = NO_HACK_FOR_GAME;
      frameBufferOptions.bRenderTextureWriteBack = false;
      rt_fail_target = 0;
      memset(ram + DL_PC, 0, 80);
      RiceFB_Initialize();
      for (k = 0; k <= numOfTxtBufInfos; k++)
      {
         ci.dwAddr = 0x10000 + k * 0x30000;
         ci.dwFormat = G_IM_FMT_RGBA;
         ci.dwSize = G_IM_SIZ_16b;
         ci.dwWidth = 64;
         ci.bpl = 128;
         status.gUcodeCount = (k == oldest || k == 9) ? 50 : 100 + k;
         before = rt_deleted;
         RiceFB_SetCIAddr(&ci);
         RiceFB_ActiveTextureBuffer();
         status.bFrameBufferIsDrawn = status.bFrameBufferDrawnByTriangles = true;
         if (k < numOfTxtBufInfos)
            CHECK(gRenderTextureInfos[k].isUsed && gRenderTextureInfos[k].CI_Info.dwAddr == ci.dwAddr);
      }
      CHECK(gRenderTextureInfos[oldest].CI_Info.dwAddr == 0x10000 + numOfTxtBufInfos * 0x30000);
      CHECK(gRenderTextureInfos[9].CI_Info.dwAddr == 0x10000 + 9 * 0x30000);
      CHECK(oldest == 0 || gRenderTextureInfos[0].CI_Info.dwAddr == 0x10000);
      CHECK(rt_deleted == before + 1);
      CHECK(g_pRenderTextureInfo == &gRenderTextureInfos[oldest]);
      RiceFB_CloseUp();
    }
   }

   directed();

   /* a back buffer saved: zeros where it is not set, not the stack's */
   {
      SetImgInfo ci;
      int i, found = 0;
      seed = 7;
      memset(&status, 0, sizeof(status));
      status.gDlistCount = 10;
      settings();
      currentRomOptions.N64RenderToTextureEmuType = TXT_BUF_NONE;
      frameBufferOptions.bWriteBackBufToRDRAM = false;
      options.enableHackForGames = NO_HACK_FOR_GAME;
      RiceFB_Initialize();
      g_CI.dwAddr = ZI_ADDR;
      g_CI.dwFormat = G_IM_FMT_I;     /* the saved one's format, kept */
      g_CI.dwSize = G_IM_SIZ_16b;
      g_CI.dwWidth = 320;
      g_CI.bpl = 640;
      ci = g_ZI;
      ci.dwAddr = 0x100000;
      g_uRecentCIInfoPtrs[0]->bCopied = false;
      status.bCIBufferIsRendered = true;
      dirty_stack();
      RiceFB_SetCIAddr(&ci);
      for (i = 0; i < numOfTxtBufInfos; i++)
      {
         if (gRenderTextureInfos[i].isUsed)
         {
            found = 1;
            CHECK(gRenderTextureInfos[i].pRenderTexture != NULL);
            CHECK(gRenderTextureInfos[i].pRenderTexture->m_usage == AS_BACK_BUFFER_SAVE);
            CHECK(gRenderTextureInfos[i].txtEntry.pTexture == gRenderTextureInfos[i].pRenderTexture->m_pTexture);
            CHECK(gRenderTextureInfos[i].txtEntry.txtrBufIdx == i + 1);
            CHECK(gRenderTextureInfos[i].txtEntry.dwCRC == 0);
            CHECK(gRenderTextureInfos[i].CI_Info.bpl == 0);
            CHECK(gRenderTextureInfos[i].CI_Info.dwFormat == G_IM_FMT_I);
            CHECK(gRenderTextureInfos[i].CI_Info.dwAddr == ZI_ADDR);
            CHECK(gRenderTextureInfos[i].txtEntry.ti.Address == 0);
         }
      }
      CHECK(found);

      /* saved again: the slot at its address dropped for a new one, not
         taken over */
      {
         int made = rt_made, deleted = rt_deleted;
         g_CI.dwAddr = ZI_ADDR;
         g_CI.dwFormat = G_IM_FMT_I;
         g_uRecentCIInfoPtrs[0]->bCopied = false;
         status.bCIBufferIsRendered = true;
         RiceFB_SetCIAddr(&ci);
         CHECK(rt_made == made + 1);
         CHECK(rt_deleted == deleted + 1);
      }
      RiceFB_CloseUp();
   }

   if (failures)
      return 1;
   printf("rice frame buffers: ok\n");
   return 0;
}
#endif
