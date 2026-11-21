/* Rice's texture conversions: N64 texels, from RDRAM or TMEM, to the
 * texture's pixels (A8R8G8B8, or A4R4G4B4 for the _16 ones).
 *
 *   * texels whose pixels are known: each format and size, from RDRAM as
 *     the RSP leaves it (bytes swapped in each word) and as loaded to TMEM,
 *     every other row's words swapped, through the palette in TMEM or
 *     RDRAM, as A8R8G8B8 and A4R4G4B4;
 *   * every conversion over random texels, all its ways (rows swapped or
 *     not, from TMEM or RDRAM, from RDRAM with full TMEM on, each palette
 *     format and TLUT mode, each format, one texel wide and an odd width,
 *     Conker's swap), the pixels and what is left of the texture hashed
 *     and held to the hashes Rice's conversions gave in C++;
 *   * the tables pick the conversions by format and size they did;
 *   * no pixels: nothing written, nothing uploaded; each conversion
 *     uploads once.
 *
 * Built with RICE_CONVERT_CXX, against the conversions as C++ (their
 * TxtrInfo by reference, the options and RDP state Rice's), it prints the
 * hashes instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RICE_CONVERT_CXX
#include "Config.h"
#include "ConvertImage.h"
#include "RenderBase.h"
GlobalOptions options;
ALIGN(16, RDP_Options gRDP);
TmemType g_Tmem;
bool conkerSwapHack;
int g_convk0, g_convk1, g_convk2, g_convk3, g_convk4, g_convk5;
float g_convc0, g_convc1, g_convc2, g_convc3, g_convc4, g_convc5;
static void set_full_tmem(bool on) { options.bUseFullTMEM = on; }
static void set_text_tlut(uint32_t t) { gRDP.otherMode.text_tlut = t; }
#define CALL(f, tex, ti) f(tex, *(ti))
#else
#include "ConvertImage.h"
TmemType g_Tmem;
bool conkerSwapHack;
int g_convk0, g_convk1, g_convk2, g_convk3, g_convk4, g_convk5;
float g_convc0, g_convc1, g_convc2, g_convc3, g_convc4, g_convc5;
static bool full_tmem;
static uint32_t text_tlut;
bool RiceConfig_UseFullTMEM(void) { return full_tmem; }
uint32_t RiceRDP_TextTlut(void) { return text_tlut; }
static void set_full_tmem(bool on) { full_tmem = on; }
static void set_text_tlut(uint32_t t) { text_tlut = t; }
#define CALL(f, tex, ti) f(tex, ti)
#endif

struct gDPInfo gDP;

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- a texture of its own: counts its uploads ---- */
static int uploads;
static void count_upload(CTexture *tex) { (void)tex; uploads++; }
static void count_release(CTexture *tex) { (void)tex; }
static const CTextureOps counting = { count_upload, count_release };

#define TW 16
#define TH 8
static CTexture tex;
static uint32_t pixels[TW * TH];

static void texture(uint32_t w, uint32_t h, TextureFmt fmt)
{
   CTexture_Init(&tex, &counting, w, h, AS_NORMAL);
   tex.m_dwCreatedTextureWidth  = TW;
   tex.m_dwCreatedTextureHeight = TH;
   tex.m_dwTextureFmt = fmt;
   tex.m_pTexture = pixels;
   memset(pixels, 0xCD, sizeof(pixels));
}

static uint32_t px32(int x, int y) { return pixels[y * TW + x]; }
static uint16_t px16(int x, int y) { return ((uint16_t *)pixels)[y * TW + x]; }

/* ---- RDRAM and TMEM ---- */
static uint8_t rdram[8192];
#define SRC (rdram + 256)

/* @b at byte @i of a texture in RDRAM: each word's bytes swapped */
static void put8(uint8_t *base, uint32_t i, uint8_t b) { base[i ^ 3] = b; }
static void put16(uint8_t *base, uint32_t i, uint16_t w) { *(uint16_t *)&base[(i * 2) ^ 2] = w; }

