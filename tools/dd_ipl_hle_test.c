/* dd_ipl_hle_test -- a 64DD disk boots without the IPL ROM, into the same
 * machine state the retail IPL hands a game.
 *
 *   dd_ipl_hle_test disk <file> <blocks>      write a test disk (SDK dump
 *                                            format) with that many boot blocks
 *   dd_ipl_hle_test check <ramdump> <blocks>  check what its boot code recorded
 *   dd_ipl_hle_test print <ramdump> <blocks>  print the record as a table
 *
 * The disk's boot code records the state it starts in -- the general and
 * FPU registers, CP0, the bytes the IPL leaves in low RDRAM, the drive ROM
 * words games test, and every RCP and drive register, VI_CURRENT included
 * as a probe of the VI timing -- and stops. `make check-dd-ipl` boots it
 * through the core with no 64DD_IPL.bin present and checks the record
 * against the one the retail Japanese IPL produces for the same disk, for
 * a 43-block boot (the length of Mario Artist: Talent Studio's) and an
 * 8-block one, whose last block sits on another track. The IPL's FPU
 * registers, VI origin and PI DRAM address follow how long its logo ran,
 * so the 8-block record leaves those out. Without the built-in IPL the
 * core cannot boot the disk at all and nothing is recorded.
 *
 * Exit status 0 on pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_SIZE      0x3dec800u
#define LBA0_BLOCK    0x4d08u      /* zone 0 block: 85 sectors of 232 */
#define SECTOR0       232u
#define ENTRY         0x80100000u
#define RECORD        0x00300000u  /* physical address of the record */
#define RECORD_SIZE   0x620u
#define RECORD_MARK   0x64dd0001u

/* ---- the boot code ------------------------------------------------------ */

enum { R_AT = 1, R_K0 = 26, R_K1 = 27 };

static unsigned int code[1024];
static unsigned int ncode;

static void op(unsigned int w) { code[ncode++] = w; }
static void lui(unsigned int rt, unsigned int imm) { op(0x3c000000u | (rt << 16) | (imm & 0xffff)); }
static void sd(unsigned int rt, unsigned int off, unsigned int base) { op(0xfc000000u | (base << 21) | (rt << 16) | (off & 0xffff)); }
static void sw(unsigned int rt, unsigned int off, unsigned int base) { op(0xac000000u | (base << 21) | (rt << 16) | (off & 0xffff)); }
static void sb(unsigned int rt, unsigned int off, unsigned int base) { op(0xa0000000u | (base << 21) | (rt << 16) | (off & 0xffff)); }
static void lw(unsigned int rt, unsigned int off, unsigned int base) { op(0x8c000000u | (base << 21) | (rt << 16) | (off & 0xffff)); }
static void lbu(unsigned int rt, unsigned int off, unsigned int base) { op(0x90000000u | (base << 21) | (rt << 16) | (off & 0xffff)); }
static void mfc0(unsigned int rt, unsigned int rd) { op(0x40000000u | (rt << 16) | (rd << 11)); }
static void cfc1(unsigned int rt, unsigned int fs) { op(0x44400000u | (rt << 16) | (fs << 11)); }
static void dmfc1(unsigned int rt, unsigned int fs) { op(0x44200000u | (rt << 16) | (fs << 11)); }

/* Copy n words from the uncached address hi:lo to the record. */
static void copy_words(unsigned int hi, unsigned int lo, unsigned int n,
                       unsigned int at)
{
    unsigned int i;
    lui(R_AT, hi);
    for (i = 0; i < n; ++i)
    {
        lw(R_K1, lo + 4 * i, R_AT);
        sw(R_K1, at + 4 * i, R_K0);
    }
}

