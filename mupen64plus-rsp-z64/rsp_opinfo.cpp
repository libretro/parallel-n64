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

#include "rsp_opinfo.h"

static const int vector_elements_2[16][8] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7 },     // none
    { 0, 1, 2, 3, 4, 5, 6, 7 },     // ???
    { 0, 0, 2, 2, 4, 4, 6, 6 },     // 0q
    { 1, 1, 3, 3, 5, 5, 7, 7 },     // 1q
    { 0, 0, 0, 0, 4, 4, 4, 4 },     // 0h
    { 1, 1, 1, 1, 5, 5, 5, 5 },     // 1h
    { 2, 2, 2, 2, 6, 6, 6, 6 },     // 2h
    { 3, 3, 3, 3, 7, 7, 7, 7 },     // 3h
    { 0, 0, 0, 0, 0, 0, 0, 0 },     // 0
    { 1, 1, 1, 1, 1, 1, 1, 1 },     // 1
    { 2, 2, 2, 2, 2, 2, 2, 2 },     // 2
    { 3, 3, 3, 3, 3, 3, 3, 3 },     // 3
    { 4, 4, 4, 4, 4, 4, 4, 4 },     // 4
    { 5, 5, 5, 5, 5, 5, 5, 5 },     // 5
    { 6, 6, 6, 6, 6, 6, 6, 6 },     // 6
    { 7, 7, 7, 7, 7, 7, 7, 7 },     // 7
};