static void info(TxtrInfo *ti, uint32_t fmt, uint32_t siz, uint32_t w, uint32_t h, uint32_t pitch)
{
   memset(ti, 0, sizeof(*ti));
   ti->Format = fmt;
   ti->Size = siz;
   ti->WidthToLoad = ti->WidthToCreate = w;
   ti->HeightToLoad = ti->HeightToCreate = h;
   ti->Pitch = pitch;
   ti->pPhysicalAddress = SRC;
   ti->tileNo = -1;
   ti->TLutFmt = TLUT_FMT_NONE;
}

/* ---- texels whose pixels are known ---- */
static void known(void)
{
   TxtrInfo ti;
   uint16_t pal[16];
   int i;

   set_full_tmem(false);
   set_text_tlut(0);

   /* RGBA16: 5551 to 8888, each channel's top bits again below */
   memset(rdram, 0, sizeof(rdram));
   put16(SRC, 0, 0xF801);      /* red, opaque */
   put16(SRC, 1, 0x07C0);      /* green, clear */
   put16(SRC, 2, 0x003F);      /* blue, opaque */
   put16(SRC, 3, 0x8421);      /* 10000 10000 10000 1 */
   info(&ti, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 1, 8);
   texture(4, 1, TEXTURE_FMT_A8R8G8B8);
   uploads = 0;
   CALL(ConvertRGBA16, &tex, &ti);
   CHECK(px32(0, 0) == 0xFFFF0000);
   CHECK(px32(1, 0) == 0x0000FF00);
   CHECK(px32(2, 0) == 0xFF0000FF);
   CHECK(px32(3, 0) == 0xFF848484);
   CHECK(px32(4, 0) == 0xCDCDCDCD);   /* past the width: untouched */
   CHECK(uploads == 1);
   /* as A4R4G4B4: each channel's top four bits, alpha all or nothing */
   texture(4, 1, TEXTURE_FMT_A4R4G4B4);
   CALL(ConvertRGBA16_16, &tex, &ti);
   CHECK(px16(0, 0) == 0xFF00 && px16(1, 0) == 0x00F0 && px16(2, 0) == 0xF00F && px16(3, 0) == 0xF888);

   /* the second row 8 bytes on, from the second texel; swapped rows have
    * their words swapped (odd rows read at ^ 0x6 rather than ^ 0x2) */
   memset(rdram, 0, sizeof(rdram));
   put16(SRC, 4 + 1, 0xF801);
   *(uint16_t *)&SRC[(8 + 2) ^ 6] = 0x07C1;
   info(&ti, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 2, 8);
   ti.TopToLoad = 0;
   ti.LeftToLoad = 1;
   texture(1, 2, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertRGBA16, &tex, &ti);
   CHECK(px32(0, 1) == 0xFFFF0000);
   ti.bSwapped = true;
   CALL(ConvertRGBA16, &tex, &ti);
   CHECK(px32(0, 1) == 0xFF00FF00);

   /* RGBA32: RGBA bytes to ARGB */
   memset(rdram, 0, sizeof(rdram));
   SRC[0] = 0x11; SRC[1] = 0x22; SRC[2] = 0x33; SRC[3] = 0x44;
   info(&ti, G_IM_FMT_RGBA, G_IM_SIZ_32b, 1, 1, 4);
   texture(1, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertRGBA32, &tex, &ti);
   CHECK(px32(0, 0) == 0x11443322);    /* the word 0x44332211: R 44 G 33 B 22 A 11 */

   /* IA8: four bits of intensity, four of alpha */
   memset(rdram, 0, sizeof(rdram));
   put8(SRC, 0, 0xA5);
   put8(SRC, 1, 0x0F);
   info(&ti, G_IM_FMT_IA, G_IM_SIZ_8b, 2, 1, 8);
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertIA8, &tex, &ti);
   CHECK(px32(0, 0) == 0x55AAAAAA);
   CHECK(px32(1, 0) == 0xFF000000);

   /* IA16: intensity byte, alpha byte */
   memset(rdram, 0, sizeof(rdram));
   put16(SRC, 0, 0x80C0);
   info(&ti, G_IM_FMT_IA, G_IM_SIZ_16b, 1, 1, 8);
   texture(1, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertIA16, &tex, &ti);
   CHECK(px32(0, 0) == 0xC0808080);
   texture(1, 1, TEXTURE_FMT_A4R4G4B4);
   CALL(ConvertIA16_16, &tex, &ti);
   CHECK(px16(0, 0) == 0xC888);

   /* I8: intensity in all four */
   memset(rdram, 0, sizeof(rdram));
   put8(SRC, 0, 0x7E);
   info(&ti, G_IM_FMT_I, G_IM_SIZ_8b, 1, 1, 8);
   texture(1, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertI8, &tex, &ti);
   CHECK(px32(0, 0) == 0x7E7E7E7E);

   /* I4: two texels a byte, the high nibble first */
   memset(rdram, 0, sizeof(rdram));
   put8(SRC, 0, 0x3C);
   info(&ti, G_IM_FMT_I, G_IM_SIZ_4b, 2, 1, 8);
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertI4, &tex, &ti);
   CHECK(px32(0, 0) == 0x33333333 && px32(1, 0) == 0xCCCCCCCC);

   /* IA4: three bits of intensity, one of alpha */
   memset(rdram, 0, sizeof(rdram));
   put8(SRC, 0, 0xB4);            /* 101 1, 010 0 */
   info(&ti, G_IM_FMT_IA, G_IM_SIZ_4b, 2, 1, 8);
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertIA4, &tex, &ti);
   CHECK(px32(0, 0) == 0xFFB6B6B6 && px32(1, 0) == 0x00494949);

   /* CI4 through an RGBA16 palette in RDRAM (its entries' halves swapped
    * in each word, as the RSP leaves them) */
   memset(rdram, 0, sizeof(rdram));
   for (i = 0; i < 16; i++)
      pal[i] = 0;
   pal[0x2 ^ 1] = 0xF801;
   pal[0x7 ^ 1] = 0x003F;
   put8(SRC, 0, 0x27);
   info(&ti, G_IM_FMT_CI, G_IM_SIZ_4b, 2, 1, 8);
   ti.PalAddress = (uint8_t *)pal;
   ti.TLutFmt = TLUT_FMT_RGBA16;
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertCI4, &tex, &ti);
   CHECK(px32(0, 0) == 0xFFFF0000 && px32(1, 0) == 0xFF0000FF);
   /* an IA16 palette */
   pal[0x2 ^ 1] = 0x40FF;
   pal[0x7 ^ 1] = 0xFF00;
   ti.TLutFmt = TLUT_FMT_IA16;
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(ConvertCI4, &tex, &ti);
   CHECK(px32(0, 0) == 0xFF404040 && px32(1, 0) == 0x00FFFFFF);
   /* no palette format: nothing */
   ti.TLutFmt = TLUT_FMT_NONE;
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   uploads = 0;
   CALL(ConvertCI4, &tex, &ti);
   CHECK(px32(0, 0) == 0xCDCDCDCD && uploads == 0);

   /* from TMEM: the texels at the tile's, its rows the tile's line apart,
    * every other row's words swapped; the palette in TMEM's upper half, 16
    * entries apart each palette, each entry four times over */
   set_full_tmem(true);
   memset(&g_Tmem, 0, sizeof(g_Tmem));
   memset(&gDP, 0, sizeof(gDP));
   gDP.tiles[3].tmem = 0x20;
   gDP.tiles[3].line = 1;
   g_Tmem.g_Tmem8bit[0x20 * 8 + 0] = 0x51;
   g_Tmem.g_Tmem8bit[0x20 * 8 + 8 + 4] = 0x7E;    /* row 1 */
   info(&ti, G_IM_FMT_I, G_IM_SIZ_8b, 1, 2, 8);
   ti.tileNo = 3;
   texture(1, 2, TEXTURE_FMT_A8R8G8B8);
   CALL(Convert8b, &tex, &ti);
   CHECK(px32(0, 0) == 0x51515151);
   CHECK(px32(0, 1) == 0x7E7E7E7E);

   g_Tmem.g_Tmem8bit[0x20 * 8 + 0] = 0x12;
   g_Tmem.g_Tmem16bit[0x400 + 2 * 0x40 + (1 << 2)] = 0xF801;
   g_Tmem.g_Tmem16bit[0x400 + 2 * 0x40 + (2 << 2)] = 0x07C1;
   info(&ti, G_IM_FMT_CI, G_IM_SIZ_4b, 2, 1, 8);
   ti.tileNo = 3;
   ti.Palette = 2;
   ti.TLutFmt = TLUT_FMT_RGBA16;
   set_text_tlut(2);
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(Convert4b, &tex, &ti);
   CHECK(px32(0, 0) == 0xFFFF0000 && px32(1, 0) == 0xFF00FF00);
   /* a TLUT mode of none and an I format: intensity, not the palette */
   set_text_tlut(0);
   ti.Format = G_IM_FMT_I;
   texture(2, 1, TEXTURE_FMT_A8R8G8B8);
   CALL(Convert4b, &tex, &ti);
   CHECK(px32(0, 0) == 0x11111111 && px32(1, 0) == 0x22222222);
   set_full_tmem(false);
}

