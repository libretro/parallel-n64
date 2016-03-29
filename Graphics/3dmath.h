#ifndef _3DMATH_H
#define _3DMATH_H

#include <math.h>

#include <retro_inline.h>
#include <retro_miscellaneous.h>

static INLINE float DotProduct(const float *v0, const float *v1)
{
   return v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
}

static INLINE void NormalizeVector(float *v)
{
   float len = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
   if (len == 0.0f)
      return;
   len = sqrtf( len );
   v[0] /= len;
   v[1] /= len;
   v[2] /= len;
}

#endif
