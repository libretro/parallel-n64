/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - VectorMath.h                                            *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2002 Rice1964                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifndef VECTORMATH_H
#define VECTORMATH_H

/* Rice's 4x4 matrices and vectors: C structs and C functions; for C++,
 * the same structs with the constructors and the product its code uses. */

typedef struct _MATRIX {
    union {
        struct {
            float        _11, _12, _13, _14;
            float        _21, _22, _23, _24;
            float        _31, _32, _33, _34;
            float        _41, _42, _43, _44;
        };
        float m[4][4];
    };
} MATRIX;

typedef struct _VECTOR3
{
    float x;
    float y;
    float z;
} VECTOR3;

typedef struct _VECTOR4
{
    float x;
    float y;
    float z;
    float w;
} VECTOR4;

#ifdef __cplusplus
struct XMATRIX;
struct XVECTOR3;
struct XVECTOR4;
#else
typedef MATRIX XMATRIX;
typedef VECTOR3 XVECTOR3;
typedef VECTOR4 XVECTOR4;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* pOut = pA times pB; pOut is neither of them */
void MatrixMultiply(XMATRIX *pOut, const XMATRIX *pA, const XMATRIX *pB);
/* pOut = pM transposed; pOut is not pM */
XMATRIX *MatrixTranspose(XMATRIX *pOut, const XMATRIX *pM);
/* pOut = (pV, 1) times pM */
void Vec3Transform(XVECTOR4 *pOut, const XVECTOR3 *pV, const XMATRIX *pM);

#ifdef __cplusplus
}

struct XMATRIX : public MATRIX
{
    XMATRIX() { }
    XMATRIX(float a11, float a12, float a13, float a14,
            float a21, float a22, float a23, float a24,
            float a31, float a32, float a33, float a34,
            float a41, float a42, float a43, float a44)
    {
        _11 = a11; _12 = a12; _13 = a13; _14 = a14;
        _21 = a21; _22 = a22; _23 = a23; _24 = a24;
        _31 = a31; _32 = a32; _33 = a33; _34 = a34;
        _41 = a41; _42 = a42; _43 = a43; _44 = a44;
    }
    XMATRIX operator * (const XMATRIX &b) const
    {
        XMATRIX r;
        MatrixMultiply(&r, this, &b);
        return r;
    }
};

struct XVECTOR3 : public VECTOR3
{
    XVECTOR3() { }
    XVECTOR3(float _x, float _y, float _z) { x = _x; y = _y; z = _z; }
};

struct XVECTOR4 : public VECTOR4
{
    XVECTOR4() { }
};
#endif

#endif
