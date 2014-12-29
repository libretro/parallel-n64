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

#include "rsp_recomp.h"
#include <assert.h>

#define GENDEBUG

struct gen_t {
    UINT32 crc;
    int lbc;
    rsp_bc_t * bc;
#ifdef GENDEBUG
    char name[32];
#endif
};

struct opinfo_t {
    int visit, labeled;
    int label;

    unsigned int nbgen;
    unsigned int szgen;
    gen_t * gentable;
    gen_t * curgen;
};

struct branch_t {
    int start, end;
};

static int curvisit;
static opinfo_t opinfo[0x1000/4];
static int jumps[0x1000];
static unsigned int nb_branches;
static branch_t branches[256];
static unsigned int nb_labels;
static int labels[256];

#define OPI(pc) opinfo[(pc)>>2]
/*inline*/ void SETLABEL(int pc) {
    //printf("%x\n", pc);
    //pc &= 0xfff;
    assert(pc >= 0 && pc < 0x1000);
    if (OPI(pc).labeled != curvisit) {
        labels[nb_labels] = pc;
        OPI(pc).label = nb_labels++;
        assert(nb_labels < sizeof(labels)/sizeof(labels[0]));
        OPI(pc).labeled = curvisit;
    }
}

#define ABS(addr) (((addr) << 2) & 0xfff)
#define REL(offset) ((pc + ((offset) << 2)) & 0xfff)

static UINT32 prep_gen(int pc, UINT32 crc, int & len)
{
    UINT32 op;
    int br = 0;

    branches[nb_branches].start = pc;

    while ( !br )
    {
        if (OPI(pc).visit == curvisit) {
            SETLABEL((pc)&0xfff);
            SETLABEL((pc+4)&0xfff);
            break;
        }

        OPI(pc).visit = curvisit;

        op = ROPCODE(pc);
        crc = ((crc<<1)|(crc>>31))^op^pc;
        pc = (pc+4)&0xfff;
        len++;

        switch (op >> 26)
        {
        case 0x00:      /* SPECIAL */
            {
                switch (op & 0x3f)
                {
                case 0x08:      /* JR */
                    br = 1;
                    break;
                case 0x09:      /* JALR */
                    //br = 1;
                    break;
                case 0x0d:      /* BREAK */
                    br = 1;
                    break;
                }
                break;
            }

        case 0x01:      /* REGIMM */
            {
                switch (RTREG)
                {
                case 0x00:      /* BLTZ */
                case 0x01:      /* BGEZ */
                    SETLABEL(REL(SIMM16));
                    break;
                case 0x11:      /* BGEZAL */
                    //br = 1;
                    break;
                }
                break;
            }

        case 0x02:      /* J */
            SETLABEL(ABS(UIMM26));
            br = 1;
            break;
        case 0x04:      /* BEQ */
        case 0x05:      /* BNE */
        case 0x06:      /* BLEZ */
        case 0x07:      /* BGTZ */
            SETLABEL(REL(SIMM16));
            break;
        case 0x03:      /* JAL */
            //SETLABEL(ABS(UIMM26));
            //br = 1;
            break;
        }

    }

    branches[nb_branches++].end = pc;
    assert(nb_branches < sizeof(branches)/sizeof(branches[0]));

    return crc;
}

