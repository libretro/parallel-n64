// 2008.03.29 H.Morii - added SSE 3DNOW! 3x3 1x3 matrix multiplication
//                      and 3DNOW! 4x4 4x4 matrix multiplication

#include <xmmintrin.h>
#include "3dmathsse.h"

void MulMatricesSSE(float m1[4][4],float m2[4][4],float r[4][4])
{
   /* [row][col]*/
   typedef float v4sf __attribute__ ((vector_size (16)));
   v4sf row0 = _mm_loadu_ps(m2[0]);
   v4sf row1 = _mm_loadu_ps(m2[1]);
   v4sf row2 = _mm_loadu_ps(m2[2]);
   v4sf row3 = _mm_loadu_ps(m2[3]);

   for (int i = 0; i < 4; ++i)
   {
      v4sf leftrow = _mm_loadu_ps(m1[i]);

      // Fill tmp with four copies of leftrow[0]
      v4sf tmp = leftrow;
      tmp = _mm_shuffle_ps (tmp, tmp, 0);
      // Calculate the four first summands
      v4sf destrow = tmp * row0;

      // Fill tmp with four copies of leftrow[1]
      tmp = leftrow;
      tmp = _mm_shuffle_ps (tmp, tmp, 1 + (1 << 2) + (1 << 4) + (1 << 6));
      destrow += tmp * row1;

      // Fill tmp with four copies of leftrow[2]
      tmp = leftrow;
      tmp = _mm_shuffle_ps (tmp, tmp, 2 + (2 << 2) + (2 << 4) + (2 << 6));
      destrow += tmp * row2;

      // Fill tmp with four copies of leftrow[3]
      tmp = leftrow;
      tmp = _mm_shuffle_ps (tmp, tmp, 3 + (3 << 2) + (3 << 4) + (3 << 6));
      destrow += tmp * row3;

      __builtin_ia32_storeups(r[i], destrow);
   }
}
