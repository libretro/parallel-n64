/* savestate_latch_test -- the COP0 and COP2 move latches across a savestate.
 *
 *   savestate_latch_test rom FILE          write the test cartridge
 *   savestate_latch_test saved RAMDUMP     check RAM at the save point
 *   savestate_latch_test check RAMDUMP     check RAM after the state ran on
 *
 * The cartridge's boot code, run from DMEM by the HLE PIF boot, programs
 * the VI's V_SYNC so frames are timed, copies a
 * payload to RDRAM 0x400 by PI DMA and jumps to it. The payload waits a few
 * frames, moves CP2_VALUE in with MTC2 and CP0_VALUE with MTC0 to an unused
 * COP0 register, marks phase 1, waits several more frames, then reads both
 * latches back with MFC0/MFC2 and marks phase 2.
 *
 * `make check-savestate-latch` saves a state during the second wait, loads
 * it into a fresh boot that has not reached the latch writes, and checks the
 * values read back are the ones moved in before the save.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE       0x100000
#define PAYLOAD_ROM    0x1000
#define PAYLOAD_RDRAM  0x400
#define PAYLOAD_WORDS  64

#define CP0_RESULT     0x308
#define CP2_RESULT     0x30c
#define PHASE_ADDR     0x310

#define CP0_VALUE      0x1234c0deUL
#define CP2_VALUE      0x5678c2deUL

#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T4 12
#define T5 13
#define T6 14

#define LUI(rt, imm)        (0x3c000000UL | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ORI(rt, rs, imm)    (0x34000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define OR(rd, rs, rt)      (0x00000025UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define ADDIU(rt, rs, imm)  (0x24000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ANDI(rt, rs, imm)   (0x30000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define SW(rt, off, base)   (0xac000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define LW(rt, off, base)   (0x8c000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define BNE(rs, rt, off)    (0x14000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define BEQ(rs, rt, off)    (0x10000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define JR(rs)              (0x00000008UL | ((unsigned long)(rs) << 21))
#define MFC0(rt, rd)        (0x40000000UL | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define MTC0(rt, rd)        (0x40800000UL | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define MFC2(rt, rd)        (0x48000000UL | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define MTC2(rt, rd)        (0x48800000UL | ((unsigned long)(rt) << 16) | ((unsigned long)(rd) << 11))
#define NOP                 0UL

#define CP0_STATUS 12
#define CP0_UNUSED 7

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
    memcpy(rom + 0x20, "SAVESTATE LATCHES   ", 20);

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
    emit_wait(&e, T5, 0x0010);                    /* past the first frames */
    emit(&e, MFC0(T1, CP0_STATUS));               /* make COP2 usable */
    emit(&e, LUI(T2, 0x4000));
    emit(&e, OR(T1, T1, T2));
    emit(&e, MTC0(T1, CP0_STATUS));
    emit(&e, LUI(T3, CP2_VALUE >> 16));
    emit(&e, ORI(T3, T3, CP2_VALUE & 0xffff));
    emit(&e, MTC2(T3, 0));
    emit(&e, LUI(T4, CP0_VALUE >> 16));
    emit(&e, ORI(T4, T4, CP0_VALUE & 0xffff));
    emit(&e, MTC0(T4, CP0_UNUSED));
    emit(&e, ORI(T1, 0, 1));
    emit(&e, SW(T1, PHASE_ADDR, T6));
    emit_wait(&e, T5, 0x0080);                    /* the save lands here */
    emit(&e, MFC0(T1, CP0_UNUSED));
    emit(&e, SW(T1, CP0_RESULT, T6));
    emit(&e, MFC2(T1, 0));
    emit(&e, SW(T1, CP2_RESULT, T6));
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
            fprintf(stderr, "FAIL: %s: the save point is in phase %lu, not between the latch moves and their read-back\n",
                    argv[2], ram_word(ram, PHASE_ADDR));
            return 1;
        }
        return 0;
    }

    if (argc == 3 && !strcmp(argv[1], "check"))
    {
        unsigned long cp0, cp2;

        if (load_ram(argv[2], ram, sizeof(ram)))
            return 1;
        cp0 = ram_word(ram, CP0_RESULT);
        cp2 = ram_word(ram, CP2_RESULT);
        if (ram_word(ram, PHASE_ADDR) != 2 || cp0 != CP0_VALUE || cp2 != CP2_VALUE)
        {
            fprintf(stderr, "FAIL: %s: phase %lu, COP0 latch %#lx, COP2 latch %#lx, expected 2, %#lx, %#lx\n",
                    argv[2], ram_word(ram, PHASE_ADDR), cp0, cp2, CP0_VALUE, CP2_VALUE);
            return 1;
        }
        printf("savestate_latch_test: %s: latches survived the state\n", argv[2]);
        return 0;
    }

    fprintf(stderr, "usage: %s rom FILE | saved RAMDUMP | check RAMDUMP\n", argv[0]);
    return 2;
}
