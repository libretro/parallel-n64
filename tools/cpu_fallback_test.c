/* cpu_fallback_test -- instructions a recompiler hands to the interpreter.
 *
 *   cpu_fallback_test rom FILE             write the test cartridge
 *   cpu_fallback_test check RAMDUMP        check the reference run's log
 *   cpu_fallback_test compare REF RAMDUMP  check a run logged what REF did
 *
 * The cartridge's boot code, run from DMEM by the HLE PIF boot, programs the
 * VI's V_SYNC so frames are timed, copies a payload to RDRAM 0x400 and an
 * exception handler to 0x180 by PI DMA, and jumps to the payload. The
 * handler saves $t7, logs Cause.BD and Cause.ExcCode, steps EPC past the
 * instruction, or past the branch and its delay slot, and returns.
 * The payload runs with interrupts off. It runs BREAK with a register value
 * pending across it, a guarded BREAK skipped and taken as compilers emit
 * after a division, the COP2 moves with COP2 unusable and usable, DCFC1 and
 * DCTC1, and the traps, including a TEQ whose low words match while the
 * upper words differ and the TEQ compilers place after a division. Then
 * taken, not taken, nullified, linking, register and BC1 branches with a
 * COP2 move, a trap, SYSCALL or BREAK in their delay slot, and a BREAK and
 * a trap whose handler reads a value the code after them overwrites, and
 * the FPU moves, sign operations and conversions, a loop keeping eight
 * registers live across interrupt checks, and LL/SC and LLD/SCD. It
 * logs each exception, each value moved back and a marker per step, then
 * stores how many words it logged and a marker.
 *
 * `make check-cpu-fallback` takes the pure interpreter's log as the
 * reference, checks its exception codes, and requires every other CPU core
 * to log the same words.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE       0x100000
#define PAYLOAD_ROM    0x1000
#define PAYLOAD_RDRAM  0x400
#define PAYLOAD_WORDS  320
#define HANDLER_ROM    0x2000
#define HANDLER_RDRAM  0x180
#define HANDLER_WORDS  16

#define LOG_ADDR       0x200
#define LOG_WORDS      60
#define SAVED_ADDR     0x1f4
#define LINKED_ADDR    0x2f0
#define COUNT_ADDR     0x1f8
#define MARKER_ADDR    0x1fc
#define MARKER         0x600dUL

#define EXC_BP         0x24UL  /* Breakpoint << 2 */
#define EXC_CPU        0x2cUL  /* Coprocessor Unusable << 2 */
#define EXC_FPE        0x3cUL  /* Floating-Point << 2 */
#define EXC_TR         0x34UL  /* Trap << 2 */
#define EXC_SYS        0x20UL  /* Syscall << 2 */
#define CAUSE_BD       0x80000000UL

#define VAL_MOVE       0x1357c2deUL
#define VAL_DMOVE      0x89abcdefUL  /* sign-extends to 64 bits */
#define VAL_CTRL       0x2468c2c2UL
#define VAL_LIVE       0x5a5a1235UL
#define VAL_DS1        0x0d5c2001UL
#define VAL_DS2        0x0d5c2002UL
#define VAL_DEAD       0x0dead123UL
#define LOOP_SUM       0x8a73c024UL

#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T4 12
#define T5 13
#define T6 14
#define T7 15
#define S0 16
#define S1 17
#define S2 18
#define S3 19
#define S7 23
#define K0 26
#define K1 27
#define RA 31

#define R(op, rs, rt, rd, sa, fn) (((unsigned long)(op) << 26) | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) \
                                  | ((unsigned long)(rd) << 11) | ((unsigned long)(sa) << 6) | (unsigned long)(fn))
#define I(op, rs, rt, imm)        (((unsigned long)(op) << 26) | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) \
                                  | ((unsigned long)(imm) & 0xffffUL))

