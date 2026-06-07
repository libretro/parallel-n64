/* Compile-time SIMD dispatch shared by the angrylion RDP and VI
 * kernels, in the libretro-prboom style: SSE2 is baseline on x86-64,
 * NEON on AArch64 (the kernels use v8-only horizontal reductions, so
 * 32-bit ARM keeps the scalar paths). The scalar code next to each
 * kernel remains the fallback and the bit-exactness reference. */

#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64)
#define AL_SIMD_SSE2 1
#include <emmintrin.h>
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && (defined(__aarch64__) || defined(_M_ARM64))
#define AL_SIMD_NEON 1
#include <arm_neon.h>
#endif