/* Record layout (offsets from RECORD). */
#define AT_GPR    0x000   /* r1..r31 as doublewords; k0 is the base */
#define AT_CP0    0x100   /* 16 CP0 registers */
#define AT_FCR31  0x140
#define AT_FPR    0x180   /* even FPRs as doublewords */
#define AT_REGION 0x200   /* the bytes at 0x10, 0x90, 0x110, 0x190 */
#define AT_DISKID 0x210   /* 0x20 bytes from 0x1a0 */
#define AT_OS     0x240   /* 0x300..0x31f */
#define AT_DDROM  0x260   /* drive ROM words 0, 0x1010, 0x9ff00 */
#define AT_VI     0x280   /* 14 */
#define AT_PI     0x2c0   /* 13 */
#define AT_RI     0x300   /* 8 */
#define AT_MI     0x320   /* 4 */
#define AT_SI     0x330   /* 7 */
#define AT_AI     0x350   /* 6 */
#define AT_SP     0x370   /* 8 */
#define AT_DD     0x390   /* 19 */
#define AT_MARK   0x600

static const unsigned int cp0_list[16] = {
    9, 12, 13, 14, 16, 2, 3, 0, 4, 8, 30, 17, 10, 5, 6, 1
};

static void build_code(void)
{
    unsigned int i;

    ncode = 0;
    lui(R_K0, 0x8000 | (RECORD >> 16));
    for (i = 1; i < 32; ++i)
        if (i != R_K0)
            sd(i, AT_GPR + 8 * i, R_K0);
    for (i = 0; i < 16; ++i)
    {
        mfc0(R_K1, cp0_list[i]);
        sw(R_K1, AT_CP0 + 4 * i, R_K0);
    }
    cfc1(R_K1, 31);
    sw(R_K1, AT_FCR31, R_K0);
    for (i = 0; i < 16; ++i)
    {
        dmfc1(R_K1, 2 * i);
        sd(R_K1, AT_FPR + 8 * i, R_K0);
    }
    lui(R_AT, 0xa000);
    for (i = 0; i < 4; ++i)
    {
        lbu(R_K1, 0x10 + 0x80 * i, R_AT);
        sb(R_K1, AT_REGION + i, R_K0);
    }
    copy_words(0xa000, 0x1a0, 8, AT_DISKID);
    copy_words(0xa000, 0x300, 8, AT_OS);
    copy_words(0xa600, 0x0000, 1, AT_DDROM);
    copy_words(0xa600, 0x1010, 1, AT_DDROM + 4);
    copy_words(0xa60a, 0xff00, 1, AT_DDROM + 8);     /* 0xa609ff00: the
                                                        offset is negative */
    copy_words(0xa440, 0, 14, AT_VI);
    copy_words(0xa460, 0, 13, AT_PI);
    copy_words(0xa470, 0, 8, AT_RI);
    copy_words(0xa430, 0, 4, AT_MI);
    copy_words(0xa480, 0, 1, AT_SI);
    copy_words(0xa480, 4, 1, AT_SI + 4);
    copy_words(0xa480, 0x10, 1, AT_SI + 8);
    copy_words(0xa480, 0x18, 1, AT_SI + 12);
    copy_words(0xa450, 0, 6, AT_AI);
    copy_words(0xa404, 0, 8, AT_SP);
    copy_words(0xa500, 0x500, 19, AT_DD);
    lui(R_K1, RECORD_MARK >> 16);
    op(0x37600000u | (R_K1 << 16) | (RECORD_MARK & 0xffff)); /* ori k1,k1,lo */
    sw(R_K1, AT_MARK, R_K0);
    op(0x1000ffffu);                                      /* b . */
    op(0);
}

/* ---- the disk ----------------------------------------------------------- */

