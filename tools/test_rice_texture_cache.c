/* Rice's texture cache (mupen64plus-video-rice/src/TextureManager.c), on a
 * renderer of its own that counts what it is asked:
 *
 *   * a texture converted once and given back while its texels and the
 *     palette entries it uses are the same; converted again into the same
 *     entry where a texel or a used entry changes, not where an unused
 *     entry does; every time where CRCs are not checked (by the caller or
 *     the game's settings); the CRC kept within one RDP time;
 *   * the palette's entries where the conversion reads them: in RDRAM,
 *     in Rice's copy of TLUT, in TMEM; of CI, RGBA and, in a TLUT mode, I;
 *   * textures of addresses in one hash bucket kept apart;
 *   * the texels past those loaded clamped, mirrored or wrapped, each way,
 *     as the tile says, and on the GPU once with them;
 *   * a texture made smaller than asked loaded to the size made;
 *   * render textures: given back as they are when of the texture's format
 *     and size, loaded from otherwise (as intensity for I and IA colour
 *     images), and their CRC and frame used;
 *   * textures unused five seconds let go, but not those bound, those used
 *     since, or others in their bucket; all let go on recycling and clean
 *     up (the colour textures too), nothing left behind;
 *   * the colour textures: black, the primitive, environment and LOD
 *     fraction colours, made again only when the colour changes;
 *   * CRCs and the highest palette index of texels in RDRAM over random
 *     texels, each size, pitch and offset, fast and slow, held to what they
 *     gave as C++ in FrameBuffer.cpp; and nothing read out of RDRAM,
 *     counted from the first texel read.
 *
 * Built with RICE_CACHE_GOLDEN against the C++ CRCs it prints their hashes
 * instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RICE_CACHE_GOLDEN
#include <stdint.h>
#include <boolean.h>
#define G_IM_SIZ_4b  0
#define G_IM_SIZ_8b  1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3
uint32_t CalculateRDRAMCRC(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
      uint32_t size, uint32_t pitchInBytes);
unsigned char CalculateMaxCI(void *pPhysicalAddress, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
      uint32_t size, uint32_t pitchInBytes);
extern struct { void *RDRAM; } gfx_info;
extern uint32_t g_dwRamSize;
extern struct { bool bFastTexCRC; } currentRomOptions;
extern struct { bool bLoadHiResTextures; } options;
#else
#include "Config.h"
#include "ConvertImage.h"
#include "CombinerDefs.h"
#include "RenderBase.h"
#include "TextureManager.h"
#include "Video.h"
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* RDRAM: room for the widest, highest texture the CRCs are given */
static uint8_t ram[0x100000];

static uint32_t seed;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static uint32_t fnv(uint32_t h, uint32_t v)
{
   int i;
   for (i = 0; i < 4; i++)
      h = (h ^ ((v >> (i * 8)) & 0xFF)) * 16777619u;
   return h;
}

/* CRCs and the highest index over random texels: each size, pitch, offset,
   width and height, the fast CRC and the slow */
static uint32_t crcs(int maxci)
{
   uint32_t h = 2166136261u;
   unsigned i, n;
   seed = 1;
   for (n = 0; n < 4000; n++)
   {
      /* now and then wide or high enough for the fast CRC's longest steps */
      uint32_t size = n & 3, w = 1 + rnd() % ((n & 32) ? 640 : 80), hgt = 1 + rnd() % ((n & 64) ? 160 : 48);
      uint32_t left = rnd() % 16, top = rnd() % 8;
      uint32_t pitch = (((w + left) << size) + 1) / 2 + (rnd() % 4) * 4;
      seed = 0x51ED270Bu * (n + 1);
      for (i = 0; i < 0xC000; i++)
         ram[i] = (uint8_t)rnd();
      if (maxci)
      {
         /* small indices, and a few larger where the texels may or may
            not reach */
         for (i = 0; i < 0xC000; i++)
            ram[i] &= (n & 8) ? 0x33 : 0x03;
         for (i = 0; i < 3; i++)
            ram[64 + rnd() % 0x4000] = (uint8_t)(0x40 + rnd() % 0xC0);
      }
      pitch = (pitch + 3) & ~3u;
      currentRomOptions.bFastTexCRC = (n & 4) != 0;
      options.bLoadHiResTextures    = (n & 8) != 0 && (n & 16) != 0;
      if (maxci)
      {
         if (size == G_IM_SIZ_4b || size == G_IM_SIZ_8b)
            h = fnv(h, CalculateMaxCI(ram + 64, left, top, w, hgt, size, pitch));
      }
      else
         h = fnv(h, CalculateRDRAMCRC(ram + 64, left, top, w, hgt, size, pitch));
   }
   return h;
}

#ifdef RICE_CACHE_GOLDEN
int main(void)
{
   gfx_info.RDRAM = ram;
   g_dwRamSize = sizeof(ram);
   printf("#define CRC_HASH   0x%08xu\n", (unsigned)crcs(0));
   printf("#define MAXCI_HASH 0x%08xu\n", (unsigned)crcs(1));
   return 0;
}
#else

#define CRC_HASH   0x5fbec6e9u
#define MAXCI_HASH 0xec3503b2u

/* ---- what Rice gives the cache ---- */
ALIGN(16, RDP_Options gRDP);
RenderTexture g_textures[MAX_TEXTURES];
PluginStatus status;
GlobalOptions options;
FrameBufferOptions frameBufferOptions;
RomOptions currentRomOptions;
RomOptions defaultRomOptions;
GameSetting g_curRomInfo;
GFX_INFO gfx_info;
uint32_t g_dwRamSize = sizeof(ram);
struct gDPInfo gDP;
TmemType g_Tmem;
bool conkerSwapHack;
int g_convk0, g_convk1, g_convk2, g_convk3, g_convk4, g_convk5;
float g_convc0, g_convc1, g_convc2, g_convc3, g_convc4, g_convc5;
bool RiceConfig_UseFullTMEM(void) { return options.bUseFullTMEM; }
uint32_t RiceRDP_TextTlut(void) { return gRDP.otherMode.text_tlut; }

/* ---- a renderer of its own ---- */
static int uploads, releases, made, made_cap = 1024;
static int sixteen;      /* the renderer's textures A4R4G4B4, not A8R8G8B8 */
#define SLACK 64         /* past each texture's pixels, to stay untouched */

static size_t bytes_of(const CTexture *tex)
{
   return tex->m_dwCreatedTextureWidth * tex->m_dwCreatedTextureHeight * (sixteen ? 2 : 4);
}

static void count_upload(CTexture *tex) { (void)tex; uploads++; }
static void count_release(CTexture *tex)
{
   const uint8_t *past = (const uint8_t *)tex->m_pTexture + bytes_of(tex);
   int i, ok = 1;
   for (i = 0; i < SLACK; i++)
      ok &= past[i] == (uint8_t)(i * 37 + 11);
   CHECK(ok);   /* nothing written past the pixels */
   releases++;
}
static const CTextureOps counting = { count_upload, count_release };

