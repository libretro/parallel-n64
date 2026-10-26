/* test_alist_ops -- audio list arithmetic and buffer commands match the
 * microcode.
 *
 *  - the ENVMIXER VMADN low clamp saturates on the whole of ACC[47:16],
 *    including values whose low half alone looks in range;
 *  - NEAD CLEARBUFF clears whole 16-byte blocks and takes a 16-bit count;
 *  - the NEAD commands recovered from the cartridge microcode are
 *    dispatched, not dropped: MULTIPLY saturates each product and reads
 *    a whole block before storing, INDEXED keeps an exact multiple of
 *    the period, and the pipelined S8DEC prefetches its next source row
 *    before each store while the plain one does not.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/alist.c"
#include "../mupen64plus-rsp-hle/src/alist_nead.c"

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

static void test_clamp_low(void)
{
    CHECK(ev_clamp_al(((int64_t)0x17fff << 16) | 0x1234) == (int16_t)0xffff,
          "VMADN: ACC[47:16] = 0x17fff saturates high");
    CHECK(ev_clamp_al(-((int64_t)0x18000 * 65536) + 0x1234) == 0,
          "VMADN: ACC[47:16] = -0x18000 saturates low");
    CHECK(ev_clamp_al(((int64_t)0x8000 << 16) | 0x1234) == (int16_t)0xffff,
          "VMADN: ACC[47:16] just past +32767 saturates high");
    CHECK(ev_clamp_al(-65536 + 0x1234) == 0x1234,
          "VMADN: an in-range ACC[47:16] passes the low half through");
}

static void test_nead_clearbuff(void)
{
    static struct hle_t hle;
    unsigned i;
    int ok = 1;

    memset(hle.alist_buffer, 0xaa, sizeof(hle.alist_buffer));
    CLEARBUFF(&hle, 0x100, 0x18);
    for (i = 0x100; i < 0x120; i++)
        if (*alist_u8(&hle, (uint16_t)i) != 0)
            ok = 0;
    CHECK(ok && *alist_u8(&hle, 0x120) == 0xaa && *alist_u8(&hle, 0xff) == 0xaa,
          "NEAD CLEARBUFF: a partial block clears sixteen bytes");

    memset(hle.alist_buffer, 0xaa, sizeof(hle.alist_buffer));
    CLEARBUFF(&hle, 0x000, 0x1000);
    ok = 1;
    for (i = 0; i < 0x1000; i++)
        if (*alist_u8(&hle, (uint16_t)i) != 0)
            ok = 0;
    CHECK(ok, "NEAD CLEARBUFF: the count is sixteen bits wide");
}

/* ---- NEAD task harness: one alist in DRAM, run through a ucode table -- */

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

static int16_t dmem16(uint16_t at)
{
    return *alist_s16(&s_task, at);
}

static void test_nead_multiply(void)
{
    unsigned i;
    int ok = 1;

    task_setup();
    for (i = 0; i < 32; ++i) {
        *alist_s16(&s_task, (uint16_t)(0x600 + 2*i)) = (int16_t)(i == 0 ? 3 : 2);
        *alist_s16(&s_task, (uint16_t)(0x400 + 2*i)) = (int16_t)(100 * i);
        *alist_s16(&s_task, (uint16_t)(0x440 + 2*i)) = (int16_t)(-7 * i);
    }
    *alist_s16(&s_task, 0x400) = 20000;
    *alist_s16(&s_task, 0x402) = -20000;
    task_cmd(0x00000000u | 64, (0x400u << 16) | 0x600);   /* MULTIPLY */
    alist_process_nead_mm(&s_task);

    CHECK(dmem16(0x400) == 32767, "NEAD MULTIPLY: a positive product saturates");
    CHECK(dmem16(0x402) == -32768, "NEAD MULTIPLY: a negative product saturates");
    for (i = 2; i < 32; ++i)
        if (dmem16((uint16_t)(0x400 + 2*i)) != (int16_t)(200 * i))
            ok = 0;
    CHECK(ok, "NEAD MULTIPLY: each lane takes its own coefficient");
    CHECK(dmem16(0x440) == 0 && dmem16(0x442) == -7,
          "NEAD MULTIPLY: a 64-byte count stops after one block");

    /* a 128-byte run over the coefficient table itself: the coefficients
     * are latched once, so the second block still sees the old ones */
    task_setup();
    for (i = 0; i < 64; ++i)
        *alist_s16(&s_task, (uint16_t)(0x400 + 2*i)) = 2;
    task_cmd(0x00000000u | 128, (0x400u << 16) | 0x400);
    alist_process_nead_mm(&s_task);
    CHECK(dmem16(0x400) == 4 && dmem16(0x43e) == 4 && dmem16(0x440) == 4,
          "NEAD MULTIPLY: coefficients are latched before the first store");
}

