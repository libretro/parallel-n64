/*
 * z64
 *
 * Copyright (C) 2007  ziggy
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
 *
**/

#include "rsp.h"

struct rsp_regmask_t {
    //UINT32 r;
    UINT8 v[32];
    UINT32 accu;
    UINT8 flag;
};

#define RSP_GET_REG_I(i, R)   ( (i).r & (1<<(R)) )
#define RSP_SET_REG_I(i, R)   (i).r |= (1<<(R))
#define RSP_CLEAR_REG_I(i, R) (i).r &= ~(1<<(R))

#define RSP_GET_VEC_I(i, R, I)   ( (i).v[R] & (1<<(I)) )
#define RSP_SET_VEC_I(i, R, I)   (i).v[R] |= (1<<(I))
#define RSP_CLEAR_VEC_I(i, R, I) (i).v[R] &= ~(1<<(I))

#define RSP_GET_ACCU_I(i, I, P)   ( (i).accu & ((P)<<(I)*4) )
#define RSP_SET_ACCU_I(i, I, P)   (i).accu |= ((P)<<(I)*4)
#define RSP_CLEAR_ACCU_I(i, I, P) (i).accu &= ~((P)<<(I)*4)

#define RSP_GET_FLAG_I(i, R)   ( (i).flag & (1<<(R)) )
#define RSP_SET_FLAG_I(i, R)   (i).flag |= (1<<(R))
#define RSP_CLEAR_FLAG_I(i, R) (i).flag &= ~(1<<(R))

#define RSP_OPINFO_JUMP  1
#define RSP_OPINFO_BREAK 2
#define RSP_OPINFO_COND  4
#define RSP_OPINFO_LINK  8
#define RSP_OPINFO_USEPC 16
struct rsp_opinfo_t {
    UINT32        op;   // original opcode
    int           op2;  // simplified opcode
    rsp_regmask_t used;
    rsp_regmask_t set;
    int           flags;
};

void rsp_get_opinfo(UINT32 op, rsp_opinfo_t * info);

#define RSP_BASIC_OFFS     0x000
#define RSP_SPECIAL_OFFS   0x040
#define RSP_LWC2_OFFS      0x0a0
#define RSP_SWC2_OFFS      0x0c0
#define RSP_COP2_1_OFFS    0x080
#define RSP_COP2_2_OFFS    0x100
#define RSP_CONTROL_OFFS   0x140


#define RSP_STOP          (RSP_CONTROL_OFFS + 0x00)
#define RSP_LOOP          (RSP_CONTROL_OFFS + 0x01)
#define RSP_JUMP          (RSP_CONTROL_OFFS + 0x02)
#define RSP_CONDJUMP      (RSP_CONTROL_OFFS + 0x03)
#define RSP_JUMPLOCAL     (RSP_CONTROL_OFFS + 0x04)
#define RSP_CONDJUMPLOCAL (RSP_CONTROL_OFFS + 0x05)