CTexture *RiceDevice_CreateTexture(uint32_t width, uint32_t height)
{
   CTexture *tex = (CTexture *)malloc(sizeof(*tex));
   uint32_t w, h;
   CTexture_Init(tex, &counting, width, height, AS_NORMAL);
   for (w = 1; w < width && w < (uint32_t)made_cap; w <<= 1);
   for (h = 1; h < height && h < (uint32_t)made_cap; h <<= 1);
   tex->m_dwCreatedTextureWidth  = w;
   tex->m_dwCreatedTextureHeight = h;
   tex->m_dwTextureFmt = sixteen ? TEXTURE_FMT_A4R4G4B4 : TEXTURE_FMT_A8R8G8B8;
   tex->m_pTexture = malloc(bytes_of(tex) + SLACK);
   memset(tex->m_pTexture, 0xCD, bytes_of(tex));
   {
      /* not grey: what would turn intensity changes */
      int i;
      for (i = 0; i < SLACK; i++)
         ((uint8_t *)tex->m_pTexture)[bytes_of(tex) + i] = (uint8_t)(i * 37 + 11);
   }
   made++;
   return tex;
}

/* render textures: one, at @rt_addr, of @rt_fmt and size, drawn at frame
   @rt_frame with CRC @rt_crc */
static int rt_idx = -1, rt_loads, rt_after_backbuf;
static uint32_t rt_addr, rt_fmt, rt_siz, rt_crc, rt_frame, ci_fmt, bb_memsize;
static TxtrCacheEntry rt_entry;
/* with @rt_after_backbuf, a render texture only once the back buffers
   were looked at (one made of a back buffer) */
int RiceFB_CheckAddrInRenderTextures(uint32_t addr)
{
   if (rt_after_backbuf == 1)
      return -1;
   return addr == rt_addr ? rt_idx : -1;
}
int RiceFB_CheckAddrInBackBuffers(uint32_t addr, uint32_t memsize)
{
   bb_memsize = memsize;
   if (rt_after_backbuf)
      rt_after_backbuf = 2;
   return addr == rt_addr ? rt_idx : -1;
}
TxtrCacheEntry *RiceFB_RenderTextureEntry(int idx, const TxtrInfo *ti)
{
   CHECK(idx == rt_idx);
   if (ti->Format != rt_fmt || ti->Size != rt_siz)
      return NULL;
   rt_entry.ti = *ti;
   return &rt_entry;
}
uint32_t RiceFB_RenderTextureCRC(int idx) { (void)idx; return rt_crc; }
uint32_t RiceFB_RenderTextureUpdatedAt(int idx) { (void)idx; return rt_frame; }
uint32_t RiceFB_CIFormat(void) { return ci_fmt; }
void RiceFB_LoadTextureFromRenderTexture(TxtrCacheEntry *pEntry, int idx)
{
   DrawInfo di;
   uint32_t i;
   CHECK(idx == rt_idx);
   CTexture_StartUpdate(pEntry->pTexture, &di);
   /* A 80, R 30, G 60, B 90, plus the texel's place in each */
   for (i = 0; i < di.dwCreatedWidth * di.dwCreatedHeight; i++)
      ((uint32_t *)di.lpSurface)[i] = 0x80306090 + 0x010101 * i;
   CTexture_EndUpdate(pEntry->pTexture);
   rt_loads++;
}

/* ---- textures in RDRAM ---- */
/* the palette in RDRAM (as a sprite's is) */
#define pal ((uint16_t *)(ram + 0x8000))

/* an I8 texture @w x @h at @addr, @pitch bytes a row, texel (x, y) = @f */
static void i8(uint32_t addr, uint32_t w, uint32_t h, uint32_t pitch, uint8_t (*f)(uint32_t, uint32_t))
{
   uint32_t x, y;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
         ram[(addr + y * pitch + x) ^ 3] = f(x, y);
}

static uint8_t texel_xy(uint32_t x, uint32_t y) { return (uint8_t)(0x10 + y * 0x10 + x); }

static void info(TxtrInfo *ti, uint32_t addr, uint32_t fmt, uint32_t siz, uint32_t w, uint32_t h, uint32_t pitch)
{
   memset(ti, 0, sizeof(*ti));
   ti->Address  = addr;
   ti->pPhysicalAddress = ram + addr;
   ti->Format   = fmt;
   ti->Size     = siz;
   ti->WidthToLoad  = ti->WidthToCreate  = w;
   ti->HeightToLoad = ti->HeightToCreate = h;
   ti->Pitch    = pitch;
   ti->tileNo   = -1;
   ti->PalAddress = (uint8_t *)pal;
   ti->TLutFmt  = TLUT_FMT_RGBA16;
}

static uint32_t px(TxtrCacheEntry *e, uint32_t x, uint32_t y)
{
   uint32_t i = y * e->pTexture->m_dwCreatedTextureWidth + x;
   if (sixteen)
      return ((uint16_t *)e->pTexture->m_pTexture)[i];
   return ((uint32_t *)e->pTexture->m_pTexture)[i];
}

/* an intensity as the renderer's pixel: all four of 8 bits, or of 4 */
#define I(v) (sixteen ? 0x1111u * ((uint8_t)(v) >> 4) : 0x01010101u * (uint8_t)(v))
/* an A8R8G8B8 colour as the renderer's pixel: as it is, or each channel's
   top four bits */
#define C(c) (sixteen ? (uint32_t)((((c) >> 16) & 0xF000) | (((c) >> 12) & 0xF00) | (((c) >> 8) & 0xF0) \
      | (((c) >> 4) & 0xF)) : (uint32_t)(c))
/* a pixel the cache did not write */
#define UNTOUCHED (sixteen ? 0xCDCDu : 0xCDCDCDCDu)

static void reset(void)
{
   TextureManager_CleanUp();
   memset(&gRDP, 0, sizeof(gRDP));
   memset(&status, 0, sizeof(status));
   memset(&frameBufferOptions, 0, sizeof(frameBufferOptions));
   memset(&g_curRomInfo, 0, sizeof(g_curRomInfo));
   memset(g_textures, 0, sizeof(g_textures));
   memset(&gDP, 0, sizeof(gDP));
   rt_idx = -1;
   rt_addr = 0xFFFFFFFF;
   made_cap = 1024;
   uploads = releases = made = rt_loads = 0;
   status.gDlistCount = 1;
   status.gRDPTime = 1;
}

