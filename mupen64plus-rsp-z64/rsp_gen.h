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

inline UINT32 get_cop0_reg(RSP_REGS & rsp, int reg)
{
    if (reg >= 0 && reg < 8)
    {
        return sp_read_reg(rsp ,reg);
    }
    else if (reg >= 8 && reg < 16)
    {
        return n64_dp_reg_r(rsp, reg - 8, 0x00000000);
    }
    else
    {
        log(M64MSG_ERROR, "RSP: get_cop0_reg: %d", reg);
    }
}

inline void set_cop0_reg(RSP_REGS & rsp, int reg, UINT32 data)
{
    if (reg >= 0 && reg < 8)
    {
        sp_write_reg(rsp, reg, data);
    }
    else if (reg >= 8 && reg < 16)
    {
        n64_dp_reg_w(rsp, reg - 8, data, 0x00000000);
    }
    else
    {
        log(M64MSG_ERROR, "RSP: set_cop0_reg: %d, %08X\n", reg, data);
    }
}

static const int vector_elements_2[16][8] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7 },		// none
    { 0, 1, 2, 3, 4, 5, 6, 7 },		// ???
    { 0, 0, 2, 2, 4, 4, 6, 6 },		// 0q
    { 1, 1, 3, 3, 5, 5, 7, 7 },		// 1q
    { 0, 0, 0, 0, 4, 4, 4, 4 },		// 0h
    { 1, 1, 1, 1, 5, 5, 5, 5 },		// 1h
    { 2, 2, 2, 2, 6, 6, 6, 6 },		// 2h
    { 3, 3, 3, 3, 7, 7, 7, 7 },		// 3h
    { 0, 0, 0, 0, 0, 0, 0, 0 },		// 0
    { 1, 1, 1, 1, 1, 1, 1, 1 },		// 1
    { 2, 2, 2, 2, 2, 2, 2, 2 },		// 2
    { 3, 3, 3, 3, 3, 3, 3, 3 },		// 3
    { 4, 4, 4, 4, 4, 4, 4, 4 },		// 4
    { 5, 5, 5, 5, 5, 5, 5, 5 },		// 5
    { 6, 6, 6, 6, 6, 6, 6, 6 },		// 6
    { 7, 7, 7, 7, 7, 7, 7, 7 },		// 7
};
INLINE UINT16 SATURATE_ACCUM(int accum, int slice, UINT16 negative, UINT16 positive)
{
    if ((INT16)ACCUM_H(accum) < 0)
    {
        if ((UINT16)(ACCUM_H(accum)) != 0xffff)
        {
            return negative;
        }
        else
        {
            if ((INT16)ACCUM_M(accum) >= 0)
            {
                return negative;
            }
            else
            {
                if (slice == 0)
                {
                    return ACCUM_L(accum);
                }
                else if (slice == 1)
                {
                    return ACCUM_M(accum);
                }
            }
        }
    }
    else
    {
        if ((UINT16)(ACCUM_H(accum)) != 0)
        {
            return positive;
        }
        else
        {
            if ((INT16)ACCUM_M(accum) < 0)
            {
                return positive;
            }
            else
            {
                if (slice == 0)
                {
                    return ACCUM_L(accum);
                }
                else
                {
                    return ACCUM_M(accum);
                }
            }
        }
    }

    return 0;
}

INLINE UINT16 SATURATE_ACCUM1(int accum, UINT16 negative, UINT16 positive)
{
    if ((INT16)ACCUM_H(accum) < 0)
    {
        if ((UINT16)(ACCUM_H(accum)) != 0xffff)
            return negative;
        else
        {
            if ((INT16)ACCUM_M(accum) >= 0)
                return negative;
            else
                return ACCUM_M(accum);
        }
    }
    else
    {
        if ((UINT16)(ACCUM_H(accum)) != 0)
            return positive;
        else
        {
            if ((INT16)ACCUM_M(accum) < 0)
                return positive;
            else
                return ACCUM_M(accum);
        }
    }

    return 0;
}

#define WRITEBACK_RESULT()              \
    do {                                \
    VREG_S(VDREG, 0) = vres[0];         \
    VREG_S(VDREG, 1) = vres[1];         \
    VREG_S(VDREG, 2) = vres[2];         \
    VREG_S(VDREG, 3) = vres[3];         \
    VREG_S(VDREG, 4) = vres[4];         \
    VREG_S(VDREG, 5) = vres[5];         \
    VREG_S(VDREG, 6) = vres[6];         \
    VREG_S(VDREG, 7) = vres[7];         \
    } while(0)