#define RSP_SPECIAL (RSP_BASIC_OFFS + 0x00)
#define RSP_REGIMM (RSP_BASIC_OFFS + 0x01)
#define RSP_J (RSP_BASIC_OFFS + 0x02)
#define RSP_JAL (RSP_BASIC_OFFS + 0x03)
#define RSP_BEQ (RSP_BASIC_OFFS + 0x04)
#define RSP_BNE (RSP_BASIC_OFFS + 0x05)
#define RSP_BLEZ (RSP_BASIC_OFFS + 0x06)
#define RSP_BGTZ (RSP_BASIC_OFFS + 0x07)
#define RSP_ADDI (RSP_BASIC_OFFS + 0x08)
#define RSP_ADDIU (RSP_BASIC_OFFS + 0x09)
#define RSP_SLTI (RSP_BASIC_OFFS + 0x0a)
#define RSP_SLTIU (RSP_BASIC_OFFS + 0x0b)
#define RSP_ANDI (RSP_BASIC_OFFS + 0x0c)
#define RSP_ORI (RSP_BASIC_OFFS + 0x0d)
#define RSP_XORI (RSP_BASIC_OFFS + 0x0e)
#define RSP_LUI (RSP_BASIC_OFFS + 0x0f)
#define RSP_COP0 (RSP_BASIC_OFFS + 0x10)
#define RSP_COP2 (RSP_BASIC_OFFS + 0x12)
#define RSP_LB (RSP_BASIC_OFFS + 0x20)
#define RSP_LH (RSP_BASIC_OFFS + 0x21)
#define RSP_LW (RSP_BASIC_OFFS + 0x23)
#define RSP_LBU (RSP_BASIC_OFFS + 0x24)
#define RSP_LHU (RSP_BASIC_OFFS + 0x25)
#define RSP_SB (RSP_BASIC_OFFS + 0x28)
#define RSP_SH (RSP_BASIC_OFFS + 0x29)
#define RSP_SW (RSP_BASIC_OFFS + 0x2b)
#define RSP_LWC2 (RSP_BASIC_OFFS + 0x32)
#define RSP_SWC2 (RSP_BASIC_OFFS + 0x3a)
#define RSP_BLTZ (RSP_BASIC_OFFS + 0x3b)
#define RSP_BGEZ (RSP_BASIC_OFFS + 0x3c)
#define RSP_BGEZAL (RSP_BASIC_OFFS + 0x3d)

#define RSP_SLL (RSP_SPECIAL_OFFS + 0x00)
#define RSP_SRL (RSP_SPECIAL_OFFS + 0x02)
#define RSP_SRA (RSP_SPECIAL_OFFS + 0x03)
#define RSP_SLLV (RSP_SPECIAL_OFFS + 0x04)
#define RSP_SRLV (RSP_SPECIAL_OFFS + 0x06)
#define RSP_SRAV (RSP_SPECIAL_OFFS + 0x07)
#define RSP_JR (RSP_SPECIAL_OFFS + 0x08)
#define RSP_JALR (RSP_SPECIAL_OFFS + 0x09)
#define RSP_BREAK (RSP_SPECIAL_OFFS + 0x0d)
#define RSP_ADD (RSP_SPECIAL_OFFS + 0x20)
#define RSP_ADDU (RSP_SPECIAL_OFFS + 0x21)
#define RSP_SUB (RSP_SPECIAL_OFFS + 0x22)
#define RSP_SUBU (RSP_SPECIAL_OFFS + 0x23)
#define RSP_AND (RSP_SPECIAL_OFFS + 0x24)
#define RSP_OR (RSP_SPECIAL_OFFS + 0x25)
#define RSP_XOR (RSP_SPECIAL_OFFS + 0x26)
#define RSP_NOR (RSP_SPECIAL_OFFS + 0x27)
#define RSP_SLT (RSP_SPECIAL_OFFS + 0x2a)
#define RSP_SLTU (RSP_SPECIAL_OFFS + 0x2b)

#define RSP_MFC2 (RSP_COP2_1_OFFS + 0x00)
#define RSP_CFC2 (RSP_COP2_1_OFFS + 0x02)
#define RSP_MTC2 (RSP_COP2_1_OFFS + 0x04)
#define RSP_CTC2 (RSP_COP2_1_OFFS + 0x06)


#define RSP_LBV (RSP_LWC2_OFFS + 0x00)
#define RSP_LSV (RSP_LWC2_OFFS + 0x01)
#define RSP_LLV (RSP_LWC2_OFFS + 0x02)
#define RSP_LDV (RSP_LWC2_OFFS + 0x03)
#define RSP_LQV (RSP_LWC2_OFFS + 0x04)
#define RSP_LRV (RSP_LWC2_OFFS + 0x05)
#define RSP_LPV (RSP_LWC2_OFFS + 0x06)
#define RSP_LUV (RSP_LWC2_OFFS + 0x07)
#define RSP_LHV (RSP_LWC2_OFFS + 0x08)
#define RSP_LFV (RSP_LWC2_OFFS + 0x09)
#define RSP_LWV (RSP_LWC2_OFFS + 0x0a)
#define RSP_LTV (RSP_LWC2_OFFS + 0x0b)

