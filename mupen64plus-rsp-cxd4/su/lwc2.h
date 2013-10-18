/*
 * mupen64plus-rsp-cxd4 - RSP Interpreter
 * Copyright (C) 2012-2013  RJ 'Iconoclast' Swedlow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/******************************************************************************\
* Project:  SIMD Virtualization Table for Load Word to Vector Unit (LWC2)      *
* Authors:  Iconoclast                                                         *
* Release:  2013.03.10                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

extern void LBV(int vt, int element, signed int offset, int base);
extern void LDV(int vt, int element, signed int offset, int base);
extern void LFV(int vt, int element, signed int offset, int base);
extern void LHV(int vt, int element, signed int offset, int base);
extern void LLV(int vt, int element, signed int offset, int base);
extern void LPV(int vt, int element, signed int offset, int base);
extern void LQV(int vt, int element, signed int offset, int base);
extern void LRV(int vt, int element, signed int offset, int base);
extern void LSV(int vt, int element, signed int offset, int base);
extern void LTV(int vt, int element, signed int offset, int base);
extern void LUV(int vt, int element, signed int offset, int base);

#include "lwc2/lbv.h"
#include "lwc2/lsv.h"
#include "lwc2/llv.h"
#include "lwc2/ldv.h"
#include "lwc2/lqv.h"
#include "lwc2/lrv.h"
#include "lwc2/lpv.h"
#include "lwc2/luv.h"
/* omitted from the RCP:  LXV (replaced with LHV) */
/* omitted from the RCP:  LZV (replaced with LFV) */
#include "lwc2/lhv.h"
#include "lwc2/lfv.h"
/* omitted from the RCP:  LAV */
/* omitted from the RCP:  LTWV */
#include "lwc2/ltv.h"
/* reserved */

/*
 * Note about reserved vector transfer instructions.
 *
 * The op-code matrices for LWC2 and SWC2 in compliance with modern-day
 * vector units (not only SGI) are public domain information and were
 * examined and converted from various VU patents, including but not
 * limited to GNU VICE MSP and United States patent no. 5,812,147 by SGI.
 *
 * In cases of the Nintendo 64's RCP for which this cannot be applied
 * (reserved instructions), the operations are commented out because they
 * had deserved that modern binary index into the op-code matrix but were
 * subsetted out of the matrix in a way not forward-extensible.  (HV goes
 * where XV should go, FV goes where ZV should go, etc.)
 */

void res_062(int vt, int element, signed int offset, int base)
{
    base = offset = element = vt = 0;
    message("LWC2\nRESERVED", 3);
    return;
}

static void (*SP_LWC2[32])(int, int, int, int) = {
    LBV    ,LSV    ,LLV    ,LDV    ,LQV    ,LRV    ,LPV    ,LUV    , /* 00 */
    LHV,    LFV    ,res_062,LTV,    res_062,res_062,res_062,res_062, /* 01 */
    res_062,res_062,res_062,res_062,res_062,res_062,res_062,res_062, /* 10 */
    res_062,res_062,res_062,res_062,res_062,res_062,res_062,res_062  /* 11 */
}; /* 000     001     010     011     100     101     110     111 */