/* ---- every conversion, all its ways, hashed ---- */
typedef void (*conv_fn)(CTexture *, const TxtrInfo *);
#ifdef RICE_CONVERT_CXX
#define WRAP(f) static void w_##f(CTexture *t, const TxtrInfo *ti) { f(t, *ti); }
#else
#define WRAP(f) static void w_##f(CTexture *t, const TxtrInfo *ti) { f(t, ti); }
#endif
#define CONVERSIONS(X) \
   X(ConvertRGBA16) X(ConvertRGBA32) X(ConvertIA4) X(ConvertIA8) X(ConvertIA16) X(ConvertI4) X(ConvertI8) \
   X(ConvertCI4) X(ConvertCI8) X(ConvertCI4_RGBA16) X(ConvertCI4_IA16) X(ConvertCI8_RGBA16) \
   X(ConvertCI8_IA16) X(ConvertYUV) X(Convert4b) X(Convert8b) X(Convert16b) \
   X(ConvertRGBA16_16) X(ConvertRGBA32_16) X(ConvertIA4_16) X(ConvertIA8_16) X(ConvertIA16_16) \
   X(ConvertI4_16) X(ConvertI8_16) X(ConvertCI4_16) X(ConvertCI8_16) X(ConvertCI4_RGBA16_16) \
   X(ConvertCI4_IA16_16) X(ConvertCI8_RGBA16_16) X(ConvertCI8_IA16_16) X(ConvertYUV_16) \
   X(Convert4b_16) X(Convert8b_16) X(Convert16b_16)
