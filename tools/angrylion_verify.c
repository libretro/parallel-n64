/* Angrylion bit-exactness harness: renders randomized shade+Z (mode 0)
 * or 2-cycle textured (mode 1) triangles and hashes the colour and depth
 * buffers, for comparing two builds of the renderer at each lane count.
 *
 * Built by `make tools` from the tree's own angrylion objects (the
 * target is in the top-level Makefile).
 *
 * Usage: angrylion_verify WORKERS TRIANGLES SEEDS MODE [SYNC] [REPS] [SCALE]
 *   AL_REALISTIC=1 draws shade and depth gradients of the magnitudes a game
 *   uses instead of random 32-bit words, which is what a comparison across
 *   scaling factors needs: the random ones wrap the 32-bit attribute
 *   arithmetic, and a wrapped result does not halve when its input does
 *   MODE 0 shade+Z, 1 2-cycle textured, 2 render-to-texture rounds;
 *   SYNC 0 low, 1 medium, 2 high (default); REPS frames per seed (default 1);
 *   SCALE renders on a grid that many times finer and resolves the shared
 *   sample (default 1)
 */
#include "n64video.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define RDRAM_SIZE (8u << 20)
#define FB_ADDR    0x100000u
#define FB2_ADDR   0x140000u
#define ZB_ADDR    0x180000u
#define TEX_ADDR   0x300000u
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
#ifndef NO_FLUSH_COUNT
uint32_t n64video_flush_count(void);
#endif
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
    if (getenv("AL_REALISTIC"))
    {
        /* base colour 0..255 in 16.16, derivatives up to +-2/256 of full
         * scale per pixel or scanline: what a game's gradients look like */
        uint32_t c[4], dx[4], de[4], dy[4], k;
        for (k = 0; k < 4; k++) { c[k] = (uint32_t)rnd(0, 255) << 16; dx[k] = (uint32_t)rnd(-0x20000, 0x20000); de[k] = (uint32_t)rnd(-0x20000, 0x20000); dy[k] = (uint32_t)rnd(-0x20000, 0x20000); }
        /* RDP layout: int parts of r,g / b,a ; dx int ; frac parts ... */
        emit((c[0] & 0xffff0000) | (c[1] >> 16)); emit((c[2] & 0xffff0000) | (c[3] >> 16));
        emit((dx[0] & 0xffff0000) | (dx[1] >> 16)); emit((dx[2] & 0xffff0000) | (dx[3] >> 16));
        emit((c[0] << 16) | (c[1] & 0xffff)); emit((c[2] << 16) | (c[3] & 0xffff));
        emit((dx[0] << 16) | (dx[1] & 0xffff)); emit((dx[2] << 16) | (dx[3] & 0xffff));
        emit((de[0] & 0xffff0000) | (de[1] >> 16)); emit((de[2] & 0xffff0000) | (de[3] >> 16));
        emit((dy[0] & 0xffff0000) | (dy[1] >> 16)); emit((dy[2] & 0xffff0000) | (dy[3] >> 16));
        emit((de[0] << 16) | (de[1] & 0xffff)); emit((de[2] << 16) | (de[3] & 0xffff));
        emit((dy[0] << 16) | (dy[1] & 0xffff)); emit((dy[2] << 16) | (dy[3] & 0xffff));
    }
    else
    for (i = 0; i < 16; i++) emit(rng());
    /* texture: random s/t/w and gradients */
    if (texmode) for (i = 0; i < 16; i++) emit(rng());
    /* z: random */
    if (getenv("AL_REALISTIC")) { emit((uint32_t)rnd(0, 0x7fff) << 16); emit((uint32_t)rnd(-0x800, 0x800)); emit((uint32_t)rnd(-0x800, 0x800)); emit((uint32_t)rnd(-0x800, 0x800)); }
    else
    for (i = 0; i < 4; i++) emit(rng());
}

/* Render-to-texture scene: draw into one buffer, load a tile out of it
 * (a load that must wait for that drawing), draw with the tile into the
 * other buffer, swap. Every other round the tile comes from static
 * texture memory instead, a load nothing has to wait for. */
