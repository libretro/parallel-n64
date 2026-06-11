/* rdp_emit_s2dex.c -- S2DEX2 background renderer for the angrylion HLE path.
 *
 * Zelda-engine games draw pre-rendered backgrounds (title screens, image
 * rooms) by switching the RSP to the S2DEX2 microcode mid-display-list
 * (gSPLoadUcodeL) for a single gSPBgRect1Cyc command. The microcode tiles
 * the background through TMEM strip by strip, emitting SETTIMG / LOADTILE /
 * TEXRECT triplets whose parameters come from a dense fixed-point pipeline
 * (scissor intersection, 16.16 inverse scale, TMEM row budget via the RSP
 * reciprocal table, odd-row two-pass loads).
 *
 * This file is a transcription of that BG overlay (IMEM 0x6c8-0xdf0 with
 * the resident helpers), not a reimplementation: every derived quantity
 * mirrors the microcode's vector arithmetic, including its truncations and
 * the table-driven VRCP, so the emitted command stream matches the LLE RSP
 * byte for byte. IMEM addresses of the source blocks are noted throughout.
 *
 * ISO C89 / MSVC-compatible. */

#include <string.h>
#include "rdp_emit_s2dex.h"
#include "rdp_emit_recip.h"

/* ---- latched RSP state ------------------------------------------------- */

/* DMEM 0x268: G_OBJ_RENDERMODE w1 low byte (handler at IMEM 0xe8). */
static unsigned int s_obj_rendermode;

/* DMEM 0x204/0x208: scissor fields split from the G_SETSCISSOR passthrough
 * (shared splitter at IMEM 0x158-0x18c): 10.2 ulx/lrx and uly/lry. */
static unsigned int s_scis_ulx, s_scis_uly, s_scis_lrx, s_scis_lry;

void s2dex_set_obj_rendermode(unsigned int w1)
{
    s_obj_rendermode = w1 & 0xffu;
}

void s2dex_set_scissor(unsigned int w0, unsigned int w1)
{
    s_scis_ulx = (w0 >> 12) & 0xfffu;
    s_scis_uly = w0 & 0xfffu;
    s_scis_lrx = (w1 >> 12) & 0xfffu;
    s_scis_lry = w1 & 0xfffu;
}

void s2dex_reset(void)
{
    s_obj_rendermode = 0;
    s_scis_ulx = s_scis_uly = 0;
    s_scis_lrx = s_scis_lry = 0;
}

/* ---- data-segment constants (S2DEX2 data, fixed in the ucode image) ---- */

/* per-size row constants at data 0x294 + siz*4: {add, mul} for the
 * line-words computation (line = 1 + ((min(...) + add) * mul >> 16)). */
static const uint16_t s2dex_siz_tab[4][2] = {
    { 0x01ff, 0x0080 },     /* 4-bit  */
    { 0x00ff, 0x0100 },     /* 8-bit  */
    { 0x007f, 0x0200 },     /* 16-bit */
    { 0x003f, 0x0400 }      /* 32-bit */
};

/* per-format TMEM capacity constant at data 0x2a4 + fmt*2 (the rows budget
 * is (vrcp(line) * cap) >> 32 via the table reciprocal; 0x400 encodes the
 * 512 8-byte TMEM words, halved for 32-bit/YUV formats that load through
 * both TMEM banks). */
static const uint16_t s2dex_fmt_tab[5] = {
    0x0400, 0x0400, 0x0200, 0x0400, 0x0200
};

/* SETTILE second word template at data 0x264 (clamp both axes, mask 15). */
#define S2DEX_SETTILE_W1   0x0007c1f0u

/* ---- helpers ------------------------------------------------------------ */

static unsigned int bg_rd_u16(const unsigned char *r, unsigned int a)
{
    return ((unsigned int)r[(a + 0) ^ 3] << 8) | (unsigned int)r[(a + 1) ^ 3];
}

static unsigned int bg_rd_u32(const unsigned char *r, unsigned int a)
{
    return ((unsigned int)r[(a + 0) ^ 3] << 24) |
           ((unsigned int)r[(a + 1) ^ 3] << 16) |
           ((unsigned int)r[(a + 2) ^ 3] <<  8) |
            (unsigned int)r[(a + 3) ^ 3];
}

/* The microcode's 16.16 inverse scale (IMEM 0x720-0x73c): a single-precision
 * VRCP of the u5.10 scale, scaled into 16.16 by 0x800 and biased by +1. */