CONVERSIONS(WRAP)
#define ENTRY(f) { #f, w_##f, f },
static const struct { const char *name; conv_fn call; ConvertFunction fn; } conversions[] = {
   CONVERSIONS(ENTRY)
};
#define NCONV (sizeof(conversions) / sizeof(conversions[0]))

#ifndef RICE_CONVERT_CXX
/* the hashes the C++ conversions gave, in the order above */
static const uint32_t expected[] = {
   0xbaafeb39u, /* ConvertRGBA16 */
   0x4a016a57u, /* ConvertRGBA32 */
   0x6fa73029u, /* ConvertIA4 */
   0x453aee01u, /* ConvertIA8 */
   0x4a64c111u, /* ConvertIA16 */
   0x7b7f339du, /* ConvertI4 */
   0x9bdc25e5u, /* ConvertI8 */
   0x69e20e6eu, /* ConvertCI4 */
   0x26105e7cu, /* ConvertCI8 */
   0x6c78ab30u, /* ConvertCI4_RGBA16 */
   0xc726ddb0u, /* ConvertCI4_IA16 */
   0x178605fbu, /* ConvertCI8_RGBA16 */
   0x9ae09416u, /* ConvertCI8_IA16 */
   0x90fb60b3u, /* ConvertYUV */
   0xa7489950u, /* Convert4b */
   0xf02b99c3u, /* Convert8b */
   0xa6b931a4u, /* Convert16b */
   0xc2f8c403u, /* ConvertRGBA16_16 */
   0x3527cb30u, /* ConvertRGBA32_16 */
   0xef0228bbu, /* ConvertIA4_16 */
   0xef0bfb29u, /* ConvertIA8_16 */
   0x920f6a71u, /* ConvertIA16_16 */
   0xf5b65c8au, /* ConvertI4_16 */
   0x66bed855u, /* ConvertI8_16 */
   0x315bfba4u, /* ConvertCI4_16 */
   0xe51651dbu, /* ConvertCI8_16 */
   0x0f53e862u, /* ConvertCI4_RGBA16_16 */
   0x6fc899ddu, /* ConvertCI4_IA16_16 */
   0x8ebbdd91u, /* ConvertCI8_RGBA16_16 */
   0x3ad1031fu, /* ConvertCI8_IA16_16 */
   0x2e86d573u, /* ConvertYUV_16 */
   0x277c2767u, /* Convert4b_16 */
   0x4e06d207u, /* Convert8b_16 */
   0x768a99f9u, /* Convert16b_16 */
};
#endif