static void cached(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *a, *b, *c;
   int i;

   reset();
   memset(ram, 0, sizeof(ram));
   i8(0x1000, 8, 4, 8, texel_xy);
   info(&ti, 0x1000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 8);

   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && made == 1 && uploads == 1);
   CHECK(a->pTexture->m_dwCreatedTextureWidth == 8 && a->pTexture->m_dwCreatedTextureHeight == 4);
   CHECK(px(a, 0, 0) == I(0x10) && px(a, 7, 3) == I(0x47));
   CHECK(gRDP.texturesAreReloaded);
   CHECK(TxtrInfo_Equal(&a->ti, &ti));

   /* the same texels, a frame on: as it was */
   status.gRDPTime = 2;
   status.gDlistCount = 2;
   gRDP.texturesAreReloaded = false;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && made == 1 && uploads == 1);
   CHECK(gRDP.texturesAreReloaded);
   CHECK(a->FrameLastUsed == 2 && a->dwTimeLastUsed == 2);

   /* a texel changed: converted again, the same entry */
   status.gRDPTime = 3;
   ram[(0x1000 + 9) ^ 3] = 0xEE;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && made == 1 && uploads == 2);
   CHECK(px(a, 1, 1) == I(0xEE));

   /* used again in this RDP time: the CRC kept from then on, a change
      unseen */
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && uploads == 2 && a->dwTimeLastUsed == 3);
   ram[(0x1000 + 10) ^ 3] = 0x77;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && uploads == 2 && px(a, 2, 1) == I(0x22));
   /* but not in the first display list */
   status.gDlistCount = 0;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(uploads == 3 && px(a, 2, 1) == I(0x77));
   status.gDlistCount = 3;
   /* nor where the N64's frame buffer is used */
   ram[(0x1000 + 11) ^ 3] = 0x78;
   status.bN64FrameBufferIsUsed = true;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(uploads == 4 && px(a, 3, 1) == I(0x78));
   status.bN64FrameBufferIsUsed = false;

   /* CRCs not checked: converted every time */
   status.gRDPTime = 4;
   b = TextureManager_GetTexture(&ti, false, false, false);
   CHECK(b == a && uploads == 5);
   g_curRomInfo.bDisableTextureCRC = true;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && uploads == 6);
   g_curRomInfo.bDisableTextureCRC = false;
   status.gRDPTime = 5;
   b = TextureManager_GetTexture(&ti, false, true, false);
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(uploads == 7);

   /* another size, another entry */
   ti.WidthToLoad = ti.WidthToCreate = 4;
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c != a && made == 2);
   ti.WidthToLoad = ti.WidthToCreate = 8;

   /* addresses in one bucket (809 buckets, four bytes each) kept apart */
   i8(0x1000 + 809 * 4, 8, 4, 8, texel_xy);
   ram[(0x1000 + 809 * 4) ^ 3] = 0x99;
   ti.Address = 0x1000 + 809 * 4;
   ti.pPhysicalAddress = ram + ti.Address;
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c != a && made == 3 && px(c, 0, 0) == I(0x99));
   ti.Address = 0x1000;
   ti.pPhysicalAddress = ram + 0x1000;
   status.gRDPTime = 6;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && made == 3 && px(a, 0, 0) == I(0x10));

   /* the palette's entries: those used checked, the rest not */
   reset();
   memset(ram, 0, sizeof(ram));
   for (i = 0; i < 256; i++)
      pal[i] = 0;
   for (i = 0; i < 32; i++)
      ram[(0x2000 + i) ^ 3] = (uint8_t)(i & 3);      /* indices 0..3 */
   pal[0x1 ^ 1] = 0xF801;
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 4, 8);
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && uploads == 1 && a->maxCI == 3);
   CHECK(px(a, 1, 0) == C(0xFFFF0000));
   status.gRDPTime = 2;
   pal[200] = 0x1234;                                  /* unused */
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && uploads == 1);
   status.gRDPTime = 3;
   pal[0x1 ^ 1] = 0x07C1;                                /* used */
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == a && uploads == 2 && px(a, 1, 0) == C(0xFF00FF00));
   /* 4-bit: 16 entries (the caller points at the tile's palette) */
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_4b, 8, 4, 4);
   for (i = 0; i < 16; i++)
      ram[(0x2000 + i) ^ 3] = 0x11;                    /* index 1 */
   pal[0x1 ^ 1] = 0x003F;
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c != a && c->maxCI == 1 && px(c, 0, 0) == C(0xFF0000FF));
   status.gRDPTime = 4;
   pal[0x5 ^ 1] = 0x1234;                                /* unused */
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == c && uploads == 3);
   status.gRDPTime = 5;
   pal[0x1 ^ 1] = 0xF801;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b == c && uploads == 4 && px(c, 0, 0) == C(0xFFFF0000));
   /* RGBA of 4 and 8 bits are checked as indices too */
   info(&ti, 0x2000, G_IM_FMT_RGBA, G_IM_SIZ_8b, 8, 4, 8);
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c->maxCI == 0x11);
   info(&ti, 0x2000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4, 8);
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c->maxCI == 0);
   info(&ti, 0x2000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 8);
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(c && c->maxCI == 0);

   /* a texture made smaller than asked: loaded to the size made */
   reset();
   memset(ram, 0, sizeof(ram));
   made_cap = 4;
   i8(0x3000, 8, 8, 8, texel_xy);
   info(&ti, 0x3000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 8, 8);
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->pTexture->m_dwCreatedTextureWidth == 4 && a->pTexture->m_dwCreatedTextureHeight == 4);
   CHECK(px(a, 3, 3) == I(0x10 + 3 * 0x10 + 3));
   CHECK(a->ti.WidthToLoad == 8 && a->ti.HeightToLoad == 8);   /* as asked, after */
   CHECK(!a->pTexture->m_bScaledS && !a->pTexture->m_bScaledT);
}

/* ---- clamped, mirrored, wrapped ---- */

/* texels whose top four bits differ along each row and column too */
static uint8_t texel2(uint32_t x, uint32_t y)
{
   return (uint8_t)((((x * 3 + y * 5) & 15) << 4) | ((x * 7 + y) & 15));
}
static TxtrCacheEntry *expand(uint32_t wl, uint32_t wc, uint32_t hl, uint32_t hc, uint32_t maskS, uint32_t maskT,
      bool mirrorS, bool mirrorT, bool clampS, bool clampT)
{
   TxtrInfo ti;
   TxtrCacheEntry *e;
   reset();
   memset(ram, 0, sizeof(ram));
   i8(0x4000, wl, hl, 16, texel2);
   info(&ti, 0x4000, G_IM_FMT_I, G_IM_SIZ_8b, wl, hl, 16);
   ti.WidthToCreate = wc;
   ti.HeightToCreate = hc;
   ti.maskS = maskS; ti.maskT = maskT;
   ti.mirrorS = mirrorS; ti.mirrorT = mirrorT;
   ti.clampS = clampS; ti.clampT = clampT;
   e = TextureManager_GetTexture(&ti, false, true, true);
   CHECK(e && uploads == 1);   /* once, expanded */
   return e;
}

