#pragma once

#include <stdint.h>

// inlining
#define INLINE inline

#ifdef _MSC_VER
#define STRICTINLINE __forceinline
#else
#define STRICTINLINE INLINE
#endif

// thread-local storage
#if defined(_MSC_VER)
    #define TLS __declspec(thread)
#elif defined(__GNUC__)
   #define TLS __thread
#else
    #define TLS _Thread_local // C11
#endif

#define GET_LOW(x)  (((x) & 0x3e) << 2)
#define GET_MED(x)  (((x) & 0x7c0) >> 3)
#define GET_HI(x)   (((x) >> 8) & 0xf8)

int32_t irand(void);
