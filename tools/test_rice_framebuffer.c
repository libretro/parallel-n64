/* Rice's texels copied into the N64's colour image in RDRAM
 * (mupen64plus-video-rice/src/FrameBufferRDRAM.c):
 *
 *   * 16 bits: the tile's texture, A8R8G8B8, as RGBA 5551 at the colour
 *     image's address (within RDRAM's size), its width a row, from (x0, y0);
 *     the texture read, nothing put on the GPU again;
 *   * 8 bits: the bytes the tile was loaded from, its pitch a row, from the
 *     tile's offset, stepped by the rectangle's texture scale, into the
 *     render texture's colour image, cut to its width and height;
 *   * RGBA 5551 of A8R8G8B8: each channel's top five bits, alpha its top. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FrameBuffer.h"
#include "RenderBase.h"
#include "Video.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- what Rice gives them ---- */
static uint8_t ram[0x10000];
ALIGN(16, RDP_Options gRDP);
RenderTexture g_textures[MAX_TEXTURES];
GFX_INFO gfx_info;
uint32_t g_dwRamSize = sizeof(ram);
struct gDPInfo gDP;
SetImgInfo g_CI;
TMEMLoadMapInfo g_tmemLoadAddrMap[0x200];
RenderTextureInfo gRenderTextureInfos[20];
RenderTextureInfo *g_pRenderTextureInfo;

/* ---- a texture of its own, counting its uploads ---- */
static int uploads;
static void count_upload(CTexture *tex) { (void)tex; uploads++; }
static void count_release(CTexture *tex) { (void)tex; }
static const CTextureOps counting = { count_upload, count_release };
static uint32_t pixels[8 * 4];

static uint16_t at16(uint32_t addr) { return *(uint16_t *)&ram[addr]; }

static void sixteen(void)
{
   CTexture tex;
   int i;

   CHECK(ConvertRGBATo555(0xFFFF0000) == 0xF801);
   CHECK(ConvertRGBATo555(0x0000FF00) == 0x07C0);
   CHECK(ConvertRGBATo555(0x000000FF) == 0x003E);
   CHECK(ConvertRGBATo555(0x80000000) == 0x0001);
   CHECK(ConvertRGBATo555(0x7F070707) == 0x0000);
   CHECK(ConvertRGBATo555(0x00081018) == 0x0886);   /* 1, 2, 3 of 31 */

   CTexture_Init(&tex, &counting, 3, 2, AS_NORMAL);
   tex.m_dwCreatedTextureWidth = 8;
   tex.m_dwCreatedTextureHeight = 4;
   tex.m_pTexture = pixels;
   for (i = 0; i < 8 * 4; i++)
      pixels[i] = 0x80000000u | (uint32_t)(i << 3) << 16;   /* red i, opaque */
   g_textures[2].m_pCTexture = &tex;

   memset(ram, 0xEE, sizeof(ram));
   g_CI.dwAddr = 0x1000;
   g_CI.dwWidth = 16;
   uploads = 0;
   TexRectToN64FrameBuffer_16b(5, 2, 3, 2, 2);
   CHECK(uploads == 0);
   /* row y + 2, 16 wide, from x 5: the texture's row y, its pitch 8 */
   CHECK(at16(0x1000 + (2 * 16 + 5) * 2) == ConvertRGBATo555(pixels[0]));
   CHECK(at16(0x1000 + (2 * 16 + 7) * 2) == ConvertRGBATo555(pixels[2]));
   CHECK(at16(0x1000 + (3 * 16 + 6) * 2) == ConvertRGBATo555(pixels[8 + 1]));
   CHECK(at16(0x1000 + (3 * 16 + 7) * 2) == ((9 + 1) << 11 | 1));
   /* nothing else */
   CHECK(at16(0x1000 + (2 * 16 + 4) * 2) == 0xEEEE && at16(0x1000 + (2 * 16 + 8) * 2) == 0xEEEE);
   CHECK(at16(0x1000 + (1 * 16 + 5) * 2) == 0xEEEE && at16(0x1000 + (4 * 16 + 5) * 2) == 0xEEEE);
   /* the address within RDRAM's size */
   memset(ram, 0xEE, sizeof(ram));
   g_CI.dwAddr = 0x1000 + sizeof(ram);
   TexRectToN64FrameBuffer_16b(0, 0, 1, 1, 2);
   CHECK(at16(0x1000) == ConvertRGBATo555(pixels[0]));
   /* no pixels: nothing */
   memset(ram, 0xEE, sizeof(ram));
   tex.m_pTexture = NULL;
   TexRectToN64FrameBuffer_16b(0, 0, 1, 1, 2);
   CHECK(at16(0x1000) == 0xEEEE && uploads == 0);
}