static void emit_tex_setup(uint32_t addr, uint32_t width)
{
    emit((0x3du << 24) | (2u << 19) | (width - 1)); emit(addr);
}
/* A triangle from three vertices in pixels: flat colour, coverage-AA
 * blend against the framebuffer (blend_en on), as the ground polygons
 * are drawn. */
static void emit_flat_tri(double x0, double y0, double x1, double y1, double x2, double y2, uint32_t rr, uint32_t gg, uint32_t bb)
{
    double vx[3] = { x0, x1, x2 }, vy[3] = { y0, y1, y2 };
    int i, j, flip;
    double dxhdy, dxmdy, dxldy, xh, xm, xl;
    int32_t yh, ym, yl;
    for (i = 0; i < 3; i++) for (j = i + 1; j < 3; j++)
        if (vy[j] < vy[i]) { double t = vx[i]; vx[i]=vx[j]; vx[j]=t; t=vy[i]; vy[i]=vy[j]; vy[j]=t; }
    dxhdy = (vx[2]-vx[0]) / (vy[2]-vy[0]);
    dxmdy = vy[1] > vy[0] ? (vx[1]-vx[0]) / (vy[1]-vy[0]) : 0;
    dxldy = vy[2] > vy[1] ? (vx[2]-vx[1]) / (vy[2]-vy[1]) : 0;
    yh = (int32_t)floor(vy[0]*4.0); ym = (int32_t)floor(vy[1]*4.0); yl = (int32_t)floor(vy[2]*4.0);
    xh = vx[0] + dxhdy*((yh/4.0)-vy[0]);
    xm = vx[0] + dxmdy*((yh/4.0)-vy[0]);
    xl = vx[1] + dxldy*((ym/4.0)-vy[1]);
    flip = (vx[1] > vx[0] + dxhdy*(vy[1]-vy[0])) ? 1 : 0;
    emit((0x08u << 24) | ((uint32_t)flip << 23) | ((uint32_t)yl & 0x3fff));
    emit((((uint32_t)ym & 0xffff) << 16) | ((uint32_t)yh & 0xffff));
    emit((uint32_t)(int32_t)(xl*65536.0) & 0x0fffffff); emit((uint32_t)(int32_t)(dxldy*65536.0) & 0x3fffffff);
    emit((uint32_t)(int32_t)(xh*65536.0) & 0x0fffffff); emit((uint32_t)(int32_t)(dxhdy*65536.0) & 0x3fffffff);
    emit((uint32_t)(int32_t)(xm*65536.0) & 0x0fffffff); emit((uint32_t)(int32_t)(dxmdy*65536.0) & 0x3fffffff);
    (void)rr; (void)gg; (void)bb;
}

/* The game's situation: a background polygon (the "moat", blue) drawn
 * first over the whole image, then two triangles (the "sand", yellow)
 * sharing a diagonal edge drawn over it, all with coverage-AA blend.
 * The shared edge must not let the background show through. */
static void build_seam(void)
{
    ncmd = 0;
    emit((0x2du << 24) | 0u); emit(((uint32_t)(FB_W<<2) << 12) | (uint32_t)(FB_H<<2));
    emit((0x3fu << 24) | (2u << 19) | (FB_W-1)); emit(FB_ADDR);
    /* clear to black with a fill */
    emit((0x2fu << 24) | (3u << 20)); emit(0);
    emit((0x37u << 24)); emit(0);
    emit((0x36u << 24) | ((uint32_t)((FB_W-1)<<2) << 12) | (uint32_t)((FB_H-1)<<2)); emit(0);
    /* 1-cycle, blend colour over memory by coverage; dither off; AA on */
    emit((0x2fu << 24) | (3u << 6) | (3u << 4) | (1u << 3)); emit((1u << 6) | (1u << 4));
    /* blend: (pixel_color * pixel_alpha) + (mem_color * (1-alpha)) */
    emit((0x3cu << 24) | (0u << 30) | (0u << 28) | (0u << 26) | (0u << 24)); emit(0);
    /* moat: full-screen blue */
    emit((0x37u << 24)); emit(0);   /* set prim/fill unused */
    emit_flat_tri(0, 0, FB_W, 0, 0, FB_H, 0, 0, 31);
    emit_flat_tri(FB_W, 0, FB_W, FB_H, 0, FB_H, 0, 0, 31);
    /* sand: the shared-edge pair, yellow */
    emit_flat_tri(20, 20, 300, 40, 40, 200, 31, 31, 0);
    emit_flat_tri(300, 40, 300, 220, 40, 200, 31, 31, 0);
    emit((0x29u << 24)); emit(0);
}

