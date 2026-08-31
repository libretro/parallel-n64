/* Angrylion bit-exactness harness: renders randomized shade+Z (mode 0)
 * or 2-cycle textured (mode 1) triangles and hashes the colour and depth
 * buffers, for comparing two builds of the renderer at each lane count.
 *
 * Built by `make tools` from the tree's own angrylion objects (the
 * target is in the top-level Makefile).
 *
 * Usage: angrylion_verify WORKERS TRIANGLES SEEDS MODE
 */
#include "n64video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define RDRAM_SIZE (8u << 20)
#define FB_ADDR    0x100000u
#define ZB_ADDR    0x180000u
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

static uint32_t rng_state = 0x9e3779b9u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
static int rnd(int lo, int hi) { return lo + (int)(rng() % (uint32_t)(hi - lo + 1)); }

/* x on the segment (x0,y0)-(x1,y1) at y, in 16.16; y in 11.2 */
static int32_t edge_x(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t y)
{
    if (y1 == y0) return x0 << 14;
    return (int32_t)(((int64_t)x0 << 14) + ((int64_t)(x1 - x0) << 14) * (y - y0) / (y1 - y0));
}
static int32_t edge_slope(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (y1 == y0) return 0;
    return (int32_t)((((int64_t)(x1 - x0)) << 16) / (y1 - y0));
}

/* random triangle in 11.2 fixed point, possibly off-screen; a few are
 * deliberately mis-ordered so ym < yh / ym > yl paths run too */
static int texmode = 0;
static void emit_tri(int garbage)
{
    int32_t x[3], y[3], t;
    int i, j, flip;
    int32_t yh, ym, yl, xh, xm, xl, dxhdy, dxmdy, dxldy;
    for (i = 0; i < 3; i++) { x[i] = rnd(-40, 360) * 4 + rnd(0, 3); y[i] = rnd(-40, 280) * 4 + rnd(0, 3); }
    /* sort by y */
    for (i = 0; i < 2; i++) for (j = i + 1; j < 3; j++) if (y[j] < y[i]) { t = x[i]; x[i] = x[j]; x[j] = t; t = y[i]; y[i] = y[j]; y[j] = t; }
    yh = y[0]; ym = y[1]; yl = y[2];
    xh = edge_x(x[0], y[0], x[2], y[2], yh); dxhdy = edge_slope(x[0], y[0], x[2], y[2]);
    xm = edge_x(x[0], y[0], x[1], y[1], yh); dxmdy = edge_slope(x[0], y[0], x[1], y[1]);
    xl = edge_x(x[1], y[1], x[2], y[2], ym); dxldy = edge_slope(x[1], y[1], x[2], y[2]);
    /* major edge on the left -> flip */
    flip = edge_x(x[0], y[0], x[2], y[2], ym) < (x[1] << 14);
    if (garbage) { t = ym; ym = (rng() & 1) ? yh - rnd(1, 40) : yl + rnd(1, 40); (void)t; }
    emit(((texmode ? 0x0fu : 0x0du) << 24) | ((uint32_t)flip << 23) | ((uint32_t)yl & 0x3fff));
    emit((((uint32_t)ym & 0xffff) << 16) | ((uint32_t)yh & 0xffff));
    emit((uint32_t)xl & 0x0fffffff); emit((uint32_t)dxldy & 0x3fffffff);
    emit((uint32_t)xh & 0x0fffffff); emit((uint32_t)dxhdy & 0x3fffffff);
    emit((uint32_t)xm & 0x0fffffff); emit((uint32_t)dxmdy & 0x3fffffff);
    /* shade: random colours and gradients */
    for (i = 0; i < 16; i++) emit(rng());
    /* texture: random s/t/w and gradients */
    if (texmode) for (i = 0; i < 16; i++) emit(rng());
    /* z: random */
    for (i = 0; i < 4; i++) emit(rng());
}