static void put32(unsigned char* p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static int write_disk(const char* path, unsigned int blocks)
{
    unsigned char sys[SECTOR0], id[SECTOR0];
    unsigned char* disk = calloc(SDK_SIZE, 1);
    unsigned int s, i;
    FILE* f;

    if (disk == NULL)
        return 1;
    memset(sys, 0, sizeof(sys));
    put32(sys + 0x00, 0xe848d316u);        /* Japan */
    sys[0x04] = 0x10;                      /* format */
    sys[0x05] = 0x10;                      /* disk type 0 */
    sys[0x06] = (unsigned char)(blocks >> 8);
    sys[0x07] = (unsigned char)blocks;     /* boot blocks */
    put32(sys + 0x1c, ENTRY);
    sys[0xe0] = 0x05; sys[0xe1] = 0x87;    /* ROM area end */
    sys[0xe2] = 0x05; sys[0xe3] = 0x8a;    /* RAM area start */
    sys[0xe4] = 0x07; sys[0xe5] = 0xad;    /* RAM area end */
    for (s = 0; s < 85; ++s)
        memcpy(disk + s * SECTOR0, sys, SECTOR0);

    memset(id, 0, sizeof(id));
    memcpy(id, "TDDJ", 4);                 /* game code */
    id[0x04] = 0x00;
    id[0x05] = 0x00;
    id[0x06] = 0x01;                       /* disk number */
    memcpy(id + 0x18, "01", 2);            /* company */
    for (s = 0; s < 85; ++s)
        memcpy(disk + 14 * LBA0_BLOCK + s * SECTOR0, id, SECTOR0);

    build_code();
    for (i = 0; i < ncode; ++i)
        put32(disk + 24 * LBA0_BLOCK + 4 * i, code[i]);

    f = fopen(path, "wb");
    if (f == NULL || fwrite(disk, 1, SDK_SIZE, f) != SDK_SIZE)
    {
        perror(path);
        if (f) fclose(f);
        free(disk);
        return 1;
    }
    fclose(f);
    free(disk);
    return 0;
}

/* ---- the record --------------------------------------------------------- */

/* RDRAM as the core exposes it: host-order 32-bit words. */
static unsigned char* ram;

static unsigned int word_at(unsigned int off)
{
    unsigned int a = RECORD + off;
    return (unsigned int)ram[a] | ((unsigned int)ram[a + 1] << 8)
         | ((unsigned int)ram[a + 2] << 16) | ((unsigned int)ram[a + 3] << 24);
}

static int load_ram(const char* path)
{
    FILE* f = fopen(path, "rb");
    long n;
    if (f == NULL) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < (long)(RECORD + RECORD_SIZE))
    {
        fclose(f);
        printf("FAIL: no system RAM in %s: the core did not boot the test disk\n", path);
        return 1;
    }
    ram = malloc((size_t)n);
    if (ram == NULL || fread(ram, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);
    return 0;
}

/* Words of the record that are compared, with what the retail Japanese
 * IPL leaves (pure interpreter, measured with `print`). */
struct expect
{
    unsigned short off;
    const char* what;
    unsigned int value;
    unsigned int mask;      /* the bits that are part of the hand-over */
};

#include "dd_ipl_hle_expected.h"

/* Words the record does not hold: r0 and k0, the record's base. */
static int skipped(unsigned int off)
{
    return off < AT_GPR + 8
        || (off >= AT_GPR + 8 * R_K0 && off < AT_GPR + 8 * R_K0 + 8);
}

/* Bits of a word that belong to the hand-over for a boot of this length.
 * Games test only the top byte of the drive ROM word at 0x9ff00. */
static unsigned int mask_of(unsigned int off, unsigned int blocks)
{
    if (off == AT_DDROM + 8)
        return 0xff000000u;
    if (blocks != 43
     && ((off >= AT_FPR && off < AT_FPR + 0x80) || off == AT_VI + 4
      || off == AT_PI))
        return 0;
    return 0xffffffffu;
}

static int check(const struct expect* expected, size_t count)
{
    size_t i;
    int failures = 0;

    if (word_at(AT_MARK) != RECORD_MARK)
    {
        printf("FAIL: the test disk's boot code never ran\n");
        return 1;
    }
    for (i = 0; i < count; ++i)
    {
        unsigned int got = word_at(expected[i].off);
        if ((got & expected[i].mask) != (expected[i].value & expected[i].mask))
        {
            printf("FAIL: %s reads %08x, the IPL leaves %08x\n",
                   expected[i].what, got & expected[i].mask,
                   expected[i].value & expected[i].mask);
            failures++;
        }
    }
    if (failures)
        return 1;
    printf("dd_ipl_hle_test: all %u checks passed\n", (unsigned)count);
    return 0;
}

static const char* name_of(unsigned int off, char* buf)
{
    if (off < AT_CP0) sprintf(buf, "r%u[%s]", (off - AT_GPR) / 8, (off & 4) ? "lo" : "hi");
    else if (off < AT_FCR31) sprintf(buf, "cp0 reg %u", cp0_list[(off - AT_CP0) / 4]);
    else if (off < AT_FPR) sprintf(buf, "fcr31");
    else if (off < AT_REGION) sprintf(buf, "f%u[%s]", (off - AT_FPR) / 4 & ~1u, (off & 4) ? "lo" : "hi");
    else if (off < AT_DISKID) sprintf(buf, "region bytes");
    else if (off < AT_OS) sprintf(buf, "disk id +%u", off - AT_DISKID);
    else if (off < AT_DDROM) sprintf(buf, "rdram 0x%03x", 0x300 + off - AT_OS);
    else if (off < AT_VI) sprintf(buf, "drive rom word %u", (off - AT_DDROM) / 4);
    else if (off < AT_PI) sprintf(buf, "vi reg %u", (off - AT_VI) / 4);
    else if (off < AT_RI) sprintf(buf, "pi reg %u", (off - AT_PI) / 4);
    else if (off < AT_MI) sprintf(buf, "ri reg %u", (off - AT_RI) / 4);
    else if (off < AT_SI) sprintf(buf, "mi reg %u", (off - AT_MI) / 4);
    else if (off < AT_AI) sprintf(buf, "si reg %u", (off - AT_SI) / 4);
    else if (off < AT_SP) sprintf(buf, "ai reg %u", (off - AT_AI) / 4);
    else if (off < AT_DD) sprintf(buf, "sp reg %u", (off - AT_SP) / 4);
    else sprintf(buf, "dd reg %u", (off - AT_DD) / 4);
    return buf;
}

static int print(unsigned int blocks)
{
    static const unsigned short spans[][2] = {
        { AT_GPR, AT_GPR + 0x100 }, { AT_CP0, AT_CP0 + 0x40 },
        { AT_FCR31, AT_FCR31 + 4 }, { AT_FPR, AT_FPR + 0x80 },
        { AT_REGION, AT_REGION + 4 }, { AT_DISKID, AT_DISKID + 0x20 },
        { AT_OS, AT_OS + 0x20 }, { AT_DDROM, AT_DDROM + 12 },
        { AT_VI, AT_VI + 56 }, { AT_PI, AT_PI + 52 }, { AT_RI, AT_RI + 32 },
        { AT_MI, AT_MI + 16 }, { AT_SI, AT_SI + 16 }, { AT_AI, AT_AI + 24 },
        { AT_SP, AT_SP + 32 }, { AT_DD, AT_DD + 76 }
    };
    size_t s;
    unsigned int off;
    char buf[64];

    if (word_at(AT_MARK) != RECORD_MARK)
    {
        fprintf(stderr, "the test disk's boot code never ran\n");
        return 1;
    }
    printf("static const struct expect expected_%u[] = {\n", blocks);
    for (s = 0; s < sizeof(spans) / sizeof(spans[0]); ++s)
        for (off = spans[s][0]; off < spans[s][1]; off += 4)
            if (!skipped(off) && mask_of(off, blocks) != 0)
                printf("    { 0x%03x, \"%s\", 0x%08xu, 0x%08xu },\n", off,
                       name_of(off, buf), word_at(off), mask_of(off, blocks));
    printf("};\n");
    return 0;
}

int main(int argc, char** argv)
{
    unsigned int blocks = argc == 4 ? (unsigned int)atoi(argv[3]) : 0;

    if (argc == 4 && strcmp(argv[1], "disk") == 0 && blocks > 0 && blocks < 64)
        return write_disk(argv[2], blocks);
    if (argc == 4 && strcmp(argv[1], "check") == 0 && blocks == 43)
        return load_ram(argv[2])
            || check(expected_43, sizeof(expected_43) / sizeof(expected_43[0]));
    if (argc == 4 && strcmp(argv[1], "check") == 0 && blocks == 8)
        return load_ram(argv[2])
            || check(expected_8, sizeof(expected_8) / sizeof(expected_8[0]));
    if (argc == 4 && strcmp(argv[1], "print") == 0 && blocks > 0)
        return load_ram(argv[2]) || print(blocks);
    fprintf(stderr, "usage: %s disk <file> <blocks> | check <ramdump> 43|8"
            " | print <ramdump> <blocks>\n", argv[0]);
    return 2;
}