static void expanded(void)
{
   TxtrCacheEntry *e;
   uint32_t x, y;
   int ok;

   /* mask 0: the last column and row again */
   e = expand(3, 3, 2, 2, 0, 0, false, false, false, false);
   CHECK(e->pTexture->m_dwCreatedTextureWidth == 4 && e->pTexture->m_dwCreatedTextureHeight == 2);
   CHECK(px(e, 3, 0) == I(texel2(2, 0)) && px(e, 3, 1) == I(texel2(2, 1)));
   e = expand(4, 4, 3, 3, 0, 0, false, false, false, false);
   CHECK(px(e, 0, 3) == I(texel2(0, 2)) && px(e, 3, 3) == I(texel2(3, 2)));
   CHECK(px(e, 1, 0) == I(texel2(1, 0)) && px(e, 2, 1) == I(texel2(2, 1)));   /* as loaded */
   /* one texel wide, one high: it, all along */
   e = expand(1, 1, 2, 2, 0, 0, false, false, false, false);
   CHECK(px(e, 0, 1) == I(texel2(0, 1)));
   e = expand(1, 4, 2, 2, 0, 0, false, false, false, false);
   CHECK(px(e, 3, 0) == I(texel2(0, 0)) && px(e, 2, 1) == I(texel2(0, 1)));
   e = expand(2, 2, 1, 4, 0, 0, false, false, false, false);
   CHECK(px(e, 1, 3) == I(texel2(1, 0)) && px(e, 0, 2) == I(texel2(0, 0)));

   /* loaded the mask's width: mirrored, to the size made */
   e = expand(4, 8, 4, 4, 2, 2, true, false, false, false);
   ok = 1;
   for (y = 0; y < 4; y++)
      for (x = 4; x < 8; x++)
         ok &= px(e, x, y) == I(texel2(7 - x, y));
   CHECK(ok);
   /* wrapped */
   e = expand(4, 8, 4, 4, 2, 2, false, false, false, false);
   ok = 1;
   for (y = 0; y < 4; y++)
      for (x = 4; x < 8; x++)
         ok &= px(e, x, y) == I(texel2(x & 3, y));
   CHECK(ok);
   /* mirrored to the size to create, clamped past it */
   e = expand(4, 6, 2, 2, 2, 1, true, false, true, false);
   CHECK(px(e, 4, 0) == I(texel2(3, 0)) && px(e, 5, 1) == I(texel2(2, 1)));
   CHECK(px(e, 6, 0) == I(texel2(2, 0)) && px(e, 7, 1) == I(texel2(2, 1)));
   /* wrapped to the size to create, clamped past it */
   e = expand(4, 6, 2, 2, 2, 1, false, false, true, false);
   CHECK(px(e, 4, 0) == I(texel2(0, 0)) && px(e, 5, 1) == I(texel2(1, 1)));
   CHECK(px(e, 6, 0) == I(texel2(1, 0)) && px(e, 7, 1) == I(texel2(1, 1)));

   /* in T: mirrored rows, wrapped rows */
   e = expand(2, 2, 4, 8, 1, 2, false, true, false, false);
   ok = 1;
   for (y = 0; y < 4; y++)
      for (x = 0; x < 2; x++)
         ok &= px(e, x, y) == I(texel2(x, y));
   for (y = 4; y < 8; y++)
      for (x = 0; x < 2; x++)
         ok &= px(e, x, y) == I(texel2(x, 7 - y));
   CHECK(ok);
   e = expand(2, 2, 4, 8, 1, 2, false, false, false, false);
   ok = 1;
   for (y = 0; y < 4; y++)
      for (x = 0; x < 2; x++)
         ok &= px(e, x, y) == I(texel2(x, y));
   for (y = 4; y < 8; y++)
      for (x = 0; x < 2; x++)
         ok &= px(e, x, y) == I(texel2(x, y & 3));
   CHECK(ok);
   /* mirrored in both */
   e = expand(4, 8, 2, 4, 2, 1, true, true, false, false);
   CHECK(px(e, 5, 3) == I(texel2(2, 0)) && px(e, 7, 2) == I(texel2(0, 1)));

   /* less than the mask, the mask the size made: wrapped as far */
   e = expand(3, 4, 2, 2, 2, 1, false, false, false, false);
   CHECK(px(e, 3, 0) == I(texel2(1, 0)) && px(e, 3, 1) == I(texel2(1, 1)));
   e = expand(2, 2, 3, 4, 1, 2, false, false, false, false);
   CHECK(px(e, 0, 3) == I(texel2(0, 0)) && px(e, 1, 3) == I(texel2(1, 0)));
   /* less than the mask, all to create loaded: clamped */
   e = expand(3, 3, 2, 2, 3, 1, false, false, false, false);
   CHECK(px(e, 3, 0) == I(texel2(2, 0)) && px(e, 3, 1) == I(texel2(2, 1)));
   /* less than the mask and than to create: clamped */
   e = expand(3, 5, 2, 2, 3, 1, false, false, false, false);
   CHECK(e->pTexture->m_dwCreatedTextureWidth == 8);
   CHECK(px(e, 3, 0) == I(texel2(2, 0)) && px(e, 7, 1) == I(texel2(2, 1)));
   /* none of these: left as converted */
   e = expand(3, 4, 2, 2, 1, 1, true, false, false, false);
   CHECK(px(e, 3, 0) == UNTOUCHED);
   /* no expansion asked: the texels past those loaded as they were */
   reset();
   {
      TxtrInfo ti;
      i8(0x4000, 3, 2, 16, texel2);
      info(&ti, 0x4000, G_IM_FMT_I, G_IM_SIZ_8b, 3, 2, 16);
      e = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(px(e, 3, 0) == UNTOUCHED);
   }
}

/* ---- render textures ---- */
static void render_textures(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *e, *f;

   reset();
   memset(ram, 0, sizeof(ram));
   rt_idx = 2;
   rt_addr = 0x5000;
   rt_fmt = G_IM_FMT_RGBA;
   rt_siz = G_IM_SIZ_16b;
   info(&ti, 0x5000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 4, 16);

   /* checked against neither: from RDRAM */
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e && e != &rt_entry && made == 1 && rt_loads == 0);

   /* render textures checked: the render texture itself, of this format */
   frameBufferOptions.bCheckRenderTextures = true;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e == &rt_entry && TxtrInfo_Equal(&rt_entry.ti, &ti) && made == 1);
   /* unless written back to RDRAM */
   frameBufferOptions.bWriteBackBufToRDRAM = true;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e != &rt_entry);
   frameBufferOptions.bWriteBackBufToRDRAM = false;
   frameBufferOptions.bCheckRenderTextures = false;
   /* back buffers checked: the render texture too */
   frameBufferOptions.bCheckBackBufs = true;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e == &rt_entry);
   /* another address: from RDRAM */
   info(&ti, 0x5800, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 4, 16);
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e != &rt_entry && rt_loads == 0);

   /* another format: loaded from the render texture, by its CRC */
   info(&ti, 0x5000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 16);
   ci_fmt = G_IM_FMT_RGBA;
   rt_crc = 0x1234;
   rt_frame = 1;
   status.gRDPTime = 10;
   status.gDlistCount = 10;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e && e != &rt_entry && rt_loads == 1 && e->dwCRC == 0x1234);
   CHECK(px(e, 0, 0) == 0x80306090);
   /* the same CRC, drawn before it was last used: as it is */
   status.gRDPTime = 11;
   status.gDlistCount = 11;
   f = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(f == e && rt_loads == 1);
   /* drawn since: loaded again */
   status.gRDPTime = 12;
   rt_frame = 11;
   f = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(f == e && rt_loads == 2);
   /* as intensity, for an I colour image: (30 + 60 + 90) / 3 = 60 */
   status.gRDPTime = 13;
   rt_frame = 12;
   ci_fmt = G_IM_FMT_I;
   f = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(rt_loads == 3 && px(f, 0, 0) == 0x60606060);
   /* each texel its own: (30 + i, 60 + i, 90 + i) */
   CHECK(px(f, 1, 0) == 0x61616161 && px(f, 0, 1) == 0x68686868 && px(f, 7, 3) == 0x7F7F7F7F);
   /* and IA: the alpha kept */
   status.gRDPTime = 14;
   rt_frame = 13;
   ci_fmt = G_IM_FMT_IA;
   f = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(rt_loads == 4 && px(f, 0, 0) == 0x80606060);
   CHECK(px(f, 1, 0) == 0x80616161 && px(f, 7, 3) == 0x807F7F7F);
   frameBufferOptions.bCheckBackBufs = false;
}