#define LUI(rt, imm)        I(0x0f, 0, rt, imm)
#define ORI(rt, rs, imm)    I(0x0d, rs, rt, imm)
#define ANDI(rt, rs, imm)   I(0x0c, rs, rt, imm)
#define ADDIU(rt, rs, imm)  I(0x09, rs, rt, imm)
#define LW(rt, off, base)   I(0x23, base, rt, off)
#define SW(rt, off, base)   I(0x2b, base, rt, off)
#define LL(rt, off, base)   I(0x30, base, rt, off)
#define SC(rt, off, base)   I(0x38, base, rt, off)
#define LLD(rt, off, base)  I(0x34, base, rt, off)
#define SCD(rt, off, base)  I(0x3c, base, rt, off)
#define DADDIU(rt, rs, imm) I(0x19, rs, rt, imm)
#define BNE(rs, rt, off)    I(0x05, rs, rt, off)
#define BEQ(rs, rt, off)    I(0x04, rs, rt, off)
#define BEQL(rs, rt, off)   I(0x14, rs, rt, off)
#define BC1F(off)           I(0x11, 0x08, 0, off)
#define JAL(addr)           ((3UL << 26) | (((unsigned long)(addr) >> 2) & 0x3ffffffUL))
#define OR(rd, rs, rt)      R(0, rs, rt, rd, 0, 0x25)
#define AND(rd, rs, rt)     R(0, rs, rt, rd, 0, 0x24)
#define ADDU(rd, rs, rt)    R(0, rs, rt, rd, 0, 0x21)
#define SUBU(rd, rs, rt)    R(0, rs, rt, rd, 0, 0x23)
#define SRL(rd, rt, sa)     R(0, 0, rt, rd, sa, 0x02)
#define NOR(rd, rs, rt)     R(0, rs, rt, rd, 0, 0x27)
#define DSRL32(rd, rt, sa)  R(0, 0, rt, rd, sa, 0x3e)
#define DSLL32(rd, rt, sa)  R(0, 0, rt, rd, sa, 0x3c)
#define DIV(rs, rt)         R(0, rs, rt, 0, 0, 0x1a)
#define TEQ(rs, rt, code)   R(0, rs, rt, 0, 0, 0x34 | ((code) << 6))
#define TNE(rs, rt)         R(0, rs, rt, 0, 0, 0x36)
#define TLTU(rs, rt)        R(0, rs, rt, 0, 0, 0x33)
#define TEQI(rs, imm)       I(0x01, rs, 0x0c, imm)
#define TGEI(rs, imm)       I(0x01, rs, 0x08, imm)
#define JR(rs)              R(0, rs, 0, 0, 0, 0x08)
#define BREAK               R(0, 0, 0, 0, 0, 0x0d)
#define SYSCALL             R(0, 0, 0, 0, 0, 0x0c)
#define MFC0(rt, rd)        R(0x10, 0x00, rt, rd, 0, 0)
#define MTC0(rt, rd)        R(0x10, 0x04, rt, rd, 0, 0)
#define ERET                R(0x10, 0x10, 0, 0, 0, 0x18)
#define DCFC1(rt, rd)       R(0x11, 0x03, rt, rd, 0, 0)
#define DCTC1(rt, rd)       R(0x11, 0x07, rt, rd, 0, 0)
#define CTC1(rt, rd)        R(0x11, 0x06, rt, rd, 0, 0)
#define MTC1(rt, fs)        R(0x11, 0x04, rt, fs, 0, 0)
#define MFC1(rt, fs)        R(0x11, 0x00, rt, fs, 0, 0)
#define DMFC1(rt, fs)       R(0x11, 0x01, rt, fs, 0, 0)
#define FOP(fmt, fd, fs, fn) R(0x11, fmt, 0, fs, fd, fn)
#define FMT_S 0x10
#define FMT_D 0x11
#define FMT_W 0x14
#define MFC2(rt, rd)        R(0x12, 0x00, rt, rd, 0, 0)
#define DMFC2(rt, rd)       R(0x12, 0x01, rt, rd, 0, 0)
#define CFC2(rt, rd)        R(0x12, 0x02, rt, rd, 0, 0)
#define MTC2(rt, rd)        R(0x12, 0x04, rt, rd, 0, 0)
#define DMTC2(rt, rd)       R(0x12, 0x05, rt, rd, 0, 0)
#define CTC2(rt, rd)        R(0x12, 0x06, rt, rd, 0, 0)
#define NOP                 0UL

#define CP0_STATUS 12
#define CP0_CAUSE  13
#define CP0_EPC    14
#define STATUS_CU1 0x2000
#define STATUS_CU2 0x4000

struct emitter
{
    unsigned char* base;
    unsigned n;
};

