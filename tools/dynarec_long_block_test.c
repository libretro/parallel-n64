/* dynarec_long_block_test -- a straight-line block longer than the
 * recompiler's block buffer.
 *
 *   dynarec_long_block_test rom FILE        write the test cartridge
 *   dynarec_long_block_test check RAMDUMP   check the RAM it leaves behind
 *
 * The cartridge's boot code, run from DMEM by the HLE PIF boot, programs
 * the VI's V_SYNC so frames are timed, copies a
 * payload to RDRAM 0x400 by PI DMA and jumps to it. The payload counts in
 * $t0 with STRAIGHT_LINE consecutive ADDIUs and no branch, so the recompiler
 * decodes one block until its instruction buffer is full and must end it
 * there, then stores the count at 0x300 and a marker at 0x304 and spins.
 * A never-taken branch at block index MAXBLOCK - 4 checks that the block
 * keeps the branch's delay slot.
 *
 * `make check-dynarec-block` boots it through the core with each CPU core;
 * built with SANITIZER=address,undefined it also catches any read past the
 * end of the decoder's per-instruction arrays.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE       0x100000
#define PAYLOAD_ROM    0x1000
#define PAYLOAD_RDRAM  0x400
#define STRAIGHT_LINE  4200
#define BRANCH_INDEX   4092 /* MAXBLOCK - 4 */
#define RESULT_ADDR    0x300
#define MARKER_ADDR    0x304
#define MARKER         0x600d

#define T0 8
#define T1 9
#define T2 10

#define LUI(rt, imm)        (0x3c000000UL | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ORI(rt, rs, imm)    (0x34000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ADDIU(rt, rs, imm)  (0x24000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define ANDI(rt, rs, imm)   (0x30000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((imm) & 0xffffUL))
#define SW(rt, off, base)   (0xac000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define LW(rt, off, base)   (0x8c000000UL | ((unsigned long)(base) << 21) | ((unsigned long)(rt) << 16) | ((off) & 0xffffUL))
#define BNE(rs, rt, off)    (0x14000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define BEQ(rs, rt, off)    (0x10000000UL | ((unsigned long)(rs) << 21) | ((unsigned long)(rt) << 16) | ((unsigned long)(off) & 0xffffUL))
#define JR(rs)              (0x00000008UL | ((unsigned long)(rs) << 21))
#define NOP                 0UL

static void put32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int write_rom(const char* path)
{
    unsigned char* rom = (unsigned char*)calloc(1, ROM_SIZE);
    unsigned char* p;
    unsigned long payload_len = (unsigned long)(STRAIGHT_LINE + 9) * 4;
    unsigned i;
    int wait;
    FILE* f;

    if (!rom)
        return 1;

    /* header: big-endian image, PI timings, entry point */
    put32(rom + 0x00, 0x80371240UL);
    put32(rom + 0x04, 0x0000000fUL);
    put32(rom + 0x08, 0x80000400UL);
    memcpy(rom + 0x20, "DYNAREC LONG BLOCK  ", 20);

    /* boot code at 0x40, run from DMEM: PI DMA the payload, jump to it */
    p = rom + 0x40;
    put32(p, LUI(T2, 0xa440));                                   p += 4; /* VI: frames need V_SYNC */
    put32(p, ORI(T1, 0, 2));                                     p += 4;
    put32(p, SW(T1, 0x0c, T2));                                  p += 4;
    put32(p, ORI(T1, 0, 0x20d));                                 p += 4;
    put32(p, SW(T1, 0x18, T2));                                  p += 4;
    put32(p, LUI(T0, 0xa460));                                   p += 4;
    put32(p, ORI(T1, 0, PAYLOAD_RDRAM));                         p += 4;
    put32(p, SW(T1, 0x00, T0));                                  p += 4;
    put32(p, LUI(T1, 0x1000));                                   p += 4;
    put32(p, ORI(T1, T1, PAYLOAD_ROM));                          p += 4;
    put32(p, SW(T1, 0x04, T0));                                  p += 4;
    put32(p, LUI(T1, (payload_len - 1) >> 16));                  p += 4;
    put32(p, ORI(T1, T1, (payload_len - 1) & 0xffff));           p += 4;
    put32(p, SW(T1, 0x0c, T0));                                  p += 4;
    wait = (int)((p - (rom + 0x40)) / 4);
    put32(p, LW(T1, 0x10, T0));                                  p += 4;
    put32(p, ANDI(T1, T1, 3));                                   p += 4;
    put32(p, BNE(T1, 0, wait - ((int)((p - (rom + 0x40)) / 4) + 1))); p += 4;
    put32(p, NOP);                                               p += 4;
    put32(p, LUI(T2, 0x8000));                                   p += 4;
    put32(p, ORI(T2, T2, PAYLOAD_RDRAM));                        p += 4;
    put32(p, JR(T2));                                            p += 4;
    put32(p, NOP);                                               p += 4;

    /* payload: count STRAIGHT_LINE times, publish, spin; a never-taken
     * branch sits where a block cut at the buffer limit would separate it
     * from its delay slot */
    p = rom + PAYLOAD_ROM;
    put32(p, ORI(T0, 0, 0));                                     p += 4;
    for (i = 0; i < STRAIGHT_LINE; ++i)
    {
        if ((p - (rom + PAYLOAD_ROM)) / 4 == BRANCH_INDEX)
        {
            put32(p, BNE(T0, T0, 1));                            p += 4;
        }
        put32(p, ADDIU(T0, T0, 1));                              p += 4;
    }
    put32(p, LUI(T1, 0x8000));                                   p += 4;
    put32(p, SW(T0, RESULT_ADDR, T1));                           p += 4;
    put32(p, ORI(T2, 0, MARKER));                                p += 4;
    put32(p, SW(T2, MARKER_ADDR, T1));                           p += 4;
    put32(p, BEQ(0, 0, -1));                                     p += 4;
    put32(p, NOP);                                               p += 4;

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

static int check_ram(const char* path)
{
    unsigned char ram[0x400];
    unsigned long count, marker;
    FILE* f = fopen(path, "rb");

    if (!f || fread(ram, 1, sizeof(ram), f) != sizeof(ram))
    {
        perror(path);
        if (f)
            fclose(f);
        return 1;
    }
    fclose(f);

    count  = ram_word(ram, RESULT_ADDR);
    marker = ram_word(ram, MARKER_ADDR);
    if (marker != MARKER || count != STRAIGHT_LINE)
    {
        fprintf(stderr, "FAIL: %s: count %lu marker %#lx, expected %u and %#x\n",
                path, count, marker, STRAIGHT_LINE, MARKER);
        return 1;
    }
    printf("dynarec_long_block_test: %s: %u-instruction straight line ran\n", path, STRAIGHT_LINE);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 3 && !strcmp(argv[1], "rom"))
        return write_rom(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "check"))
        return check_ram(argv[2]);
    fprintf(stderr, "usage: %s rom FILE | check RAMDUMP\n", argv[0]);
    return 2;
}