/* ---- letting go ---- */
static void let_go(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *a, *b, *c, *d;

   reset();
   memset(ram, 0, sizeof(ram));
   status.gDlistCount = 100;
   info(&ti, 0x6000, G_IM_FMT_I, G_IM_SIZ_8b, 4, 4, 4);
   a = TextureManager_GetTexture(&ti, false, true, false);
   ti.Address = 0x6000 + 809 * 4;                 /* a's bucket */
   b = TextureManager_GetTexture(&ti, false, true, false);
   ti.Address = 0x6000 + 2 * 809 * 4;
   c = TextureManager_GetTexture(&ti, false, true, false);
   ti.Address = 0x6100;
   d = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && b && c && d && made == 4);
   g_textures[3].pTextureEntry = d;                /* bound */

   /* 150 frames: kept */
   status.gDlistCount = 250;
   TextureManager_PurgeOldTextures();
   CHECK(releases == 0);
   /* b used since */
   status.gRDPTime = 2;
   ti.Address = 0x6000 + 809 * 4;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == b);
   /* 151: a and c let go, b used since and d bound kept */
   status.gDlistCount = 251;
   TextureManager_PurgeOldTextures();
   CHECK(releases == 2);
   status.gRDPTime = 3;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == b && made == 4);
   ti.Address = 0x6100;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == d && made == 4);
   ti.Address = 0x6000;
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(made == 5);
   ti.Address = 0x6000 + 2 * 809 * 4;
   c = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(made == 6);

   /* recycled: all let go */
   TextureManager_RecycleAllTextures();
   CHECK(releases == 2 + 4);
   ti.Address = 0x6100;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) != NULL && made == 7);

   /* cleaned up: the colour textures too */
   TextureManager_GetBlackTexture();
   gRDP.primitiveColor = 0x11223344;
   TextureManager_GetConstantColorTexture(MUX_PRIM);
   releases = 0;
   TextureManager_CleanUp();
   CHECK(releases == 3);
   CHECK(TextureManager_GetBlackTexture()->pTexture != NULL);
   releases = 0;
   TextureManager_CleanUp();
   CHECK(releases == 1);
}

/* ---- colour textures ---- */
static void colours(void)
{
   TxtrCacheEntry *e;
   int i, ok;

   reset();
   e = TextureManager_GetBlackTexture();
   CHECK(e && made == 1 && uploads == 1 && e->ti.WidthToCreate == 4 && e->ti.HeightToCreate == 4);
   ok = 1;
   for (i = 0; i < 16; i++)
      ok &= px(e, i & 3, i >> 2) == 0;
   CHECK(ok);
   CHECK(!gRDP.texturesAreReloaded);
   CHECK(TextureManager_GetBlackTexture() == e && made == 1 && uploads == 1);

   gRDP.primitiveColor = 0x11223344;
   gRDP.envColor = 0x55667788;
   gRDP.LODFrac = 0x9A;
   gRDP.primLODFrac = 0x3C;
   e = TextureManager_GetConstantColorTexture(MUX_PRIM);
   CHECK(made == 2 && uploads == 2 && px(e, 3, 3) == C(0x11223344) && gRDP.texturesAreReloaded);
   gRDP.texturesAreReloaded = false;
   CHECK(TextureManager_GetConstantColorTexture(MUX_PRIM) == e && uploads == 2 && !gRDP.texturesAreReloaded);
   gRDP.primitiveColor = 0x01020304;
   CHECK(TextureManager_GetConstantColorTexture(MUX_PRIM) == e && uploads == 3 && px(e, 0, 0) == C(0x01020304));
   CHECK(gRDP.texturesAreReloaded);

   gRDP.texturesAreReloaded = false;
   e = TextureManager_GetConstantColorTexture(MUX_ENV);
   CHECK(made == 3 && uploads == 4 && px(e, 2, 1) == C(0x55667788) && gRDP.texturesAreReloaded);
   gRDP.envColor = 0x55667789;
   CHECK(TextureManager_GetConstantColorTexture(MUX_ENV) == e && uploads == 5 && px(e, 0, 0) == C(0x55667789));
   CHECK(TextureManager_GetConstantColorTexture(MUX_ENV) == e && uploads == 5);

   gRDP.texturesAreReloaded = false;
   e = TextureManager_GetConstantColorTexture(MUX_LODFRAC);
   CHECK(made == 4 && uploads == 6 && px(e, 1, 2) == C(0x9A9A9A9A) && gRDP.texturesAreReloaded);
   gRDP.LODFrac = 0x19A;   /* the fraction is its byte */
   CHECK(TextureManager_GetConstantColorTexture(MUX_LODFRAC) == e && uploads == 6);
   gRDP.LODFrac = 0x9B;
   CHECK(TextureManager_GetConstantColorTexture(MUX_LODFRAC) == e && uploads == 7 && px(e, 0, 0) == C(0x9B9B9B9B));

   gRDP.texturesAreReloaded = false;
   e = TextureManager_GetConstantColorTexture(MUX_PRIMLODFRAC);
   CHECK(made == 5 && uploads == 8 && px(e, 0, 3) == C(0x3C3C3C3C) && gRDP.texturesAreReloaded);
   CHECK(TextureManager_GetConstantColorTexture(MUX_K5) == e && uploads == 8);
   gRDP.primLODFrac = 0x3D;
   CHECK(TextureManager_GetConstantColorTexture(MUX_PRIMLODFRAC) == e && uploads == 9 && px(e, 0, 0) == C(0x3D3D3D3D));

   /* each its own */
   CHECK(TextureManager_GetConstantColorTexture(MUX_PRIM) != TextureManager_GetConstantColorTexture(MUX_ENV));
   CHECK(TextureManager_GetConstantColorTexture(MUX_LODFRAC) != TextureManager_GetConstantColorTexture(MUX_PRIMLODFRAC));
   CHECK(TextureManager_GetConstantColorTexture(MUX_PRIM) != TextureManager_GetBlackTexture());
}