void rsp_get_opinfo(UINT32 op, rsp_opinfo_t * info)
{
    int op2;
    int i;
    info->op = op;
    switch (op>>26) {
    case 0:     /* SPECIAL */
        op2 = RSP_SPECIAL_OFFS + (op&0x3f);
        break;
    case 0x12:  /* COP2 */
        if (((op>>21)&0x1f) >= 0x10)
            op2 = RSP_COP2_2_OFFS + (op & 0x3f);
        else
            op2 = RSP_COP2_1_OFFS + ((op >> 21) & 0x1f);
        break;
    case 0x32:  /* LWC2 */
        op2 = RSP_LWC2_OFFS + ((op>>11)&0x1f);
        break;
    case 0x3a:  /* SWC2 */
        op2 = RSP_SWC2_OFFS + ((op>>11)&0x1f);
        break;
    default:
        op2 = RSP_BASIC_OFFS + (op>>26);
        if (op2 == RSP_REGIMM) {
            switch (RTREG)
            {
            case 0x00:	/* BLTZ */
                op2 = RSP_BLTZ;
                break;
            case 0x01:	/* BGEZ */
                op2 = RSP_BGEZ;
                break;
            case 0x11:	/* BGEZAL */
                op2 = RSP_BGEZAL;
                break;
            }
        }
    }
    info->op2 = op2;

    memset(&info->used, 0, sizeof(info->used));
    memset(&info->set, 0, sizeof(info->set));
    info->used.accu = info->used.flag = 0;
    info->set.accu = info->set.flag = 0;
    info->flags = 0;

    int dest = (op >> 16) & 0x1f;
    int index = (op >> 7) & 0xf;
    int offset = (op & 0x7f);
    if (offset & 0x40)
        offset |= 0xffffffc0;

    switch(op2) {
    case RSP_SPECIAL:
    case RSP_BLTZ:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_COND | RSP_OPINFO_USEPC;
        break;
    case RSP_BGEZ:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_COND | RSP_OPINFO_USEPC;
        break;
    case RSP_BGEZAL:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_COND | RSP_OPINFO_LINK | RSP_OPINFO_USEPC;
        break;
    case RSP_J:
        info->flags = RSP_OPINFO_JUMP;
        break;
    case RSP_JAL:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_LINK | RSP_OPINFO_USEPC;
        break;
    case RSP_BEQ:
    case RSP_BNE:
    case RSP_BLEZ:
    case RSP_BGTZ:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_COND | RSP_OPINFO_USEPC;
        break;
    case RSP_ADDI:
    case RSP_ADDIU:
    case RSP_SLTI:
    case RSP_SLTIU:
    case RSP_ANDI:
    case RSP_ORI:
    case RSP_XORI:
    case RSP_LUI:
    case RSP_COP0:
    case RSP_LB:
    case RSP_LH:
    case RSP_LW:
    case RSP_LBU:
    case RSP_LHU:
    case RSP_SB:
    case RSP_SH:
    case RSP_SW:
        break;

    case RSP_SLL:
    case RSP_SRL:
    case RSP_SRA:
    case RSP_SLLV:
    case RSP_SRLV:
    case RSP_SRAV:
        break;

    case RSP_JR:
        info->flags = RSP_OPINFO_JUMP;
        break;
    case RSP_JALR:
        info->flags = RSP_OPINFO_JUMP | RSP_OPINFO_LINK | RSP_OPINFO_USEPC;
        break;
    case RSP_BREAK:
        info->flags = RSP_OPINFO_BREAK;
        break;

    case RSP_ADD:
    case RSP_ADDU:
    case RSP_SUB:
    case RSP_SUBU:
    case RSP_AND:
    case RSP_OR:
    case RSP_XOR:
    case RSP_NOR:
    case RSP_SLT:
    case RSP_SLTU:
        break;

    case RSP_MFC2:
        {
            int el = op >> 7;
            RSP_SET_VEC_I(info->used, VS1REG, ((el+0)&0xf)>>1);
            RSP_SET_VEC_I(info->used, VS1REG, ((el+1)&0xf)>>1);
            break;
        }
    case RSP_CFC2:
        RSP_SET_FLAG_I(info->used, RDREG & 3);
        break;
    case RSP_MTC2:
        {
            int el = op >> 7;
            RSP_SET_VEC_I(info->set, VS1REG, ((el+0)&0xf)>>1);
            RSP_SET_VEC_I(info->set, VS1REG, ((el+1)&0xf)>>1);
            break;
        }
    case RSP_CTC2:
        RSP_SET_FLAG_I(info->set, RDREG & 3);
        break;


    case RSP_LBV:
        RSP_SET_VEC_I(info->set, dest, index>>1);
        break;
    case RSP_LSV:
        for (i=index; i<index+2; i++)
            RSP_SET_VEC_I(info->set, dest, (i>>1)&7);
        break;
    case RSP_LLV:
        for (i=index; i<index+4; i++)
            RSP_SET_VEC_I(info->set, dest, (i>>1)&7);
        break;
    case RSP_LDV:
        for (i=index; i<index+8; i++)
            RSP_SET_VEC_I(info->set, dest, (i>>1)&7);
        break;
    case RSP_LQV:
    case RSP_LRV:
        // WARNING WARNING WARNING
        // we assume this instruction always used to load the full vector
        // i.e. the address is always 16 bytes aligned
        //       for (i=0; i<8; i++)
        //         RSP_SET_VEC_I(info->set, dest, i);
        break;
    case RSP_LPV:
    case RSP_LUV:
    case RSP_LHV:
    case RSP_LWV:
        for (i=0; i<8; i++)
            RSP_SET_VEC_I(info->set, dest, i);
        break;
    case RSP_LFV:
        for (i=(index>>1); i<(index>>1)+4; i++)
            RSP_SET_VEC_I(info->set, dest, i);
        break;
    case RSP_LTV:
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 110010 | BBBBB | TTTTT | 01011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Loads one element to maximum of 8 vectors, while incrementing element index

            // FIXME: has a small problem with odd indices

            int element;
            int vs = dest;
            int ve = dest + 8;
            if (ve > 32)
                ve = 32;

            element = 7 - (index >> 1);

            if (index & 1)
                log(M64MSG_ERROR, "RSP: LTV: index = %d\n", index);

            for (i=vs; i < ve; i++)
            {
                element = ((8 - (index >> 1) + (i-vs)) << 1);
                RSP_SET_VEC_I(info->set, i, (element & 0xf)>>1);
                RSP_SET_VEC_I(info->set, i, ((element+1) & 0xf)>>1);
            }
            break;
        }

    case RSP_SBV:
        RSP_SET_VEC_I(info->used, dest, index>>1);
        break;
    case RSP_SSV:
        for (i=index; i<index+2; i++)
            RSP_SET_VEC_I(info->used, dest, (i>>1)&7);
        break;
    case RSP_SLV:
        for (i=index; i<index+4; i++)
            RSP_SET_VEC_I(info->used, dest, (i>>1)&7);
        break;
    case RSP_SDV:
        for (i=index; i<index+8; i++)
            RSP_SET_VEC_I(info->used, dest, (i>>1)&7);
        break;
    case RSP_SQV:
    case RSP_SRV:
        // WARNING WARNING WARNING
        // we assume this instruction always used to store the full vector
        // i.e. the address is always 16 bytes aligned
        for (i=0; i<8; i++)
            RSP_SET_VEC_I(info->used, dest, i);
        break;
    case RSP_SPV:
    case RSP_SUV:
    case RSP_SHV:
    case RSP_SWV:
        for (i=0; i<8; i++)
            RSP_SET_VEC_I(info->used, dest, i);
        break;
    case RSP_SFV:
        for (i=(index>>1); i<(index>>1)+4; i++)
            RSP_SET_VEC_I(info->used, dest, i);
        break;
    case RSP_STV:
        {
            // 31       25      20      15      10     6        0
            // --------------------------------------------------
            // | 111010 | BBBBB | TTTTT | 01011 | IIII | Offset |
            // --------------------------------------------------
            //
            // Stores one element from maximum of 8 vectors, while incrementing element index

            int element;
            int vs = dest;
            int ve = dest + 8;
            if (ve > 32)
                ve = 32;

            element = 8 - (index >> 1);
            if (index & 0x1)
                log(M64MSG_ERROR, "RSP: STV: index = %d at %08X\n", index, rsp.ppc);

            for (i=vs; i < ve; i++)
            {
                RSP_SET_VEC_I(info->used, i, element & 0x7);
                element++;
            }
            break;
        }

    case RSP_VMULF:
    case RSP_VMULU:
    case RSP_VMUDL:
    case RSP_VMUDM:
    case RSP_VMUDN:
    case RSP_VMUDH:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 14);
            }
            break;
        }
    case RSP_VMACF:
    case RSP_VMACU:
    case RSP_VMADL:
    case RSP_VMADM:
    case RSP_VMADN:
    case RSP_VMADH:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->used, i, 14);
                RSP_SET_ACCU_I(info->set, i, 14);
            }
            break;
        }
    case RSP_VADD:
    case RSP_VSUB:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->used, 0);
            RSP_SET_FLAG_I(info->set, 0);
            break;
        }
    case RSP_VABS:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            break;
        }
    case RSP_VADDC:
    case RSP_VSUBC:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->set, 0);
            break;
        }
    case RSP_VSAW:
        switch (EL)
        {
        case 0x08:		// VSAWH
            {
                for (i=0; i < 8; i++)
                {
                    RSP_SET_VEC_I(info->set, VDREG, i);
                    RSP_SET_ACCU_I(info->used, i, 8);
                }
                break;
            }
        case 0x09:		// VSAWM
            {
                for (i=0; i < 8; i++)
                {
                    RSP_SET_VEC_I(info->set, VDREG, i);
                    RSP_SET_ACCU_I(info->used, i, 4);
                }
                break;
            }
        case 0x0a:		// VSAWL
            {
                for (i=0; i < 8; i++)
                {
                    RSP_SET_VEC_I(info->set, VDREG, i);
                    RSP_SET_ACCU_I(info->used, i, 2);
                }
                break;
            }
        default:
            log(M64MSG_ERROR, "RSP: VSAW: el = %d\n", EL);
        }
        break;
    case RSP_VLT:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->set, 0);
            RSP_SET_FLAG_I(info->set, 1);
            break;
        }
    case RSP_VEQ:
    case RSP_VNE:
    case RSP_VGE:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->used, 0);
            RSP_SET_FLAG_I(info->set, 0);
            RSP_SET_FLAG_I(info->set, 1);
            break;
        }
    case RSP_VCL:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->used, 0);
            RSP_SET_FLAG_I(info->used, 1);
            RSP_SET_FLAG_I(info->set, 0);
            RSP_SET_FLAG_I(info->set, 1);
            RSP_SET_FLAG_I(info->set, 2);
            break;
        }
    case RSP_VCH:
    case RSP_VCR:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->set, 0);
            RSP_SET_FLAG_I(info->set, 1);
            RSP_SET_FLAG_I(info->set, 2);
            break;
        }
    case RSP_VMRG:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            RSP_SET_FLAG_I(info->used, 1);
            break;
        }
    case RSP_VAND:
    case RSP_VNAND:
    case RSP_VOR:
    case RSP_VNOR:
    case RSP_VXOR:
    case RSP_VNXOR:
        {
            for (i=0; i < 8; i++)
            {
                int sel = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS1REG, i);
                RSP_SET_VEC_I(info->used, VS2REG, sel);
                RSP_SET_VEC_I(info->set, VDREG, i);
                RSP_SET_ACCU_I(info->set, i, 2);
            }
            break;
        }
    case RSP_VRCP:
    case RSP_VRCPL:
    case RSP_VRCPH:
    case RSP_VRSQL:
    case RSP_VRSQH:
        {
            int del = (VS1REG & 7);
            int sel = VEC_EL_2(EL, del);

            RSP_SET_VEC_I(info->used, VS2REG, sel);

            for (i=0; i < 8; i++)
            {
                int element = VEC_EL_2(EL, i);
                RSP_SET_VEC_I(info->used, VS2REG, element);
                RSP_SET_ACCU_I(info->set, i, 2);
            }

            RSP_SET_VEC_I(info->set, VDREG, del);
            break;
        }
    case RSP_VMOV:
        {
            int element = VS1REG & 7;
            RSP_SET_VEC_I(info->used, VS2REG, VEC_EL_2(EL, 7-element));
            RSP_SET_VEC_I(info->set, VDREG, element);
            break;
        }

    default:
        {
            //       char string[200];
            //       rsp_dasm_one(string, 0x800, op);
            //       if (strcmp(string, "???")) {
            //         printf("%s\n", string);
            //         printf("unimplemented opcode\n");
            //       }
            break;
        }
    }
}