static void test_nead_indexed(void)
{
    task_setup();
    /* the index list sits at the start of the sample buffer */
    *alist_s16(&s_task, 0x400) = 6;              /* 6 against period 3: stays 3 */
    *alist_s16(&s_task, 0x402) = 2;
    *alist_s16(&s_task, 0x404) = 0x7000;
    *alist_s16(&s_task, 0x406) = (int16_t)0x9001;
    task_cmd(0x00000000u | (3u << 16) | 1, 0x400);    /* INDEXED, no swaps */
    alist_process_nead_ac(&s_task);

    CHECK((uint16_t)dmem16(0x406) == 0x0001 && (uint16_t)dmem16(0x404) == 0x0001,
          "NEAD INDEXED: an exact multiple of the period keeps the divisor, sums wrap");
}

static void s8_fill(void)
{
    unsigned k;
    task_setup();
    for (k = 0; k < 32; ++k)
        *alist_u8(&s_task, (uint16_t)(0x220 + k)) = (uint8_t)(0x11 + k);
    for (k = 0x240; k < 0x260; ++k)
        *alist_u8(&s_task, (uint16_t)k) = 0xee;
    task_cmd(0x08000000u | 0x220, (0x200u << 16) | 64);   /* SETBUFF */
    task_cmd(0x17000000u | 0x10000u, 0x2000);             /* S8DEC, fresh */
}

static void test_nead_s8dec(void)
{
    unsigned k;
    int ok = 1;

    /* input two rows ahead of the output: the pipelined decoder has the
     * second source row in registers before its first store lands */
    s8_fill();
    alist_process_nead_1080(&s_task);
    for (k = 0; k < 16; ++k)
        if (*alist_u8(&s_task, (uint16_t)(0x240 + 2*k)) != 0x11 + 16 + k
         || *alist_u8(&s_task, (uint16_t)(0x241 + 2*k)) != 0)
            ok = 0;
    CHECK(ok, "NEAD S8DEC (pipelined): overlapping input is read before the store");
    CHECK(*alist_u8(&s_task, 0x220) == 0x11 && *alist_u8(&s_task, 0x200) == 0,
          "NEAD S8DEC (pipelined): state row cleared, first row expanded");
    CHECK(*dram_u8(&s_task, 0x2000) == 0x11 + 16 && *dram_u8(&s_task, 0x2002) == 0x11 + 17,
          "NEAD S8DEC (pipelined): the last row is saved as the state");

    /* the plain build loads each source row after the previous store */
    s8_fill();
    alist_process_nead_sfj(&s_task);
    ok = 1;
    for (k = 0; k < 32; ++k)
        if (*alist_u8(&s_task, (uint16_t)(0x240 + k)) != ((k & 3) ? 0 : 0x11 + 8 + k / 4))
            ok = 0;
    CHECK(ok, "NEAD S8DEC (plain): overlapping input is read after the store");
}

int main(void)
{
    test_clamp_low();
    test_nead_clearbuff();
    test_nead_multiply();
    test_nead_indexed();
    test_nead_s8dec();

    if (s_failures) {
        printf("test_alist_ops: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_alist_ops: all checks passed\n");
    return 0;
}
