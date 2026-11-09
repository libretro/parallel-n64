#ifndef _3DMATH_H
#define _3DMATH_H
#ifdef __cplusplus
#include <utility>
#include "GBI.h"
#endif
#include <memory.h>
#include <string.h>
#include <retro_inline.h>
#include <Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void gli_MultMatrix(float m0[4][4], float m1[4][4], float dest[4][4]);
void gli_MultMatrix2(float m0[4][4], float m1[4][4]);
void gli_TransformVectorNormalize(float *vec, float mtx[4][4]);
void gli_InverseTransformVectorNormalize(float *src, float *dst, float mtx[4][4]);
void gli_InverseTransformVectorNormalizeN(float src[][3], float dst[][3], float mtx[4][4], u32 count);
void gli_Normalize(float *v);
void gli_CopyMatrix( float m0[4][4], float m1[4][4]);

static INLINE float DotProduct(const float *v0, const float *v1)
{
	float	dot;
#ifdef WIN32_ASM
	__asm {
		mov		esi, dword ptr [v0]
		mov		edi, dword ptr [v1]
		lea		ebx, [dot]

		fld		dword ptr [esi]
		fmul	dword ptr [edi]
		fld		dword ptr [esi+04h]
		fmul	dword ptr [edi+04h]
		fld		dword ptr [esi+08h]
		fmul	dword ptr [edi+08h]
		fadd
		fadd
		fstp	dword ptr [ebx]
	}
#else /* WIN32_ASM */
	dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2];
#endif /* WIN32_ASM */
	return dot;
}

static INLINE float GetFloatMatrixElement(s16 _int, u16 _fract)
{
	const s32 element = (_int << 16) | _fract;
	return (float)element * 1.52587890625e-05f;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
inline std::pair<s16, u16> GetIntMatrixElement(f32 _elem)
{
	const s32 value = static_cast<s32>(_elem * 65536.0f);
	return std::pair<s16, u16>(static_cast<s16>(value >> 16), static_cast<u16>(value & 0xFFFF));
}

#endif

#endif