static uint32_t seed;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
   const uint8_t *b = (const uint8_t *)p;
   while (n--)
      h = (h ^ *b++) * 16777619u;
   return h;
}

static uint32_t all_ways(unsigned c)
{
   static const uint32_t tluts[4] = { TLUT_FMT_NONE, TLUT_FMT_UNKNOWN, TLUT_FMT_RGBA16, TLUT_FMT_IA16 };
   static const uint32_t formats[4] = { G_IM_FMT_I, G_IM_FMT_IA, G_IM_FMT_CI, G_IM_FMT_RGBA };
   static uint16_t pal[256];
   uint32_t h = 2166136261u;
   unsigned way;
   bool sixteen = strstr(conversions[c].name, "_16") != NULL
      && strcmp(conversions[c].name, "ConvertCI4_IA16") != 0 && strcmp(conversions[c].name, "ConvertCI8_IA16") != 0
      && strcmp(conversions[c].name, "ConvertCI4_RGBA16") != 0 && strcmp(conversions[c].name, "ConvertCI8_RGBA16") != 0;
   size_t i;

   for (way = 0; way < 1024; way++)
   {
      TxtrInfo ti;
      bool tmem = (way & 1) != 0, swapped = (way & 2) != 0, narrow = (way & 64) != 0;
      bool odd = way >= 512;   /* the second half: odd widths, and RDRAM with full TMEM on */
      uint32_t fmt = formats[(way >> 7) & 3];

      seed = 0x9E3779B9u * (c + 1) + way;
      for (i = 0; i < sizeof(rdram); i++)
         rdram[i] = (uint8_t)rnd();
      for (i = 0; i < sizeof(g_Tmem); i++)
         g_Tmem.g_Tmem8bit[i] = (uint8_t)rnd();
      for (i = 0; i < 256; i++)
         pal[i] = (uint16_t)rnd();
      memset(&gDP, 0, sizeof(gDP));
      for (i = 0; i < 8; i++)
      {
         gDP.tiles[i].tmem = 0x40 + 0x20 * (uint32_t)i;
         gDP.tiles[i].line = 2 + (uint32_t)i % 3;
      }

      set_full_tmem(tmem);
      set_text_tlut((way >> 4) & 3);
      conkerSwapHack = (way & 32) != 0;
      g_convk0 = 175; g_convk1 = -43; g_convk2 = -89; g_convk3 = 222; g_convk4 = 114; g_convk5 = 42;
      g_convc0 = g_convk5 / 255.0f + 1.0f;
      g_convc1 = g_convk0 / 255.0f * g_convc0;
      g_convc2 = g_convk1 / 255.0f * g_convc0;
      g_convc3 = g_convk2 / 255.0f * g_convc0;
      g_convc4 = g_convk3 / 255.0f * g_convc0;

      info(&ti, fmt, G_IM_SIZ_8b, narrow ? 1 : odd ? 5 : 8 - (way & 4), 4 + ((way >> 3) & 1) * 2, 40);
      ti.LeftToLoad = 2;
      ti.TopToLoad = 1;
      ti.bSwapped = swapped;
      ti.PalAddress = (uint8_t *)pal;
      ti.Palette = (way >> 2) & 15;
      ti.TLutFmt = tluts[(way >> 4) & 3];
      ti.tileNo = tmem && !(odd && (way & 4)) ? (int)(way >> 1) % 8 : -1;

      texture(ti.WidthToLoad, ti.HeightToLoad, sixteen ? TEXTURE_FMT_A4R4G4B4 : TEXTURE_FMT_A8R8G8B8);
      uploads = 0;
      conversions[c].call(&tex, &ti);
      h = fnv(h, pixels, sizeof(pixels));
      h = fnv(h, &uploads, sizeof(uploads));
      h = fnv(h, &conkerSwapHack, sizeof(conkerSwapHack));
      h = fnv(h, &tex.m_bScaledS, sizeof(tex.m_bScaledS));
      h = fnv(h, &tex.m_bClampedT, sizeof(tex.m_bClampedT));
   }
   return h;
}

