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
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>

#define GENDEBUG

typedef int (* gen_f)(RSP_REGS & rsp);

struct gen_t {
    UINT32 crc;
    void * lib;
    gen_f f;
#ifdef GENDEBUG
    char name[32];
#endif
};

struct opinfo_t {
    int visit, labeled;
    int label;

    int nbgen;
    int szgen;
    gen_t * gentable;
    gen_t * curgen;
};

struct branch_t {
    int start, end;
};

static int curvisit;
static opinfo_t opinfo[0x1000/4];
static int nb_branches;
static branch_t branches[256];
static int nb_labels;
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
        case 0x00:	/* SPECIAL */
            {
                switch (op & 0x3f)
                {
                case 0x08:	/* JR */
                    br = 1;
                    break;
                case 0x09:	/* JALR */
                    //br = 1;
                    break;
                case 0x0d:	/* BREAK */
                    br = 1;
                    break;
                }
                break;
            }

        case 0x01:	/* REGIMM */
            {
                switch (RTREG)
                {
                case 0x00:	/* BLTZ */
                case 0x01:	/* BGEZ */
                    SETLABEL(REL(SIMM16));
                    break;
                case 0x11:	/* BGEZAL */
                    //br = 1;
                    break;
                }
                break;
            }

        case 0x02:	/* J */
            SETLABEL(ABS(UIMM26));
            br = 1;
            break;
        case 0x04:	/* BEQ */
        case 0x05:	/* BNE */
        case 0x06:	/* BLEZ */
        case 0x07:	/* BGTZ */
            SETLABEL(REL(SIMM16));
            break;
        case 0x03:	/* JAL */
            //SETLABEL(ABS(UIMM26));
            //br = 1;
            break;
        }

    }

    branches[nb_branches++].end = pc;
    assert(nb_branches < sizeof(branches)/sizeof(branches[0]));

    return crc;
}

static char tmps[1024];
static char * delayed;
static int has_cond;

#define COND                                    \
    has_cond = 1, fprintf

#define NOCOND() \
    if (cont && OPI((pc+4)&0xfff).labeled == curvisit) { \
    COND(fp, "cond = 1; \n");                          \
    } else                                               \
    has_cond = 0


static void D_JUMP_ABS(UINT32 addr)
{
    int a = addr&0xfff;
    sprintf(tmps, "%s { /*if (rsp.inval_gen) { rsp.nextpc=0x%x; return 0; }*/ %s goto L%d; }", has_cond? "if (cond)":"", a, has_cond? "cond=0; ":"", OPI(a).label);
    delayed = tmps;
}

static void D_JUMP_REL(int pc, int offset)
{
    D_JUMP_ABS(pc+4 + ((offset) << 2));
}

static void D_JUMP()
{
    sprintf(tmps, "%s { return 0; }", has_cond? "if (cond)":"");
    delayed = tmps;
}

static void D_JUMPL(int pc)
{
    sprintf(tmps,
        "%s { \n"
        "%s"
        "  int res;\n"
        "  if (res = rsp_jump(rsp.nextpc)) return res; \n"
        "  if (/*rsp.inval_gen || */sp_pc != 0x%x) return 0; \n"
        "}", has_cond? "if (cond)":"", has_cond?"  cond=0; \n":"", (pc+8)&0xfff);
    delayed = tmps;
    has_cond = 1;
}

static void dogen(const char * s, UINT32 op, FILE * fp)
{
    fprintf(fp, "#define op 0x%x\n%s\n#undef op\n", op, s);
}

#define GEN(s) dogen(s, op, fp)