static void emit(struct emitter* e, unsigned long v)
{
    unsigned char* p = e->base + 4 * e->n++;
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void emit_li(struct emitter* e, unsigned rt, unsigned long v)
{
    emit(e, LUI(rt, v >> 16));
    emit(e, ORI(rt, rt, v & 0xffff));
}

/* PI DMA len bytes from cartridge offset cart to RDRAM dram; $t0 = PI */
static void emit_dma(struct emitter* e, unsigned long cart, unsigned long dram, unsigned long len)
{
    unsigned wait;

    emit(e, ORI(T1, 0, dram));
    emit(e, SW(T1, 0x00, T0));
    emit_li(e, T1, 0x10000000UL + cart);
    emit(e, SW(T1, 0x04, T0));
    emit(e, ORI(T1, 0, len - 1));
    emit(e, SW(T1, 0x0c, T0));
    wait = e->n;
    emit(e, LW(T1, 0x10, T0));
    emit(e, ANDI(T1, T1, 3));
    emit(e, BNE(T1, 0, (int)wait - (int)(e->n + 1)));
    emit(e, NOP);
}

/* log $rt at $s7 and advance it */
static void emit_log(struct emitter* e, unsigned rt)
{
    emit(e, SW(rt, 0, S7));
    emit(e, ADDIU(S7, S7, 4));
}

/* log a step marker */
static void emit_mark(struct emitter* e, unsigned n)
{
    emit(e, ORI(T6, 0, 0x100 + n));
    emit_log(e, T6);
}

/* a Status.CU bit (upper half) on or off, through $t1/$t2 */
static void emit_cu(struct emitter* e, unsigned long cu, int on)
{
    emit(e, MFC0(T1, CP0_STATUS));
    emit(e, LUI(T2, cu));
    if (on)
        emit(e, OR(T1, T1, T2));
    else
    {
        emit(e, NOR(T2, T2, 0));
        emit(e, AND(T1, T1, T2));
    }
    emit(e, MTC0(T1, CP0_STATUS));
}

static int write_rom(const char* path)
{
    unsigned char* rom = (unsigned char*)calloc(1, ROM_SIZE);
    struct emitter e;
    unsigned link, loop;
    FILE* f;

    if (!rom)
        return 1;

    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    rom[7] = 0x0f;
    rom[8] = 0x80; rom[10] = 0x04;
    memcpy(rom + 0x20, "CPU FALLBACK        ", 20);

    /* boot code at 0x40, run from DMEM */
    e.base = rom + 0x40;
    e.n = 0;
    emit(&e, LUI(T2, 0xa440));                    /* VI: frames need V_SYNC */
    emit(&e, ORI(T1, 0, 2));
    emit(&e, SW(T1, 0x0c, T2));
    emit(&e, ORI(T1, 0, 0x20d));
    emit(&e, SW(T1, 0x18, T2));
    emit(&e, LUI(T0, 0xa460));
    emit_dma(&e, HANDLER_ROM, HANDLER_RDRAM, HANDLER_WORDS * 4);
    emit_dma(&e, PAYLOAD_ROM, PAYLOAD_RDRAM, PAYLOAD_WORDS * 4);
    emit_li(&e, T2, 0x80000000UL + PAYLOAD_RDRAM);
    emit(&e, JR(T2));
    emit(&e, NOP);

    /* exception handler: log BD and ExcCode, return past the instruction,
     * or past the branch and its delay slot */
    e.base = rom + HANDLER_ROM;
    e.n = 0;
    emit(&e, LUI(K1, 0x8000));
    emit(&e, SW(T7, SAVED_ADDR, K1));              /* $t7 as the handler sees it */
    emit(&e, MFC0(K0, CP0_CAUSE));
    emit(&e, LUI(K1, CAUSE_BD >> 16));
    emit(&e, ORI(K1, K1, 0x7c));
    emit(&e, AND(K0, K0, K1));
    emit_log(&e, K0);
    emit(&e, SRL(K1, K0, 29));                    /* 4 with BD */
    emit(&e, MFC0(K0, CP0_EPC));
    emit(&e, ADDIU(K0, K0, 4));
    emit(&e, ADDU(K0, K0, K1));
    emit(&e, MTC0(K0, CP0_EPC));
    emit(&e, NOP);
    emit(&e, ERET);
    emit(&e, NOP);
    if (e.n > HANDLER_WORDS)
        goto overflow;

    /* payload */
    e.base = rom + PAYLOAD_ROM;
    e.n = 0;
    emit_li(&e, S7, 0x80000000UL + LOG_ADDR);
    emit(&e, MFC0(T1, CP0_STATUS));                       /* interrupts off */
    emit(&e, ADDIU(T2, 0, 0xfffe));
    emit(&e, AND(T1, T1, T2));
    emit(&e, MTC0(T1, CP0_STATUS));
    emit_li(&e, T5, VAL_LIVE - 1);
    emit(&e, ADDIU(T5, T5, 1));                           /* dirty across BREAK */
    emit(&e, BREAK);                                      /* Bp */
    emit_log(&e, T5);                                     /* VAL_LIVE */
    emit(&e, ORI(T1, 0, 3));                              /* guarded, skipped */
    emit(&e, BNE(T1, 0, 2));
    emit(&e, NOP);
    emit(&e, BREAK);
    emit_log(&e, T1);                                     /* 3 */
    emit(&e, ORI(T1, 0, 0));                              /* guarded, taken */
    emit(&e, BNE(T1, 0, 2));
    emit(&e, NOP);
    emit(&e, BREAK);                                      /* Bp */
    emit_log(&e, T1);                                     /* 0 */
    emit_cu(&e, STATUS_CU2, 0);
    emit_li(&e, T3, VAL_MOVE);
    emit(&e, MTC2(T3, 0));                                /* CpU */
    emit_cu(&e, STATUS_CU2, 1);
    emit(&e, MTC2(T3, 0));
    emit(&e, MFC2(T4, 0));
    emit_log(&e, T4);                                     /* VAL_MOVE */
    emit_li(&e, T3, VAL_DMOVE);
    emit(&e, DMTC2(T3, 0));
    emit(&e, DMFC2(T5, 0));
    emit(&e, DSRL32(T6, T5, 0));
    emit_log(&e, T5);                                     /* low word */
    emit_log(&e, T6);                                     /* sign extension */
    emit_li(&e, T3, VAL_CTRL);
    emit(&e, CTC2(T3, 0));
    emit(&e, CFC2(T4, 0));
    emit_log(&e, T4);                                     /* VAL_CTRL */
    emit(&e, DCFC1(T4, 0));                               /* FPE */
    emit(&e, DCTC1(T4, 0));                               /* FPE */
    emit_cu(&e, STATUS_CU2, 0);
    emit(&e, MFC2(T4, 0));                                /* CpU */

    /* traps: 5 vs 3, 5 vs 5, 2^32+5 vs 5 (low words equal) */
    emit(&e, ORI(T1, 0, 5));
    emit(&e, ORI(T2, 0, 3));
    emit(&e, ORI(T3, 0, 1));
    emit(&e, DSLL32(T3, T3, 0));
    emit(&e, ORI(T3, T3, 5));
    emit(&e, TEQ(T1, T2, 0));                             /* no trap */
    emit_mark(&e, 1);
    emit(&e, ORI(T2, 0, 5));
    emit(&e, TEQ(T1, T2, 0));                             /* Tr */
    emit_mark(&e, 2);
    emit(&e, TEQ(T3, T1, 0));                             /* no trap */
    emit_mark(&e, 3);
    emit(&e, TEQI(T1, 5));                                /* Tr */
    emit(&e, TEQI(T1, 6));                                /* no trap */
    emit_mark(&e, 4);
    emit(&e, TNE(T1, T2));                                /* no trap */
    emit(&e, TNE(T3, T1));                                /* Tr */
    emit_mark(&e, 5);
    emit(&e, TGEI(T1, 0xffff));                           /* Tr: 5 >= -1 */
    emit(&e, TLTU(T1, T2));                               /* no trap: 5 < 5 */
    emit_mark(&e, 6);
    /* a division guarded the way compilers emit it */
    emit(&e, ORI(T2, 0, 7));
    emit(&e, DIV(T1, T2));
    emit(&e, TEQ(T2, 0, 7));                              /* no trap */
    emit_mark(&e, 7);
    emit(&e, DIV(T1, 0));
    emit(&e, TEQ(0, 0, 7));                               /* Tr */
    emit_mark(&e, 8);

    /* branches whose delay slot only the interpreter runs */
    emit_cu(&e, STATUS_CU2, 1);
    emit(&e, ORI(T1, 0, 5));
    emit(&e, ORI(T2, 0, 7));
    emit_li(&e, T3, VAL_DS1);
    emit(&e, BNE(T1, T2, 4));                             /* taken */
    emit(&e, MTC2(T3, 0));
    emit_mark(&e, 0xff);                                  /* skipped */
    emit(&e, MFC2(T4, 0));
    emit_log(&e, T4);                                     /* VAL_DS1 */
    emit_li(&e, T3, VAL_DS2);
    emit(&e, BEQ(T1, T2, 4));                             /* not taken */
    emit(&e, MTC2(T3, 0));
    emit_mark(&e, 9);
    emit(&e, MFC2(T4, 0));
    emit_log(&e, T4);                                     /* VAL_DS2 */
    emit(&e, BEQL(T1, T2, 4));                            /* not taken */
    emit(&e, MTC2(T1, 0));                                /* nullified */
    emit_mark(&e, 10);
    emit(&e, MFC2(T4, 0));
    emit_log(&e, T4);                                     /* VAL_DS2 */
    link = e.n + 2;
    emit(&e, JAL(0x80000000UL + PAYLOAD_RDRAM + 4 * (e.n + 5)));
    emit(&e, MFC2(T5, 0));                                /* the slot reads it */
    emit_mark(&e, 0xfe);                                  /* skipped */
    emit_log(&e, T5);                                     /* VAL_DS2 */
    emit_li(&e, T2, 0x80000000UL + PAYLOAD_RDRAM + 4 * link);
    emit(&e, SUBU(T2, RA, T2));
    emit_log(&e, T2);                                     /* 0: link */
    emit(&e, BEQ(0, 0, 1));                               /* taken */
    emit(&e, TEQ(T1, T1, 0));                             /* BD Tr */
    emit_mark(&e, 11);
    emit_li(&e, T2, 0x80000000UL + PAYLOAD_RDRAM + 4 * (e.n + 4));
    emit(&e, JR(T2));
    emit(&e, SYSCALL);                                    /* BD Sys */
    emit_mark(&e, 12);
    emit(&e, BNE(0, 0, 1));                               /* not taken */
    emit(&e, BREAK);                                      /* BD Bp */
    emit_mark(&e, 13);
    /* a value the code after the exception overwrites unread is still
     * the one the handler sees */
    emit_li(&e, T7, VAL_DEAD - 1);
    emit(&e, ADDIU(T7, T7, 1));
    emit(&e, BREAK);                                      /* Bp */
    emit(&e, ORI(T7, 0, 0));
    emit(&e, LUI(T2, 0x8000));
    emit(&e, LW(T4, SAVED_ADDR, T2));
    emit_log(&e, T4);                                     /* VAL_DEAD */
    emit_li(&e, T7, VAL_DEAD);
    emit(&e, TEQ(T1, T1, 0));                             /* Tr */
    emit(&e, ORI(T7, 0, 0));
    emit(&e, LW(T4, SAVED_ADDR, T2));
    emit_log(&e, T4);                                     /* VAL_DEAD */
    emit_cu(&e, STATUS_CU1, 1);
    emit(&e, CTC1(0, 31));                                /* condition clear */
    emit(&e, ORI(T1, 0, 5));
    emit(&e, BC1F(4));                                    /* taken */
    emit(&e, MTC2(T1, 0));
    emit_mark(&e, 0xfd);                                  /* skipped */
    emit(&e, MFC2(T4, 0));
    emit_log(&e, T4);                                     /* 5 */

    /* FPU moves, sign operations and conversions */
    emit(&e, MTC1(T1, 0));                                /* 5 */
    emit(&e, FOP(FMT_W, 2, 0, 0x21));                     /* cvt.d.w */
    emit(&e, FOP(FMT_D, 4, 2, 0x20));                     /* cvt.s.d */
    emit(&e, FOP(FMT_S, 6, 4, 0x07));                     /* neg.s */
    emit(&e, MFC1(T4, 6));
    emit_log(&e, T4);                                     /* -5.0f */
    emit(&e, FOP(FMT_S, 8, 6, 0x05));                     /* abs.s */
    emit(&e, FOP(FMT_S, 10, 8, 0x06));                    /* mov.s */
    emit(&e, FOP(FMT_S, 12, 10, 0x21));                   /* cvt.d.s */
    emit(&e, FOP(FMT_D, 14, 12, 0x07));                   /* neg.d */
    emit(&e, FOP(FMT_D, 14, 14, 0x05));                   /* abs.d */
    emit(&e, FOP(FMT_D, 16, 14, 0x06));                   /* mov.d */
    emit(&e, DMFC1(T4, 16));
    emit(&e, DSRL32(T5, T4, 0));
    emit_log(&e, T4);                                     /* 5.0 low */
    emit_log(&e, T5);                                     /* 5.0 high */
    emit(&e, FOP(FMT_D, 18, 16, 0x0d));                   /* trunc.w.d */
    emit(&e, MFC1(T4, 18));
    emit_log(&e, T4);                                     /* 5 */

    /* a loop long enough to cross interrupt checks, with every value it
     * keeps in a register live across them */
    emit(&e, ORI(T0, 0, 1));
    emit(&e, ORI(T1, 0, 2));
    emit(&e, ORI(T2, 0, 3));
    emit(&e, ORI(T3, 0, 4));
    emit(&e, ORI(T4, 0, 5));
    emit(&e, ORI(T5, 0, 6));
    emit(&e, ORI(S0, 0, 7));
    emit(&e, ORI(S1, 0, 8));
    emit(&e, LUI(T6, 2));                                 /* 0x20000 turns */
    loop = e.n;
    emit(&e, ADDIU(T0, T0, 1));
    emit(&e, ADDU(T1, T1, T0));
    emit(&e, ADDU(T2, T2, T1));
    emit(&e, ADDU(T3, T3, T2));
    emit(&e, ADDU(T4, T4, T3));
    emit(&e, ADDU(T5, T5, T4));
    emit(&e, ADDU(S0, S0, T5));
    emit(&e, ADDU(S1, S1, S0));
    emit(&e, ADDIU(T6, T6, 0xffff));
    emit(&e, BNE(T6, 0, (int)loop - (int)(e.n + 1)));
    emit(&e, NOP);
    emit(&e, ADDU(T0, T0, T1));
    emit(&e, ADDU(T0, T0, T2));
    emit(&e, ADDU(T0, T0, T3));
    emit(&e, ADDU(T0, T0, T4));
    emit(&e, ADDU(T0, T0, T5));
    emit(&e, ADDU(T0, T0, S0));
    emit(&e, ADDU(T0, T0, S1));
    emit_log(&e, T0);                                     /* LOOP_SUM */

    /* LL/SC and LLD/SCD */
    emit(&e, LUI(T2, 0x8000));
    emit(&e, LL(T4, SAVED_ADDR, T2));
    emit(&e, ADDIU(T4, T4, 1));
    emit(&e, SC(T4, SAVED_ADDR, T2));
    emit_log(&e, T4);                                     /* 1 */
    emit(&e, LW(T4, SAVED_ADDR, T2));
    emit_log(&e, T4);                                     /* VAL_DEAD + 1 */
    emit(&e, LLD(T4, LINKED_ADDR, T2));
    emit(&e, DADDIU(T4, T4, 0xffff));
    emit(&e, SCD(T4, LINKED_ADDR, T2));
    emit_log(&e, T4);                                     /* 1 */
    emit(&e, LW(T4, LINKED_ADDR, T2));
    emit(&e, LW(T5, LINKED_ADDR + 4, T2));
    emit_log(&e, T4);                                     /* -1 high */
    emit_log(&e, T5);                                     /* -1 low */
    emit_li(&e, T1, 0x80000000UL);
    emit_li(&e, T2, LOG_ADDR + 0x80000000UL);
    emit(&e, R(0, S7, T2, T2, 0, 0x23));                  /* SUBU: bytes logged */
    emit(&e, SW(T2, COUNT_ADDR, T1));
    emit(&e, ORI(T2, 0, MARKER));
    emit(&e, SW(T2, MARKER_ADDR, T1));
    emit(&e, BEQ(0, 0, 0xffff));
    emit(&e, NOP);
    if (e.n > PAYLOAD_WORDS)
        goto overflow;

    f = fopen(path, "wb");
    if (!f || fwrite(rom, 1, ROM_SIZE, f) != ROM_SIZE)
    {
        perror(path);
        if (f)
            fclose(f);
        free(rom);
        return 1;
    }
    fclose(f);
    free(rom);
    return 0;

overflow:
    fprintf(stderr, "code overflows its DMA\n");
    free(rom);
    return 1;
}

/* RETRO_MEMORY_SYSTEM_RAM holds RDRAM as host-order 32-bit words */
static unsigned long ram_word(const unsigned char* ram, unsigned long addr)
{
    return (unsigned long)ram[addr] | ((unsigned long)ram[addr + 1] << 8)
         | ((unsigned long)ram[addr + 2] << 16) | ((unsigned long)ram[addr + 3] << 24);
}

static int load_log(const char* path, unsigned long* log, unsigned* count)
{
    unsigned char ram[LOG_ADDR + 4 * LOG_WORDS];
    unsigned i;
    FILE* f = fopen(path, "rb");

    if (!f || fread(ram, 1, sizeof(ram), f) != sizeof(ram))
    {
        perror(path);
        if (f)
            fclose(f);
        return 1;
    }
    fclose(f);

    if (ram_word(ram, MARKER_ADDR) != MARKER)
    {
        fprintf(stderr, "FAIL: %s: the payload did not finish\n", path);
        return 1;
    }
    *count = (unsigned)(ram_word(ram, COUNT_ADDR) / 4);
    if (*count > LOG_WORDS)
    {
        fprintf(stderr, "FAIL: %s: %u log words\n", path, *count);
        return 1;
    }
    for (i = 0; i < *count; ++i)
        log[i] = ram_word(ram, LOG_ADDR + 4 * i);
    return 0;
}

int main(int argc, char** argv)
{
    static const unsigned long expect[] = {
        EXC_BP, VAL_LIVE, 3, EXC_BP, 0,
        EXC_CPU, VAL_MOVE, VAL_DMOVE, 0xffffffffUL, VAL_CTRL, EXC_FPE, EXC_FPE, EXC_CPU,
        0x101, EXC_TR, 0x102, 0x103, EXC_TR, 0x104, EXC_TR, 0x105, EXC_TR, 0x106,
        0x107, EXC_TR, 0x108,
        VAL_DS1, 0x109, VAL_DS2, 0x10a, VAL_DS2, VAL_DS2, 0,
        CAUSE_BD | EXC_TR, 0x10b, CAUSE_BD | EXC_SYS, 0x10c, CAUSE_BD | EXC_BP, 0x10d,
        EXC_BP, VAL_DEAD, EXC_TR, VAL_DEAD, 5,
        0xc0a00000UL, 0, 0x40140000UL, 5, LOOP_SUM,
        1, VAL_DEAD + 1, 1, 0xffffffffUL, 0xffffffffUL
    };
    unsigned long ref[LOG_WORDS], got[LOG_WORDS];
    unsigned nref, ngot, i;

    if (argc == 3 && !strcmp(argv[1], "rom"))
        return write_rom(argv[2]);

    if (argc == 3 && !strcmp(argv[1], "check"))
    {
        if (load_log(argv[2], ref, &nref))
            return 1;
        if (nref != sizeof(expect) / sizeof(expect[0]))
        {
            fprintf(stderr, "FAIL: %s: %u log words, expected %u\n", argv[2], nref,
                    (unsigned)(sizeof(expect) / sizeof(expect[0])));
            return 1;
        }
        for (i = 0; i < nref; ++i)
            if (ref[i] != expect[i])
            {
                fprintf(stderr, "FAIL: %s: log word %u is %#lx, expected %#lx\n", argv[2], i, ref[i], expect[i]);
                return 1;
            }
        printf("cpu_fallback_test: %s: reference log as expected\n", argv[2]);
        return 0;
    }

    if (argc == 4 && !strcmp(argv[1], "compare"))
    {
        if (load_log(argv[2], ref, &nref) || load_log(argv[3], got, &ngot))
            return 1;
        for (i = 0; i < nref || i < ngot; ++i)
            if (i >= nref || i >= ngot || ref[i] != got[i])
            {
                fprintf(stderr, "FAIL: %s: log word %u is %#lx, the reference logged %#lx\n", argv[3], i,
                        i < ngot ? got[i] : 0UL, i < nref ? ref[i] : 0UL);
                return 1;
            }
        printf("cpu_fallback_test: %s: matches the reference\n", argv[3]);
        return 0;
    }

    fprintf(stderr, "usage: %s rom FILE | check RAMDUMP | compare REF RAMDUMP\n", argv[0]);
    return 2;
}
