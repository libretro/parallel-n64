/* test_re2_fill -- the Resident Evil 2 video double-buffer task averages
 * every byte of the two frames, alpha included.
 *
 * The microcode loads and stores whole pixel pairs (LUV/SUV), so the
 * alpha byte is averaged like red, green and blue; the HLE version used
 * to write it as zero.
 *
 * It signals the way the microcode does: the final SP_STATUS write
 * clears SIG0 and sets SIG1 with task done, and a task started with SIG0
 * already set (a fill the CPU pre-empted) completes without touching
 * pixels.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/re2.c"

#include <stdio.h>

void HleVerboseMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void HleWarnMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
static unsigned int s_break_bits;
static int s_breaks;

void rsp_break(struct hle_t* hle, unsigned int setbits)
{
    *hle->sp_status |= setbits;
    s_break_bits = setbits;
    s_breaks++;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

int main(void)
{
    static struct hle_t hle;
    static unsigned int sp_status;
    unsigned i;

    hle.dram = (unsigned char*)calloc(1, 0x800000u);
    hle.dmem = (unsigned char*)calloc(1, 0x1000u);
    if (hle.dram == NULL || hle.dmem == NULL)
        return 2;
    hle.sp_status = &sp_status;

    *dmem_u32(&hle, TASK_UCODE_DATA) = 0x1000;
    *dram_u32(&hle, 0x1000) = 0x2000;        /* source frame */
    *dram_u32(&hle, 0x1004) = 0x3000;        /* destination frame */
    *dram_u32(&hle, 0x1008) = 16;            /* 8 bytes a row: two pixels */
    *dram_u32(&hle, 0x1010) = 1;             /* two rows */
    *dram_u32(&hle, 0x101c) = 0x80;          /* 0x40 bytes between rows */
    *dram_u32(&hle, 0x1028) = 0x80000000u;

    for (i = 0; i < 2; i++) {
        *dram_u32(&hle, 0x2000 + i * 4) = 0x10305070u;
        *dram_u32(&hle, 0x3000 + i * 4) = 0x30507090u;
        *dram_u32(&hle, 0x2040 + i * 4) = 0xff0000ffu;
        *dram_u32(&hle, 0x3040 + i * 4) = 0x01000001u;
    }

    sp_status = 0;
    fill_video_double_buffer_task(&hle);

    CHECK(*dram_u32(&hle, 0x3000) == 0x20406080u && *dram_u32(&hle, 0x3004) == 0x20406080u,
          "RE2 fill: the first row averages colour and alpha");
    CHECK(*dram_u32(&hle, 0x3040) == 0x80000080u && *dram_u32(&hle, 0x3044) == 0x80000080u,
          "RE2 fill: the second row averages colour and alpha");
    CHECK(*dram_u32(&hle, 0x3008) == 0, "RE2 fill: stops at the row width");
    CHECK(s_breaks == 1 && s_break_bits == (SP_STATUS_SIG1 | SP_STATUS_TASKDONE),
          "RE2 fill: completion sets SIG1 with task done");

    /* pre-empted: SIG0 already set when the task starts */
    *dram_u32(&hle, 0x3000) = 0x12345678u;
    sp_status = SP_STATUS_SIG0;
    s_breaks = 0;
    fill_video_double_buffer_task(&hle);
    CHECK(*dram_u32(&hle, 0x3000) == 0x12345678u,
          "RE2 fill: a pre-empted task leaves the frame alone");
    CHECK(s_breaks == 1 && !(sp_status & SP_STATUS_SIG0)
       && (sp_status & SP_STATUS_SIG1) && (sp_status & SP_STATUS_TASKDONE),
          "RE2 fill: a pre-empted task clears SIG0 and sets SIG1");

    free(hle.dram);
    free(hle.dmem);
    if (s_failures) {
        printf("test_re2_fill: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_re2_fill: all checks passed\n");
    return 0;
}
