/* angrylion_noise_frame_test -- RDP noise in a frame does not depend on
 * how many primitives earlier frames drew.
 *
 * The noise seed mixes the frame counter (bumped by SYNC_FULL), the
 * index of the primitive and the scanline. The primitive index used to
 * run for the whole session, so an earlier frame with one more or one
 * fewer primitive (an HLE display list against the LLE microcode, for
 * example) shifted the noise of every later frame by one step.
 *
 * The test draws two sessions that differ only in the number of
 * primitives in the first frame, then draws the same noise rectangle in
 * the second frame of each and compares the pixels. With the counter
 * restarting at every SYNC_FULL the two rectangles match.
 *
 * It also checks that the noise phase a savestate carries
 * (n64video_noise_save/load) puts the noise back where it was: a
 * rectangle drawn after a restore matches the one drawn without the
 * interruption.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "n64video.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RDRAM_SIZE (8u << 20)
#define CMD_ADDR   0x200000u
#define FB_ADDR    0x100000u
#define FB_WIDTH   64u
#define FB_HEIGHT  16u
#define FB_BYTES   (FB_WIDTH * FB_HEIGHT * 2u)

extern uint32_t rdp_noise_field;
void n64video_noise_save(uint32_t* out);
void n64video_noise_load(const uint32_t* in);

static uint8_t *rdram;
static uint8_t dmem[4096];
static uint32_t regs[8];
static uint32_t *dp_reg[8];
static uint32_t *vi_reg[16];
static uint32_t vi_regs[16];
static uint32_t mi_intr;
static void intr_cb(void) {}
static uint32_t ncmd;

static void emit(uint32_t w0, uint32_t w1)
{
    uint32_t *cmd = (uint32_t*)(rdram + CMD_ADDR);
    cmd[ncmd++] = w0;
    cmd[ncmd++] = w1;
}

/* Texture rectangle from (x0,y0) to (x1,y1), whole pixels. */
static void tex_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    emit(0x24000000u | ((x1 << 2) << 12) | (y1 << 2),
         ((x0 << 2) << 12) | (y0 << 2));
    emit(0, (1024u << 16) | 1024u);
}

static void run_session(unsigned history_prims, uint8_t *out)
{
    unsigned i;

    ncmd = 0;
    memset(rdram + FB_ADDR, 0, FB_BYTES);

    emit(0x3f100000u | (FB_WIDTH - 1), FB_ADDR);          /* RGBA16 image */
    emit(0x2d000000u, ((FB_WIDTH << 2) << 12) | (FB_HEIGHT << 2));
    emit(0x2f0000f0u, 0);              /* 1-cycle, no dither, no blend */
    /* (noise - 0) * prim + 0 for colour, zero alpha. */
    emit(0x3c000000u | (7u << 20) | (3u << 15) | (7u << 12) | (7u << 9)
         | (7u << 5) | 3u,
         (8u << 28) | (8u << 24) | (7u << 21) | (7u << 18) | (7u << 15)
         | (7u << 12) | (7u << 9) | (7u << 6) | (7u << 3) | 7u);
    emit(0x3a000000u, 0xffffffffu);

    /* Frame 1: a varying number of primitives in the lower half. */
    for (i = 0; i < history_prims; i++)
        tex_rect(i * 4u, 8u, i * 4u + 4u, 16u);
    emit(0x27000000u, 0);
    emit(0x29000000u, 0);

    /* Frame 2: the same noise rectangle in the upper half. */
    tex_rect(0, 0, FB_WIDTH, 8u);
    emit(0x27000000u, 0);
    emit(0x29000000u, 0);

    rdp_noise_field = 0;
    regs[DP_STATUS] = 0;
    regs[DP_START] = regs[DP_CURRENT] = CMD_ADDR;
    regs[DP_END] = CMD_ADDR + ncmd * 4u;
    n64video_process_list();

    memcpy(out, rdram + FB_ADDR, FB_WIDTH * 8u * 2u);
}

static void run_list(void)
{
    regs[DP_STATUS] = 0;
    regs[DP_START] = regs[DP_CURRENT] = CMD_ADDR;
    regs[DP_END] = CMD_ADDR + ncmd * 4u;
    n64video_process_list();
    ncmd = 0;
}

