#ifndef _3DMATH_H
#define _3DMATH_H
#include <memory.h>
#include <string.h>

void MultMatrix( float m0[4][4], float m1[4][4], float dest[4][4]);
void MultMatrix2(float m0[4][4], float m1[4][4] );
void TransformVectorNormalize(float vec[3], float mtx[4][4]);

inline void CopyMatrix( float m0[4][4], float m1[4][4] )
{
	memcpy( m0, m1, 16 * sizeof( float ) );
}

inline void Transpose3x3Matrix( float mtx[4][4] )
{
	float tmp;

	tmp = mtx[0][1];
	mtx[0][1] = mtx[1][0];
	mtx[1][0] = tmp;

	tmp = mtx[0][2];
	mtx[0][2] = mtx[2][0];
	mtx[2][0] = tmp;

	tmp = mtx[1][2];
	mtx[1][2] = mtx[2][1];
	mtx[2][1] = tmp;
}

inline void Normalize(float v[3])
{
	float len;

	len = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	if (len != 0.0)	{
		len = sqrtf( len );
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
}


inline float DotProduct(const float v0[3], const float v1[3])
{
	float dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2];
	return dot;
}

#endif
