/* angrylion_hazard_wrap_test -- a texture load wrapping past 16 MiB onto
 * a framebuffer still being drawn waits for that drawing.
 *
 * With parallel workers at the high sync level, a texture load is held
 * back only when its source overlaps the RDRAM the buffered batch draws
 * into. The RDP's 24-bit pointer wraps, so a load starting just below
 * 16 MiB reads the bottom of RDRAM; compared as raw ranges it did not
 * overlap a framebuffer at address 0, and the load ran beside drawing
 * into its own source. Here the batch draws a rectangle into a colour
 * image at 0, loads a block from 0xffff00 that wraps onto it, and draws
 * again: the load must split the work into two batches.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "n64video.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define RDRAM_SIZE (8u << 20)
#define CMD_ADDR   0x200000u

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

static void fill_rect(void)
{
    emit(0x27000000u, 0);                                  /* sync pipe */
    emit(0x36000000u | (40u << 2 << 12) | (40u << 2),      /* fill rect */
         (8u << 2 << 12) | (8u << 2));
}

int main(void)
{
    struct n64video_config cfg;
    unsigned i;
    unsigned batches;

    rdram = calloc(RDRAM_SIZE, 1);
    for (i = 0; i < 8; i++) dp_reg[i] = &regs[i];
    for (i = 0; i < 16; i++) vi_reg[i] = &vi_regs[i];
    n64video_config_init(&cfg);
    cfg.gfx.rdram = rdram; cfg.gfx.rdram_size = RDRAM_SIZE; cfg.gfx.dmem = dmem;
    cfg.gfx.vi_reg = vi_reg; cfg.gfx.dp_reg = dp_reg;
    cfg.gfx.mi_intr_reg = &mi_intr; cfg.gfx.mi_intr_cb = intr_cb;
    cfg.parallel = true; cfg.num_workers = 2; cfg.dp.compat = DP_COMPAT_HIGH;
    n64video_init(&cfg);

    emit(0x3f000000u | (2u << 19) | (320u - 1u), 0x000000u); /* colour image at 0 */
    emit(0x2d000000u, (320u << 2 << 12) | (240u << 2));      /* scissor */
    emit(0x2f300000u, 0);                                    /* fill cycle */
    emit(0x37000000u, 0xf801f801u);                          /* fill colour */
    fill_rect();
    /* 1 KiB of 16-bit texels from 0xffff00: 256 bytes, then the bottom */
    emit(0x3d000000u | (2u << 19) | (512u - 1u), 0xffff00u);
    emit(0x35000000u | (2u << 19), 0x07000000u);             /* tile 7 */
    emit(0x26000000u, 0);                                    /* sync load */
    emit(0x33000000u, 0x07000000u | (0x1ffu << 12) | 0x800u); /* load block */
    fill_rect();
    emit(0x29000000u, 0);                                    /* sync full */

    regs[DP_STATUS] = 0;
    regs[DP_START] = regs[DP_CURRENT] = CMD_ADDR;
    regs[DP_END] = CMD_ADDR + ncmd * 4u;
    n64video_process_list();
    batches = n64video_flush_count();
    n64video_close();
    free(rdram);

    if (batches != 2) {
        printf("FAIL: a load wrapping onto the framebuffer ran in %u batch(es), expected 2\n",
               batches);
        return 1;
    }
    printf("angrylion_hazard_wrap_test: all checks passed\n");
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
