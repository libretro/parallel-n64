/* savestate_code_test -- cached code across a savestate loaded in session.
 *
 *   savestate_code_test rom FILE          write the test cartridge
 *   savestate_code_test saved RAMDUMP     check RAM at the save point
 *   savestate_code_test check RAMDUMP     check RAM after both versions ran
 *
 * The payload maps virtual 0x00400000 to the code page at RDRAM 0x20000
 * through the TLB, writes a function returning VALUE_A there and marks
 * phase 1; the state is saved in the wait that follows. It then calls the
 * function through KSEG0 and through the TLB, rewrites it through KSEG0
 * to return VALUE_B, calls it both ways again and marks phase 2: the TLB
 * call must find the rewrite even though the KSEG0 call compiled the
 * physical page again first.
 *
 * `make check-savestate-code` loads the state back into the same session
 * once phase 2 is reached, as run-ahead and rewind do: the CPU cores then
 * hold VALUE_B's code compiled while RDRAM holds VALUE_A's again, and the
 * calls after the load must return VALUE_A.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE       0x100000
#define PAYLOAD_ROM    0x1000
#define PAYLOAD_RDRAM  0x400
#define PAYLOAD_WORDS  128

#define CODE_PAGE      0x20000
#define CODE_VADDR     0x00400000UL

#define PHASE_ADDR     0x300
#define KSEG0_FIRST    0x304
#define TLB_FIRST      0x308
#define KSEG0_SECOND   0x30c
#define TLB_SECOND     0x310

#define VALUE_A        0x0aaa
#define VALUE_B        0x0bbb

#define V0 2
#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T4 12
#define T5 13
#define T6 14
#define RA 31

#define LUI(rt, imm)        (0x3c000000UL | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ORI(rt, rs, imm)    (0x34000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ADDIU(rt, rs, imm)  (0x24000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ANDI(rt, rs, imm)   (0x30000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define SW(rt, off, base)   (0xac000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define LW(rt, off, base)   (0x8c000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define BNE(rs, rt, off)    (0x14000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define BEQ(rs, rt, off)    (0x10000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define JR(rs)              (0x00000008UL | ((unsigned long)(rs) << 21))
#define JALR(rs)            (0x0000f809UL | ((unsigned long)(rs) << 21))
#define MTC0(rt, rd)        (0x40800000UL | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define TLBWI               0x42000002UL
#define NOP                 0UL

#define CP0_INDEX    0
#define CP0_ENTRYLO0 2
#define CP0_ENTRYLO1 3
#define CP0_PAGEMASK 5
#define CP0_ENTRYHI  10

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

/* count reg down from count << 16 to zero */
static void emit_wait(struct emitter* e, unsigned reg, unsigned count)
{
    unsigned loop;

    emit(e, LUI(reg, count));
    loop = e->n;
    emit(e, ADDIU(reg, reg, 0xffff));
    emit(e, BNE(reg, 0, (int)loop - (int)(e->n + 1)));
    emit(e, NOP);
}

/* store word w at T4 + off */
static void emit_store(struct emitter* e, unsigned off, unsigned long w)
{
    emit(e, LUI(T3, w >> 16));
    emit(e, ORI(T3, T3, w & 0xffff));
    emit(e, SW(T3, off, T4));
}

/* the function at the code page: return value */
static void emit_function(struct emitter* e, unsigned value)
{
    emit_store(e, 0, ORI(V0, 0, value));
    emit_store(e, 4, JR(RA));
    emit_store(e, 8, NOP);
}

/* call the function at hi << 16, storing what it returns at at */
static void emit_call(struct emitter* e, unsigned long hi, unsigned at)
{
    emit(e, LUI(T2, hi));
    emit(e, JALR(T2));
    emit(e, NOP);
    emit(e, SW(V0, at, T6));
}