static void build_rtt(int count, int seed)
{
    int i, round;
    uint32_t cur = FB_ADDR, other = FB2_ADDR;
    rng_state = 0x9e3779b9u ^ (uint32_t)seed;
    ncmd = 0;
    for (i = 0; i < 2048; i++) rdram[TEX_ADDR + i] = (uint8_t)rng();
    emit(0x3eu << 24); emit(ZB_ADDR);
    emit((0x2du << 24) | (0u << 12) | 0u); emit(((uint32_t)(FB_W << 2) << 12) | (uint32_t)(FB_H << 2));
    emit((0x2fu << 24) | (1u << 20) | (1u << 19) | (1u << 16) | (1u << 13)); emit((1u << 4) | (1u << 5) | (1u << 6));
    emit((0x3cu << 24) | (1u << 20) | (4u << 15) | (1u << 12) | (4u << 9)); emit((8u << 28) | (7u << 15) | (7u << 12) | (7u << 9) | (7u << 3));
    emit((0x35u << 24) | (2u << 19) | (8u << 9)); emit((0u << 24) | (5u << 14) | (5u << 4));
    emit((0x32u << 24)); emit((0u << 24) | (31u << 14) | (31u << 2));
    for (round = 0; round < count / 8; round++)
    {
        uint32_t t;
        emit((0x3fu << 24) | (0u << 21) | (2u << 19) | (FB_W - 1)); emit(cur);
        for (i = 0; i < 8; i++)
            emit_tri(0);
        if (round & 1)
        {
            /* tile from static texture memory: no hazard */
            emit_tex_setup(TEX_ADDR, 32);
            emit((0x35u << 24) | (2u << 19) | (8u << 9)); emit((7u << 24) | (5u << 14) | (5u << 4));
            emit((0x34u << 24)); emit((7u << 24) | ((31u << 2) << 12) | (31u << 2));
        }
        else
        {
            /* 32x32 tile out of the buffer just drawn: must wait */
            uint32_t sx = (uint32_t)rnd(0, FB_W - 33), sy = (uint32_t)rnd(0, FB_H - 33);
            emit_tex_setup(cur, FB_W);
            emit((0x35u << 24) | (2u << 19) | (8u << 9)); emit((7u << 24) | (5u << 14) | (5u << 4));
            emit((0x34u << 24) | ((sx << 2) << 12) | (sy << 2)); emit((7u << 24) | (((sx + 31) << 2) << 12) | ((sy + 31) << 2));
        }
        t = cur; cur = other; other = t;
    }
    emit(0x29u << 24); emit(0);
    memcpy(rdram + CMD_ADDR, cmd, ncmd * 4);
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
        /* 1-cycle, z compare + update, image read; dither off (both
         * selectors 3) so a comparison across scaling factors measures
         * the interpolation rather than a noise pattern whose period is
         * the grid it was generated on */
        emit((0x2fu << 24) | (3u << 6) | (3u << 4)); emit((1u << 4) | (1u << 5) | (1u << 6));
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
    int compat;
    int reps, r;
    int scale;
    texmode     = argc > 4 ? atoi(argv[4]) : 0;
    compat      = argc > 5 ? atoi(argv[5]) : 2;
    reps        = argc > 6 ? atoi(argv[6]) : 1;
    scale       = argc > 7 ? atoi(argv[7]) : 1;
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
    cfg.parallel = true; cfg.num_workers = (uint32_t)workers; cfg.dp.compat = (enum dp_compat_profile)compat;
    cfg.upscale = (uint32_t)scale;
    n64video_init(&cfg);

    for (sd = 0; sd < seeds; sd++)
    {
        unsigned written = 0;
        memset(rdram + FB_ADDR, 0, FB_W * FB_H * 2);
        memset(rdram + FB2_ADDR, 0, FB_W * FB_H * 2);
        memset(rdram + ZB_ADDR, 0xff, FB_W * FB_H * 2);
        if (scale > 1)
        {
            /* the buffers live in the pixel domain at this factor, so
             * clear them there rather than in RDRAM */
            unsigned samples = (unsigned)(scale * scale);
            uint8_t *dom = (uint8_t*)n64video_pixel_domain();
            memset(dom + (size_t)FB_ADDR * samples, 0,    (size_t)FB_W * FB_H * 2 * samples);
            memset(dom + (size_t)FB2_ADDR * samples, 0,   (size_t)FB_W * FB_H * 2 * samples);
            memset(dom + (size_t)ZB_ADDR * samples, 0xff, (size_t)FB_W * FB_H * 2 * samples);
        }
        if (texmode == 3)
            build_seam();
        else if (texmode == 2)
        {
            texmode = 1;
            build_rtt(count, sd);
            texmode = 2;
        }
        else
            build(count, sd);
        for (r = 0; r < reps; r++)
            run_frame();
        for (i = 0; i < FB_W * FB_H * 2; i++) if (rdram[FB_ADDR + i]) written++;
        if (scale > 1)
        {
            /* Take the sample every console pixel shares with an
             * unscaled render - the top-left of its block - so the two
             * can be compared directly. */
            unsigned x, y, samples = (unsigned)(scale * scale);
            unsigned stride = FB_W * (unsigned)scale;
            uint16_t *src = (uint16_t*)n64video_pixel_domain();
            uint16_t *dst = (uint16_t*)(rdram + FB_ADDR);
            uint16_t *zdst = (uint16_t*)(rdram + ZB_ADDR);
            unsigned fbbase = (FB_ADDR * samples) / 2;
            unsigned zbbase = (ZB_ADDR * samples) / 2;
            for (y = 0; y < FB_H; y++)
                for (x = 0; x < FB_W; x++)
                {
                    unsigned o = stride * (y * (unsigned)scale) + x * (unsigned)scale;
                    dst[y * FB_W + x]  = src[fbbase + o];
                    zdst[y * FB_W + x] = src[zbbase + o];
                }
        }
        if (getenv("AL_DUMP"))
        {
            FILE *f = fopen(getenv("AL_DUMP"), "wb");
            if (f) { fwrite(rdram + FB_ADDR, 2, FB_W * FB_H, f); fclose(f); }
        }
        if (texmode == 3)
        {
            unsigned x, y, bg = 0;
            const uint16_t *fbp = (const uint16_t*)(rdram + FB_ADDR);
            for (y = 45; y < 195; y++)
                for (x = 45; x < 295; x++)
                    if ((fbp[y*FB_W+x] & 0x003e) >= 0x0030 && ((fbp[y*FB_W+x] >> 11) & 31) < 12) bg++;
            printf("seam: %u background pixels inside the sand quad\n", bg);
        }
        printf("workers=%2d seed=%d fb=%016llx fb2=%016llx zb=%016llx written=%u", workers, sd,
               (unsigned long long)fnv(rdram + FB_ADDR, FB_W * FB_H * 2),
               (unsigned long long)fnv(rdram + FB2_ADDR, FB_W * FB_H * 2),
               (unsigned long long)fnv(rdram + ZB_ADDR, FB_W * FB_H * 2), written);
#ifndef NO_FLUSH_COUNT
        printf(" batches=%u", n64video_flush_count());
#endif
        printf("\n");
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
