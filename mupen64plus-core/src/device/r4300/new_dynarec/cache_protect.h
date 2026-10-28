/* Page range covering the dynarec cache.
 *
 * The cache sits in g_dev.r4300.extra_memory, which is 4 KiB aligned inside
 * g_dev, while mprotect needs a start on the host's page boundary and hosts
 * run 4, 16 or 64 KiB pages. The cache is protected through the whole pages
 * that hold it: the neighbouring g_dev bytes on the first and last page keep
 * their read/write access and are only made executable as well. */
#ifndef M64P_DEVICE_R4300_NEW_DYNAREC_CACHE_PROTECT_H
#define M64P_DEVICE_R4300_NEW_DYNAREC_CACHE_PROTECT_H

#include <stddef.h>
#include <stdint.h>

/* page_size is a power of two */
static void new_dynarec_cache_pages(uintptr_t addr, size_t size, size_t page_size,
      uintptr_t* start, size_t* length)
{
   uintptr_t mask  = (uintptr_t)page_size - 1;
   uintptr_t first = addr & ~mask;
   uintptr_t last  = (addr + size + mask) & ~mask;

   *start  = first;
   *length = (size_t)(last - first);
}

#endif