/* ---- CRCs ---- */
static void crc(void)
{
   uint32_t c1, c2;
   CHECK(crcs(0) == CRC_HASH);
   CHECK(crcs(1) == MAXCI_HASH);
   /* nothing read past RDRAM: a CRC of 0 */
   memset(ram, 0x5A, sizeof(ram));
   currentRomOptions.bFastTexCRC = false;
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 64, 0, 0, 32, 2, G_IM_SIZ_16b, 64) == 0);
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 64, 0, 0, 32, 1, G_IM_SIZ_16b, 64) != 0);
   CHECK(CalculateRDRAMCRC(ram - 4, 0, 0, 2, 1, G_IM_SIZ_8b, 64) == 0);
   CHECK(CalculateRDRAMCRC(ram, 0, 0, 8, 1, G_IM_SIZ_8b, 64) != 0);   /* RDRAM's first bytes */
   CHECK(CalculateRDRAMCRC(ram, 0, 0, 0xFFFFFFF0u, 1, G_IM_SIZ_8b, 64) == 0);
   /* the texel at (left, top) counted: past RDRAM's end from there, none */
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 64, 0, 0, 16, 1, G_IM_SIZ_8b, 64) != 0);
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 64, 56, 0, 16, 1, G_IM_SIZ_8b, 64) == 0);
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 128, 0, 1, 16, 1, G_IM_SIZ_8b, 64) != 0);
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 128, 0, 2, 16, 1, G_IM_SIZ_8b, 64) == 0);
   currentRomOptions.bFastTexCRC = true;
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 4096, 0, 8, 64, 32, G_IM_SIZ_8b, 64) != 0);
   CHECK(CalculateRDRAMCRC(ram + sizeof(ram) - 4096, 0, 40, 64, 32, G_IM_SIZ_8b, 64) == 0);
   currentRomOptions.bFastTexCRC = false;
   /* and the highest index: every one where not all in RDRAM */
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 64, 0, 0, 16, 1, G_IM_SIZ_8b, 64) == 0x5A);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 64, 56, 0, 16, 1, G_IM_SIZ_8b, 64) == 0xFF);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 64, 0, 1, 16, 1, G_IM_SIZ_8b, 64) == 0xFF);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 8, 0, 0, 16, 1, G_IM_SIZ_4b, 8) == 0xA);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 8, 4, 0, 16, 1, G_IM_SIZ_4b, 8) == 0xF);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 8, 2, 0, 16, 1, G_IM_SIZ_4b, 8) == 0xF);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 9, 2, 0, 16, 1, G_IM_SIZ_4b, 8) == 0xA);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 16, 0, 1, 16, 1, G_IM_SIZ_4b, 8) == 0xA);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 16, 0, 2, 16, 1, G_IM_SIZ_4b, 8) == 0xF);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 32, 0, 0, 16, 4, G_IM_SIZ_4b, 8) == 0xA);
   CHECK(CalculateMaxCI(ram + sizeof(ram) - 31, 0, 0, 16, 5, G_IM_SIZ_4b, 8) == 0xF);
   /* no rows: none, in RDRAM or not */
   CHECK(CalculateMaxCI(ram, 0, 0, 16, 0, G_IM_SIZ_8b, 64) == 0);
   CHECK(CalculateMaxCI(ram + sizeof(ram), 0, 0, 16, 0, G_IM_SIZ_8b, 64) == 0);
   CHECK(CalculateMaxCI(ram + sizeof(ram) + 1, 0, 0, 16, 0, G_IM_SIZ_8b, 64) == 0xFF);
   /* no rows: none */
   CHECK(CalculateRDRAMCRC(ram, 0, 0, 4, 0, G_IM_SIZ_8b, 64) == 0);
   /* a texel changed: another CRC; one past the width: the same */
   c1 = CalculateRDRAMCRC(ram + 64, 0, 0, 8, 2, G_IM_SIZ_8b, 16);
   ram[64 + 16 + 3] ^= 1;
   c2 = CalculateRDRAMCRC(ram + 64, 0, 0, 8, 2, G_IM_SIZ_8b, 16);
   CHECK(c1 != c2);
   ram[64 + 16 + 9] ^= 1;
   CHECK(CalculateRDRAMCRC(ram + 64, 0, 0, 8, 2, G_IM_SIZ_8b, 16) == c2);
   /* the highest index */
   memset(ram, 0, 256);
   ram[64 + 2 * 16 + 5] = 0x37;
   CHECK(CalculateMaxCI(ram + 64, 0, 0, 8, 4, G_IM_SIZ_8b, 16) == 0x37);
   CHECK(CalculateMaxCI(ram + 64, 0, 0, 5, 4, G_IM_SIZ_8b, 16) == 0);
   CHECK(CalculateMaxCI(ram + 64, 0, 3, 8, 1, G_IM_SIZ_8b, 16) == 0);
   ram[64 + 1] = 0x0C;
   CHECK(CalculateMaxCI(ram + 64, 0, 0, 4, 1, G_IM_SIZ_4b, 16) == 0xC);
   ram[64 + 1] = 0xA0;
   CHECK(CalculateMaxCI(ram + 64, 0, 0, 4, 1, G_IM_SIZ_4b, 16) == 0xA);
   CHECK(CalculateMaxCI(ram + 64, 4, 0, 4, 1, G_IM_SIZ_4b, 16) == 0);
   ram[64 + 1] = 0xFF;
   ram[64 + 2] = 0x01;
   CHECK(CalculateMaxCI(ram + 64, 0, 0, 8, 1, G_IM_SIZ_8b, 16) == 0xFF);
}