#if 0
inline void rsp_execute_one(UINT32 op)
{
    switch (op >> 26)
    {
    case 0x00:	/* SPECIAL */
        {
            switch (op & 0x3f)
            {
            case 0x00:	/* SLL */		if (RDREG) RDVAL = (UINT32)RTVAL << SHIFT; break;
            case 0x02:	/* SRL */		if (RDREG) RDVAL = (UINT32)RTVAL >> SHIFT; break;
            case 0x03:	/* SRA */		if (RDREG) RDVAL = (INT32)RTVAL >> SHIFT; break;
            case 0x04:	/* SLLV */		if (RDREG) RDVAL = (UINT32)RTVAL << (RSVAL & 0x1f); break;
            case 0x06:	/* SRLV */		if (RDREG) RDVAL = (UINT32)RTVAL >> (RSVAL & 0x1f); break;
            case 0x07:	/* SRAV */		if (RDREG) RDVAL = (INT32)RTVAL >> (RSVAL & 0x1f); break;
            case 0x08:	/* JR */		JUMP_PC(RSVAL); break;
            case 0x09:	/* JALR */		JUMP_PC_L(RSVAL, RDREG); break;
            case 0x0d:	/* BREAK */
                {
                    *z64_rspinfo.SP_STATUS_REG |= (SP_STATUS_HALT | SP_STATUS_BROKE );
                    if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) {
                        *z64_rspinfo.MI_INTR_REG |= 1;
                        z64_rspinfo.CheckInterrupts();
                    }
                    //sp_set_status(0x3);

#if LOG_INSTRUCTION_EXECUTION
                    fprintf(exec_output, "\n---------- break ----------\n\n");
#endif
                    break;
                }
            case 0x20:	/* ADD */		if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
            case 0x21:	/* ADDU */		if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
            case 0x22:	/* SUB */		if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
            case 0x23:	/* SUBU */		if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
            case 0x24:	/* AND */		if (RDREG) RDVAL = RSVAL & RTVAL; break;
            case 0x25:	/* OR */		if (RDREG) RDVAL = RSVAL | RTVAL; break;
            case 0x26:	/* XOR */		if (RDREG) RDVAL = RSVAL ^ RTVAL; break;
            case 0x27:	/* NOR */		if (RDREG) RDVAL = ~(RSVAL | RTVAL); break;
            case 0x2a:	/* SLT */		if (RDREG) RDVAL = (INT32)RSVAL < (INT32)RTVAL; break;
            case 0x2b:	/* SLTU */		if (RDREG) RDVAL = (UINT32)RSVAL < (UINT32)RTVAL; break;
            default:	unimplemented_opcode(op); break;
            }
            break;
        }

    case 0x01:	/* REGIMM */
        {
            switch (RTREG)
            {
            case 0x00:	/* BLTZ */		if ((INT32)(RSVAL) < 0) JUMP_REL(SIMM16); break;
            case 0x01:	/* BGEZ */		if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); break;
                // VP according to the doc, link is performed even when condition fails,
                // this sound pretty stupid but let's try it that way
            case 0x11:	/* BGEZAL */	LINK(31); if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); break;
                //case 0x11:	/* BGEZAL */	if ((INT32)(RSVAL) >= 0) JUMP_REL_L(SIMM16, 31); break;
            default:	unimplemented_opcode(op); break;
            }
            break;
        }

    case 0x02:	/* J */			JUMP_ABS(UIMM26); break;
    case 0x03:	/* JAL */		JUMP_ABS_L(UIMM26, 31); break;
    case 0x04:	/* BEQ */		if (RSVAL == RTVAL) JUMP_REL(SIMM16); break;
    case 0x05:	/* BNE */		if (RSVAL != RTVAL) JUMP_REL(SIMM16); break;
    case 0x06:	/* BLEZ */		if ((INT32)RSVAL <= 0) JUMP_REL(SIMM16); break;
    case 0x07:	/* BGTZ */		if ((INT32)RSVAL > 0) JUMP_REL(SIMM16); break;
    case 0x08:	/* ADDI */		if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
    case 0x09:	/* ADDIU */		if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
    case 0x0a:	/* SLTI */		if (RTREG) RTVAL = (INT32)(RSVAL) < ((INT32)SIMM16); break;
    case 0x0b:	/* SLTIU */		if (RTREG) RTVAL = (UINT32)(RSVAL) < (UINT32)((INT32)SIMM16); break;
    case 0x0c:	/* ANDI */		if (RTREG) RTVAL = RSVAL & UIMM16; break;
    case 0x0d:	/* ORI */		if (RTREG) RTVAL = RSVAL | UIMM16; break;
    case 0x0e:	/* XORI */		if (RTREG) RTVAL = RSVAL ^ UIMM16; break;
    case 0x0f:	/* LUI */		if (RTREG) RTVAL = UIMM16 << 16; break;

    case 0x10:	/* COP0 */
        {
            switch ((op >> 21) & 0x1f)
            {
            case 0x00:	/* MFC0 */		if (RTREG) RTVAL = get_cop0_reg(RDREG); break;
            case 0x04:	/* MTC0 */		set_cop0_reg(RDREG, RTVAL); break;
            default:
                printf("unimplemented cop0 %x (%x)\n", (op >> 21) & 0x1f, op);
                break;
            }
            break;
        }

    case 0x12:	/* COP2 */
        {
            switch ((op >> 21) & 0x1f)
            {
            case 0x00:	/* MFC2 */
                {
                    // 31       25      20      15      10     6         0
                    // ---------------------------------------------------
                    // | 010010 | 00000 | TTTTT | DDDDD | IIII | 0000000 |
                    // ---------------------------------------------------
                    //

                    int el = (op >> 7) & 0xf;
                    UINT16 b1 = VREG_B(VS1REG, (el+0) & 0xf);
                    UINT16 b2 = VREG_B(VS1REG, (el+1) & 0xf);
                    if (RTREG) RTVAL = (INT32)(INT16)((b1 << 8) | (b2));
                    break;
                }
            case 0x02:	/* CFC2 */
                {
                    // 31       25      20      15      10            0
                    // ------------------------------------------------
                    // | 010010 | 00010 | TTTTT | DDDDD | 00000000000 |
                    // ------------------------------------------------
                    //

                    // VP to sign extend or to not sign extend ?
                    //if (RTREG) RTVAL = (INT16)rsp.flag[RDREG];
                    if (RTREG) RTVAL = rsp.flag[RDREG];
                    break;
                }
            case 0x04:	/* MTC2 */
                {
                    // 31       25      20      15      10     6         0
                    // ---------------------------------------------------
                    // | 010010 | 00100 | TTTTT | DDDDD | IIII | 0000000 |
                    // ---------------------------------------------------
                    //

                    int el = (op >> 7) & 0xf;
                    VREG_B(VS1REG, (el+0) & 0xf) = (RTVAL >> 8) & 0xff;
                    VREG_B(VS1REG, (el+1) & 0xf) = (RTVAL >> 0) & 0xff;
                    break;
                }
            case 0x06:	/* CTC2 */
                {
                    // 31       25      20      15      10            0
                    // ------------------------------------------------
                    // | 010010 | 00110 | TTTTT | DDDDD | 00000000000 |
                    // ------------------------------------------------
                    //

                    rsp.flag[RDREG] = RTVAL & 0xffff;
                    break;
                }

            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
            case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                {
                    handle_vector_ops(op);
                    break;
                }

            default:	unimplemented_opcode(op); break;
            }
            break;
        }

    case 0x20:	/* LB */		if (RTREG) RTVAL = (INT32)(INT8)READ8(RSVAL + SIMM16); break;
    case 0x21:	/* LH */		if (RTREG) RTVAL = (INT32)(INT16)READ16(RSVAL + SIMM16); break;
    case 0x23:	/* LW */		if (RTREG) RTVAL = READ32(RSVAL + SIMM16); break;
    case 0x24:	/* LBU */		if (RTREG) RTVAL = (UINT8)READ8(RSVAL + SIMM16); break;
    case 0x25:	/* LHU */		if (RTREG) RTVAL = (UINT16)READ16(RSVAL + SIMM16); break;
    case 0x28:	/* SB */		WRITE8(RSVAL + SIMM16, RTVAL); break;
    case 0x29:	/* SH */		WRITE16(RSVAL + SIMM16, RTVAL); break;
    case 0x2b:	/* SW */		WRITE32(RSVAL + SIMM16, RTVAL); break;
    case 0x32:	/* LWC2 */		handle_lwc2(op); break;
    case 0x3a:	/* SWC2 */		handle_swc2(op); break;

    default:
        {
            unimplemented_opcode(op);
            break;
        }
    }
}

#endif
