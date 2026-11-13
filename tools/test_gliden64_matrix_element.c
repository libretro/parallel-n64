/* test_gliden64_matrix_element -- GLideN64's fixed point matrix
 * elements (mupen64plus-video-gliden64/src/3DMath.h).
 *
 * An N64 matrix element is a signed 16-bit integer part and a 16-bit
 * fraction. Joining them shifted the integer part left as a signed
 * value, which is undefined for the negative ones (and stopped the
 * sanitized build on the first matrix with a negative element). Every
 * integer part, with a spread of fractions, must come out as
 * integer + fraction / 65536.
 */
#include <stdio.h>
#include <stdlib.h>

#include "3DMath.h"

int main(void)
{
   static const u16 fractions[] = { 0, 1, 0x7fff, 0x8000, 0xfffe, 0xffff, 0x1234 };
   int failures = 0;
   int whole;
   unsigned f;

   for (whole = -32768; whole <= 32767; whole++)
      for (f = 0; f < sizeof(fractions) / sizeof(fractions[0]); f++)
      {
         const float got = GetFloatMatrixElement((s16)whole, fractions[f]);
         /* (exact in a double; the function rounds to float once) */
         const float want = (float)((double)whole * 65536.0 + (double)fractions[f]) * 1.52587890625e-05f;
         if (got != want)
         {
            if (failures < 5)
               fprintf(stderr, "element %d + %u/65536 is %f, wanted %f\n", whole, (unsigned)fractions[f], got, want);
            failures++;
         }
      }

   if (GetFloatMatrixElement(-2, 0) != -2.0f || GetFloatMatrixElement(-1, 0x8000) != -0.5f
         || GetFloatMatrixElement(1, 0x4000) != 1.25f)
   {
      fprintf(stderr, "worked elements are wrong\n");
      failures++;
   }

   if (failures)
   {
      fprintf(stderr, "test_gliden64_matrix_element: %d failure(s)\n", failures);
      return 1;
   }
   printf("test_gliden64_matrix_element: ok\n");
   return 0;
}