static void rsp_gen(int pc)
{
    unsigned int i;

    curvisit++;
    if (!curvisit) {
        // we looped, reset all visit counters
        for (i=0; i<0x1000/4; i++) {
            opinfo[i].visit = 0;
            opinfo[i].labeled = 0;
        }
        curvisit++;
    }

    nb_branches = 0;
    nb_labels = 0;

    int len = 0;
    UINT32 crc = prep_gen(pc, 0, len);

    for (i=0; i<nb_labels; i++) {
        if (OPI(labels[i]).visit != curvisit)
            crc = prep_gen(labels[i], crc, len);
    }

    opinfo_t * opi = &OPI(pc);
    if (opi->gentable) {
        for (i=0; i<opi->nbgen; i++)
            if (opi->gentable[i].crc == crc) {
                opi->curgen = opi->gentable + i;
                return;
            }
    }
    if (opi->nbgen >= opi->szgen) {
        if (opi->szgen)
            opi->szgen *= 2;
        else
            opi->szgen = 4;
        opi->gentable = (gen_t *) realloc(opi->gentable, sizeof(gen_t)*(opi->szgen));
    }
    gen_t * gen;
    gen = opi->gentable + opi->nbgen++;
    gen->crc = crc;
    opi->curgen = gen;

    // convert to bytecode
    unsigned int lbc = 0;
    static rsp_bc_t bc[0x1000*2+10];
    for (i=0; i<nb_branches; i++) {
        int pc;
        int loopc;
        int cont = 1;
        rsp_opinfo_t delayed;
        delayed.op = 0;
        for (pc = branches[i].start; cont || delayed.op; pc = (pc+4)&0xfff) {
            UINT32 op = ROPCODE(pc);

            //       int realpc = pc;
            //       char s[128];
            //       rsp_dasm_one(s, realpc, op);
            //       printf("%d %3x\t%s\n", lbc, realpc, s);

            rsp_opinfo_t info;
            rsp_get_opinfo(op, &info);
            if ((info.flags & RSP_OPINFO_JUMP) && !cont)
                info.flags = 0;
            else {
                int nop = 0;
                switch (info.op2) {
                case RSP_SLL:
                case RSP_SRL:
                case RSP_SRA:
                    if (RDREG == RTREG && SHIFT == 0) 
                        nop = 1;
                    break;
                }
                if (cont)
                    jumps[pc] = lbc;
                if (!nop) {
                    bc[lbc].op = op;
                    bc[lbc].op2 = info.op2;
                    bc[lbc].flags = info.flags | (((pc&0xffc)<<5)-2) | (!cont? (1<<15):0);
                    lbc++;
                }
                loopc = (pc+4)&0xfff;
            }
            if (delayed.op) {
                int addop = 0;
                const UINT32 op = delayed.op;
                switch (delayed.op2) {
                case RSP_BLTZ:
                case RSP_BGEZ:
                case RSP_BEQ:
                case RSP_BNE:
                case RSP_BLEZ:
                case RSP_BGTZ:
                    addop = RSP_CONDJUMPLOCAL;
                    bc[lbc].flags = (pc + (SIMM16<<2))&0xfff; // address to be resolved later
                    break;
                case RSP_J:
                    addop = RSP_JUMPLOCAL;
                    bc[lbc].flags = (UIMM26<<2)&0xfff; // address to be resolved later
                    break;
                case RSP_BGEZAL:
                    addop = RSP_CONDJUMP;
                    break;
                case RSP_JAL:
                case RSP_JR:
                case RSP_JALR:
                    addop = RSP_JUMP;
                    break;
                }
                bc[lbc].op = delayed.op;
                bc[lbc].op2 = addop;
                lbc++;
            }
            if (info.flags & RSP_OPINFO_JUMP) {
                delayed = info;
            } else
                delayed.op = 0;
            if (((pc + 4)&0xfff) == branches[i].end)
                cont = 0;
        }
        if (bc[lbc-1].op2 != RSP_JUMP &&
            bc[lbc-1].op2 != RSP_JUMPLOCAL &&
            bc[lbc-1].op2 != RSP_BREAK &&
            bc[lbc-1].op2 != RSP_STOP) {

                bc[lbc].op = 0;
                bc[lbc].op2 = RSP_LOOP;
                bc[lbc].flags = loopc; // address to be resolved later
                lbc++;
        }
    }

    // resolve local jumps
    for (i=0; i<lbc; i++) {
        //     printf("%d %x\n", i, bc[i].op2);
        //     if (bc[i].op2 < RSP_CONTROL_OFFS) {
        //       int realpc = (bc[i].flags>>3)&0xffc;
        //       char s[128];
        //       rsp_dasm_one(s, realpc, bc[i].op);
        //       printf("%3x\t%s\n", realpc, s);
        //     }
        switch (bc[i].op2) {
        case RSP_JUMPLOCAL:
        case RSP_CONDJUMPLOCAL:
        case RSP_LOOP:
            {
                //         int pc;
                //         for (pc = 0; pc<lbc; pc++)
                //           if (bc[pc].op2 < RSP_CONTROL_OFFS &&
                //               !(bc[pc].flags & (1<<15)) &&
                //               ((bc[pc].flags>>5)<<2) == bc[i].flags)
                //             break;
                //         assert(pc < lbc);
                //         bc[i].flags = pc<<5;
                bc[i].flags = jumps[bc[i].flags]<<5;
                break;
            }
        }
    }

    gen->lbc = lbc;
    gen->bc = (rsp_bc_t *) malloc(sizeof(rsp_bc_t)*lbc);
    memcpy(gen->bc, bc, sizeof(rsp_bc_t)*lbc);
}

void rsp_invalidate(int begin, int len)
{
    //printf("invalidate %x %x\n", begin, len);
    begin = 0; len = 0x1000;
    assert(begin+len<=0x1000);
    while (len > 0) {
        OPI(begin).curgen = 0;
        begin += 4;
        len -= 4;
    }
    rsp.inval_gen = 1;
}