/* ---- more of the cache ---- */
static void more_cache(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *a, *b;
   int i;

   /* made in this RDP time: its CRC kept until the next */
   reset();
   memset(ram, 0, sizeof(ram));
   status.gRDPTime = 7;
   status.gDlistCount = 5;
   i8(0x1000, 8, 4, 8, texel_xy);
   info(&ti, 0x1000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 8);
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && uploads == 1 && a->dwTimeLastUsed == 7 && a->FrameLastUsed == 5);
   ram[(0x1000 + 1) ^ 3] = 0x55;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 1);
   status.gRDPTime = 8;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 2 && px(a, 1, 0) == I(0x55));

   /* loaded less than to create, made as big: the rest as it was */
   info(&ti, 0x1000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 8);
   ti.WidthToLoad = 5;
   ti.HeightToLoad = 3;
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->pTexture->m_dwCreatedTextureWidth == 8 && a->pTexture->m_dwCreatedTextureHeight == 4);
   CHECK(px(a, 4, 2) == I(texel_xy(4, 2)) && px(a, 5, 0) == UNTOUCHED && px(a, 0, 3) == UNTOUCHED);
   CHECK(a->ti.WidthToLoad == 5 && a->ti.HeightToLoad == 3);

   /* CI: a texel changed, the palette not: converted again */
   reset();
   memset(ram, 0, sizeof(ram));
   for (i = 0; i < 32; i++)
      ram[(0x2000 + i) ^ 3] = (uint8_t)(i & 3);
   for (i = 0; i < 16; i++)
      pal[i ^ 1] = (uint16_t)(0x0841 * i | 1);
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 4, 8);
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->maxCI == 3 && uploads == 1);
   status.gRDPTime = 2;
   ram[(0x2000 + 1) ^ 3] = 2;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 2);
   /* an index higher than any before: the palette checked that far */
   status.gRDPTime = 3;
   ram[(0x2000 + 5) ^ 3] = 9;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 3 && a->maxCI == 9);
   status.gRDPTime = 4;
   pal[0x9 ^ 1] = 0xF801;                                /* the highest used */
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 4 && px(a, 5, 0) == C(0xFFFF0000));
   status.gRDPTime = 5;
   pal[0xA ^ 1] = 0xF801;                               /* one past it */
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 4);
   status.gRDPTime = 6;
   pal[0x0 ^ 1] = 0x07C1;                               /* the first */
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 5 && px(a, 0, 0) == C(0xFF00FF00));
   uploads--;
   /* 8-bit: from the first entry, whatever the tile's palette */
   status.gRDPTime = 6;
   ti.Palette = 5;
   b = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(b && b != a && uploads == 5);
   status.gRDPTime = 7;
   pal[0x2 ^ 1] = 0x07C1;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == b && uploads == 6);
   /* 4-bit: the entries the conversion reads, from where the caller
      points (at the tile's palette), not a palette further on */
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_4b, 8, 4, 4);
   ti.Palette = 2;
   ti.PalAddress = (uint8_t *)&pal[32];
   for (i = 0; i < 16; i++)
      ram[(0x2000 + i) ^ 3] = 0x11;
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->maxCI == 1 && uploads == 7);
   status.gRDPTime = 8;
   pal[(32 + 1) ^ 1] = 0xF801;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 8 && px(a, 0, 0) == C(0xFFFF0000));
   status.gRDPTime = 9;
   pal[(64 + 1) ^ 1] = 0x1234;
   pal[(16 + 1) ^ 1] = 0x1234;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 8);
   /* the pair of the highest used, in its word: checked too */
   status.gRDPTime = 10;
   pal[(32 + 0) ^ 1] = 0x07C1;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 9);

   /* a palette in Rice's copy of TLUT, not in RDRAM: checked as well */
   {
      static uint16_t tlut[0x200];
      info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 4, 8);
      ti.PalAddress = (uint8_t *)tlut;
      for (i = 0; i < 32; i++)
         ram[(0x2000 + i) ^ 3] = 3;
      tlut[3 ^ 1] = 0xF801;
      a = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(a && uploads == 10 && px(a, 0, 0) == C(0xFFFF0000));
      status.gRDPTime = 11;
      tlut[3 ^ 1] = 0x003F;
      CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 11 && px(a, 0, 0) == C(0xFF0000FF));
      status.gRDPTime = 12;
      tlut[0x10] = 0x1234;                             /* unused */
      CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 11);
      /* I through the palette, in a TLUT mode */
      gRDP.otherMode.text_tlut = 2;
      ti.Format = G_IM_FMT_I;
      a = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(a && uploads == 12 && a->maxCI == 3 && px(a, 0, 0) == C(0xFF0000FF));
      status.gRDPTime = 13;
      tlut[3 ^ 1] = 0x07C1;
      CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 13 && px(a, 0, 0) == C(0xFF00FF00));
      /* not in a TLUT mode: no palette */
      gRDP.otherMode.text_tlut = 0;
      a = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(a && a->maxCI == 0 && px(a, 0, 0) == I(3));
      status.gRDPTime = 14;
      tlut[3 ^ 1] = 0x003F;
      CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == 14);
   }

   /* IA through the palette too, in a TLUT mode */
   {
      static uint16_t tlut2[0x200];
      info(&ti, 0x2000, G_IM_FMT_IA, G_IM_SIZ_8b, 8, 4, 8);
      ti.PalAddress = (uint8_t *)tlut2;
      gRDP.otherMode.text_tlut = 3;
      a = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(a && a->maxCI == 3);
      status.gRDPTime = 50;
      b = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(b == a);
      i = uploads;
      status.gRDPTime = 51;
      tlut2[3 ^ 1] ^= 0x7777;
      CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == (int)i + 1);
      /* not YUV */
      ti.Format = G_IM_FMT_YUV;
      a = TextureManager_GetTexture(&ti, false, true, false);
      CHECK(a && a->maxCI == 0);
      gRDP.otherMode.text_tlut = 0;
   }

   /* a palette at RDRAM's end: to it, and none past it */
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_4b, 8, 4, 4);
   for (i = 0; i < 16; i++)
      ram[(0x2000 + i) ^ 3] = 0x23;                    /* indices 2, 3 */
   ti.PalAddress = ram + sizeof(ram) - 8;              /* entries 0 to 3 */
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->maxCI == 3);
   i = uploads;
   status.gRDPTime = 52;
   ram[sizeof(ram) - 2] ^= 0x55;                       /* entry 3's swapped pair, 2 */
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == (int)i + 1);
   ti.PalAddress = ram + sizeof(ram) - 6;              /* entry 3 past the end */
   for (i = 0; i < 16; i++)
      ram[(0x2000 + i) ^ 3] = 0x33;                    /* index 3: entry 2 of the word */
   a = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(a && a->dwPalCRC == 0);
   i = uploads;
   status.gRDPTime = 53;
   ram[sizeof(ram) - 6] ^= 0x55;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == (int)i);

   /* from TMEM: its palette, each entry four times over, the tile's of 16 */
   options.bUseFullTMEM = true;
   status.bAllowLoadFromTMEM = true;
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   gDP.tiles[1].tmem = 0x20;
   gDP.tiles[1].line = 1;
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_4b, 8, 4, 4);
   ti.tileNo = 1;
   ti.Palette = 3;
   for (i = 0; i < 16; i++)
      ram[(0x2000 + i) ^ 3] = 0x22;                    /* the CRC's and maxCI's texels */
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (2 << 2)] = 0x1234;
   i = uploads;
   a = TextureManager_GetTexture(&ti, true, true, false);
   CHECK(a && a->maxCI == 2 && uploads == (int)i + 1);
   status.gRDPTime = 15;
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (2 << 2)] = 0x4321;
   CHECK(TextureManager_GetTexture(&ti, true, true, false) == a && uploads == (int)i + 2);
   status.gRDPTime = 16;
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (2 << 2) + 1] = 0x4321;   /* between entries */
   g_Tmem.g_Tmem16bit[0x400 + 2 * 0x40 + (2 << 2)] = 0x4321;       /* another palette */
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (4 << 2)] = 0x4321;       /* past the highest */
   CHECK(TextureManager_GetTexture(&ti, true, true, false) == a && uploads == (int)i + 2);
   /* not from TMEM, or not allowed: PalAddress's */
   ti.PalAddress = (uint8_t *)pal;
   status.gRDPTime = 17;
   a = TextureManager_GetTexture(&ti, false, true, false);
   i = uploads;
   status.gRDPTime = 18;
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (2 << 2)] = 0x1111;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == (int)i);
   status.gRDPTime = 19;
   pal[0x2 ^ 1] ^= 0x0F0F;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == a && uploads == (int)i + 1);
   status.bAllowLoadFromTMEM = false;
   status.gRDPTime = 20;
   a = TextureManager_GetTexture(&ti, true, true, false);
   i = uploads;
   status.gRDPTime = 21;
   g_Tmem.g_Tmem16bit[0x400 + 3 * 0x40 + (2 << 2)] = 0x2222;
   CHECK(TextureManager_GetTexture(&ti, true, true, false) == a && uploads == (int)i);
   status.bAllowLoadFromTMEM = true;
   /* 8-bit from TMEM: the first palette, whatever the tile's */
   info(&ti, 0x2000, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 4, 8);
   ti.tileNo = 1;
   ti.Palette = 3;
   for (i = 0; i < 32; i++)
      ram[(0x2000 + i) ^ 3] = 1;
   a = TextureManager_GetTexture(&ti, true, true, false);
   CHECK(a && a->maxCI == 1);
   i = uploads;
   status.gRDPTime = 22;
   g_Tmem.g_Tmem16bit[0x400 + (1 << 2)] = 0x3333;
   CHECK(TextureManager_GetTexture(&ti, true, true, false) == a && uploads == (int)i + 1);
   options.bUseFullTMEM = false;

   /* bound in the first and last texture unit: kept */
   reset();
   memset(ram, 0, sizeof(ram));
   status.gDlistCount = 10;
   info(&ti, 0, G_IM_FMT_I, G_IM_SIZ_8b, 4, 4, 4);   /* bucket 0 */
   a = TextureManager_GetTexture(&ti, false, true, false);
   ti.Address = 0x100;
   b = TextureManager_GetTexture(&ti, false, true, false);
   g_textures[0].pTextureEntry = a;
   g_textures[MAX_TEXTURES - 1].pTextureEntry = b;
   status.gDlistCount = 1000;
   TextureManager_PurgeOldTextures();
   CHECK(releases == 0);
   g_textures[0].pTextureEntry = NULL;
   TextureManager_PurgeOldTextures();
   CHECK(releases == 1);                               /* bucket 0's too */
   g_textures[MAX_TEXTURES - 1].pTextureEntry = NULL;
   TextureManager_PurgeOldTextures();
   CHECK(releases == 2);

   /* cleaned up: each colour texture made again when next asked for */
   reset();
   TextureManager_GetBlackTexture();
   for (i = 0; i < 4; i++)
      TextureManager_GetConstantColorTexture(i == 0 ? MUX_PRIM : i == 1 ? MUX_ENV : i == 2 ? MUX_LODFRAC : MUX_PRIMLODFRAC);
   CHECK(made == 5);
   CHECK(TextureManager_CleanUp() == true && releases == 5);
   TextureManager_GetBlackTexture();
   for (i = 0; i < 4; i++)
      TextureManager_GetConstantColorTexture(i == 0 ? MUX_PRIM : i == 1 ? MUX_ENV : i == 2 ? MUX_LODFRAC : MUX_PRIMLODFRAC);
   CHECK(made == 10);
   for (i = 0; i < 5; i++)
   {
      TxtrCacheEntry *e = i == 4 ? TextureManager_GetBlackTexture()
         : TextureManager_GetConstantColorTexture(i == 0 ? MUX_PRIM : i == 1 ? MUX_ENV : i == 2 ? MUX_LODFRAC : MUX_PRIMLODFRAC);
      CHECK(e->pTexture->m_dwWidth == 4 && e->pTexture->m_dwHeight == 4);
      CHECK(e->ti.WidthToCreate == 4 && e->ti.HeightToCreate == 4);
   }

   /* nothing loaded: nothing to expand from */
   {
      TxtrCacheEntry *e = expand(0, 4, 2, 2, 0, 0, false, false, false, false);
      CHECK(px(e, 0, 0) == UNTOUCHED && px(e, 3, 1) == UNTOUCHED);
      e = expand(2, 2, 0, 4, 0, 0, false, false, false, false);
      CHECK(px(e, 0, 0) == UNTOUCHED && px(e, 1, 3) == UNTOUCHED);
   }
}

