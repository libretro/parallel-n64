/* Angrylion rasterizer benchmark: renders a frame of shade rectangles
 * through n64video_process_list() and reports ms/frame.
 *
 * Built by `make tools` from the tree's own angrylion objects (the
 * target is in the top-level Makefile).
 *
 * Usage: angrylion_bench WORKERS FRAMES TRI_H TRI_W COUNT FLIP TRIS_PER_FLUSH
 * On a single core the wall time is the summed work of all lanes, which
 * is what the per-lane replicated setup shows up in; on a real machine
 * it is the wall time the emulator sees.
 */
#include "n64video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <features/features_cpu.h>

#define RDRAM_SIZE (8u << 20)
#define FB_ADDR    0x100000u
#define CMD_ADDR   0x200000u
#define FB_W 320
#define FB_H 240

static uint8_t *rdram;
static uint8_t dmem[4096];
static uint32_t regs[8];
static uint32_t *dp_reg[8];
static uint32_t *vi_reg[16];
static uint32_t vi_regs[16];
static uint32_t mi_intr;
static void intr_cb(void) {}

static uint32_t *cmd;
static uint32_t ncmd;

static void emit(uint32_t w) { cmd[ncmd++] = w; }

/* rectangle as a shade triangle: left edge x0, right edge x0+w, rows
 * y0..y0+h (in whole lines) */
static void emit_rect(int x0, int y0, int w, int h, int flip)
{
    int yh = y0 << 2, yl = (y0 + h) << 2;
    int32_t xa = x0 << 16, xb = (x0 + w) << 16;
    int32_t xl, xh, xm;
    if (flip) { xh = xa; xm = xb; xl = xb; }
    else      { xh = xb; xm = xa; xl = xa; }
    emit((0x0cu << 24) | ((uint32_t)flip << 23) | (uint32_t)yl);
    emit(((uint32_t)yh << 16) | (uint32_t)yh);
    emit((uint32_t)xl); emit(0);
    emit((uint32_t)xh); emit(0);
    emit((uint32_t)xm); emit(0);
    /* shade: R,G,B,A = 200,100,50,255, no gradients */
    emit((200u << 16) | 100u); emit((50u << 16) | 255u);
    emit(0); emit(0);      /* dx int */
    emit(0); emit(0);      /* frac */
    emit(0); emit(0);      /* dx frac */
    emit(0); emit(0);      /* de int */
    emit(0); emit(0);      /* dy int */
    emit(0); emit(0);      /* de frac */
    emit(0); emit(0);      /* dy frac */
}

static int batch = 0; /* triangles per flush; 0 = one batch per frame */
static void build_frame(int tri_w, int tri_h, int count, int flip)
{
    int i, x = 0, y = 0;
    ncmd = 0;
    /* SET_COLOR_IMAGE 16-bit RGBA, width 320 */
    emit((0x3fu << 24) | (0u << 21) | (2u << 19) | (FB_W - 1)); emit(FB_ADDR);
    /* SET_SCISSOR full frame */
    emit((0x2du << 24) | (0u << 12) | 0u); emit(((uint32_t)(FB_W << 2) << 12) | (uint32_t)(FB_H << 2));
    /* SET_OTHER_MODES: 1-cycle */
    emit(0x2fu << 24); emit(0);
    /* SET_COMBINE: rgb = shade, alpha = 1 */
    emit(0x3cu << 24); emit((4u << 15) | (6u << 9) | (4u << 6) | (6u << 0));
    for (i = 0; i < count; i++)
    {
        emit_rect(x, y, tri_w, tri_h, flip);
        /* SET_TEXTURE_IMAGE is a sync point under HIGH compat */
        if (batch && (i % batch) == batch - 1) { emit((0x3du << 24) | (2u << 19) | 31u); emit(0x300000u); }
        x += tri_w;
        if (x + tri_w > FB_W) { x = 0; y += tri_h; if (y + tri_h > FB_H) y = 0; }
    }
    emit(0x29u << 24); emit(0); /* SYNC_FULL */
    memcpy(rdram + CMD_ADDR, cmd, ncmd * 4);
}

static void run_frame(void)
{
    regs[DP_STATUS]  = 0;
    regs[DP_START]   = CMD_ADDR;
    regs[DP_CURRENT] = CMD_ADDR;
    regs[DP_END]     = CMD_ADDR + ncmd * 4;
    n64video_process_list();
}

static unsigned count_written(void)
{
    unsigned i, n = 0;
    for (i = 0; i < FB_W * FB_H * 2; i++)
        if (rdram[FB_ADDR + i]) n++;
    return n;
}

int main(int argc, char **argv)
{
    int workers = argc > 1 ? atoi(argv[1]) : 1;
    int frames  = argc > 2 ? atoi(argv[2]) : 100;
    int tri_h   = argc > 3 ? atoi(argv[3]) : 16;
    int tri_w   = argc > 4 ? atoi(argv[4]) : 32;
    int count   = argc > 5 ? atoi(argv[5]) : 600;
    int flip    = argc > 6 ? atoi(argv[6]) : 0;
    batch       = argc > 7 ? atoi(argv[7]) : 0;
    struct n64video_config cfg;
    retro_time_t t0, t1;
    int f, i;
    double ms;

    rdram = calloc(RDRAM_SIZE, 1);
    cmd = calloc(1 << 20, 4);
    for (i = 0; i < 8; i++) dp_reg[i] = &regs[i];
    for (i = 0; i < 16; i++) vi_reg[i] = &vi_regs[i];

    n64video_config_init(&cfg);
    cfg.gfx.rdram = rdram; cfg.gfx.rdram_size = RDRAM_SIZE; cfg.gfx.dmem = dmem;
    cfg.gfx.vi_reg = vi_reg; cfg.gfx.dp_reg = dp_reg;
    cfg.gfx.mi_intr_reg = &mi_intr; cfg.gfx.mi_intr_cb = intr_cb;
    cfg.parallel = true; cfg.num_workers = (uint32_t)workers;
    cfg.dp.compat = DP_COMPAT_HIGH;
    n64video_init(&cfg);

    build_frame(tri_w, tri_h, count, flip);
    memset(rdram + FB_ADDR, 0, FB_W * FB_H * 2);
    run_frame();
    printf("workers=%d pixels written: %u of %u\n", workers, count_written() / 2, FB_W * FB_H);

    t0 = cpu_features_get_time_usec();
    for (f = 0; f < frames; f++)
        run_frame();
    t1 = cpu_features_get_time_usec();
    ms = (double)(t1 - t0) / 1e3 / frames;
    printf("workers=%d tri=%dx%d count=%d batches/frame=%d : %.3f ms/frame\n", workers, tri_w, tri_h, count, batch ? count / batch : 1, ms);
    n64video_close();
    return 0;
}

/* stubs for the plugin-side symbols n64video.c expects */
#include <stdarg.h>
void msg_error(const char *err, ...) { va_list ap; va_start(ap, err); vfprintf(stderr, err, ap); va_end(ap); fputc('\n', stderr); exit(1); }
void msg_warning(const char *err, ...) { (void)err; }
void msg_debug(const char *err, ...) { (void)err; }
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb; (void)w; (void)h; (void)p; (void)o; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
