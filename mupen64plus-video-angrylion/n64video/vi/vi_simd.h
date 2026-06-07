/* VI-side aliases for the shared angrylion SIMD dispatch; see
 * n64video/simd.h for the detection rationale. */

#include "../simd.h"

#if defined(AL_SIMD_SSE2)
#define AL_VI_SSE2 1
#elif defined(AL_SIMD_NEON)
#define AL_VI_NEON 1
#endif
