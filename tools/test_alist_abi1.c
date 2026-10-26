/* test_alist_abi1 -- the ABI1 (aspMain) buffer commands keep the
 * microcode's load/store order when source and destination overlap.
 *
 *  - MIXER loads two vectors ahead: the second vector of a pair mixes
 *    the old samples even when the first store has already overwritten
 *    them;
 *  - the Blast Corps / Diddy Kong Racing MIXER loads four vectors ahead;
 *  - DMEMMOVE moves whole 16-byte rows, so a forward overlapping move
 *    copies the source instead of smearing it;
 *  - INTERLEAVE reads eight samples of each side before storing;
 *  - RESAMPLE with a zero count still produces one 16-byte block, and
 *    it reads every tap of an eight-lane block before storing any lane.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/alist.c"
#include "../mupen64plus-rsp-hle/src/alist_audio.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void HleVerboseMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void HleWarnMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void rsp_break(struct hle_t* hle, unsigned int setbits)
{
    (void)hle; (void)setbits;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static struct hle_t s_task;

static void task_setup(void)
{
    static unsigned char* dram;
    static unsigned char* dmem;
    static unsigned int sp_status;

    if (dram == NULL) {
        dram = (unsigned char*)malloc(0x800000u);
        dmem = (unsigned char*)malloc(0x1000u);
        if (dram == NULL || dmem == NULL)
            exit(2);
    }
    memset(dram, 0, 0x800000u);
    memset(dmem, 0, 0x1000u);
    memset(&s_task, 0, sizeof(s_task));
    s_task.dram = dram;
    s_task.dmem = dmem;
    s_task.sp_status = &sp_status;
    *dmem_u32(&s_task, TASK_UCODE) = 0x100000;
    *dmem_u32(&s_task, TASK_DATA_PTR) = 0x1000;
    *dmem_u32(&s_task, TASK_DATA_SIZE) = 0;
}

static void task_cmd(uint32_t w1, uint32_t w2)
{
    uint32_t at = 0x1000 + *dmem_u32(&s_task, TASK_DATA_SIZE);
    *dram_u32(&s_task, at) = w1;
    *dram_u32(&s_task, at + 4) = w2;
    *dmem_u32(&s_task, TASK_DATA_SIZE) += 8;
}

static int16_t get16(uint16_t at) { return *alist_s16(&s_task, at); }
static void put16(uint16_t at, int16_t v) { *alist_s16(&s_task, at) = v; }

/* vmulf(dst, 0x7fff) + vmacf(src, gain) */
static int16_t mixref(int16_t d, int16_t s, int16_t gain)
{
    int64_t acc = 0x8000 + 2 * (int64_t)((int32_t)d * 0x7fff)
                         + 2 * (int64_t)((int32_t)s * (int32_t)gain);
    acc >>= 16;
    return (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
}

/* in at DMEM 0x600, out at 0x620: the source runs two vectors behind */
enum { MIX_IN = 0x600, MIX_OUT = 0x620, MIX_GAIN = 0x4000 };

static int16_t s_old[48];

static void mix_setup(void)
{
    unsigned k;
    task_setup();
    for (k = 0; k < 48; ++k) {
        s_old[k] = (int16_t)(311 * k - 4000);
        put16((uint16_t)(MIX_IN + 2*k), s_old[k]);
    }
    task_cmd(0x08000000u | (MIX_IN - DMEM_BASE),
             ((uint32_t)(MIX_OUT - DMEM_BASE) << 16) | 64);   /* SETBUFF */
    task_cmd(0x0c000000u | MIX_GAIN,
             ((uint32_t)(MIX_IN - DMEM_BASE) << 16) | (MIX_OUT - DMEM_BASE));
}

static void test_mixer(void)
{
    int16_t block0[8], block1[8];
    unsigned i;
    int ok = 1;

    mix_setup();
    alist_process_audio(&s_task);
    for (i = 0; i < 8; ++i) {
        block0[i] = mixref(s_old[16 + i], s_old[i], MIX_GAIN);
        block1[i] = mixref(s_old[24 + i], s_old[8 + i], MIX_GAIN);
        if (get16((uint16_t)(MIX_OUT + 2*i)) != block0[i]
         || get16((uint16_t)(MIX_OUT + 16 + 2*i)) != block1[i])
            ok = 0;
        /* block 2 is loaded after block 0 is stored... */
        if (get16((uint16_t)(MIX_OUT + 32 + 2*i)) != mixref(s_old[32 + i], block0[i], MIX_GAIN))
            ok = 0;
        /* ...but block 3's source was loaded before block 1 was stored */
        if (get16((uint16_t)(MIX_OUT + 48 + 2*i)) != mixref(s_old[40 + i], s_old[24 + i], MIX_GAIN))
            ok = 0;
    }
    CHECK(ok, "ABI1 MIXER: two vectors of lookahead over an overlapping source");

    mix_setup();
    alist_process_audio_bc(&s_task);
    ok = 1;
    for (i = 0; i < 32; ++i)
        if (get16((uint16_t)(MIX_OUT + 2*i)) != mixref(s_old[16 + i], s_old[i], MIX_GAIN))
            ok = 0;
    CHECK(ok, "ABI1 MIXER (Blast Corps): four vectors of lookahead");
}

static void test_dmemmove(void)
{
    unsigned k;
    int ok = 1;

    task_setup();
    for (k = 0; k < 32; ++k)
        *alist_u8(&s_task, (uint16_t)(0x600 + k)) = (uint8_t)(0x40 + k);
    task_cmd(0x0a000000u | (0x600 - DMEM_BASE),
             ((uint32_t)(0x608 - DMEM_BASE) << 16) | 16);      /* DMEMMOVE */
    alist_process_audio(&s_task);
    for (k = 0; k < 16; ++k)
        if (*alist_u8(&s_task, (uint16_t)(0x608 + k)) != 0x40 + k)
            ok = 0;
    CHECK(ok, "ABI1 DMEMMOVE: a forward overlapping move copies a whole row");
}

static void test_interleave(void)
{
    static const int16_t left_hi[8] = { 4, 104, 5, 105, 6, 106, 7, 107 };
    unsigned i;
    int ok = 1;

    task_setup();
    for (i = 0; i < 16; ++i) {
        put16((uint16_t)(0x600 + 2*i), (int16_t)i);           /* left, also out */
        put16((uint16_t)(0x640 + 2*i), (int16_t)(100 + i));   /* right */
    }
    task_cmd(0x08000000u | 0, ((uint32_t)(0x600 - DMEM_BASE) << 16) | 32);
    task_cmd(0x0d000000u, ((uint32_t)(0x600 - DMEM_BASE) << 16) | (0x640 - DMEM_BASE));
    alist_process_audio(&s_task);
    for (i = 0; i < 8; ++i) {
        if (get16((uint16_t)(0x600 + 4*i)) != (int16_t)i
         || get16((uint16_t)(0x602 + 4*i)) != (int16_t)(100 + i))
            ok = 0;
        /* the second row of left samples was overwritten by the first
         * block's stores before it was loaded */
        if (get16((uint16_t)(0x620 + 4*i)) != left_hi[i]
         || get16((uint16_t)(0x622 + 4*i)) != (int16_t)(108 + i))
            ok = 0;
    }
    CHECK(ok, "ABI1 INTERLEAVE: eight samples a side are loaded before the store");
}

static void test_resample_zero(void)
{
    unsigned i;
    int ok = 1;

    task_setup();
    for (i = 0; i < 16; ++i)
        put16((uint16_t)(0x7c0 + 2*i), 0x5555);
    task_cmd(0x08000000u | (0x6c0 - DMEM_BASE),
             ((uint32_t)(0x7c0 - DMEM_BASE) << 16) | 0);       /* count 0 */
    task_cmd(0x05000000u | ((uint32_t)A_INIT << 16) | 0x8000, 0x3000);
    alist_process_audio(&s_task);
    for (i = 0; i < 8; ++i)
        if (get16((uint16_t)(0x7c0 + 2*i)) != 0)
            ok = 0;
    CHECK(ok, "ABI1 RESAMPLE: a zero count still writes one block");
    CHECK(get16(0x7d0) == 0x5555, "ABI1 RESAMPLE: and only one block");
}

/* Pitch 0 resampled over its own window: every lane interpolates the
 * same four taps, and the output starts on the first tap, so a lane
 * stored early would feed the next lane of the same vector. */
static void test_resample_in_place(void)
{
    unsigned i;
    int ok = 1;

    task_setup();
    for (i = 0; i < 4; ++i)
        *dram_u16(&s_task, 0x3000 + 2*i) = 10000;          /* saved window */
    task_cmd(0x08000000u | (0x6c8 - DMEM_BASE),
             ((uint32_t)(0x6c0 - DMEM_BASE) << 16) | 16);
    task_cmd(0x05000000u | 0, 0x3000);                     /* pitch 0, resume */
    alist_process_audio(&s_task);
    for (i = 1; i < 8; ++i)
        if (get16((uint16_t)(0x6c0 + 2*i)) != get16(0x6c0))
            ok = 0;
    CHECK(ok && get16(0x6c0) > 9000,
          "ABI1 RESAMPLE: all taps of a block are read before its store");
}

int main(void)
{
    test_mixer();
    test_dmemmove();
    test_interleave();
    test_resample_zero();
    test_resample_in_place();

    if (s_failures) {
        printf("test_alist_abi1: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_alist_abi1: all checks passed\n");
    return 0;
}
