/* test_gliden64_combiner_scales -- two of the combiner values, exact under -Ofast.
 *
 * The screen's coordinate scale is twice 1 over the buffer's size, and a
 * tile's shift is a power of two to scale its texture by. Moved to C, the
 * two float quotients of the coordinate scale sat side by side, and -Ofast
 * made them one approximate reciprocal: 2/320 came out 0x3bcccccc for
 * 0x3bcccccd, a bit off, and Killer Instinct's frames changed. Built with
 * -Ofast as the core is, the scales worked out many at a time must be what
 * an exact float quotient gives: for every 16-bit size, and every shift.
 *
 *   ./test_gliden64_combiner_scales
 */
#include <stdio.h>
#include <string.h>

#include "Graphics/CombinerValues.h"

#define N 65536

static float got[N], neg[N];

int main(void)
{
   unsigned n, bad = 0, s;
   /* side by side, as the compiler likes them */
   for (n = 1; n < N; n++)
   {
      got[n] = GLI_COORDS_SCALE(n);
      neg[n] = -GLI_COORDS_SCALE(n);
   }
   for (n = 1; n < N; n++)
   {
      volatile float fn = (float)n, one = 1.0f;
      volatile float r = one / fn;
      const float want = 2.0f * r, wantNeg = -2.0f * r;
      if (memcmp(&got[n], &want, sizeof(want)) || memcmp(&neg[n], &wantNeg, sizeof(want)))
      {
         if (bad++ < 5)
            printf("FAIL: size %u: %.9g, want %.9g\n", n, got[n], want);
      }
   }
   for (s = 0; s < 16; s++)
   {
      volatile float one = 1.0f, p = (float)(1 << (s > 10 ? 16 - s : s));
      const float want = s > 10 ? p : s > 0 ? one / p : 1.0f, scale = GLI_SHIFT_SCALE(s);
      if (memcmp(&scale, &want, sizeof(want)))
      {
         printf("FAIL: shift %u: %.9g, want %.9g\n", s, scale, want);
         bad++;
      }
   }
   printf("test_gliden64_combiner_scales: %s\n", bad ? "FAIL" : "ok");
   return bad != 0;
}
