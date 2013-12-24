#include <math.h>

#ifdef __LIBRETRO__ // Prefix symbol
#define DotProduct gln64DotProduct
#endif

static void MultMatrix_default( float m1[4][4], float m2[4][4],
        float r[4][4])
{
	r[0][0] = m1[0][0] * m2[0][0] + m1[0][1] * m2[1][0] + m1[0][2] * m2[2][0] + m1[0][3] * m2[3][0];
	r[0][1] = m1[0][0] * m2[0][1] + m1[0][1] * m2[1][1] + m1[0][2] * m2[2][1] + m1[0][3] * m2[3][1];
	r[0][2] = m1[0][0] * m2[0][2] + m1[0][1] * m2[1][2] + m1[0][2] * m2[2][2] + m1[0][3] * m2[3][2];
	r[0][3] = m1[0][0] * m2[0][3] + m1[0][1] * m2[1][3] + m1[0][2] * m2[2][3] + m1[0][3] * m2[3][3];

	r[1][0] = m1[1][0] * m2[0][0] + m1[1][1] * m2[1][0] + m1[1][2] * m2[2][0] + m1[1][3] * m2[3][0];
	r[1][1] = m1[1][0] * m2[0][1] + m1[1][1] * m2[1][1] + m1[1][2] * m2[2][1] + m1[1][3] * m2[3][1];
	r[1][2] = m1[1][0] * m2[0][2] + m1[1][1] * m2[1][2] + m1[1][2] * m2[2][2] + m1[1][3] * m2[3][2];
	r[1][3] = m1[1][0] * m2[0][3] + m1[1][1] * m2[1][3] + m1[1][2] * m2[2][3] + m1[1][3] * m2[3][3];

	r[2][0] = m1[2][0] * m2[0][0] + m1[2][1] * m2[1][0] + m1[2][2] * m2[2][0] + m1[2][3] * m2[3][0];
	r[2][1] = m1[2][0] * m2[0][1] + m1[2][1] * m2[1][1] + m1[2][2] * m2[2][1] + m1[2][3] * m2[3][1];
	r[2][2] = m1[2][0] * m2[0][2] + m1[2][1] * m2[1][2] + m1[2][2] * m2[2][2] + m1[2][3] * m2[3][2];
	r[2][3] = m1[2][0] * m2[0][3] + m1[2][1] * m2[1][3] + m1[2][2] * m2[2][3] + m1[2][3] * m2[3][3];

	r[3][0] = m1[3][0] * m2[0][0] + m1[3][1] * m2[1][0] + m1[3][2] * m2[2][0] + m1[3][3] * m2[3][0];
	r[3][1] = m1[3][0] * m2[0][1] + m1[3][1] * m2[1][1] + m1[3][2] * m2[2][1] + m1[3][3] * m2[3][1];
	r[3][2] = m1[3][0] * m2[0][2] + m1[3][1] * m2[1][2] + m1[3][2] * m2[2][2] + m1[3][3] * m2[3][2];
	r[3][3] = m1[3][0] * m2[0][3] + m1[3][1] * m2[1][3] + m1[3][2] * m2[2][3] + m1[3][3] * m2[3][3];
}

static void TransformVectorNormalize_default(float vec[3], float mtx[4][4])
{
    float len;

    vec[0] = mtx[0][0] * vec[0]
           + mtx[1][0] * vec[1]
           + mtx[2][0] * vec[2];
    vec[1] = mtx[0][1] * vec[0]
           + mtx[1][1] * vec[1]
           + mtx[2][1] * vec[2];
    vec[2] = mtx[0][2] * vec[0]
           + mtx[1][2] * vec[1]
           + mtx[2][2] * vec[2];
    len = vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2];
    if (len != 0.0)
    {
        len = sqrtf(len);
        vec[0] /= len;
        vec[1] /= len;
        vec[2] /= len;
    }
}

static void Normalize_default(float v[3])
{
    float len;

    len = (float)(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len != 0.0)
    {
        len = (float)sqrt( len );
        v[0] /= (float)len;
        v[1] /= (float)len;
        v[2] /= (float)len;
    }
}

static float DotProduct_default( float v0[3], float v1[3] )
{
    float dot;
    dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2];
    return dot;
}

void (*MultMatrix)(float m0[4][4], float m1[4][4], float dest[4][4]) =
        MultMatrix_default;
void (*TransformVectorNormalize)(float vec[3], float mtx[4][4]) =
        TransformVectorNormalize_default;
void (*Normalize)(float v[3]) = Normalize_default;
float (*DotProduct)(float v0[3], float v1[3]) = DotProduct_default;