static uint8_t at8(uint32_t addr) { return ram[addr ^ 3]; }

static void eight(void)
{
   uint32_t i;
   RenderTextureInfo *rt = &gRenderTextureInfos[3];

   memset(ram, 0, sizeof(ram));
   for (i = 0; i < 0x1000; i++)
      ram[0x4000 + (i ^ 3)] = (uint8_t)(i * 7 + 1);     /* byte i: i * 7 + 1 */
   gDP.tiles[1].tmem = 0x40;
   g_tmemLoadAddrMap[0x40].dwLoadAddress = 0x4000;
   gRDP.tilesinfo[1].dwPitch = 32;
   gRDP.tilesinfo[1].hilite_sl = 2;
   gRDP.tilesinfo[1].hilite_tl = 1;
   rt->N64Width = 20;
   rt->N64Height = 10;
   rt->CI_Info.dwAddr = 0x8000;
   rt->CI_Info.dwWidth = 24;
   g_pRenderTextureInfo = rt;

   /* scale 1: (x, y) from (x + 2, y + 1), 32 a row, to (x + 3, y + 4), 24 */
   TexRectToFrameBuffer_8b(3, 4, 7, 6, 0, 0, 4, 2, 1);
   CHECK(at8(0x8000 + 4 * 24 + 3) == (uint8_t)((1 * 32 + 2) * 7 + 1));
   CHECK(at8(0x8000 + 5 * 24 + 6) == (uint8_t)((2 * 32 + 5) * 7 + 1));
   CHECK(at8(0x8000 + 4 * 24 + 2) == 0 && at8(0x8000 + 4 * 24 + 7) == 0);
   CHECK(at8(0x8000 + 3 * 24 + 3) == 0 && at8(0x8000 + 6 * 24 + 3) == 0);

   /* scale 2 across, 3 down */
   memset(ram + 0x8000, 0, 0x1000);
   TexRectToFrameBuffer_8b(0, 0, 4, 2, 0, 0, 8, 6, 1);
   CHECK(at8(0x8000 + 0 * 24 + 1) == (uint8_t)((1 * 32 + 2 + 2) * 7 + 1));
   CHECK(at8(0x8000 + 1 * 24 + 3) == (uint8_t)((4 * 32 + 2 + 6) * 7 + 1));

   /* cut to the colour image's width and height (its pitch its width) */
   rt->CI_Info.dwWidth = 20;
   memset(ram + 0x8000, 0, 0x1000);
   TexRectToFrameBuffer_8b(18, 8, 26, 14, 0, 0, 8, 6, 1);
   CHECK(at8(0x8000 + 8 * 20 + 19) != 0 && at8(0x8000 + 9 * 20 + 18) != 0);
   CHECK(at8(0x8000 + 8 * 20 + 20) == 0 && at8(0x8000 + 10 * 20 + 18) == 0);
   CHECK(at8(0x8000 + 8 * 20 + 17) == 0);
   /* below it: nothing */
   memset(ram + 0x8000, 0, 0x1000);
   TexRectToFrameBuffer_8b(0, 10, 4, 12, 0, 0, 4, 2, 1);
   for (i = 0; i < 0x1000; i++)
      CHECK(ram[0x8000 + i] == 0);
   /* a pitch wider than the width: no further than width times height
      bytes (200; byte order within words as RDRAM's) */
   rt->CI_Info.dwWidth = 24;
   memset(ram + 0x8000, 0, 0x1000);
   TexRectToFrameBuffer_8b(0, 8, 20, 10, 0, 0, 20, 2, 1);
   CHECK(at8(0x8000 + 8 * 24 + 7) != 0 && at8(0x8000 + 8 * 24 + 4) != 0);
   CHECK(at8(0x8000 + 8 * 24 + 8) == 0 && at8(0x8000 + 8 * 24 + 12) == 0);
   CHECK(at8(0x8000 + 9 * 24 + 0) == 0);
   CHECK(ram[0x8000 + 200] == 0);              /* (8 * 24 + 11) ^ 3: past it */
   /* its pitch its width: no row past its height either */
   rt->CI_Info.dwWidth = 20;
   memset(ram + 0x8000, 0, 0x1000);
   TexRectToFrameBuffer_8b(0, 9, 4, 12, 0, 0, 4, 3, 1);
   CHECK(at8(0x8000 + 9 * 20 + 3) != 0);
   for (i = 200; i < 0x1000; i++)
      CHECK(ram[0x8000 + i] == 0);
}

int main(void)
{
   gfx_info.RDRAM = ram;
   sixteen();
   eight();
   printf("rice frame buffer: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
