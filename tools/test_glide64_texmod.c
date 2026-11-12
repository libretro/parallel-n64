/* test_glide64_texmod -- Glide64's texture modifications keep eight bits
 * a channel.
 *
 * They used to run on ARGB 4444, every modified texture cut to four bits
 * a channel first. Here every 8-bit texel value goes through each
 * modification set up to change nothing, and must come back as it went
 * in; a few are worked through with numbers; and each texture format
 * must widen to ARGB 8888 by repeating bits, white staying white.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../glide2gl/src/Glide64/TexMod.h"

static int failures;

#define FAIL2(msg, a, b) do { fprintf(stderr, "%s: %08x, wanted %08x\n", msg, (unsigned)(a), (unsigned)(b)); failures++; } while (0)

/* the modification with these colours and factor changes no texel */
static void identity(const char *name, unsigned mod, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t fac)
{
   uint32_t v;
   for (v = 0; v < 256; v++)
   {
      /* different channels, so that one written into another shows */
      const uint32_t in = (v << 24) | (v << 16) | ((v ^ 0x55) << 8) | (255 - v);
      uint32_t t = in;
      glide64_tex_mod_8888(&t, 1, mod, c0, c1, c2, fac);
      if (t != in)
      {
         FAIL2(name, t, in);
         return;
      }
   }
}

static void expect(const char *name, unsigned mod, uint32_t in, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t fac, uint32_t want)
{
   uint32_t t = in;
   glide64_tex_mod_8888(&t, 1, mod, c0, c1, c2, fac);
   if (t != want)
      FAIL2(name, t, want);
}

