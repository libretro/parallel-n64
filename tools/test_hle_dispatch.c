/* test_hle_dispatch -- HLE task dispatch follows what the RSP would run.
 *
 *  - a program started without an OSTask (the CIC x105 boot code) is
 *    detected from IMEM even when the stale DMEM header matches a cached
 *    task, and it is not cached;
 *  - a yielded relaunch of the Rogue Squadron and Naboo graphics tasks
 *    clears SIG1 like a fresh launch, so osSpTaskYielded does not read
 *    the completion as another yield;
 *  - so does a yielded relaunch of a display list handed to the graphics
 *    plugin, and ZSortp's resident initialisation clears SIG1 and sets
 *    SIG4 as its SP_STATUS write does.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/hle.c"

#include <stdio.h>
#include <string.h>

/* ---- the rest of the plugin, reduced to what hle.c reaches ------------- */

static int s_cic_runs;
static int s_rs_result;

#define STUB_TASK(name) void name(struct hle_t* hle) { (void)hle; }
void cicx105_ucode(struct hle_t* hle) { (void)hle; s_cic_runs++; }
int angrylion_rs_dlist(int resume) { (void)resume; return s_rs_result; }
int angrylion_naboo_dlist(int resume, int emit) { (void)resume; (void)emit; return s_rs_result; }
int angrylion_streaming_dlist(int resume) { (void)resume; return 0; }
int angrylion_zboss_dlist(int resume, unsigned int *sp_status) { (void)resume; (void)sp_status; return 0; }
void rs_set_fog_block(unsigned int addr) { (void)addr; }
void HleCheckInterrupts(void* user_defined) { (void)user_defined; }
int HleForwardTask(void* user_defined) { (void)user_defined; return -1; }
void HleProcessAlistList(void* user_defined) { (void)user_defined; }
void HleProcessDlistList(void* user_defined) { (void)user_defined; }
void HleWarnMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void HleVerboseMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
STUB_TASK(alist_process_audio) STUB_TASK(alist_process_audio_bc)
STUB_TASK(alist_process_audio_ge) STUB_TASK(alist_process_naudio)
STUB_TASK(alist_process_naudio_bk) STUB_TASK(alist_process_naudio_cbfd)
STUB_TASK(alist_process_naudio_dk) STUB_TASK(alist_process_naudio_mp3)
STUB_TASK(alist_process_nead_1080) STUB_TASK(alist_process_nead_ac)
STUB_TASK(alist_process_nead_efz) STUB_TASK(alist_process_nead_fz)
STUB_TASK(alist_process_nead_mats) STUB_TASK(alist_process_nead_mk)
STUB_TASK(alist_process_nead_mm) STUB_TASK(alist_process_nead_mmb)
STUB_TASK(alist_process_nead_oot) STUB_TASK(alist_process_nead_sf)
STUB_TASK(alist_process_nead_sfj) STUB_TASK(alist_process_nead_wrjb)
STUB_TASK(alist_process_nead_ys) STUB_TASK(decode_video_frame_task)
STUB_TASK(fill_video_double_buffer_task) STUB_TASK(hvqm2_decode_sp1_task)
STUB_TASK(hvqm2_decode_sp2_task) STUB_TASK(jpeg_decode_OB)
STUB_TASK(jpeg_decode_PS) STUB_TASK(jpeg_decode_PS0)
STUB_TASK(musyx_v1_task) STUB_TASK(musyx_v2_task)
STUB_TASK(resize_bilinear_task)

/* ---- harness ----------------------------------------------------------- */

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static unsigned char s_dram[0x10000];
static unsigned char s_dmem[0x1000];
static unsigned char s_imem[0x1000];
static unsigned int s_regs[32];
static struct hle_t s_hle;
static int s_trap_runs;

static void trap(struct hle_t* hle) { (void)hle; s_trap_runs++; }

static void setup(void)
{
    memset(&s_hle, 0, sizeof(s_hle));
    memset(s_dmem, 0, sizeof(s_dmem));
    memset(s_imem, 0, sizeof(s_imem));
    memset(s_regs, 0, sizeof(s_regs));
    hle_init(&s_hle, s_dram, s_dmem, s_imem,
             &s_regs[0], &s_regs[1], &s_regs[2], &s_regs[3], &s_regs[4],
             &s_regs[5], &s_regs[6], &s_regs[7], &s_regs[8], &s_regs[9],
             &s_regs[10], &s_regs[11], &s_regs[12], &s_regs[13], &s_regs[14],
             &s_regs[15], &s_regs[16], &s_regs[17], NULL);
}