static void build(int count, int seed)
{
    int i;
    rng_state = 0x9e3779b9u ^ (uint32_t)seed;
    ncmd = 0;
    emit((0x3fu << 24) | (0u << 21) | (2u << 19) | (FB_W - 1)); emit(FB_ADDR);
    emit(0x3eu << 24); emit(ZB_ADDR);
    emit((0x2du << 24) | (0u << 12) | 0u); emit(((uint32_t)(FB_W << 2) << 12) | (uint32_t)(FB_H << 2));
    if (texmode)
    {
        /* random 32x32 16-bit texture, loaded to tmem via tile 7 */
        for (i = 0; i < 2048; i++) rdram[0x300000 + i] = (uint8_t)rng();
        emit((0x3du << 24) | (2u << 19) | 31u); emit(0x300000u);
        emit((0x35u << 24) | (2u << 19) | (8u << 9)); emit((7u << 24) | (5u << 14) | (5u << 4));
        emit(0x33u << 24); emit((7u << 24) | (1023u << 12) | 256u);
        emit((0x35u << 24) | (2u << 19) | (8u << 9)); emit((0u << 24) | (5u << 14) | (5u << 4));
        emit((0x32u << 24)); emit((0u << 24) | (31u << 14) | (31u << 2));
        /* 2-cycle, perspective, lod, bilinear; z compare + update */
        emit((0x2fu << 24) | (1u << 20) | (1u << 19) | (1u << 16) | (1u << 13)); emit((1u << 4) | (1u << 5) | (1u << 6));
        emit((0x3cu << 24) | (1u << 20) | (4u << 15) | (1u << 12) | (4u << 9)); emit((8u << 28) | (7u << 15) | (7u << 12) | (7u << 9) | (7u << 3));
    }
    else
    {
        /* 1-cycle, z compare + update, image read */
        emit(0x2fu << 24); emit((1u << 4) | (1u << 5) | (1u << 6));
        emit(0x3cu << 24); emit((4u << 15) | (6u << 9) | (4u << 6) | (6u << 0));
    }
    for (i = 0; i < count; i++)
        emit_tri((i % 17) == 16);
    emit(0x29u << 24); emit(0);
    memcpy(rdram + CMD_ADDR, cmd, ncmd * 4);
}

static void run_frame(void)
{
    regs[DP_STATUS] = 0; regs[DP_START] = CMD_ADDR; regs[DP_CURRENT] = CMD_ADDR; regs[DP_END] = CMD_ADDR + ncmd * 4;
    n64video_process_list();
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    while (n--) { h ^= *p++; h *= 1099511628211ull; }
    return h;
}

int main(int argc, char **argv)
{
    int workers = argc > 1 ? atoi(argv[1]) : 1;
    int count   = argc > 2 ? atoi(argv[2]) : 3000;
    int seeds   = argc > 3 ? atoi(argv[3]) : 4;
    texmode     = argc > 4 ? atoi(argv[4]) : 0;
    struct n64video_config cfg;
    int i, sd;

    rdram = calloc(RDRAM_SIZE, 1);
    cmd = calloc(1 << 20, 4);
    for (i = 0; i < 8; i++) dp_reg[i] = &regs[i];
    for (i = 0; i < 16; i++) vi_reg[i] = &vi_regs[i];
    n64video_config_init(&cfg);
    cfg.gfx.rdram = rdram; cfg.gfx.rdram_size = RDRAM_SIZE; cfg.gfx.dmem = dmem;
    cfg.gfx.vi_reg = vi_reg; cfg.gfx.dp_reg = dp_reg;
    cfg.gfx.mi_intr_reg = &mi_intr; cfg.gfx.mi_intr_cb = intr_cb;
    cfg.parallel = true; cfg.num_workers = (uint32_t)workers; cfg.dp.compat = DP_COMPAT_HIGH;
    n64video_init(&cfg);

    for (sd = 0; sd < seeds; sd++)
    {
        unsigned written = 0;
        memset(rdram + FB_ADDR, 0, FB_W * FB_H * 2);
        memset(rdram + ZB_ADDR, 0xff, FB_W * FB_H * 2);
        build(count, sd);
        run_frame();
        for (i = 0; i < FB_W * FB_H * 2; i++) if (rdram[FB_ADDR + i]) written++;
        printf("workers=%2d seed=%d fb=%016llx zb=%016llx written=%u\n", workers, sd,
               (unsigned long long)fnv(rdram + FB_ADDR, FB_W * FB_H * 2),
               (unsigned long long)fnv(rdram + ZB_ADDR, FB_W * FB_H * 2), written);
    }
    n64video_close();
    return 0;
}

void msg_error(const char *err, ...) { va_list ap; va_start(ap, err); vfprintf(stderr, err, ap); va_end(ap); fputc('\n', stderr); exit(1); }
void msg_warning(const char *err, ...) { (void)err; }
void msg_debug(const char *err, ...) { (void)err; }
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb; (void)w; (void)h; (void)p; (void)o; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