int main(void)
{
   uint32_t out[4];
   uint32_t v;

   /* ---- formats ---- */
   for (v = 0; v < 32; v++)
   {
      const uint16_t px = (uint16_t)(0x8000 | (v << 10) | ((31 - v) << 5) | v);
      const uint32_t w = (v << 3) | (v >> 2), w2 = ((31 - v) << 3) | ((31 - v) >> 2);
      glide64_tex_to_8888(GR_TEXFMT_ARGB_1555, &px, out, 1);
      if (out[0] != (0xFF000000u | (w << 16) | (w2 << 8) | w))
         FAIL2("ARGB 1555", out[0], 0xFF000000u | (w << 16) | (w2 << 8) | w);
   }
   for (v = 0; v < 256; v++)
   {
      const uint8_t  b   = (uint8_t)v;
      const uint16_t ai  = (uint16_t)((v << 8) | (255 - v));
      const uint32_t i44 = (v & 15) * 17u, a44 = (v >> 4) * 17u;
      glide64_tex_to_8888(GR_TEXFMT_ALPHA_8, &b, out, 1);
      if (out[0] != v * 0x01010101u)
         FAIL2("alpha 8", out[0], v * 0x01010101u);
      glide64_tex_to_8888(GR_TEXFMT_ALPHA_INTENSITY_44, &b, out, 1);
      if (out[0] != ((a44 << 24) | (i44 * 0x010101u)))
         FAIL2("alpha-intensity 4+4", out[0], (a44 << 24) | (i44 * 0x010101u));
      glide64_tex_to_8888(GR_TEXFMT_ALPHA_INTENSITY_88, &ai, out, 1);
      if (out[0] != ((v << 24) | ((255 - v) * 0x010101u)))
         FAIL2("alpha-intensity 8+8", out[0], (v << 24) | ((255 - v) * 0x010101u));
   }
   {
      const uint16_t white4444 = 0xFFFF, white565 = 0xFFFF;
      const uint32_t keep = 0x12345678;
      glide64_tex_to_8888(GR_TEXFMT_ARGB_4444, &white4444, out, 1);
      if (out[0] != 0xFFFFFFFFu) FAIL2("ARGB 4444 white", out[0], 0xFFFFFFFFu);
      glide64_tex_to_8888(GR_TEXFMT_RGB_565, &white565, out, 1);
      if (out[0] != 0xFFFFFFFFu) FAIL2("RGB 565 white", out[0], 0xFFFFFFFFu);
      glide64_tex_to_8888(GR_TEXFMT_ARGB_8888, &keep, out, 1);
      if (out[0] != keep) FAIL2("ARGB 8888", out[0], keep);
      if (glide64_tex_to_8888(0x7f, &keep, out, 1)) FAIL2("unknown format taken", 1, 0);
   }

   /* ---- nothing lost: every 8-bit value through each modification ---- */
   identity("tex inter colour, factor 0",     TMOD_TEX_INTER_COLOR_USING_FACTOR, 0x80402000, 0, 0, 0);
   identity("tex inter colour using black",   TMOD_TEX_INTER_COL_USING_COL1,     0x80402000, 0x00000000, 0, 0);
   identity("colour inter tex using white",   TMOD_COL_INTER_TEX_USING_COL1,     0x80402000, 0xFFFFFF00, 0, 0);
   identity("tex scale factor 0 add factor",  TMOD_TEX_SCALE_FAC_ADD_FAC,        0, 0, 0, 255);
   identity("tex sub colour mul 0 add tex",   TMOD_TEX_SUB_COL_MUL_FAC_ADD_TEX,  0x80402000, 0, 0, 0);
   identity("tex scale white add black",      TMOD_TEX_SCALE_COL_ADD_COL,        0xFFFFFF00, 0x00000000, 0, 0);
   identity("tex add black",                  TMOD_TEX_ADD_COL,                  0x00000000, 0, 0, 0);
   identity("tex sub black",                  TMOD_TEX_SUB_COL,                  0x00000000, 0, 0, 0);
   identity("tex sub black mul 1",            TMOD_TEX_SUB_COL_MUL_FAC,          0x00000000, 0, 0, 255);
   identity("black mul alpha add tex",        TMOD_COL_MUL_TEXA_ADD_TEX,         0x00000000, 0, 0, 0);
   identity("tex inter noise using black",    TMOD_TEX_INTER_NOISE_USING_COL,    0x00000000, 0, 0, 0);
   identity("tex mul white",                  TMOD_TEX_MUL_COL,                  0xFFFFFF00, 0, 0, 0);
   identity("tex scale 1 add black",          TMOD_TEX_SCALE_FAC_ADD_COL,        0x00000000, 0, 0, 255);
   identity("no modification",                0,                                 0x80402000, 0x10203040, 0x50607080, 77);

   /* ---- worked through: texel ARGB, colours RGBA ---- */
   /* (200,100,50) halfway to (100,200,150) */
   expect("tex inter colour", TMOD_TEX_INTER_COLOR_USING_FACTOR, 0x80C86432, 0x64C89600, 0, 0, 128, 0x80969664);
   /* (64,128,255) * (128,255,64) / 255 */
   expect("tex mul colour", TMOD_TEX_MUL_COL, 0xFF4080FF, 0x80FF4000, 0, 0, 0, 0xFF208040);
   /* additions and subtractions stop at white and black */
   expect("tex add colour", TMOD_TEX_ADD_COL, 0x40C86432, 0x64646400, 0, 0, 0, 0x40FFC896);
   expect("tex sub colour", TMOD_TEX_SUB_COL, 0x40C86432, 0x64646400, 0, 0, 0, 0x40640000);
   /* colour 0 to colour 1 by each texel channel: 0, 255, 51 of 255 */
   expect("colour inter colour 1 using tex", TMOD_COL_INTER_COL1_USING_TEX, 0x7F00FF33, 0x00FF0A00, 0xFF00FA00, 0, 0, 0x7F00003A);
   /* by the texel's alpha, 51 of 255 */
   expect("colour inter colour 1 using alpha", TMOD_COL_INTER_COL1_USING_TEXA, 0x33000000, 0x00FF0A00, 0xFF00FA00, 0, 0, 0x3333CC3A);
   expect("colour inter tex using alpha", TMOD_COL_INTER_TEX_USING_TEXA, 0x33FF00FA, 0x00FF0A00, 0, 0, 0, 0x3333CC3A);
   expect("tex inter colour using alpha", TMOD_TEX_INTER_COL_USING_TEXA, 0x3300FF0A, 0xFF00FA00, 0, 0, 0, 0x3333CC3A);
   /* colour minus texel, alpha too */
   expect("full colour sub tex", TMOD_FULL_COLOR_SUB_TEX, 0x10203040, 0x50403080, 0, 0, 0, 0x70301000);
   /* alpha halfway to opaque */
   expect("tex scale factor add factor", TMOD_TEX_SCALE_FAC_ADD_FAC, 0x00112233, 0, 0, 0, 0, 0xFF112233);
   expect("tex scale factor add colour", TMOD_TEX_SCALE_FAC_ADD_COL, 0x40804020, 0x10101000, 0, 0, 255, 0x40905030);

   if (failures)
   {
      fprintf(stderr, "test_glide64_texmod: %d failures\n", failures);
      return 1;
   }
   printf("test_glide64_texmod: ok\n");
   return 0;
}
