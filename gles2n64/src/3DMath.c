#include <math.h>

#ifdef __LIBRETRO__ // Prefix symbol
#define DotProduct gln64DotProduct
#endif

static void MultMatrix_default( float m0[4][4], float m1[4][4],
        float dest[4][4])
{
   dest[0][0] = m0[0][0]*m1[0][0] + m0[1][0]*m1[0][1] + m0[2][0]*m1[0][2] + m0[3][0]*m1[0][3];
   dest[1][0] = m0[0][0]*m1[1][0] + m0[1][0]*m1[1][1] + m0[2][0]*m1[1][2] + m0[3][0]*m1[1][3];
   dest[2][0] = m0[0][0]*m1[2][0] + m0[1][0]*m1[2][1] + m0[2][0]*m1[2][2] + m0[3][0]*m1[2][3];
   dest[3][0] = m0[3][0]*m1[3][3] + m0[2][0]*m1[3][2] + m0[1][0]*m1[3][1] + m0[0][0]*m1[3][0];

   dest[0][1] = m0[0][1]*m1[0][0] + m0[1][1]*m1[0][1] + m0[2][1]*m1[0][2] + m0[3][1]*m1[0][3];
   dest[1][1] = m0[0][1]*m1[1][0] + m0[1][1]*m1[1][1] + m0[2][1]*m1[1][2] + m0[3][1]*m1[1][3];
   dest[2][1] = m0[0][1]*m1[2][0] + m0[1][1]*m1[2][1] + m0[2][1]*m1[2][2] + m0[3][1]*m1[2][3];
   dest[3][1] = m0[3][1]*m1[3][3] + m0[2][1]*m1[3][2] + m0[1][1]*m1[3][1] + m0[0][1]*m1[3][0];

   dest[0][2] = m0[0][2]*m1[0][0] + m0[1][2]*m1[0][1] + m0[2][2]*m1[0][2] + m0[3][2]*m1[0][3];
   dest[1][2] = m0[0][2]*m1[1][0] + m0[1][2]*m1[1][1] + m0[2][2]*m1[1][2] + m0[3][2]*m1[1][3];
   dest[2][2] = m0[0][2]*m1[2][0] + m0[1][2]*m1[2][1] + m0[2][2]*m1[2][2] + m0[3][2]*m1[2][3];
   dest[3][2] = m0[3][2]*m1[3][3] + m0[2][2]*m1[3][2] + m0[1][2]*m1[3][1] + m0[0][2]*m1[3][0];

   dest[0][3] = m0[0][3]*m1[0][0] + m0[1][3]*m1[0][1] + m0[2][3]*m1[0][2] + m0[3][3]*m1[0][3];
   dest[1][3] = m0[0][3]*m1[1][0] + m0[1][3]*m1[1][1] + m0[2][3]*m1[1][2] + m0[3][3]*m1[1][3];
   dest[2][3] = m0[0][3]*m1[2][0] + m0[1][3]*m1[2][1] + m0[2][3]*m1[2][2] + m0[3][3]*m1[2][3];
   dest[3][3] = m0[3][3]*m1[3][3] + m0[2][3]*m1[3][2] + m0[1][3]*m1[3][1] + m0[0][3]*m1[3][0];
}

static void Normalize_default(float v[3])
{
   float len = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
   if (len != 0.0f)
   {
      len = sqrtf( len );
      v[0] /= len;
      v[1] /= len;
      v[2] /= len;
   }
}

static void TransformVectorNormalize_default(float vec[3], float mtx[4][4])
{
   float len;
   float x = vec[0];
   float y = vec[1];
   float z = vec[2];

   vec[0] = mtx[0][0] * x
      + mtx[1][0] * y
      + mtx[2][0] * z;
   vec[1] = mtx[0][1] * x
      + mtx[1][1] * y
      + mtx[2][1] * z;
   vec[2] = mtx[0][2] * x
      + mtx[1][2] * y
      + mtx[2][2] * z;
   Normalize_default(vec);
}

static float DotProduct_default( float v0[3], float v1[3] )
{
   return v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2];
}


void (*MultMatrix)(float m0[4][4], float m1[4][4], float dest[4][4]) =
        MultMatrix_default;
void (*TransformVectorNormalize)(float vec[3], float mtx[4][4]) =
        TransformVectorNormalize_default;
void (*Normalize)(float v[3]) = Normalize_default;
float (*DotProduct)(float v0[3], float v1[3]) = DotProduct_default;