inline void rsp_execute_one(RSP_REGS & rsp, const UINT32 op)
{
    switch (op >> 26)
    {
    case 0x12:      /* COP2 */
        {
            handle_vector_ops(op);
            break;
        }

    case 0x32:      /* LWC2 */              handle_lwc2(op); break;
    case 0x3a:      /* SWC2 */              handle_swc2(op); break;

    default:
        {
            unimplemented_opcode(op);
            break;
        }
    }
}

static int cond;
static int run(RSP_REGS & rsp, gen_t * gen)
{
    int pc = 0;

    cond = 0;
    for ( ; ; ) {
        const rsp_bc_t & bc = gen->bc[pc];
        const UINT32 op = bc.op;
        const int op2 = bc.op2;

        //     if (op2 < RSP_CONTROL_OFFS) {
        //       int realpc = (bc.flags>>3)&0xffc;
        //       char s[128];
        //       rsp_dasm_one(s, realpc, op);
        //       fprintf(stderr, "%3x\t%s\n", realpc, s);
        //     }

        pc++;
        switch (op2) {
        case RSP_LOOP:
            pc = bc.flags>>5;
            break;
        case RSP_JUMPLOCAL:
        case RSP_CONDJUMPLOCAL:
            if (cond) {
                pc = bc.flags>>5;
                cond = 0;
            }
            break;
        case RSP_JUMP:
        case RSP_CONDJUMP:
            if (cond) {
                return 0;
            }
            break;

        #define _LINK(l) rsp.r[l] = ((bc.flags >>3)+8)&0xffc
        #define _JUMP_PC(a) { cond=1; rsp.nextpc = ((a) & 0xfff); }
        #define _JUMP_PC_L(a, l) { _LINK(l); _JUMP_PC(a); }
        #define _JUMP_REL(a) _JUMP_PC(((bc.flags >>3)+4+(a<<2))&0xffc)
        #define _JUMP_REL_L(a, l) _JUMP_PC_L(((bc.flags >>3)+4+(a<<2))&0xffc, l)

        case RSP_SLL:             if (RDREG) RDVAL = (UINT32)RTVAL << SHIFT; break;
        case RSP_SRL:             if (RDREG) RDVAL = (UINT32)RTVAL >> SHIFT; break;
        case RSP_SRA:             if (RDREG) RDVAL = (INT32)RTVAL >> SHIFT; break;
        case RSP_SLLV:            if (RDREG) RDVAL = (UINT32)RTVAL << (RSVAL & 0x1f); break;
        case RSP_SRLV:            if (RDREG) RDVAL = (UINT32)RTVAL >> (RSVAL & 0x1f); break;
        case RSP_SRAV:            if (RDREG) RDVAL = (INT32)RTVAL >> (RSVAL & 0x1f); break;
        case RSP_JR:              _JUMP_PC(RSVAL); break;
        case RSP_JALR:            _JUMP_PC_L(RSVAL, RDREG); break;
        case RSP_BREAK:
            {
                *z64_rspinfo.SP_STATUS_REG |= (SP_STATUS_HALT | SP_STATUS_BROKE );
                if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) {
                    *z64_rspinfo.MI_INTR_REG |= 1;
                    z64_rspinfo.CheckInterrupts();
                }
                return 1;
            }
        case RSP_ADD:             if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
        case RSP_ADDU:            if (RDREG) RDVAL = (INT32)(RSVAL + RTVAL); break;
        case RSP_SUB:             if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
        case RSP_SUBU:            if (RDREG) RDVAL = (INT32)(RSVAL - RTVAL); break;
        case RSP_AND:             if (RDREG) RDVAL = RSVAL & RTVAL; break;
        case RSP_OR:              if (RDREG) RDVAL = RSVAL | RTVAL; break;
        case RSP_XOR:             if (RDREG) RDVAL = RSVAL ^ RTVAL; break;
        case RSP_NOR:             if (RDREG) RDVAL = ~(RSVAL | RTVAL); break;
        case RSP_SLT:             if (RDREG) RDVAL = (INT32)RSVAL < (INT32)RTVAL; break;
        case RSP_SLTU:            if (RDREG) RDVAL = (UINT32)RSVAL < (UINT32)RTVAL; break;
        case RSP_BLTZ:            if ((INT32)(RSVAL) < 0) cond = 1; break;
        case RSP_BGEZ:            if ((INT32)(RSVAL) >= 0) cond = 1; break;
        case RSP_BGEZAL:  _LINK(31); if ((INT32)(RSVAL) >= 0) _JUMP_REL(SIMM16); break;
        case RSP_J:                     cond = 1; break;
        case RSP_JAL:           _JUMP_PC_L(UIMM26<<2, 31); break;
        case RSP_BEQ:           if (RSVAL == RTVAL) cond = 1; break;
        case RSP_BNE:           if (RSVAL != RTVAL) cond = 1; break;
        case RSP_BLEZ:          if ((INT32)RSVAL <= 0) cond = 1; break;
        case RSP_BGTZ:          if ((INT32)RSVAL > 0) cond = 1; break;
        case RSP_ADDI:          if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
        case RSP_ADDIU:         if (RTREG) RTVAL = (INT32)(RSVAL + SIMM16); break;
        case RSP_SLTI:          if (RTREG) RTVAL = (INT32)(RSVAL) < ((INT32)SIMM16); break;
        case RSP_SLTIU:         if (RTREG) RTVAL = (UINT32)(RSVAL) < (UINT32)((INT32)SIMM16); break;
        case RSP_ANDI:          if (RTREG) RTVAL = RSVAL & UIMM16; break;
        case RSP_ORI:           if (RTREG) RTVAL = RSVAL | UIMM16; break;
        case RSP_XORI:          if (RTREG) RTVAL = RSVAL ^ UIMM16; break;
        case RSP_LUI:           if (RTREG) RTVAL = UIMM16 << 16; break;

        case RSP_COP0:
            {
                switch ((op >> 21) & 0x1f)
                {
                case 0x00:      /* MFC0 */
                    if (RTREG)
                        RTVAL = get_cop0_reg(RDREG);
                    break;
                case 0x04:      /* MTC0 */
                    set_cop0_reg(RDREG, RTVAL);
                    if (rsp.inval_gen) {
                        rsp.inval_gen = 0;
                        sp_pc = ((bc.flags >>3) + 4)&0xffc;
                        return 2;
                    }
                    break;
                default:
                    log(M64MSG_WARNING, "unimplemented cop0 %x (%x)\n", (op >> 21) & 0x1f, op);
                    break;
                }
                break;
            }

        case RSP_MFC2:
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
        case RSP_CFC2:
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
        case RSP_MTC2:
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
        case RSP_CTC2:
            {
                // 31       25      20      15      10            0
                // ------------------------------------------------
                // | 010010 | 00110 | TTTTT | DDDDD | 00000000000 |
                // ------------------------------------------------
                //

                rsp.flag[RDREG] = RTVAL & 0xffff;
                break;
            }
        case RSP_LB:            if (RTREG) RTVAL = (INT32)(INT8)READ8(RSVAL + SIMM16); break;
        case RSP_LH:            if (RTREG) RTVAL = (INT32)(INT16)READ16(RSVAL + SIMM16); break;
        case RSP_LW:            if (RTREG) RTVAL = READ32(RSVAL + SIMM16); break;
        case RSP_LBU:           if (RTREG) RTVAL = (UINT8)READ8(RSVAL + SIMM16); break;
        case RSP_LHU:           if (RTREG) RTVAL = (UINT16)READ16(RSVAL + SIMM16); break;
        case RSP_SB:            WRITE8(RSVAL + SIMM16, RTVAL); break;
        case RSP_SH:            WRITE16(RSVAL + SIMM16, RTVAL); break;
        case RSP_SW:            WRITE32(RSVAL + SIMM16, RTVAL); break;

        default:
            switch (op >> 26)
            {
            case 0x12:    /* COP2 */
                handle_vector_ops(op);
                break;
            case 0x32:    /* LWC2 */
                handle_lwc2(op);
                break;
            case 0x3a:    /* SWC2 */
                handle_swc2(op);
                break;
            }
        }
    }
}

int rsp_gen_cache_hit;
int rsp_gen_cache_miss;
int rsp_jump(int pc)
{
    pc &= 0xfff;
    sp_pc = pc;
    rsp.nextpc = ~0;
    opinfo_t * opi = &OPI(pc);
    gen_t * gen = opi->curgen;
    rsp_gen_cache_hit++;
    if (!gen) {
        rsp_gen_cache_miss++;
        rsp_gen(pc);
    }
    gen = opi->curgen;
    //fprintf(stderr, "rsp_jump %x (%s)\n", pc, gen->name);

    int res = run(rsp, gen);

    //fprintf(stderr, "r31 %x from %x nextpc %x pc %x res %d (%s)\n", rsp.r[31], pc, rsp.nextpc, sp_pc, res, gen->name);
    if (rsp.nextpc != ~0U)
    {
        sp_pc = (rsp.nextpc & 0xfff);
        rsp.nextpc = ~0U;
    }
    else
    {
        //sp_pc = ((sp_pc+4)&0xfff);
    }
    return res;
}
