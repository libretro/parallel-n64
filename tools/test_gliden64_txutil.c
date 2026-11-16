/* test_gliden64_txutil -- GLideNHQ's checksums give what they gave.
 *
 * Hi-res texture packs are named by Rice's CRC32 of each texture, and of
 * its palette for 4- and 8-bit ones, as far as its indices reach: a
 * checksum off by a bit and no texture of a pack is found. TxUtil went
 * from a C++ class to C89 functions. Each case here runs on noise (the
 * 4- and 8-bit ones with their indices held to a range, so the palette's
 * reach varies), at sizes square and not, rows padded and not, and its
 * CRC32, its 64 bits without a palette and with one must be what the C++
 * class gave: the values below were taken from it. The texture buffers
 * and the pool's acquire and release are checked too.
 *
 *   ./test_gliden64_txutil
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLideNHQ/TxUtil.h"

static const struct
{
   int w, h, bpl, size;
   uint32 crc;
   uint64 crc64, crc64_pal;
} cases[] = {
   { 8, 8, 4, 0x000, 0x8a7f175cu, 0x000000008a7f175cull, 0xc1df8bfc8a7f175cull },
   { 8, 8, 4, 0x100, 0x8a7f175cu, 0x000000008a7f175cull, 0xc1df8bfc8a7f175cull },
   { 8, 8, 4, 0x200, 0x8a7f175cu, 0x000000008a7f175cull, 0xc1df8bfc8a7f175cull },
   { 8, 8, 8, 0x001, 0x4b500b22u, 0x000000004b500b22ull, 0x4fd00a164b500b22ull },
   { 8, 8, 8, 0x101, 0x4b500b22u, 0x000000004b500b22ull, 0x4fd00a164b500b22ull },
   { 8, 8, 8, 0x201, 0x4b500b22u, 0x000000004b500b22ull, 0x4fd00a164b500b22ull },
   { 8, 8, 16, 0x002, 0x6a95c691u, 0x000000006a95c691ull, 0x000000006a95c691ull },
   { 8, 8, 16, 0x102, 0x6a95c691u, 0x000000006a95c691ull, 0x000000006a95c691ull },
   { 8, 8, 16, 0x202, 0x6a95c691u, 0x000000006a95c691ull, 0x000000006a95c691ull },
   { 8, 8, 32, 0x003, 0x431dc4bdu, 0x00000000431dc4bdull, 0x00000000431dc4bdull },
   { 8, 8, 32, 0x103, 0x431dc4bdu, 0x00000000431dc4bdull, 0x00000000431dc4bdull },
   { 8, 8, 32, 0x203, 0x431dc4bdu, 0x00000000431dc4bdull, 0x00000000431dc4bdull },
   { 32, 16, 16, 0x000, 0x7f248bd8u, 0x000000007f248bd8ull, 0x99e814c17f248bd8ull },
   { 32, 16, 16, 0x100, 0x7f248bd8u, 0x000000007f248bd8ull, 0x99e814c17f248bd8ull },
   { 32, 16, 16, 0x200, 0x7f248bd8u, 0x000000007f248bd8ull, 0x99e814c17f248bd8ull },
   { 32, 16, 32, 0x001, 0xdba8f4e6u, 0x00000000dba8f4e6ull, 0xdaa4b7b7dba8f4e6ull },
   { 32, 16, 32, 0x101, 0xdba8f4e6u, 0x00000000dba8f4e6ull, 0xdaa4b7b7dba8f4e6ull },
   { 32, 16, 32, 0x201, 0xdba8f4e6u, 0x00000000dba8f4e6ull, 0xdaa4b7b7dba8f4e6ull },
   { 32, 16, 64, 0x002, 0xea994fb8u, 0x00000000ea994fb8ull, 0x00000000ea994fb8ull },
   { 32, 16, 64, 0x102, 0xea994fb8u, 0x00000000ea994fb8ull, 0x00000000ea994fb8ull },
   { 32, 16, 64, 0x202, 0xea994fb8u, 0x00000000ea994fb8ull, 0x00000000ea994fb8ull },
   { 32, 16, 128, 0x003, 0xa0d4c89cu, 0x00000000a0d4c89cull, 0x00000000a0d4c89cull },
   { 32, 16, 128, 0x103, 0xa0d4c89cu, 0x00000000a0d4c89cull, 0x00000000a0d4c89cull },
   { 32, 16, 128, 0x203, 0xa0d4c89cu, 0x00000000a0d4c89cull, 0x00000000a0d4c89cull },
   { 13, 7, 7, 0x000, 0x62c2110fu, 0x0000000062c2110full, 0xc1df8bfc62c2110full },
   { 13, 7, 7, 0x100, 0x62c2110fu, 0x0000000062c2110full, 0xc1df8bfc62c2110full },
   { 13, 7, 7, 0x200, 0x62c2110fu, 0x0000000062c2110full, 0xc1df8bfc62c2110full },
   { 13, 7, 13, 0x001, 0xb1c5b692u, 0x00000000b1c5b692ull, 0xa0c5eeb1b1c5b692ull },
   { 13, 7, 13, 0x101, 0xb1c5b692u, 0x00000000b1c5b692ull, 0xa0c5eeb1b1c5b692ull },
   { 13, 7, 13, 0x201, 0xb1c5b692u, 0x00000000b1c5b692ull, 0xa0c5eeb1b1c5b692ull },
   { 13, 7, 26, 0x002, 0x86d083b6u, 0x0000000086d083b6ull, 0x0000000086d083b6ull },
   { 13, 7, 26, 0x102, 0x86d083b6u, 0x0000000086d083b6ull, 0x0000000086d083b6ull },
   { 13, 7, 26, 0x202, 0x86d083b6u, 0x0000000086d083b6ull, 0x0000000086d083b6ull },
   { 13, 7, 52, 0x003, 0x7022c968u, 0x000000007022c968ull, 0x000000007022c968ull },
   { 13, 7, 52, 0x103, 0x7022c968u, 0x000000007022c968ull, 0x000000007022c968ull },
   { 13, 7, 52, 0x203, 0x7022c968u, 0x000000007022c968ull, 0x000000007022c968ull },
   { 64, 32, 48, 0x000, 0xe10c24cfu, 0x00000000e10c24cfull, 0x99e814c1e10c24cfull },
   { 64, 32, 48, 0x100, 0xe10c24cfu, 0x00000000e10c24cfull, 0x99e814c1e10c24cfull },
   { 64, 32, 48, 0x200, 0xe10c24cfu, 0x00000000e10c24cfull, 0x99e814c1e10c24cfull },
   { 64, 32, 80, 0x001, 0x5ea609f4u, 0x000000005ea609f4ull, 0xdaa4b7b75ea609f4ull },
   { 64, 32, 80, 0x101, 0x5ea609f4u, 0x000000005ea609f4ull, 0xdaa4b7b75ea609f4ull },
   { 64, 32, 80, 0x201, 0x5ea609f4u, 0x000000005ea609f4ull, 0xdaa4b7b75ea609f4ull },
   { 64, 32, 144, 0x002, 0x47aaa7a9u, 0x0000000047aaa7a9ull, 0x0000000047aaa7a9ull },
   { 64, 32, 144, 0x102, 0x47aaa7a9u, 0x0000000047aaa7a9ull, 0x0000000047aaa7a9ull },
   { 64, 32, 144, 0x202, 0x47aaa7a9u, 0x0000000047aaa7a9ull, 0x0000000047aaa7a9ull },
   { 64, 32, 272, 0x003, 0x79f408a4u, 0x0000000079f408a4ull, 0x0000000079f408a4ull },
   { 64, 32, 272, 0x103, 0x79f408a4u, 0x0000000079f408a4ull, 0x0000000079f408a4ull },
   { 64, 32, 272, 0x203, 0x79f408a4u, 0x0000000079f408a4ull, 0x0000000079f408a4ull },
   { 1, 1, 1, 0x000, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 1, 0x100, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 1, 0x200, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 1, 0x001, 0x00000000u, 0x0000000000000000ull, 0x30ed717a00000000ull },
   { 1, 1, 1, 0x101, 0x00000000u, 0x0000000000000000ull, 0x30ed717a00000000ull },
   { 1, 1, 1, 0x201, 0x00000000u, 0x0000000000000000ull, 0x30ed717a00000000ull },
   { 1, 1, 2, 0x002, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 2, 0x102, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 2, 0x202, 0x00000000u, 0x0000000000000000ull, 0x0000000000000000ull },
   { 1, 1, 4, 0x003, 0x810430b4u, 0x00000000810430b4ull, 0x00000000810430b4ull },
   { 1, 1, 4, 0x103, 0x810430b4u, 0x00000000810430b4ull, 0x00000000810430b4ull },
   { 1, 1, 4, 0x203, 0x810430b4u, 0x00000000810430b4ull, 0x00000000810430b4ull },
   { 4, 64, 10, 0x000, 0x000007e0u, 0x00000000000007e0ull, 0x99e814c1000007e0ull },
   { 4, 64, 10, 0x100, 0x000007e0u, 0x00000000000007e0ull, 0x99e814c1000007e0ull },
   { 4, 64, 10, 0x200, 0x000007e0u, 0x00000000000007e0ull, 0x99e814c1000007e0ull },
   { 4, 64, 12, 0x001, 0x0214a95au, 0x000000000214a95aull, 0xdaa4b7b70214a95aull },
   { 4, 64, 12, 0x101, 0x0214a95au, 0x000000000214a95aull, 0xdaa4b7b70214a95aull },
   { 4, 64, 12, 0x201, 0x0214a95au, 0x000000000214a95aull, 0xdaa4b7b70214a95aull },
   { 4, 64, 16, 0x002, 0xee0dee6eu, 0x00000000ee0dee6eull, 0x00000000ee0dee6eull },
   { 4, 64, 16, 0x102, 0xee0dee6eu, 0x00000000ee0dee6eull, 0x00000000ee0dee6eull },
   { 4, 64, 16, 0x202, 0xee0dee6eu, 0x00000000ee0dee6eull, 0x00000000ee0dee6eull },
   { 4, 64, 24, 0x003, 0x374b6c71u, 0x00000000374b6c71ull, 0x00000000374b6c71ull },
   { 4, 64, 24, 0x103, 0x374b6c71u, 0x00000000374b6c71ull, 0x00000000374b6c71ull },
   { 4, 64, 24, 0x203, 0x374b6c71u, 0x00000000374b6c71ull, 0x00000000374b6c71ull },
};

/* the C++ harness's noise */
static void fill(uint8 *t, int n, unsigned seed)
{
   int i;
   for (i = 0; i < n; i++)
   {
      seed = seed * 1103515245u + 12345u;
      t[i] = (uint8)(seed >> 16);
   }
}

