/* cpu_fallback_test -- instructions a recompiler hands to the interpreter.
 *
 *   cpu_fallback_test rom FILE             write the test cartridge
 *   cpu_fallback_test check RAMDUMP        check the reference run's log
 *   cpu_fallback_test compare REF RAMDUMP  check a run logged what REF did
 *
 * The cartridge's boot code, run from DMEM by the HLE PIF boot, programs the
 * VI's V_SYNC so frames are timed, copies a payload to RDRAM 0x400 and an
 * exception handler to 0x180 by PI DMA, and jumps to the payload. The
 * handler logs Cause.ExcCode, steps EPC past the instruction and returns.
 * The payload runs BREAK with a register value pending across it, a guarded
 * BREAK skipped and taken as compilers emit after a division, the COP2
 * moves with COP2 unusable and usable, DCFC1 and DCTC1, logging each
 * exception and each value moved back, then stores how many words it
 * logged and a marker.
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
#define PAYLOAD_WORDS  96
#define HANDLER_ROM    0x2000
#define HANDLER_RDRAM  0x180
#define HANDLER_WORDS  10

#define LOG_ADDR       0x200
#define LOG_WORDS      20
#define COUNT_ADDR     0x1f8
#define MARKER_ADDR    0x1fc
#define MARKER         0x600dUL

#define EXC_BP         0x24UL  /* Breakpoint << 2 */
#define EXC_CPU        0x2cUL  /* Coprocessor Unusable << 2 */
#define EXC_FPE        0x3cUL  /* Floating-Point << 2 */

#define VAL_MOVE       0x1357c2deUL
#define VAL_DMOVE      0x89abcdefUL  /* sign-extends to 64 bits */
#define VAL_CTRL       0x2468c2c2UL
#define VAL_LIVE       0x5a5a1235UL

#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T4 12
#define T5 13
#define T6 14
#define S7 23
#define K0 26

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
#define BNE(rs, rt, off)    I(0x05, rs, rt, off)
#define BEQ(rs, rt, off)    I(0x04, rs, rt, off)
#define OR(rd, rs, rt)      R(0, rs, rt, rd, 0, 0x25)
#define AND(rd, rs, rt)     R(0, rs, rt, rd, 0, 0x24)
#define NOR(rd, rs, rt)     R(0, rs, rt, rd, 0, 0x27)
#define DSRL32(rd, rt, sa)  R(0, 0, rt, rd, sa, 0x3e)
#define JR(rs)              R(0, rs, 0, 0, 0, 0x08)
#define BREAK               R(0, 0, 0, 0, 0, 0x0d)
#define MFC0(rt, rd)        R(0x10, 0x00, rt, rd, 0, 0)
#define MTC0(rt, rd)        R(0x10, 0x04, rt, rd, 0, 0)
#define ERET                R(0x10, 0x10, 0, 0, 0, 0x18)
#define DCFC1(rt, rd)       R(0x11, 0x03, rt, rd, 0, 0)
#define DCTC1(rt, rd)       R(0x11, 0x07, rt, rd, 0, 0)
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

/* Status.CU2 on or off, through $t1/$t2 */
static void emit_cu2(struct emitter* e, int on)
{
    emit(e, MFC0(T1, CP0_STATUS));
    emit(e, LUI(T2, STATUS_CU2));
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

    /* exception handler: log ExcCode, return past the instruction */
    e.base = rom + HANDLER_ROM;
    e.n = 0;
    emit(&e, MFC0(K0, CP0_CAUSE));
    emit(&e, ANDI(K0, K0, 0x7c));
    emit_log(&e, K0);
    emit(&e, MFC0(K0, CP0_EPC));
    emit(&e, ADDIU(K0, K0, 4));
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
    emit_cu2(&e, 0);
    emit_li(&e, T3, VAL_MOVE);
    emit(&e, MTC2(T3, 0));                                /* CpU */
    emit_cu2(&e, 1);
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
    emit_cu2(&e, 0);
    emit(&e, MFC2(T4, 0));                                /* CpU */
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
        EXC_CPU, VAL_MOVE, VAL_DMOVE, 0xffffffffUL, VAL_CTRL, EXC_FPE, EXC_FPE, EXC_CPU
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