/* ---- which conversion ---- */
static void which_conversion(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *a;
   uint32_t i;

   reset();
   memset(ram, 0x40, 0x8000);
   memset(&g_Tmem, 0x90, sizeof(g_Tmem));
   gDP.tiles[0].tmem = 0x10;
   gDP.tiles[0].line = 1;
   options.bUseFullTMEM = true;
   status.bAllowLoadFromTMEM = true;
   info(&ti, 0x1000, G_IM_FMT_I, G_IM_SIZ_8b, 4, 2, 8);
   ti.tileNo = 0;

   /* from TMEM, where full TMEM is used and allowed, and asked for */
   a = TextureManager_GetTexture(&ti, true, false, false);
   CHECK(a && px(a, 0, 0) == I(0x90));
   status.bAllowLoadFromTMEM = false;
   TextureManager_GetTexture(&ti, true, false, false);
   CHECK(px(a, 0, 0) == I(0x40));
   status.bAllowLoadFromTMEM = true;
   options.bUseFullTMEM = false;
   TextureManager_GetTexture(&ti, true, false, false);
   CHECK(px(a, 0, 0) == I(0x40));
   options.bUseFullTMEM = true;
   TextureManager_GetTexture(&ti, false, false, false);
   CHECK(px(a, 0, 0) == I(0x40));
   TextureManager_GetTexture(&ti, true, false, false);
   CHECK(px(a, 0, 0) == I(0x90));
   options.bUseFullTMEM = false;

   /* a TLUT mode of RGBA16 or IA16: an intensity through the palette */
   pal[0x40 ^ 1] = 0xF801;
   gRDP.otherMode.text_tlut = 2;
   TextureManager_GetTexture(&ti, false, false, false);
   CHECK(px(a, 0, 0) == C(0xFFFF0000));
   gRDP.otherMode.text_tlut = 3;
   ti.TLutFmt = TLUT_FMT_IA16;
   a = TextureManager_GetTexture(&ti, false, false, false);
   CHECK(px(a, 0, 0) == C(0x01F8F8F8));
   gRDP.otherMode.text_tlut = 1;
   TextureManager_GetTexture(&ti, false, false, false);
   CHECK(px(a, 0, 0) == I(0x40));
   gRDP.otherMode.text_tlut = 0;

   /* tile 7 of YUV: the texture is too (32-bit textures only) */
   if (!sixteen)
   {
      TxtrInfo yi;
      CTexture *ref = RiceDevice_CreateTexture(4, 2);
      int same = 1, rgba = 1;
      for (i = 0; i < 64; i++)
         ram[0x1000 + i] = (uint8_t)(i * 37);
      info(&ti, 0x1000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 2, 8);
      yi = ti;
      yi.Format = G_IM_FMT_YUV;
      gConvertFunctions[G_IM_FMT_YUV][G_IM_SIZ_16b](ref, &yi);
      gDP.tiles[7].format = G_IM_FMT_YUV;
      a = TextureManager_GetTexture(&ti, false, false, false);
      for (i = 0; i < 8; i++)
         same &= px(a, i & 3, i >> 2) == ((uint32_t *)ref->m_pTexture)[(i >> 2) * 4 + (i & 3)];
      CHECK(same);
      gDP.tiles[7].format = G_IM_FMT_RGBA;
      TextureManager_GetTexture(&ti, false, false, false);
      for (i = 0; i < 8; i++)
         rgba &= px(a, i & 3, i >> 2) == ((uint32_t *)ref->m_pTexture)[(i >> 2) * 4 + (i & 3)];
      CHECK(!rgba);
      CTexture_Delete(ref);
   }
}

/* ---- render textures, more ---- */
static void more_render_textures(void)
{
   TxtrInfo ti;
   TxtrCacheEntry *e;

   reset();
   memset(ram, 0, sizeof(ram));
   rt_idx = 0;                                         /* the first */
   rt_addr = 0x5000;
   rt_fmt = G_IM_FMT_RGBA;
   rt_siz = G_IM_SIZ_16b;
   info(&ti, 0x5000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 4, 16);
   frameBufferOptions.bCheckRenderTextures = true;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == &rt_entry);
   /* another format: loaded from it */
   info(&ti, 0x5000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 16);
   ci_fmt = G_IM_FMT_RGBA;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e && e != &rt_entry && rt_loads == 1);
   rt_loads = 0;
   info(&ti, 0x5000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 4, 16);
   frameBufferOptions.bCheckRenderTextures = false;

   /* back buffers checked: the render texture too */
   frameBufferOptions.bCheckBackBufs = true;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == &rt_entry);
   /* written back to RDRAM: neither */
   frameBufferOptions.bWriteBackBufToRDRAM = true;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e && e != &rt_entry && rt_loads == 0);
   frameBufferOptions.bWriteBackBufToRDRAM = false;

   /* a render texture made of a back buffer: found after them, the back
      buffers looked at for the texture's bytes */
   rt_after_backbuf = 1;
   bb_memsize = 0;
   CHECK(TextureManager_GetTexture(&ti, false, true, false) == &rt_entry && bb_memsize == 4 * 16);
   rt_after_backbuf = 1;
   info(&ti, 0x5000, G_IM_FMT_I, G_IM_SIZ_8b, 8, 4, 16);
   ci_fmt = G_IM_FMT_RGBA;
   e = TextureManager_GetTexture(&ti, false, true, false);
   CHECK(e && e != &rt_entry && rt_loads == 1);
   rt_after_backbuf = 0;
   frameBufferOptions.bCheckBackBufs = false;
}

int main(void)
{
   gfx_info.RDRAM = ram;
   crc();
   /* A8R8G8B8 textures, then A4R4G4B4 */
   for (sixteen = 0; sixteen < 2; sixteen++)
   {
      cached();
      more_cache();
      expanded();
      which_conversion();
      let_go();
      colours();
      TextureManager_CleanUp();
   }
   sixteen = 0;
   render_textures();
   more_render_textures();
   TextureManager_CleanUp();
   printf("rice texture cache: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
#endif