static void setup_noise(void)
{
    emit(0x3f100000u | (FB_WIDTH - 1), FB_ADDR);
    emit(0x2d000000u, ((FB_WIDTH << 2) << 12) | (FB_HEIGHT << 2));
    emit(0x2f0000f0u, 0);
    emit(0x3c000000u | (7u << 20) | (3u << 15) | (7u << 12) | (7u << 9)
         | (7u << 5) | 3u,
         (8u << 28) | (8u << 24) | (7u << 21) | (7u << 18) | (7u << 15)
         | (7u << 12) | (7u << 9) | (7u << 6) | (7u << 3) | 7u);
    emit(0x3a000000u, 0xffffffffu);
}

static int test_noise_savestate(void)
{
    static uint8_t want[FB_WIDTH * 8u * 2u];
    uint32_t saved[2];
    uint8_t* lower = rdram + FB_ADDR + FB_WIDTH * 8u * 2u;

    /* a frame, then the state is saved between frames as the frontend
     * does (SYNC_FULL also flushes the workers) */
    ncmd = 0;
    rdp_noise_field = 5;
    setup_noise();
    tex_rect(0, 0, FB_WIDTH, 8u);
    emit(0x29000000u, 0);
    run_list();
    n64video_noise_save(saved);

    memset(lower, 0, sizeof(want));
    tex_rect(0, 8u, FB_WIDTH, 16u);
    emit(0x29000000u, 0);
    run_list();
    memcpy(want, lower, sizeof(want));

    /* move the phase on by a few frames */
    tex_rect(0, 0, 8u, 8u);
    emit(0x29000000u, 0);
    tex_rect(8u, 0, 16u, 8u);
    emit(0x29000000u, 0);
    run_list();

    n64video_noise_load(saved);
    memset(lower, 0, sizeof(want));
    tex_rect(0, 8u, FB_WIDTH, 16u);
    emit(0x29000000u, 0);
    run_list();
    if (memcmp(want, lower, sizeof(want)) != 0) {
        printf("FAIL: noise after a savestate restore differs\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    struct n64video_config cfg;
    unsigned i;
    unsigned distinct = 0;
    int failures = 0;
    static uint8_t a[FB_WIDTH * 8u * 2u];
    static uint8_t b[FB_WIDTH * 8u * 2u];

    rdram = calloc(RDRAM_SIZE, 1);
    for (i = 0; i < 8; i++) dp_reg[i] = &regs[i];
    for (i = 0; i < 16; i++) vi_reg[i] = &vi_regs[i];
    n64video_config_init(&cfg);
    cfg.gfx.rdram = rdram; cfg.gfx.rdram_size = RDRAM_SIZE; cfg.gfx.dmem = dmem;
    cfg.gfx.vi_reg = vi_reg; cfg.gfx.dp_reg = dp_reg;
    cfg.gfx.mi_intr_reg = &mi_intr; cfg.gfx.mi_intr_cb = intr_cb;
    n64video_init(&cfg);

    run_session(1, a);
    run_session(3, b);

    /* The rectangle must actually carry noise, or the comparison
     * proves nothing. */
    for (i = 2; i < sizeof(a); i += 2)
        if (a[i] != a[0] || a[i + 1] != a[1])
            distinct++;
    if (distinct == 0) {
        printf("FAIL: noise rectangle is flat\n");
        failures++;
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
        unsigned diff = 0;
        for (i = 0; i < sizeof(a); i += 2)
            if (a[i] != b[i] || a[i + 1] != b[i + 1])
                diff++;
        printf("FAIL: frame-2 noise differs in %u of %u pixels when frame 1 "
               "drew a different number of primitives\n",
               diff, (unsigned)(sizeof(a) / 2));
        failures++;
    }
    failures += test_noise_savestate();
    n64video_close();
    free(rdram);
    if (failures)
        return 1;
    printf("angrylion_noise_frame_test: all checks passed\n");
    return 0;
}

void msg_error(const char *err, ...) { va_list ap; va_start(ap, err); vfprintf(stderr, err, ap); va_end(ap); fputc('\n', stderr); exit(1); }
void msg_warning(const char *err, ...) { (void)err; }
void msg_debug(const char *err, ...) { (void)err; }
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb; (void)w; (void)h; (void)p; (void)o; }
void *vdac_acquire(unsigned width, unsigned height, unsigned *pitch, int need_read) { (void)width; (void)height; (void)pitch; (void)need_read; return 0; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