#define RSP_SBV (RSP_SWC2_OFFS + 0x00)
#define RSP_SSV (RSP_SWC2_OFFS + 0x01)
#define RSP_SLV (RSP_SWC2_OFFS + 0x02)
#define RSP_SDV (RSP_SWC2_OFFS + 0x03)
#define RSP_SQV (RSP_SWC2_OFFS + 0x04)
#define RSP_SRV (RSP_SWC2_OFFS + 0x05)
#define RSP_SPV (RSP_SWC2_OFFS + 0x06)
#define RSP_SUV (RSP_SWC2_OFFS + 0x07)
#define RSP_SHV (RSP_SWC2_OFFS + 0x08)
#define RSP_SFV (RSP_SWC2_OFFS + 0x09)
#define RSP_SWV (RSP_SWC2_OFFS + 0x0a)
#define RSP_STV (RSP_SWC2_OFFS + 0x0b)

#define RSP_VMULF (RSP_COP2_2_OFFS + 0x00)
#define RSP_VMULU (RSP_COP2_2_OFFS + 0x01)
#define RSP_VMUDL (RSP_COP2_2_OFFS + 0x04)
#define RSP_VMUDM (RSP_COP2_2_OFFS + 0x05)
#define RSP_VMUDN (RSP_COP2_2_OFFS + 0x06)
#define RSP_VMUDH (RSP_COP2_2_OFFS + 0x07)
#define RSP_VMACF (RSP_COP2_2_OFFS + 0x08)
#define RSP_VMACU (RSP_COP2_2_OFFS + 0x09)
#define RSP_VMADL (RSP_COP2_2_OFFS + 0x0c)
#define RSP_VMADM (RSP_COP2_2_OFFS + 0x0d)
#define RSP_VMADN (RSP_COP2_2_OFFS + 0x0e)
#define RSP_VMADH (RSP_COP2_2_OFFS + 0x0f)
#define RSP_VADD (RSP_COP2_2_OFFS + 0x10)
#define RSP_VSUB (RSP_COP2_2_OFFS + 0x11)
#define RSP_VABS (RSP_COP2_2_OFFS + 0x13)
#define RSP_VADDC (RSP_COP2_2_OFFS + 0x14)
#define RSP_VSUBC (RSP_COP2_2_OFFS + 0x15)
#define RSP_VSAW (RSP_COP2_2_OFFS + 0x1d)
#define RSP_VLT (RSP_COP2_2_OFFS + 0x20)
#define RSP_VEQ (RSP_COP2_2_OFFS + 0x21)
#define RSP_VNE (RSP_COP2_2_OFFS + 0x22)
#define RSP_VGE (RSP_COP2_2_OFFS + 0x23)
#define RSP_VCL (RSP_COP2_2_OFFS + 0x24)
#define RSP_VCH (RSP_COP2_2_OFFS + 0x25)
#define RSP_VCR (RSP_COP2_2_OFFS + 0x26)
#define RSP_VMRG (RSP_COP2_2_OFFS + 0x27)
#define RSP_VAND (RSP_COP2_2_OFFS + 0x28)
#define RSP_VNAND (RSP_COP2_2_OFFS + 0x29)
#define RSP_VOR (RSP_COP2_2_OFFS + 0x2a)
#define RSP_VNOR (RSP_COP2_2_OFFS + 0x2b)
#define RSP_VXOR (RSP_COP2_2_OFFS + 0x2c)
#define RSP_VNXOR (RSP_COP2_2_OFFS + 0x2d)
#define RSP_VRCP (RSP_COP2_2_OFFS + 0x30)
#define RSP_VRCPL (RSP_COP2_2_OFFS + 0x31)
#define RSP_VRCPH (RSP_COP2_2_OFFS + 0x32)
#define RSP_VMOV (RSP_COP2_2_OFFS + 0x33)
#define RSP_VRSQL (RSP_COP2_2_OFFS + 0x35)
#define RSP_VRSQH (RSP_COP2_2_OFFS + 0x36)