static int write_rom(const char* path)
{
    unsigned char* rom = (unsigned char*)calloc(1, ROM_SIZE);
    struct emitter e;
    unsigned wait;
    FILE* f;

    if (!rom)
        return 1;

    rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    rom[7] = 0x0f;
    rom[8] = 0x80; rom[10] = 0x04;
    memcpy(rom + 0x20, "SAVESTATE CODE      ", 20);

    /* boot code at 0x40, run from DMEM: PI DMA the payload, jump to it */
    e.base = rom + 0x40;
    e.n = 0;
    emit(&e, LUI(T2, 0xa440));                    /* VI: frames need V_SYNC */
    emit(&e, ORI(T1, 0, 2));
    emit(&e, SW(T1, 0x0c, T2));
    emit(&e, ORI(T1, 0, 0x20d));
    emit(&e, SW(T1, 0x18, T2));
    emit(&e, LUI(T0, 0xa460));
    emit(&e, ORI(T1, 0, PAYLOAD_RDRAM));
    emit(&e, SW(T1, 0x00, T0));
    emit(&e, LUI(T1, 0x1000));
    emit(&e, ORI(T1, T1, PAYLOAD_ROM));
    emit(&e, SW(T1, 0x04, T0));
    emit(&e, ORI(T1, 0, PAYLOAD_WORDS * 4 - 1));
    emit(&e, SW(T1, 0x0c, T0));
    wait = e.n;
    emit(&e, LW(T1, 0x10, T0));
    emit(&e, ANDI(T1, T1, 3));
    emit(&e, BNE(T1, 0, (int)wait - (int)(e.n + 1)));
    emit(&e, NOP);
    emit(&e, LUI(T2, 0x8000));
    emit(&e, ORI(T2, T2, PAYLOAD_RDRAM));
    emit(&e, JR(T2));
    emit(&e, NOP);

    /* payload */
    e.base = rom + PAYLOAD_ROM;
    e.n = 0;
    emit(&e, LUI(T6, 0x8000));
    emit(&e, LUI(T4, 0x8000 | (CODE_PAGE >> 16)));
    emit_wait(&e, T5, 0x0010);                    /* past the first frames */
    /* TLB entry 0: the even 4 KiB page at CODE_VADDR onto CODE_PAGE */
    emit(&e, MTC0(0, CP0_PAGEMASK));
    emit(&e, LUI(T1, CODE_VADDR >> 16));
    emit(&e, MTC0(T1, CP0_ENTRYHI));
    emit(&e, ORI(T1, 0, ((CODE_PAGE >> 12) << 6) | 0x1f));
    emit(&e, MTC0(T1, CP0_ENTRYLO0));
    emit(&e, ORI(T1, 0, 1));
    emit(&e, MTC0(T1, CP0_ENTRYLO1));
    emit(&e, MTC0(0, CP0_INDEX));
    emit(&e, TLBWI);
    emit(&e, NOP);
    emit_function(&e, VALUE_A);
    emit(&e, ORI(T1, 0, 1));
    emit(&e, SW(T1, PHASE_ADDR, T6));
    emit_wait(&e, T5, 0x0080);                    /* the save lands here */
    /* KSEG0 first: after the load the TLB page must not keep its code
     * just because the physical page it maps was compiled again */
    emit_call(&e, 0x8000 | (CODE_PAGE >> 16), KSEG0_FIRST);
    emit_call(&e, CODE_VADDR >> 16, TLB_FIRST);
    emit_function(&e, VALUE_B);
    /* KSEG0 first again: compiling the physical page anew must not leave
     * the TLB page over it with the function the store replaced */
    emit_call(&e, 0x8000 | (CODE_PAGE >> 16), KSEG0_SECOND);
    emit_call(&e, CODE_VADDR >> 16, TLB_SECOND);
    emit(&e, ORI(T1, 0, 2));
    emit(&e, SW(T1, PHASE_ADDR, T6));
    emit(&e, BEQ(0, 0, 0xffff));
    emit(&e, NOP);

    if (e.n > PAYLOAD_WORDS)
    {
        fprintf(stderr, "payload of %u words overflows the DMA\n", e.n);
        free(rom);
        return 1;
    }

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
}

/* RETRO_MEMORY_SYSTEM_RAM holds RDRAM as host-order 32-bit words */
static unsigned long ram_word(const unsigned char* ram, unsigned long addr)
{
    return (unsigned long)ram[addr] | ((unsigned long)ram[addr + 1] << 8)
         | ((unsigned long)ram[addr + 2] << 16) | ((unsigned long)ram[addr + 3] << 24);
}

static int load_ram(const char* path, unsigned char* ram, size_t size)
{
    FILE* f = fopen(path, "rb");

    if (!f || fread(ram, 1, size, f) != size)
    {
        perror(path);
        if (f)
            fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv)
{
    unsigned char ram[0x400];

    if (argc == 3 && !strcmp(argv[1], "rom"))
        return write_rom(argv[2]);

    if (argc == 3 && !strcmp(argv[1], "saved"))
    {
        if (load_ram(argv[2], ram, sizeof(ram)))
            return 1;
        if (ram_word(ram, PHASE_ADDR) != 1)
        {
            fprintf(stderr, "FAIL: %s: the save point is in phase %lu, not before the calls\n",
                    argv[2], ram_word(ram, PHASE_ADDR));
            return 1;
        }
        return 0;
    }

    if (argc == 3 && !strcmp(argv[1], "check"))
    {
        unsigned long k1, t1, k2, t2;

        if (load_ram(argv[2], ram, sizeof(ram)))
            return 1;
        k1 = ram_word(ram, KSEG0_FIRST);
        t1 = ram_word(ram, TLB_FIRST);
        k2 = ram_word(ram, KSEG0_SECOND);
        t2 = ram_word(ram, TLB_SECOND);
        if (ram_word(ram, PHASE_ADDR) != 2 || k1 != VALUE_A || t1 != VALUE_A
              || k2 != VALUE_B || t2 != VALUE_B)
        {
            fprintf(stderr, "FAIL: %s: phase %lu, returned %#lx %#lx then %#lx %#lx, expected 2, %#x %#x then %#x %#x\n",
                    argv[2], ram_word(ram, PHASE_ADDR), k1, t1, k2, t2,
                    VALUE_A, VALUE_A, VALUE_B, VALUE_B);
            return 1;
        }
        printf("savestate_code_test: %s: the code the state holds ran\n", argv[2]);
        return 0;
    }

    fprintf(stderr, "usage: %s rom FILE | saved RAMDUMP | check RAMDUMP\n", argv[0]);
    return 2;
}
