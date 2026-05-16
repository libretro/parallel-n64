#pragma once

#if defined(__arm64__) || defined(__aarch64__) || defined(__arm__)

#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#else
#ifdef __cplusplus
extern "C" {
#endif
extern void __clear_cache(void *, void *);
#ifdef __cplusplus
}
#endif
#endif

static inline void clear_instruction_cache(void* start, void* end)
{
#ifdef __APPLE__
    return sys_icache_invalidate(start, ((char*) end) - ((char*) start));
#elif defined(__arm64__) || defined(__aarch64__)
    /* Do not rely on GCC's __clear_cache implementation: it reads the
     * cache-line sizes once at startup and caches them, which is wrong
     * on big.LITTLE arm64 where icache/dcache line size varies between
     * cores. Read CTR_EL0 ourselves and iterate the actual ranges.
     * Tracking the running minimum across calls ensures we keep
     * choosing the smallest seen line size even if the OS migrates
     * this thread between cores between calls. */
    uint64_t addr, ctr_el0;
    static unsigned long icache_line_size = 0xffff;
    static unsigned long dcache_line_size = 0xffff;
    unsigned long isize, dsize;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
    isize = 4ul << ((ctr_el0 >>  0) & 0xf);
    dsize = 4ul << ((ctr_el0 >> 16) & 0xf);

    if (isize < icache_line_size) icache_line_size = isize;
    if (dsize < dcache_line_size) dcache_line_size = dsize;
    isize = icache_line_size;
    dsize = dcache_line_size;

    /* Clean + invalidate D-cache to point-of-coherency. Use civac rather
     * than cvau to work around Cortex-A53 errata 819472, 826319, 827319,
     * 824069. */
    addr = (uint64_t)start & ~(uint64_t)(dsize - 1);
    for (; addr < (uint64_t)end; addr += dsize)
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");

    /* Invalidate I-cache to point-of-unification. */
    addr = (uint64_t)start & ~(uint64_t)(isize - 1);
    for (; addr < (uint64_t)end; addr += isize)
        __asm__ volatile("ic ivau, %0" : : "r"(addr) : "memory");

    __asm__ volatile("dsb ish" : : : "memory");
    __asm__ volatile("isb"     : : : "memory");
#else
    return __clear_cache(start, end);
#endif
}

#endif
