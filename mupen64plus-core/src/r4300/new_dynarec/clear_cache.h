#pragma once

#if defined(__arm64__) || defined(__arm__)

#ifdef __APPLE__
#include <libkern/OSCacheControl.h>
#else
extern void __clear_cache(void *, void *);
#endif

static inline void clear_instruction_cache(void* start, void* end)
{
#ifdef __APPLE__
    return sys_icache_invalidate(start, ((char*) end) - ((char*) start));
#else
    return __clear_cache(start, end);
#endif
}

#endif