int main(void)
{
   static uint8 src[256 * 256 * 4], pal[512];
   static const int pad[6] = { 0, 0, 0, 16, 0, 8 };
   unsigned k;
   int failures = 0;

   fill(pal, 512, 99);
   for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
   {
      /* the harness's order: six sizes, four texel sizes, three formats */
      const int d = (int)(k / 12), size = cases[k].size & 0xff, w = cases[k].w, h = cases[k].h;
      const int bpl = ((w << size) + 1) / 2 + pad[d];
      uint32 crc;
      uint64 c64, c64p;
      fill(src, sizeof(src), (unsigned)(1000 + d * 4 + size));
      if (size <= 1)
      {
         int i;
         for (i = 0; i < bpl * h; i++)
            src[i] &= (uint8)(size == 0 ? (d & 1 ? 0x77 : 0xff) : (d & 1 ? 0x3f : 0xff));
      }
      crc = txutil_checksum(src, w, h, cases[k].size, bpl);
      c64 = txutil_checksum64(src, w, h, cases[k].size, bpl, NULL);
      c64p = txutil_checksum64(src, w, h, cases[k].size, bpl, pal);
      if (bpl != cases[k].bpl || crc != cases[k].crc || c64 != cases[k].crc64 || c64p != cases[k].crc64_pal)
      {
         printf("FAIL: %dx%d, size %d, format %d: %08x %08x%08x %08x%08x\n", w, h, size, cases[k].size >> 8,
               (unsigned)crc, (unsigned)(c64 >> 32), (unsigned)c64, (unsigned)(c64p >> 32), (unsigned)c64p);
         failures++;
      }
   }
   if (txutil_checksum(NULL, 8, 8, 2, 16) != 0 || txutil_checksum64(NULL, 8, 8, 2, 16, pal) != 0)
   {
      printf("FAIL: no texture, a checksum\n");
      failures++;
   }

   /* the texture buffers: made, each its own, then gone */
   if (!txmembuf_init(64, 32) || !txmembuf_get(0) || !txmembuf_get(1) || txmembuf_get(0) == txmembuf_get(1)
         || txmembuf_size(0) != 64 * 32 * 4 || !txmembuf_scratch(0) || !txmembuf_scratch(1) || txmembuf_get(2))
   {
      printf("FAIL: the texture buffers\n");
      failures++;
   }
   memset(txmembuf_get(1), 0, txmembuf_size(1));
   memset(txmembuf_scratch(1), 0, 64 * 32 * sizeof(uint32));
   txmembuf_shutdown();
   if (txmembuf_get(0) || txmembuf_size(0) || txmembuf_scratch(1))
   {
      printf("FAIL: the texture buffers after shutdown\n");
      failures++;
   }

   /* the pool: there while held, gone with the last release */
   txutil_pool_acquire();
   txutil_pool_acquire();
   txutil_pool_release();
   if (!txutil_pool())
   {
      printf("FAIL: the pool gone while still held\n");
      failures++;
   }
   txutil_pool_release();
   if (txutil_pool() || txutil_cores() < 1 || txutil_cores() > MAX_NUMCORE)
   {
      printf("FAIL: the pool after its last release, or the cores\n");
      failures++;
   }

   printf("test_gliden64_txutil: %s, %u cases\n", failures ? "FAIL" : "ok", (unsigned)(sizeof(cases) / sizeof(cases[0])));
   return failures != 0;
}
