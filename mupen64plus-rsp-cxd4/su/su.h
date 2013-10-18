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
* Project:  MSP Emulation Table for Scalar Unit Operations                     *
* Authors:  Iconoclast                                                         *
* Release:  2013.01.23                                                         *
* License:  none (public domain)                                               *
\******************************************************************************/
#ifndef _SU_H
#define _SU_H

/*
 * RSP virtual registers (of scalar unit)
 * The most important are the 32 general-purpose scalar registers.
 * We have the convenience of using a 32-bit machine (Win32) to emulate
 * another 32-bit machine (MIPS/N64), so the most natural way to accurately
 * emulate the scalar GPRs is to use the standard `int` type.  Situations
 * specifically requiring sign-extension or lack thereof are forcibly
 * applied as defined in the MIPS quick reference card and user manuals.
 * Remember that these are not the same "GPRs" as in the MIPS ISA and totally
 * abandon their designated purposes on the master CPU host (the VR4300),
 * hence most of the MIPS names "k0, k1, t0, t1, v0, v1 ..." no longer apply.
 */
static int SR[32];

/*
 * system control register indexing pointer table
 * This is particularly useful for directly executing MFC0.
 * MTC0 is too complicated to make regular use of this.
 * I cannot recreate as an array; they are engraved in zilmar plugin module.
 */
#if 0
static const unsigned long **CR[32] = {
    &RSP.SP_MEM_ADDR_REG,
    &RSP.SP_DRAM_ADDR_REG,
    &RSP.SP_RD_LEN_REG,
    &RSP.SP_WR_LEN_REG,
    &RSP.SP_STATUS_REG,
    &RSP.SP_DMA_FULL_REG,
    &RSP.SP_DMA_BUSY_REG,
    &RSP.SP_SEMAPHORE_REG, /* also "SP_RESERVED" */
    &RSP.DPC_START_REG,
    &RSP.DPC_END_REG,
    &RSP.DPC_CURRENT_REG,
    &RSP.DPC_STATUS_REG,
    &RSP.DPC_CLOCK_REG,
    &RSP.DPC_BUFBUSY_REG,
    &RSP.DPC_PIPEBUSY_REG,
    &RSP.DPC_TMEM_REG,
    NULL, /* Usually there are 32 scalar CPU control registers. */
    NULL, /* In fact, RSP assembler manual defines 32 indices, but lists 16. */
    NULL, /* Conflicting sources and undefined doc reference leave this gray. */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif

void res_S(void)
{
    message("RESERVED", 3);
    return;
}

#include "cop0.h"
#include "cop2.h"
#include "lwc2.h"
#include "swc2.h"
/*
static void (*SP_PRIMARY[64])(void) = {
    SPECIAL,REGIMM ,J      ,JAL    ,BEQ    ,BNE    ,BLEZ   ,BGTZ   , // 000
    ADDI   ,ADDIU  ,SLTI   ,SLTIU  ,ANDI   ,ORI    ,XORI   ,LUI    , // 001
    COP0   ,res_S  ,COP2   ,res_S  ,res_S  ,res_S  ,res_S  ,res_S  , // 010
    res_S  ,res_S  ,res_S  ,res_S  ,res_S  ,res_S  ,res_S  ,res_S  , // 011
    LB     ,LH     ,res_S  ,LW     ,LBU    ,LHU    ,res_S  ,res_S  , // 100
    SB     ,SH     ,res_S  ,SW     ,res_S  ,res_S  ,res_S  ,res_S  , // 101
    res_S  ,res_S  ,LWC2   ,res_S  ,res_S  ,res_S  ,res_S  ,res_S  , // 110
    res_S  ,res_S  ,SWC2   ,res_S  ,res_S  ,res_S  ,res_S  ,res_S    // 111
};*//* 000     001     010     011     100     101     110     111 */

#endif
