#ifndef _3DMATH_H
#define _3DMATH_H

#include <retro_inline.h>
#include <retro_miscellaneous.h>

static INLINE float DotProduct(const float *v0, const float *v1)
{
   return v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2];
}

#endif
