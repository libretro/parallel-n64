/* Every shader the Vulkan backend writes is valid SPIR-V: the generic
 * combiner and the special shaders under every setting that shapes them,
 * and random combiners. Each module goes through spirv-val. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vk_spirv.h>
#include <vk_shaders.h>

static spv_module module_;
static unsigned   checked, failures;

static void validate(const uint32_t *words, unsigned count, const char *what)
{
   FILE *f;
   int   rc;

   if (!words)
   {
      printf("FAIL %s: did not fit\n", what);
      failures++;
      return;
   }
   f = fopen("spirv_check.spv", "wb");
   if (!f)
   {
      printf("FAIL cannot write\n");
      failures++;
      return;
   }
   fwrite(words, sizeof(uint32_t), count, f);
   fclose(f);
   rc = system("spirv-val --target-env vulkan1.0 spirv_check.spv");
   checked++;
   if (rc != 0)
   {
      char cmd[128];
      printf("FAIL %s\n", what);
      failures++;
      sprintf(cmd, "cp spirv_check.spv failed_%u.spv", failures);
      if (failures < 8 && system(cmd) != 0)
         printf("could not keep it\n");
   }
}

static int rnd(int n) { return rand() % n; }

static void random_stage(int *ops)
{
   int n = rnd(4) + 1, i;
   for (i = 0; i < 4; i++)
      ops[i] = -1;
   if (rnd(4) == 0)
   {
      ops[0] = (4 << 8) | rnd(21);
      ops[1] = rnd(21);
      ops[2] = rnd(21);
      return;
   }
   ops[0] = rnd(21);
   for (i = 1; i < n; i++)
      ops[i] = ((rnd(3) + 1) << 8) | rnd(21);
}

int main(int argc, char **argv)
{
   vkr_shader_config cfg;
   unsigned          count = 0;
   const uint32_t   *words;
   unsigned          bits;
   int               filter, msaa, i, k, randoms;

   randoms = argc > 1 ? atoi(argv[1]) : 40;
   if (!spv_init(&module_))
      return 1;

   memset(&cfg, 0, sizeof(cfg));
   cfg.noise_width  = 640;
   cfg.noise_height = 580;

   for (bits = 0; bits < 32; bits++)
   {
      cfg.legacy_blending      = (bits & 1) != 0;
      cfg.noise                = (bits & 2) != 0;
      cfg.fragment_depth_write = (bits & 4) != 0;
      cfg.n64_depth_compare    = (bits & 8) != 0;
      cfg.enable_lod           = (bits & 16) != 0;
      cfg.interlock            = cfg.n64_depth_compare && (bits & 3) == 3;
      for (filter = 0; filter < 4; filter++)
      {
         cfg.filter_mode = filter;
         cfg.three_point = (filter & 1) == 0;
         for (msaa = 0; msaa <= 4; msaa += 4)
         {
            vkr_combiner_desc desc;
            char what[64];
            cfg.msaa = msaa;
            memset(&desc, 0, sizeof(desc));
            sprintf(what, "generic bits %u filter %d msaa %d", bits, filter, msaa);
            words = vkr_write_combiner(&module_, &cfg, &desc, 1, &count);
            validate(words, count, what);
         }
      }
   }

   cfg.msaa = 2;
   for (i = 0; i < randoms; i++)
   {
      vkr_combiner_desc desc;
      char what[64];
      int cycle = rnd(4);

      memset(&desc, 0, sizeof(desc));
      for (k = 0; k < 4; k++)
         random_stage(&desc.ops[k * 4]);
      desc.flags = rnd(1 << 12) | (cycle << VKR_CF_CYCLE_SHIFT);
      cfg.legacy_blending      = rnd(2);
      cfg.noise                = rnd(2);
      cfg.fragment_depth_write = rnd(2);
      cfg.n64_depth_compare    = rnd(2);
      cfg.enable_lod           = rnd(2);
      cfg.filter_mode          = rnd(4);
      cfg.three_point          = (cfg.filter_mode & 1) == 0;
      cfg.interlock            = cfg.n64_depth_compare && rnd(2);
      sprintf(what, "combiner %d", i);
      words = vkr_write_combiner(&module_, &cfg, &desc, 0, &count);
            validate(words, count, what);
   }

   for (k = 0; k < VKR_VS_COUNT; k++)
   {
      char what[32];
      sprintf(what, "vertex %d", k);
      words = vkr_write_vertex(&module_, &cfg, k, &count);
            validate(words, count, what);
   }
   for (bits = 0; bits < 8; bits++)
   {
      cfg.fragment_depth_write = (bits & 1) != 0;
      cfg.n64_depth_compare    = (bits & 2) != 0;
      cfg.three_point          = (bits & 4) != 0;
      for (k = 0; k < VKR_FS_COUNT; k++)
      {
         char what[32];
         sprintf(what, "special %d bits %u", k, bits);
         words = vkr_write_special(&module_, &cfg, k, &count);
            validate(words, count, what);
      }
   }

   spv_free(&module_);
   remove("spirv_check.spv");
   if (failures)
   {
      printf("gliden64 spirv: %u of %u failed\n", failures, checked);
      return 1;
   }
   printf("gliden64 spirv: %u modules ok\n", checked);
   return 0;
}