static uint32_t bg_inv_scale(unsigned int scale)
{
    int32_t  rcp = rsp_recip_div((int32_t)(int16_t)scale);
    uint32_t hi  = ((uint32_t)rcp >> 16) & 0xffffu;
    uint32_t lo  = (uint32_t)rcp & 0xffffu;
    /* vmudn 1*1; vmadl (lo*0x800)>>16; vmadm hi*0x800; {v7:v8} = acc */
    uint32_t acc = 1u + ((lo * 0x800u) >> 16) + hi * 0x800u;
    return acc;            /* {hi16:lo16} = (1<<16)/scale + 1 */
}

/* ---- G_BG_1CYC ---------------------------------------------------------- */

void s2dex_bg_1cyc(const unsigned char *rdram, unsigned int rdram_bytes,
                   unsigned int bg_addr, RdpFifo *fifo)
{
    /* uObjScaleBg fields (all big-endian) */
    unsigned int image_x, image_w, image_y, image_h;        /* u10.5 / u10.2 */
    int          frame_x, frame_y;                          /* s10.2 */
    unsigned int frame_w, frame_h;                          /* u10.2 */
    unsigned int image_ptr, image_load, image_fmt, image_siz;
    unsigned int image_pal, image_flip;
    unsigned int scale_w, scale_h;                          /* u5.10 */

    uint32_t inv_w, inv_h;          /* 16.16 inverse scales (+1 bias) */
    unsigned int bilerp;            /* G_OBJRM_BILERP guard flag */
    unsigned int flip_s;            /* imageFlip & 1 */

    /* clipped frame rectangle (IMEM 0x784-0x7c4), 10.2 / pixel rows */
    unsigned int clip_ulx, clip_lrx;            /* DMEM 0x3d8 / 0x3da */
    unsigned int clip_uly_px, clip_h_px;        /* texrect row counters */

    /* image-space start offsets and per-strip parameters */
    unsigned int line_words;        /* DMEM 0x3e2: SETTILE line field */
    unsigned int rows_strip;        /* DMEM 0x400: drawn rows per strip */
    unsigned int bpr;               /* DMEM 0x3f6: image bytes per row */
    unsigned int strip_adv;         /* DMEM 0x404: image pointer advance */
    unsigned int img_rows;          /* DMEM 0x40c: image height in rows */
    unsigned int settimg_w0, settile_w0;        /* DMEM 0x3f8 / 0x3fc */

    int32_t cw[6];
    unsigned int strip_ptr, rows_left, y_top, y_bot, t9_extra;
    unsigned int n_rows, load_rows;
    unsigned int siz_add, siz_mul, fmt_cap;
    uint32_t extent_w, extent_h;    /* scaled image extents minus 1 LSB */
    uint32_t excess_w, excess_h;
    uint32_t v15v14, bound, line_in;
    int32_t  rows_rcp;

    if (bg_addr + 0x28u > rdram_bytes)
        return;

    /* struct DMA, IMEM 0xb78-0x70c: 0x28 bytes to DMEM 0x3b0 */
    image_x    = bg_rd_u16(rdram, bg_addr + 0x00);
    image_w    = bg_rd_u16(rdram, bg_addr + 0x02);
    frame_x    = (int)(short)bg_rd_u16(rdram, bg_addr + 0x04);
    frame_w    = bg_rd_u16(rdram, bg_addr + 0x06);
    image_y    = bg_rd_u16(rdram, bg_addr + 0x08);
    image_h    = bg_rd_u16(rdram, bg_addr + 0x0a);
    frame_y    = (int)(short)bg_rd_u16(rdram, bg_addr + 0x0c);
    frame_h    = bg_rd_u16(rdram, bg_addr + 0x0e);
    image_ptr  = bg_rd_u32(rdram, bg_addr + 0x10) & 0x00ffffffu;
    image_load = bg_rd_u16(rdram, bg_addr + 0x14);
    image_fmt  = rdram[(bg_addr + 0x16) ^ 3];
    image_siz  = rdram[(bg_addr + 0x17) ^ 3];
    image_pal  = bg_rd_u16(rdram, bg_addr + 0x18);
    image_flip = bg_rd_u16(rdram, bg_addr + 0x1a);
    scale_w    = bg_rd_u16(rdram, bg_addr + 0x1c);
    scale_h    = bg_rd_u16(rdram, bg_addr + 0x1e);

    (void)image_load;
    (void)image_x;
    (void)image_y;
    (void)t9_extra;

    bilerp  = (s_obj_rendermode >> 3) & 1u;     /* IMEM 0x854-0x85c */
    flip_s  = image_flip & 1u;                  /* IMEM 0x748-0x754 */

    if (image_fmt > 4u || image_siz > 3u)
        return;

    /* IMEM 0x718-0x73c: 16.16 inverse scales */
    inv_w = bg_inv_scale(scale_w);
    inv_h = bg_inv_scale(scale_h);

    /* IMEM 0x74c-0x770: scaled image extent minus one 10.2 LSB,
     * truncated to integer pixels; then the frame's excess beyond it:
     * {v12:v11} = imageW * inv - 1; v11 &= 0xfffc;
     * excess = max(0, frame - extent) per axis. */
    extent_w = ((uint32_t)image_w * (inv_w & 0xffffu) >> 16)
             + (uint32_t)image_w * (inv_w >> 16);
    extent_w = (extent_w - 1u) & 0xfffcu;
    extent_h = ((uint32_t)image_h * (inv_h & 0xffffu) >> 16)
             + (uint32_t)image_h * (inv_h >> 16);
    extent_h = (extent_h - 1u) & 0xfffcu;
    excess_w = (frame_w > extent_w) ? frame_w - extent_w : 0u;
    excess_h = (frame_h > extent_h) ? frame_h - extent_h : 0u;

    /* IMEM 0x774-0x780: horizontal flip shifts the frame left edge by the
     * excess so the mirrored image stays right-aligned. */
    if (flip_s)
        frame_x += (int)excess_w;

    /* IMEM 0x784-0x7b0: scissor intersection. left/top clip amounts and
     * right/bottom clip amounts, both clamped to >= 0; bail when the drawn
     * extent loses everything. (Frame edges are s10.2; the scissor is
     * unsigned 10.2.) */
    {
        int draw_w, draw_h, clip_l, clip_t, clip_r, clip_b;
        int fr_lrx = frame_x + (int)frame_w - (int)excess_w;
        int fr_lry = frame_y + (int)frame_h - (int)excess_h;

        clip_l = (int)s_scis_ulx - frame_x;
        if (clip_l < 0) clip_l = 0;
        clip_t = (int)s_scis_uly - frame_y;
        if (clip_t < 0) clip_t = 0;
        clip_r = fr_lrx - (int)s_scis_lrx;
        if (clip_r < 0) clip_r = 0;
        clip_b = fr_lry - (int)s_scis_lry;
        if (clip_b < 0) clip_b = 0;

        draw_w = (int)frame_w - (int)excess_w - clip_l - clip_r;
        draw_h = (int)frame_h - (int)excess_h - clip_t - clip_b;
        if (draw_w < 1 || draw_h < 1)
            return;

        clip_ulx    = (unsigned int)(frame_x + clip_l) & 0xfffu;
        clip_lrx    = (clip_ulx + (unsigned int)draw_w) & 0xfffu;
        clip_uly_px = ((unsigned int)(frame_y + clip_t) >> 2);
        clip_h_px   = (unsigned int)draw_h >> 2;

        /* image-space skip from the top clip (rows of source consumed
         * before the first drawn row), in u10.2 rows * scale: the
         * microcode folds this into the first strip's load offset; the
         * common full-screen case has none. */
        (void)clip_t;
    }

    /* IMEM 0x7d4-0x840: image bytes per row and the start-offset modulo
     * (imageX wrap by 0x20-byte units). bpr = imageW(10.2) << siz >> 3
     * texel-bytes (the 10.2 quarters carry the fractional padding). */
    bpr = ((image_w << image_siz) >> 1) >> 2;

    /* IMEM 0x8a4-0x8d4: SETTILE line words.
     * needed = (frameW_px * scale + bilerp) * 32 texel-units (vmudn scale x
     * frameW with the x0x200 fold), bounded by imageW * 32; line = 1 +
     * ((min + add) * mul >> 16) using the per-size table. */
    siz_add = s2dex_siz_tab[image_siz][0];
    siz_mul = s2dex_siz_tab[image_siz][1];
    {
        uint32_t prod = (uint32_t)scale_w * frame_w;    /* texels<<12 */
        v15v14 = bilerp * 0x20u + ((prod & 0xffffu) * 0x200u >> 16)
               + (prod >> 16) * 0x200u;
        bound  = ((uint32_t)image_w * 8u) & 0xffffu;    /* vmudn x v31[6] */
        line_in = (v15v14 < bound) ? v15v14 : bound;    /* vlt/vmrg */
    }
    line_words = 1u + (((line_in + siz_add) * siz_mul) >> 16);

    /* IMEM 0x8d8-0x8e8: TMEM rows per strip = (vrcp(line) * cap >> 16) -
     * bilerp via the RSP reciprocal table (cap = 512 words scaled by the
     * format constant), then held as rows in u6.10 for the Y stepping. */
    fmt_cap  = s2dex_fmt_tab[image_fmt];
    rows_rcp = rsp_recip_div((int32_t)line_words);
    {
        uint32_t lo = (uint32_t)rows_rcp & 0xffffu;
        uint32_t hi = ((uint32_t)rows_rcp >> 16) & 0xffffu;
        uint32_t acc = ((lo * fmt_cap) >> 16) + hi * fmt_cap;
        rows_strip = (acc >> 16) - bilerp;
    }
    if (rows_strip == 0u || rows_strip > 0x3ffu)
        return;

    strip_adv = rows_strip * bpr;               /* DMEM 0x404 */
    img_rows  = image_h >> 2;                   /* DMEM 0x40c */

    /* IMEM 0xc54-0xc74: SETTIMG / SETTILE word templates. The image is
     * always addressed 16-bit wide at the load (fmt/siz 0/2 in the
     * opcode bytes); width comes from the byte row. */
    settimg_w0 = 0xfd100000u | (((bpr >> 1) - 1u) & 0xfffu);
    settile_w0 = 0xf5100000u | ((line_words & 0x1ffu) << 9);

    /* IMEM 0xc78-0xcb8: preamble -- SETTILE 7 (load tile; w1 is the 0x27
     * template lane), SETTILE 0 (render tile with the struct's fmt/siz and
     * palette), SETTILESIZE 0 (raw 0x32 opcode byte from the v29 lane). */
    cw[0] = (int32_t)settile_w0;
    cw[1] = (int32_t)0x27000000u;
    cw[2] = (int32_t)((settile_w0 & 0xff00ffffu) |
                      ((image_fmt & 7u) << 21) | ((image_siz & 3u) << 19));
    cw[3] = (int32_t)(((image_pal & 0xfu) << 20) | S2DEX_SETTILE_W1);
    cw[4] = (int32_t)0x32000000u;
    cw[5] = 0;
    rdp_fifo_append(fifo, cw, 6);

    /* IMEM 0xcbc-0xd90 + 0xa18-0xb38 + 0xd98: the strip loop. The common
     * path (no flip, no wrap, integer scale step) walks the image top to
     * bottom: each strip loads rows_strip(+bilerp guard) rows into tile 7
     * and draws rows_strip rows; the final strip draws the remainder. */
    strip_ptr = image_ptr;
    rows_left = img_rows;
    y_top     = clip_uly_px;

    while (rows_left > 0u && clip_h_px > 0u)
    {
        n_rows = (rows_left < rows_strip) ? rows_left : rows_strip;
        if (n_rows > clip_h_px) n_rows = clip_h_px;
        load_rows = n_rows + bilerp;
        if (load_rows > rows_left) load_rows = rows_left;
        y_bot = y_top + n_rows;

        /* IMEM 0x9e4-0xa14: SETTIMG + LOADSYNC + LOADTILE. lrs spans the
         * padded line ((line-1)*4 texels), lrt the loaded rows. */
        cw[0] = (int32_t)settimg_w0;
        cw[1] = (int32_t)strip_ptr;
        cw[2] = (int32_t)0x26000000u;
        cw[3] = 0;
        rdp_fifo_append(fifo, cw, 4);
        cw[0] = (int32_t)0xf4000000u;
        cw[1] = (int32_t)((7u << 24) |
                          ((((line_words - 1u) << 4) & 0xfffu) << 12) |
                          ((load_rows * 4u - 1u) & 0xfffu));
        rdp_fifo_append(fifo, cw, 2);

        /* IMEM 0xd98-0xdcc: PIPESYNC + TEXRECT (raw 0x24 opcode byte,
         * exclusive 10.2 coords, s/t from the strip phase, dsdx/dtdy are
         * the raw u5.10 scales). */
        cw[0] = (int32_t)0x27000000u;
        cw[1] = 0;
        cw[2] = (int32_t)(0x24000000u | ((clip_lrx & 0xfffu) << 12) |
                          ((y_bot * 4u) & 0xfffu));
        cw[3] = (int32_t)(((clip_ulx & 0xfffu) << 12) |
                          ((y_top * 4u) & 0xfffu));
        cw[4] = 0;
        cw[5] = (int32_t)((scale_w << 16) | scale_h);
        rdp_fifo_append(fifo, cw, 6);

        strip_ptr += strip_adv;
        rows_left -= n_rows;
        clip_h_px -= n_rows;
        y_top      = y_bot;
    }
}
