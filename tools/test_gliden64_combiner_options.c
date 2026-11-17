/* test_gliden64_combiner_options -- the options GLideN64's combiner shaders are built for.
 *
 * GLideN64 stores its generated combiner shaders with a word of the
 * options they were built for, a bit each, and builds them again when it
 * changes. The bits went from a std::vector filled on each call to C; the
 * stored word must stay the same: each option alone must give its bit, in
 * the order stored shaders were saved with, multisampling at any count
 * its one bit.
 *
 *   ./test_gliden64_combiner_options
 */
#include <stdio.h>
#include <string.h>

#include "Config.h"

Config gliden64Config;

static int failures;

static void bit(u32 *field, u32 value, u32 want, const char *what)
{
   u32 got;
   memset(&gliden64Config, 0, sizeof(gliden64Config));
   *field = value;
   got = gli_combiner_options_bits();
   if (got != want)
   {
      printf("FAIL: %s: %03x, want %03x\n", what, (unsigned)got, (unsigned)want);
      failures++;
   }
}

int main(void)
{
   Config *c = &gliden64Config;
   memset(c, 0, sizeof(*c));
   if (gli_combiner_options_bits() != 0)
   {
      printf("FAIL: none set, not 0\n");
      failures++;
   }
   bit(&c->video.multisampling, 4, 1u << 0, "multisampling, 4 samples");
   bit(&c->texture.bilinearMode, 1, 1u << 1, "bilinear mode");
   bit(&c->texture.enableHalosRemoval, 1, 1u << 2, "halos removal");
   bit(&c->generalEmulation.enableHWLighting, 1, 1u << 3, "hardware lighting");
   bit(&c->generalEmulation.enableNoise, 1, 1u << 4, "noise");
   bit(&c->generalEmulation.enableLOD, 1, 1u << 5, "LOD");
   bit(&c->frameBufferEmulation.N64DepthCompare, 1, 1u << 6, "N64 depth compare");
   bit(&c->generalEmulation.enableLegacyBlending, 1, 1u << 7, "legacy blending");
   bit(&c->generalEmulation.enableFragmentDepthWrite, 1, 1u << 8, "fragment depth write");
   printf("test_gliden64_combiner_options: %s\n", failures ? "FAIL" : "ok");
   return failures != 0;
}
