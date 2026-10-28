/* test_dynarec_cache_pages -- host pages covering the dynarec cache.
 *
 * The ari64 cache lives in g_dev.r4300.extra_memory, aligned to 4 KiB only,
 * and is made executable with mprotect, which needs a page-aligned start on
 * hosts with 16 or 64 KiB pages:
 *
 *  - for 4, 16 and 64 KiB pages and every 4 KiB placement inside a page,
 *    the range starts on a page boundary, ends on one, and covers the whole
 *    32 MiB cache with less than one page of slack on either side;
 *  - a cache already on a page boundary is protected exactly;
 *  - on POSIX hosts, a buffer offset by 4 KiB from a 64 KiB boundary is
 *    made executable and back through the computed range.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "device/r4300/new_dynarec/cache_protect.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#define HAVE_HOST_MPROTECT 1
#endif

#define CACHE_SIZE ((size_t)1 << 25)

static int s_failures;

static void check_range(uintptr_t addr, size_t page)
{
   uintptr_t start;
   size_t length;

   new_dynarec_cache_pages(addr, CACHE_SIZE, page, &start, &length);

   if ((start & (page - 1)) != 0 || ((start + length) & (page - 1)) != 0)
   {
      ++s_failures;
      fprintf(stderr, "FAIL: page %lu, cache %#lx: range %#lx+%#lx is not page aligned\n",
            (unsigned long)page, (unsigned long)addr, (unsigned long)start, (unsigned long)length);
   }
   if (start > addr || start + length < addr + CACHE_SIZE)
   {
      ++s_failures;
      fprintf(stderr, "FAIL: page %lu, cache %#lx: range %#lx+%#lx does not cover the cache\n",
            (unsigned long)page, (unsigned long)addr, (unsigned long)start, (unsigned long)length);
   }
   if (addr - start >= page || (start + length) - (addr + CACHE_SIZE) >= page)
   {
      ++s_failures;
      fprintf(stderr, "FAIL: page %lu, cache %#lx: range %#lx+%#lx spills a whole page\n",
            (unsigned long)page, (unsigned long)addr, (unsigned long)start, (unsigned long)length);
   }
   if ((addr & (page - 1)) == 0 && (start != addr || length != CACHE_SIZE))
   {
      ++s_failures;
      fprintf(stderr, "FAIL: page %lu, aligned cache %#lx: range %#lx+%#lx is not exact\n",
            (unsigned long)page, (unsigned long)addr, (unsigned long)start, (unsigned long)length);
   }
}

static void test_ranges(void)
{
   static const size_t pages[] = { 0x1000, 0x4000, 0x10000 };
   /* a g_dev-like base: 64 KiB aligned, cache 0x901000 bytes in */
   const uintptr_t base = (uintptr_t)0x40000000UL;
   unsigned i;
   uintptr_t off;

   for (i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i)
   {
      for (off = 0; off < 0x10000; off += 0x1000)
         check_range(base + 0x900000 + off, pages[i]);
      check_range(base + 0x901000, pages[i]);
   }
}

#ifdef HAVE_HOST_MPROTECT
static void test_host_mprotect(void)
{
   long host_page = sysconf(_SC_PAGESIZE);
   size_t span = CACHE_SIZE + 0x20000;
   uint8_t* map;
   uintptr_t cache, start;
   size_t length;

   if (host_page <= 0)
      host_page = 4096;

   map = (uint8_t*)mmap(NULL, span, PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (map == (uint8_t*)MAP_FAILED)
   {
      fprintf(stderr, "test_dynarec_cache_pages: mmap failed, host check skipped\n");
      return;
   }

   /* 4 KiB past a 64 KiB boundary: misaligned for 16 and 64 KiB pages */
   cache = (((uintptr_t)map + 0xffff) & ~(uintptr_t)0xffff) + 0x1000;

   new_dynarec_cache_pages(cache, CACHE_SIZE, (size_t)host_page, &start, &length);
   if (mprotect((void*)start, length, PROT_READ | PROT_WRITE | PROT_EXEC) != 0
         || mprotect((void*)start, length, PROT_READ | PROT_WRITE) != 0)
   {
      ++s_failures;
      fprintf(stderr, "FAIL: host page %ld: mprotect of %#lx+%#lx failed\n",
            host_page, (unsigned long)start, (unsigned long)length);
   }

   ((volatile uint8_t*)cache)[0] = 1;
   ((volatile uint8_t*)cache)[CACHE_SIZE - 1] = 1;
   munmap(map, span);
}
#endif

int main(void)
{
   test_ranges();
#ifdef HAVE_HOST_MPROTECT
   test_host_mprotect();
#endif

   if (s_failures)
   {
      fprintf(stderr, "test_dynarec_cache_pages: %d failure(s)\n", s_failures);
      return 1;
   }
   printf("test_dynarec_cache_pages: all checks passed\n");
   return 0;
}
