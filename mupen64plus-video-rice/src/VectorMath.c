/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - VectorMath.c                                            *
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
#include "VectorMath.h"

void MatrixMultiply(XMATRIX *pOut, const XMATRIX *pA, const XMATRIX *pB)
{
    int i, j;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            pOut->m[i][j] = pA->m[i][0] * pB->m[0][j] +
                            pA->m[i][1] * pB->m[1][j] +
                            pA->m[i][2] * pB->m[2][j] +
                            pA->m[i][3] * pB->m[3][j];
}

XMATRIX *MatrixTranspose(XMATRIX *pOut, const XMATRIX *pM)
{
    int i, j;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            pOut->m[i][j] = pM->m[j][i];
    return pOut;
}

void Vec3Transform(XVECTOR4 *pOut, const XVECTOR3 *pV, const XMATRIX *pM)
{
    pOut->x = pV->x*pM->_11 + pV->y*pM->_21 + pV->z*pM->_31 + pM->_41;
    pOut->y = pV->x*pM->_12 + pV->y*pM->_22 + pV->z*pM->_32 + pM->_42;
    pOut->z = pV->x*pM->_13 + pV->y*pM->_23 + pV->z*pM->_33 + pM->_43;
    pOut->w = pV->x*pM->_14 + pV->y*pM->_24 + pV->z*pM->_34 + pM->_44;
}
