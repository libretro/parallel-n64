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
* Project:  SIMD Virtualization Table:  Store Word from Vector Unit (SWC2)     *
* Authors:  Iconoclast                                                         *
* Release:  2013.03.10                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/

extern void SBV(int vt, int element, signed int offset, int base);
extern void SDV(int vt, int element, signed int offset, int base);
extern void SFV(int vt, int element, signed int offset, int base);
extern void SHV(int vt, int element, signed int offset, int base);
extern void SLV(int vt, int element, signed int offset, int base);
extern void SPV(int vt, int element, signed int offset, int base);
extern void SQV(int vt, int element, signed int offset, int base);
extern void SRV(int vt, int element, signed int offset, int base);
extern void SSV(int vt, int element, signed int offset, int base);
extern void STV(int vt, int element, signed int offset, int base);
extern void SUV(int vt, int element, signed int offset, int base);
extern void SWV(int vt, int element, signed int offset, int base);

#include "swc2/sbv.h"
#include "swc2/ssv.h"
#include "swc2/slv.h"
#include "swc2/sdv.h"
#include "swc2/sqv.h"
#include "swc2/srv.h"
#include "swc2/spv.h"
#include "swc2/suv.h"
/* omitted from the RCP:  SXV (replaced with SHV) */
/* omitted from the RCP:  SZV (replaced with SFV) */
#include "swc2/shv.h"
#include "swc2/sfv.h"
/* omitted from the RCP:  SAV */
#include "swc2/swv.h"
#include "swc2/stv.h"
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

void res_072(int vt, int element, signed int offset, int base)
{
    base = offset = element = vt = 0;
    message("SWC2\nRESERVED", 3);
    return;
}

static void (*SP_SWC2[32])(int, int, int, int) = {
    SBV    ,SSV    ,SLV    ,SDV    ,SQV    ,SRV    ,SPV    ,SUV    , /* 00 */
    SHV,    SFV    ,SWV    ,STV,    res_072,res_072,res_072,res_072, /* 01 */
    res_072,res_072,res_072,res_072,res_072,res_072,res_072,res_072, /* 10 */
    res_072,res_072,res_072,res_072,res_072,res_072,res_072,res_072  /* 11 */
}; /* 000     001     010     011     100     101     110     111 */