/* ---- the tables ---- */
static const char *name_of(ConvertFunction f)
{
   size_t i;
   if (!f)
      return "-";
   for (i = 0; i < NCONV; i++)
      if (conversions[i].fn == f)
         return conversions[i].name + 7;   /* past "Convert" */
   return "?";
}

static void table(ConvertFunction t[8][4], const char *want)
{
   char got[512];
   int f, s;
   got[0] = 0;
   for (f = 0; f < 5; f++)
      for (s = 0; s < 4; s++)
      {
         strcat(got, name_of(t[f][s]));
         strcat(got, s == 3 ? ";" : ",");
      }
   for (f = 5; f < 8; f++)
      for (s = 0; s < 4; s++)
         CHECK(t[f][s] == NULL);
   if (strcmp(got, want))
   {
      printf("FAILED table: %s\n   wanted: %s\n", got, want);
      failures++;
   }
}

int main(void)
{
   unsigned c;
   int differ = 0;
   uint32_t hashes[NCONV];

   for (c = 0; c < NCONV; c++)
      hashes[c] = all_ways(c);
#ifdef RICE_CONVERT_CXX
   for (c = 0; c < NCONV; c++)
      printf("   0x%08xu, /* %s */\n", (unsigned)hashes[c], conversions[c].name);
   return 0;
#else
   CHECK(sizeof(expected) / sizeof(expected[0]) == NCONV);
   for (c = 0; c < NCONV; c++)
      if (hashes[c] != expected[c])
      {
         printf("FAILED %s: hash %08x, C++ %08x\n", conversions[c].name, (unsigned)hashes[c], (unsigned)expected[c]);
         differ++;
      }
   failures += differ;

   known();

   /* no pixels: nothing written, nothing uploaded */
   for (c = 0; c < NCONV; c++)
   {
      TxtrInfo ti;
      info(&ti, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4, 8);
      ti.TLutFmt = TLUT_FMT_RGBA16;
      CTexture_Init(&tex, &counting, 4, 4, AS_NORMAL);
      uploads = 0;
      conversions[c].call(&tex, &ti);
      CHECK(uploads == 0);
   }

   table(gConvertFunctions,
         "CI4,CI8,RGBA16,RGBA32;-,-,YUV,-;CI4,CI8,-,-;IA4,IA8,IA16,-;I4,I8,IA16,-;");
   table(gConvertFunctions_FullTMEM,
         "4b,8b,16b,RGBA32;-,-,YUV,-;4b,8b,-,-;4b,8b,16b,-;4b,8b,16b,-;");
   table(gConvertTlutFunctions,
         "CI4,CI8,RGBA16,RGBA32;-,-,YUV,-;CI4,CI8,-,-;CI4,CI8,IA16,-;CI4,CI8,IA16,-;");
   table(gConvertFunctions_16,
         "CI4_16,CI8_16,RGBA16_16,RGBA32_16;-,-,YUV_16,-;CI4_16,CI8_16,-,-;IA4_16,IA8_16,IA16_16,-;"
         "I4_16,I8_16,RGBA16_16,-;");
   table(gConvertFunctions_16_FullTMEM,
         "4b_16,8b_16,16b_16,RGBA32_16;-,-,YUV_16,-;4b_16,8b_16,-,-;4b_16,8b_16,16b_16,-;4b_16,8b_16,16b_16,-;");
   table(gConvertTlutFunctions_16,
         "CI4_16,CI8_16,RGBA16_16,RGBA32_16;-,-,YUV_16,-;CI4_16,CI8_16,-,-;CI4_16,CI8_16,IA16_16,-;"
         "CI4_16,CI8_16,RGBA16_16,-;");

   printf("rice convert: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
#endif
}