static void rsp_gen(int pc)
{
    int i;
    const char * old_delayed;
    int oldbr, br;

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

    char src[128];
    char lib[128];
    char sym[128];
    sprintf(lib, "z64/rspgen/%x-%x-%x.so", crc, pc, len);
    sprintf(sym, "doit%x", crc);

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
    opi->curgen = gen;

#ifdef GENDEBUG
    strcpy(gen->name, lib);
#endif

    gen->crc = crc;
    gen->lib = dlopen(lib, RTLD_NOW);
    if (gen->lib) {
        gen->f = (gen_f) dlsym(gen->lib, sym);
        assert(gen->f);
        fprintf(stderr, "reloaded %s\n", lib);
        return;
    }
    //   else
    //     printf("%s\n", dlerror()); 

    sprintf(src, "z64/rspgen/%x-%x-%x.cpp", crc, pc, len);
    FILE * fp = fopen(src, "w");

    fprintf(fp,
        "#include \"rsp.h\"\n"
        "\n"
        "extern \"C\" {\n"
        "int %s() {\n"
        "int cond=0;\n",
        sym);

    for (i=0; i<nb_branches; i++) {
        int cont = 1;
        delayed = 0;
        //fprintf(stderr, "branch %x --> %x\n", branches[i].start, branches[i].end-4);
        for (pc=branches[i].start; cont || delayed; pc = (pc+4)&0xfff) {
            UINT32 op = ROPCODE(pc);
            char s[128];
            rsp_dasm_one(s, pc, op);
            if (cont && OPI(pc).labeled == curvisit)
                fprintf(fp, "L%d: ;\n", OPI(pc).label);
            //fprintf(fp, "/* %3x\t%s */\n", pc, s);
            fprintf(fp, "GENTRACE(\"%3x\t%s\\n\");\n", pc, s);
            oldbr = br;
            br = 0;
            old_delayed = delayed;
            delayed = 0;

            if (((pc+4)&0xfff)==branches[i].end)
                cont = 0;

            switch (op >> 26)
            {
            case 0x00:	/* SPECIAL */
                {
                    switch (op & 0x3f)
                    {
                    case 0x08:	/* JR */
                        if (!old_delayed) {
                            br = 1|8|16;
                            NOCOND();
                            D_JUMP();
                        }
                        break;
                    case 0x09:	/* JALR */
                        if (!old_delayed) {
                            br = 1;
                            NOCOND();
                            D_JUMPL(pc);
                        }
                        break;
                    case 0x0d:	/* BREAK */
                        br = 2|8;
                        //delayed = "return 1;";
                        has_cond = 0;
                        break;
                    }
                    break;
                }

            case 0x01:	/* REGIMM */
                {
                    switch (RTREG)
                    {
                    case 0x00:	/* BLTZ */
                        if (!old_delayed) {
                            COND(fp, "		cond=(INT32)(_RSVAL(0x%x)) < 0;\n", op);
                            D_JUMP_REL(pc, _SIMM16(op));
                            br = 4;
                        }
                        break;
                    case 0x01:	/* BGEZ */
                        if (!old_delayed) {
                            COND(fp, "		cond=(INT32)(_RSVAL(0x%x)) >= 0;\n", op);
                            D_JUMP_REL(pc, _SIMM16(op));
                            br = 4;
                        }
                        break;
                    case 0x11:	/* BGEZAL */
                        br = 1;
                        COND(fp, "cond=(INT32)(_RSVAL(0x%x)) >= 0;\n", op);
                        D_JUMPL(pc);
                        break;
                    }
                    break;
                }

            case 0x02:	/* J */
                if (!old_delayed) {
                    NOCOND();
                    D_JUMP_ABS(_UIMM26(op) <<2);
                    br = 4|8|16;
                }
                break;
            case 0x04:	/* BEQ */
                if (!old_delayed) {
                    COND(fp, "		cond=_RSVAL(0x%0x) == _RTVAL(0x%0x);\n", op, op);
                    D_JUMP_REL(pc, _SIMM16(op));
                    br = 4;
                }
                break;
            case 0x05:	/* BNE */
                if (!old_delayed) {
                    COND(fp, "		cond=_RSVAL(0x%0x) != _RTVAL(0x%0x);\n", op, op);
                    D_JUMP_REL(pc, _SIMM16(op));
                    br = 4;
                }
                break;
            case 0x06:	/* BLEZ */
                if (!old_delayed) {
                    COND(fp, "		cond=(INT32)_RSVAL(0x%0x) <= 0;\n", op);
                    D_JUMP_REL(pc, _SIMM16(op));
                    br = 4;
                }
                break;
            case 0x07:	/* BGTZ */
                if (!old_delayed) {
                    COND(fp, "		cond=(INT32)_RSVAL(0x%0x) > 0;\n", op);
                    D_JUMP_REL(pc, _SIMM16(op));
                    br = 4;
                }
                break;
            case 0x03:	/* JAL */
                if (!old_delayed) {
                    br = 1;
                    NOCOND();
                    D_JUMPL(pc);
                }
                break;
            }

            if (!(br&4) && (!old_delayed || !br)) {
                if (br && !(br&16)) {
                    fprintf(fp, "sp_pc = 0x%x;\n", (pc + 4)&0xfff);
                }
                //fprintf(fp, "rsp_execute_one(0x%x);\n", op);





                switch (op >> 26)
                {
                case 0x00:	/* SPECIAL */
                    {
                        switch (op & 0x3f)
                        {
                        case 0x00:	/* SLL */		if (RDREG) GEN("RDVAL = (UINT32)RTVAL << SHIFT;"); break;
                        case 0x02:	/* SRL */		if (RDREG) GEN("RDVAL = (UINT32)RTVAL >> SHIFT; "); break;
                        case 0x03:	/* SRA */		if (RDREG) GEN("RDVAL = (INT32)RTVAL >> SHIFT; "); break;
                        case 0x04:	/* SLLV */		if (RDREG) GEN("RDVAL = (UINT32)RTVAL << (RSVAL & 0x1f); "); break;
                        case 0x06:	/* SRLV */		if (RDREG) GEN("RDVAL = (UINT32)RTVAL >> (RSVAL & 0x1f); "); break;
                        case 0x07:	/* SRAV */		if (RDREG) GEN("RDVAL = (INT32)RTVAL >> (RSVAL & 0x1f); "); break;
                        case 0x08:	/* JR */		GEN("JUMP_PC(RSVAL); "); break;
                        case 0x09:	/* JALR */		GEN("JUMP_PC_L(RSVAL, RDREG); "); break;
                        case 0x0d:	/* BREAK */
                            {
                                GEN(
                                    "               \
                                    *z64_rspinfo.SP_STATUS_REG |= (SP_STATUS_HALT | SP_STATUS_BROKE ); \
                                    if ((*z64_rspinfo.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 ) { \
                                    *z64_rspinfo.MI_INTR_REG |= 1; \
                                    z64_rspinfo.CheckInterrupts(); \
                                    } \
                                    rsp.nextpc = ~0; \
                                    return 1; \
                                    ");
                                break;
                            }
                        case 0x20:	/* ADD */		if (RDREG) GEN("RDVAL = (INT32)(RSVAL + RTVAL); "); break;
                        case 0x21:	/* ADDU */		if (RDREG) GEN("RDVAL = (INT32)(RSVAL + RTVAL); "); break;
                        case 0x22:	/* SUB */		if (RDREG) GEN("RDVAL = (INT32)(RSVAL - RTVAL); "); break;
                        case 0x23:	/* SUBU */		if (RDREG) GEN("RDVAL = (INT32)(RSVAL - RTVAL); "); break;
                        case 0x24:	/* AND */		if (RDREG) GEN("RDVAL = RSVAL & RTVAL; "); break;
                        case 0x25:	/* OR */		if (RDREG) GEN("RDVAL = RSVAL | RTVAL; "); break;
                        case 0x26:	/* XOR */		if (RDREG) GEN("RDVAL = RSVAL ^ RTVAL; "); break;
                        case 0x27:	/* NOR */		if (RDREG) GEN("RDVAL = ~(RSVAL | RTVAL); "); break;
                        case 0x2a:	/* SLT */		if (RDREG) GEN("RDVAL = (INT32)RSVAL < (INT32)RTVAL; "); break;
                        case 0x2b:	/* SLTU */		if (RDREG) GEN("RDVAL = (UINT32)RSVAL < (UINT32)RTVAL; "); break;
                        default:	GEN("unimplemented_opcode(op); "); break;
                        }
                        break;
                    }

                case 0x01:	/* REGIMM */
                    {
                        switch (RTREG)
                        {
                        case 0x00:	/* BLTZ */		GEN("if ((INT32)(RSVAL) < 0) JUMP_REL(SIMM16); "); break;
                        case 0x01:	/* BGEZ */		GEN("if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); "); break;
                            // VP according to the doc, link is performed even when condition fails
                        case 0x11:	/* BGEZAL */	GEN("LINK(31); if ((INT32)(RSVAL) >= 0) JUMP_REL(SIMM16); "); break;
                            //case 0x11:	/* BGEZAL */	if ((INT32)(RSVAL) >= 0) JUMP_REL_L(SIMM16, 31); break;
                        default:	GEN("unimplemented_opcode(op); "); break;
                        }
                        break;
                    }

                case 0x02:	/* J */			GEN("JUMP_ABS(UIMM26); "); break;
                case 0x03:	/* JAL */		GEN("JUMP_ABS_L(UIMM26, 31); "); break;
                case 0x04:	/* BEQ */		GEN("if (RSVAL == RTVAL) JUMP_REL(SIMM16); "); break;
                case 0x05:	/* BNE */		GEN("if (RSVAL != RTVAL) JUMP_REL(SIMM16); "); break;
                case 0x06:	/* BLEZ */	GEN("if ((INT32)RSVAL <= 0) JUMP_REL(SIMM16); "); break;
                case 0x07:	/* BGTZ */	GEN("if ((INT32)RSVAL > 0) JUMP_REL(SIMM16); "); break;
                case 0x08:	/* ADDI */	if (RTREG) GEN("RTVAL = (INT32)(RSVAL + SIMM16); "); break;
                case 0x09:	/* ADDIU */	if (RTREG) GEN("RTVAL = (INT32)(RSVAL + SIMM16); "); break;
                case 0x0a:	/* SLTI */	if (RTREG) GEN("RTVAL = (INT32)(RSVAL) < ((INT32)SIMM16); "); break;
                case 0x0b:	/* SLTIU */	if (RTREG) GEN("RTVAL = (UINT32)(RSVAL) < (UINT32)((INT32)SIMM16); "); break;
                case 0x0c:	/* ANDI */	if (RTREG) GEN("RTVAL = RSVAL & UIMM16; "); break;
                case 0x0d:	/* ORI */		if (RTREG) GEN("RTVAL = RSVAL | UIMM16; "); break;
                case 0x0e:	/* XORI */	if (RTREG) GEN("RTVAL = RSVAL ^ UIMM16; "); break;
                case 0x0f:	/* LUI */		if (RTREG) GEN("RTVAL = UIMM16 << 16; "); break;

                case 0x10:	/* COP0 */
                    {
                        switch ((op >> 21) & 0x1f)
                        {
                        case 0x00:	/* MFC0 */		if (RTREG) GEN("RTVAL = get_cop0_reg(rsp, RDREG); "); break;
                        case 0x04:	/* MTC0 */
                            {
                                GEN("set_cop0_reg(rsp, RDREG, RTVAL); \n");
                                if (RDREG == 0x08/4) {
                                    fprintf(fp,
                                        "if (rsp.inval_gen) {\n"
                                        "   rsp.inval_gen = 0;\n"
                                        "   sp_pc = 0x%x; \n"
                                        "   return 2; \n"
                                        "}\n"
                                        , (pc + 4)&0xfff);
                                }
                                break;
                            }
                        default:
                            log(M64MSG_WARNING, "unimplemented cop0 %x (%x)\n", (op >> 21) & 0x1f, op);
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
                                if (RTREG) GEN("\
                                               {int el = (op >> 7) & 0xf;\
                                               UINT16 b1 = VREG_B(VS1REG, (el+0) & 0xf);\
                                               UINT16 b2 = VREG_B(VS1REG, (el+1) & 0xf);\
                                               RTVAL = (INT32)(INT16)((b1 << 8) | (b2));}\
                                               ");
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
                                if (RTREG) GEN("RTVAL = rsp.flag[RDREG];");
                                break;
                            }
                        case 0x04:	/* MTC2 */
                            {
                                // 31       25      20      15      10     6         0
                                // ---------------------------------------------------
                                // | 010010 | 00100 | TTTTT | DDDDD | IIII | 0000000 |
                                // ---------------------------------------------------
                                //
                                GEN("\
                                    {int el = (op >> 7) & 0xf;\
                                    VREG_B(VS1REG, (el+0) & 0xf) = (RTVAL >> 8) & 0xff;\
                                    VREG_B(VS1REG, (el+1) & 0xf) = (RTVAL >> 0) & 0xff;}\
                                    ");
                                break;
                            }
                        case 0x06:	/* CTC2 */
                            {
                                // 31       25      20      15      10            0
                                // ------------------------------------------------
                                // | 010010 | 00110 | TTTTT | DDDDD | 00000000000 |
                                // ------------------------------------------------
                                //

                                GEN("rsp.flag[RDREG] = RTVAL & 0xffff;");
                                break;
                            }

                        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
                        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                            {
                                GEN("handle_vector_ops(rsp, op);");
                                break;
                            }

                        default:	GEN("unimplemented_opcode(op); "); break;
                        }
                        break;
                    }

                case 0x20:	/* LB */		if (RTREG) GEN("RTVAL = (INT32)(INT8)READ8(RSVAL + SIMM16); "); break;
                case 0x21:	/* LH */		if (RTREG) GEN("RTVAL = (INT32)(INT16)READ16(RSVAL + SIMM16); "); break;
                case 0x23:	/* LW */		if (RTREG) GEN("RTVAL = READ32(RSVAL + SIMM16); "); break;
                case 0x24:	/* LBU */		if (RTREG) GEN("RTVAL = (UINT8)READ8(RSVAL + SIMM16); "); break;
                case 0x25:	/* LHU */		if (RTREG) GEN("RTVAL = (UINT16)READ16(RSVAL + SIMM16); "); break;
                case 0x28:	/* SB */		GEN("WRITE8(RSVAL + SIMM16, RTVAL); "); break;
                case 0x29:	/* SH */		GEN("WRITE16(RSVAL + SIMM16, RTVAL); "); break;
                case 0x2b:	/* SW */		GEN("WRITE32(RSVAL + SIMM16, RTVAL); "); break;
                case 0x32:	/* LWC2 */		GEN("handle_lwc2(rsp, op); "); break;
                case 0x3a:	/* SWC2 */		GEN("handle_swc2(rsp, op); "); break;

                default:
                    {
                        GEN("unimplemented_opcode(op);");
                        break;
                    }
                }




                //         if (br) {
                //           if (br & 2)
                //             fprintf(fp, "return 1;\n");
                //           else
                //             fprintf(fp, "return 0;\n");
                //         }
            }
            if (old_delayed)
                fprintf(fp, "%s\n", old_delayed);
        }
        if (!((/*br|*/oldbr)&8) && ((!oldbr && !(br&2)) || has_cond)) {
            fprintf(fp, "/* jumping back to %x */\ngoto L%d;\n", pc, OPI(pc).label);
            assert(OPI(pc).labeled == curvisit);
        }
    }

    fprintf(fp, "}}\n");

    fclose(fp);

    pid_t pid = fork();
    // SDL redirect these signals, but we need them untouched for waitpid call
    signal(17, 0);
    signal(11, 0);
    if (!pid) {
        //     char s[128];
        //     atexit(0);
        //     sprintf(s, "gcc -Iz64 -g -shared -O2 %s -o %s", src, lib);
        //     system(s);
        //     exit(0);

        //setsid();
        //execl("/usr/bin/gcc", "/usr/bin/gcc", "-Iz64", "-shared", "-g", "-O3", "-fomit-frame-pointer", src, "-o", lib, "-finline-limit=10000", 0);
        //execl("/usr/bin/gcc", "/usr/bin/gcc", "-Iz64", "-shared", "-O3", src, "-o", lib, "-fomit-frame-pointer", "-ffast-math", "-funroll-loops", "-fforce-addr", "-finline-limit=10000", 0);
        //execl("/usr/bin/gcc", "/usr/bin/gcc", "-Iz64", "-shared", "-O3", src, "-o", lib, "-fomit-frame-pointer", "-ffast-math", "-funroll-loops", "-fforce-addr", "-finline-limit=10000", "-m3dnow", "-mmmx", "-msse", "-msse2", "-mfpmath=sse", 0);
        execl("/usr/bin/gcc", "/usr/bin/gcc", "-Iz64", "-shared", "-O6", src, "-o", lib, "-fomit-frame-pointer", "-ffast-math", "-funroll-loops", "-fforce-addr", "-finline-limit=10000", "-m3dnow", "-mmmx", "-msse", "-msse2", 0);
        printf("gnii ??\n");
        exit(0);
    }
    waitpid(pid, 0, __WALL);

    gen->lib = dlopen(lib, RTLD_NOW);
    if (!gen->lib)
        log(M64MSG_WARNING, "%s\n", dlerror()); 
    assert(gen->lib);
    log(M64MSG_VERBOSE, "created and loaded %s\n", lib);
    gen->f = (gen_f) dlsym(gen->lib, sym);
    assert(gen->f);
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

int rsp_jump(int pc)
{
    pc &= 0xfff;
    sp_pc = pc;
    rsp.nextpc = ~0;
    opinfo_t * opi = &OPI(pc);
    gen_t * gen = opi->curgen;
    if (!gen) rsp_gen(pc);
    gen = opi->curgen;
    GENTRACE("rsp_jump %x (%s)\n", pc, gen->name);
    int res = gen->f(rsp);
    GENTRACE("r31 %x from %x nextpc %x pc %x res %d (%s)\n", rsp.r[31], pc, rsp.nextpc, sp_pc, res, gen->name);
    if (rsp.nextpc != ~0)
    {
        sp_pc = (rsp.nextpc & 0xfff);
        rsp.nextpc = ~0;
    }
    else
    {
        //sp_pc = ((sp_pc+4)&0xfff);
    }
    return res;
}