static void test_non_task(void)
{
    struct ucode_info_t* info;

    setup();
    /* a ucode boot size past IMEM: no OSTask header */
    *dmem_u32(&s_hle, TASK_UCODE_BOOT_SIZE) = 0x2000;
    *dmem_u32(&s_hle, TASK_UCODE)           = 0x1000;
    *dmem_u32(&s_hle, TASK_UCODE_DATA)      = 0x2000;
    *dmem_u32(&s_hle, TASK_UCODE_DATA_SIZE) = 0x800;
    /* first 44 IMEM bytes summing to the CIC x105 signature */
    memset(s_imem, 0x39, 43);
    s_imem[43] = (unsigned char)(0x9e2 - 0x39 * 43);

    info = &s_hle.cached_ucodes.infos[0];
    info->uc_start  = 0x1000;
    info->uc_dstart = 0x2000;
    info->uc_dsize  = 0x800;
    info->uc_pfunc  = trap;
    s_hle.cached_ucodes.count = 1;

    s_cic_runs = s_trap_runs = 0;
    hle_execute(&s_hle);
    CHECK(s_cic_runs == 1 && s_trap_runs == 0,
          "non-task: detected from IMEM, not from a stale cached header");
    CHECK(s_hle.cached_ucodes.count == 1, "non-task: not added to the task cache");
}

static void test_yield_relaunch(void)
{
    setup();
    s_rs_result = 1;                        /* walk suspended */
    *s_hle.sp_status = 0;
    rs_gfx_task(&s_hle);                    /* fresh launch, still running */
    *s_hle.sp_status |= SP_STATUS_SIG1;     /* yield acknowledged earlier */
    *dmem_u32(&s_hle, TASK_FLAGS) = 1;      /* OS_TASK_YIELDED relaunch */
    s_rs_result = 0;
    rs_gfx_task(&s_hle);
    CHECK(!(*s_hle.sp_status & SP_STATUS_SIG1),
          "Rogue Squadron: a yielded relaunch clears SIG1");

    setup();
    s_rs_result = 1;
    *s_hle.sp_status = 0;
    naboo_gfx_task(&s_hle);
    *s_hle.sp_status |= SP_STATUS_SIG1;
    *dmem_u32(&s_hle, TASK_FLAGS) = 1;
    s_rs_result = 0;
    naboo_gfx_task(&s_hle);
    CHECK(!(*s_hle.sp_status & SP_STATUS_SIG1),
          "Naboo: a yielded relaunch clears SIG1");
}

static void test_dlist_signals(void)
{
    setup();
    *dmem_u32(&s_hle, TASK_UCODE) = 0x2000;
    *s_hle.sp_status = SP_STATUS_SIG1;
    *dmem_u32(&s_hle, TASK_FLAGS) = 0;
    send_dlist_to_gfx_plugin(&s_hle);
    CHECK(*s_hle.sp_status & SP_STATUS_SIG1, "dlist: a fresh launch leaves SIG1 alone");

    setup();
    *dmem_u32(&s_hle, TASK_UCODE) = 0x2000;
    *s_hle.sp_status = SP_STATUS_SIG1;
    *dmem_u32(&s_hle, TASK_FLAGS) = 1;      /* OS_TASK_YIELDED relaunch */
    send_dlist_to_gfx_plugin(&s_hle);
    CHECK(!(*s_hle.sp_status & SP_STATUS_SIG1), "dlist: a yielded relaunch clears SIG1");

    setup();
    *dmem_u32(&s_hle, TASK_UCODE) = 0x2000;
    *(uint32_t*)(s_dram + 0x2010) = 0x3c0b0004u;   /* lui   t3, 0x0004   */
    *(uint32_t*)(s_dram + 0x2014) = 0x356b2800u;   /* ori   t3, 0x2800   */
    *(uint32_t*)(s_dram + 0x2018) = 0x408b2000u;   /* mtc0  t3, SP_STATUS */
    *s_hle.sp_status = SP_STATUS_SIG1;
    *dmem_u32(&s_hle, TASK_FLAGS) = 0;
    send_dlist_to_gfx_plugin(&s_hle);
    CHECK(!(*s_hle.sp_status & SP_STATUS_SIG1) && (*s_hle.sp_status & SP_STATUS_SIG4),
          "dlist: ZSortp's resident init clears SIG1 and sets SIG4");
}

int main(void)
{
    test_non_task();
    test_yield_relaunch();
    test_dlist_signals();

    if (s_failures) {
        printf("test_hle_dispatch: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_hle_dispatch: all checks passed\n");
    return 0;
}
