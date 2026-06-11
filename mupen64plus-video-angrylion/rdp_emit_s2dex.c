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
    unsigned int clip_l_q, clip_t_q;            /* left/top clip, 10.2 */
    unsigned int draw_w_q;                      /* drawn width, 10.2 */

    /* image-space start offsets and per-strip parameters */
    unsigned int line_words;        /* DMEM 0x3e2: SETTILE line field */
    unsigned int rows_strip;        /* DMEM 0x400: drawn rows per strip */
    unsigned int bpr;               /* DMEM 0x3f6: image bytes per row */
    unsigned int strip_adv;         /* DMEM 0x404: image pointer advance */
    unsigned int img_rows;          /* DMEM 0x40c: image height in rows */
    unsigned int settimg_w0, settile_w0;        /* DMEM 0x3f8 / 0x3fc */
    unsigned int seam_flag;         /* DMEM 0x3e1 */
    unsigned int start_row;         /* DMEM 0x40e */
    unsigned int x_ofs;             /* DMEM 0x3f4: X byte offset */
    uint32_t     rows_q;            /* DMEM 0x3e8: rows step, u6.10 */
    uint32_t     startx32, starty32;            /* 1/32-texel starts */
    int32_t      origin32;          /* imageYorig, wrap-adjusted */
    unsigned int first_a0, first_adv;           /* DMEM 0x400 / 0x404 */
    uint32_t     v0_init;           /* DMEM 0x3ec */
    unsigned int s_coord, t_coord;  /* first texrect S/T, s10.5 */
    unsigned int drawnw_fold;       /* scale*drawW fold, hi lane (v9[0]) */

    int32_t cw[6];
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
        clip_l_q    = (unsigned int)clip_l;
        clip_t_q    = (unsigned int)clip_t;
        draw_w_q    = (unsigned int)draw_w;
    }

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

    /* IMEM 0x8ec-0x908: the u6.10 row-phase step = rows * 0x400 scaled by
     * the 16.16 inverse vertical scale (exact lane folds). */
    {
        uint32_t t  = rows_strip * 0x400u;
        uint32_t lo = t & 0xffffu, hi = (t >> 16) & 0xffffu;
        uint32_t il = inv_h & 0xffffu, ih = inv_h >> 16;
        rows_q = ((lo * il) >> 16) + hi * il + lo * ih + ((hi * ih) << 16);
    }

    img_rows  = image_h >> 2;                   /* DMEM 0x40c */

    /* IMEM 0x7d4-0x840 + 0x860-0x87c: image-space start offsets in
     * 1/32-texel units and the wrap modulo. The image is a linear texel
     * stream: an X wrap past the right edge advances Y by one row (and the
     * texture origin with it); a Y wrap subtracts the image height from
     * both. The seam flag marks draws whose right edge reaches the image
     * width (the strip loads then need the line-gap patches). */
    {
        uint32_t imagew32 = ((uint32_t)image_w * 8u) & 0xffffu;
        uint32_t imageh32 = ((uint32_t)image_h * 8u) & 0xffffu;
        uint32_t drawnw32;
        uint32_t prodl = (uint32_t)scale_w * clip_l_q;
        uint32_t prodt = (uint32_t)scale_h * clip_t_q;
        startx32 = (uint32_t)image_x
                 + (((prodl & 0xffffu) * 0x200u) >> 16) + (prodl >> 16) * 0x200u;
        starty32 = (uint32_t)image_y
                 + (((prodt & 0xffffu) * 0x200u) >> 16) + (prodt >> 16) * 0x200u;
        origin32 = (int32_t)bg_rd_u32(rdram, bg_addr + 0x20);   /* imageYorig */
        {
            uint32_t prodw = (uint32_t)scale_w * (uint32_t)draw_w_q;
            drawnw_fold = ((((prodw & 0xffffu) * 0x200u) >> 16)
                           + (prodw >> 16) * 0x200u) & 0xffffu;
            drawnw32 = drawnw_fold + 0xbu;
        }
        while (startx32 >= imagew32)            /* IMEM 0x800-0x814 */
        {
            startx32 -= imagew32;
            starty32 += 0x20u;
            origin32 += 0x20;
        }
        while (starty32 >= imageh32 && imageh32 != 0u)  /* IMEM 0x81c-0x830 */
        {
            starty32 -= imageh32;
            origin32 -= (int32_t)imageh32;
        }
        seam_flag = (startx32 + drawnw32 < imagew32) ? 0u : 1u;
    }

    /* IMEM 0xbe8-0xbf4: bytes per row from the per-size constant
     * (imageW32 * mul >> 16 real 8-byte words, then *8). The settimg/
     * settile templates and the start-row offset follow (IMEM 0xbb4-0xc20;
     * the Y origin and phase are zero in the common unscrolled case and
     * carry the imageYorig wrap otherwise). */
    {
        uint32_t imagew32 = ((uint32_t)image_w * 8u) & 0xffffu;
        bpr = ((imagew32 * siz_mul) >> 16) * 8u;
    }
    if (bpr == 0u)
        return;
    strip_adv = rows_strip * bpr;               /* DMEM 0x404 */
    settimg_w0 = 0xfd100000u | (((bpr >> 1) - 1u) & 0xfffu);
    settile_w0 = 0xf5100000u | ((line_words & 0x1ffu) << 9);

    /* IMEM 0x90c-0x9c0 + 0xbb4-0xbf8: the Y phase and its division by the
     * strip step. The scroll distance (start minus origin, in rows<<10
     * through the inverse-scale folds, fraction bits masked) is divided by
     * rows_q with the RSP reciprocal table and a Newton pick of q vs q+1;
     * the remainder gives the first strip's shortened row count, its
     * sub-row fraction lands in the first texrect's T coordinate, and the
     * quotient/remainder/origin reconstruct the absolute start row. */
    x_ofs   = (((startx32 * siz_mul) >> 16) * 8u);          /* DMEM 0x3f4 */
    s_coord = startx32 & siz_add;               /* TMEM word phase, s10.5 */
    if (flip_s)
    {
        /* Horizontal flip is a negative S into the always-mirrored render
         * tile (the SETTILE template enables mirror_s): S' = -S minus the
         * drawn width fold (IMEM 0xc44-0xc48; v9[0] from 0x848-0x850). */
        s_coord = (0u - s_coord - drawnw_fold) & 0xffffu;
    }
    {
        uint32_t phase = (starty32 - (uint32_t)origin32) << 5;  /* DMEM 0x3e4 */
        uint32_t il = inv_h & 0xffffu, ih = inv_h >> 16;
        uint32_t lo = phase & 0xffffu, hi = phase >> 16;
        uint32_t ph = ((lo * il) >> 16) + hi * il + lo * ih + ((hi * ih) << 16);
        uint32_t ph_masked = (ph & 0xffff0000u) | (ph & 0xfc00u);
        int32_t  rcp = rsp_recip_div((int32_t)rows_q);
        uint32_t rl = (uint32_t)rcp & 0xffffu, rh = ((uint32_t)rcp >> 16) & 0xffffu;
        uint32_t pl = ph_masked & 0xffffu, phh = ph_masked >> 16;
        uint32_t qf = (((pl * rl) >> 16) + phh * rl + pl * rh + ((phh * rh) << 16)) * 2u;
        uint32_t q  = qf >> 16;
        uint32_t prod, prodm, remainder, rem_scaled, rem_i, orows32;
        uint32_t rs_lo;

        if (ph_masked >= rows_q * (q + 1u))     /* IMEM 0x950-0x960 */
            q += 1u;
        prod  = rows_q * q;
        prodm = (prod & 0xffff0000u) | (prod & 0xfc00u);    /* IMEM 0x96c */
        remainder = ph_masked - prodm;

        /* remainder >> 10 with the x0x40 folds (integer remainder rows) */
        rem_i = (((remainder & 0xffffu) * 0x40u) >> 16)
              + ((remainder >> 16) & 0xffffu) * 0x40u;
        /* scale back into image rows (IMEM 0x984-0x988) */
        rem_scaled = (rem_i & 0xffffu) * scale_h
                   + (((rem_i >> 16) & 0xffffu) * scale_h << 16);
        rs_lo = rem_scaled & 0xffffu;
        /* first texrect T: the sub-row fraction (IMEM 0x9a8-0x9ac) */
        t_coord = (unsigned int)((((int32_t)(int16_t)rs_lo * 0x800) >> 16)
                                 & 0x1f);
        /* integer scaled remainder (IMEM 0x9b0-0x9b8) */
        rem_i = ((((rem_scaled & 0xffffu) * 0x40u) >> 16)
                 + ((rem_scaled >> 16) & 0xffffu) * 0x40u) & 0xffffu;

        first_a0 = rows_strip - rem_i;          /* DMEM 0x400 */
        /* v0 init: rows_q with the product's sub-1024 dust, minus the
         * remainder (IMEM 0x98c-0x99c; the vand replaces the low
         * accumulator lane). */
        v0_init = (((rows_q >> 16) << 16) | (prod & 0x3ffu))
                + (rows_q & 0xffffu) - remainder;

        /* absolute start row (IMEM 0xbb4-0xbe4) */
        orows32 = ((((uint32_t)origin32 & 0xffffu) * 0x800u) >> 16)
                + (uint32_t)((int32_t)(int16_t)((uint32_t)origin32 >> 16)
                             * 0x800);
        start_row = (q * rows_strip + rem_i + orows32) & 0xffffu;
        if ((int)(short)start_row < 0)
            start_row = (start_row + img_rows) & 0xffffu;
        if (start_row >= img_rows)
            start_row -= img_rows;
    }
    first_adv = first_a0 * bpr;                 /* DMEM 0x404 */

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

    /* ---- strip loop (IMEM 0xcbc-0xd90 head, 0xa18-0xb74 loads, 0xd98
     * texrect). Register model: v1 = image rows left, t6 = clipped frame
     * rows left, v0 = u6.10 row-phase accumulator, t5 = drawn rows this
     * strip, a0 = load rows, a1 = strip source pointer, t1/t2 = texrect
     * top/bottom row. ---- */
    {
        unsigned int v1   = img_rows - start_row;       /* IMEM 0xcdc */
        unsigned int t6   = clip_h_px;                  /* DMEM 0x3de */
        uint32_t     v0   = v0_init;                    /* DMEM 0x3ec */
        unsigned int a0   = first_a0;                   /* DMEM 0x400 */
        unsigned int adv  = first_adv;                  /* DMEM 0x404 */
        unsigned int a1   = image_ptr + x_ofs
                          + start_row * bpr;            /* IMEM 0xc1c */
        unsigned int t1   = clip_uly_px;
        unsigned int tc   = t_coord;                    /* first-strip T */
        unsigned int t5, t2, t9, t8, base;
        int          tail;

        base = image_ptr;                               /* DMEM 0x3c0 */

        for (;;)
        {
            t5 = v0 >> 10;                              /* IMEM 0xcf8 */
            if (t5 == 0u)
            {
                /* sub-row strip (large vertical scale): consume the
                 * source rows without drawing (IMEM 0xd04-0xd48). */
                int rem = (int)v1 - (int)a0;
                a1 += adv;
                if (rem <= 0)
                {
                    a1 = base + x_ofs + (unsigned int)(-rem) * bpr;
                    rem += (int)img_rows;
                }
                v1  = (unsigned int)rem;
                v0  = (v0 & 0x3ffu) + rows_q;           /* IMEM 0xdd8 */
                a0  = rows_strip;
                adv = strip_adv;                        /* DMEM 0x408 */
                tc  = 0;
                continue;
            }
            tail = (int)t6 - (int)t5;                   /* IMEM 0xd4c */
            t6   = (unsigned int)((tail < 0) ? 0 : tail);
            v0  &= 0x3ffu;                              /* IMEM 0xd54 (delay) */
            if (tail < 0)
            {
                /* frame exhausted mid-strip: shrink the drawn rows and
                 * recompute the load rows from the scaled overshoot
                 * (IMEM 0xd58-0xd8c). */
                int32_t over = (int32_t)((uint32_t)scale_h *
                                         (uint32_t)tail) >> 10;
                unsigned int cap = rows_strip;          /* DMEM 0x402 */
                t5  = (unsigned int)((int)t5 + tail);
                a0  = (unsigned int)((int)a0 + 1 + (int)over);
                if (a0 > cap) a0 = cap;
                if (t5 == 0u)
                    break;
            }
            t2 = t1 + t5;

            /* IMEM 0xa18: normal or final-strip load */
            t9 = a0 + bilerp;
            t8 = v1 - seam_flag;
            if ((int)(t8 - t9) >= 0)
            {
                /* normal strip: SETTIMG/LOADSYNC/LOADTILE of t9 rows */
                cw[0] = (int32_t)settimg_w0;
                cw[1] = (int32_t)a1;
                cw[2] = (int32_t)0x26000000u;
                cw[3] = 0;
                cw[4] = (int32_t)0xf4000000u;
                cw[5] = (int32_t)((7u << 24) |
                        ((((line_words - 1u) << 4) & 0xfffu) << 12) |
                        ((t9 * 4u - 1u) & 0xfffu));
                rdp_fifo_append(fifo, cw, 6);
                v1 -= a0;
                a1 += adv;
            }
            else
            {
                /* final image strip (IMEM 0xa4c-0xb74): the guard rows
                 * wrap to the image top and the line-gap words get their
                 * seam patches, each load prefixed by TILESYNC + SETTILE 7
                 * at its TMEM offset (the 0x9cc emitter). */
                unsigned int pre = t9 - v1;
                /* loaded data words per row: each row's load starts at
                 * the X byte offset, so the real extent is bpr minus it
                 * (IMEM 0xad0-0xae0: v24[2] = DMEM 0x3f4). */
                unsigned int dw = ((bpr - x_ofs) & 0xffffu) >> 3;

                if ((int)pre > 0)
                {
                    unsigned int rrow = v1, rsrc = base + x_ofs;
                    unsigned int rcnt = pre;
                    if (v1 & 1u)
                    {
                        rcnt += 1u;
                        rrow -= 1u;
                        rsrc -= bpr;
                    }
                    cw[0] = (int32_t)0x28000000u;
                    cw[1] = 0;
                    cw[2] = (int32_t)(settile_w0 | ((rrow * line_words) & 0x1ffu));
                    cw[3] = (int32_t)0x27000000u;
                    rdp_fifo_append(fifo, cw, 4);
                    cw[0] = (int32_t)settimg_w0;
                    cw[1] = (int32_t)rsrc;
                    cw[2] = (int32_t)0x26000000u;
                    cw[3] = 0;
                    cw[4] = (int32_t)0xf4000000u;
                    cw[5] = (int32_t)((7u << 24) |
                            ((((line_words - 1u) << 4) & 0xfffu) << 12) |
                            ((rcnt * 4u - 1u) & 0xfffu));
                    rdp_fifo_append(fifo, cw, 6);
                }
                if (seam_flag)
                {
                    unsigned int t8e = t8, cnt = (t8 & 1u) + 1u;
                    unsigned int src2 = base, src3 = a1 + t8 * bpr;
                    if (t8 & 1u)
                    {
                        t8e -= 1u;
                        src2 -= bpr;
                        src3 -= bpr;
                    }
                    /* gap patch: first texels of the wrap row into the
                     * line padding at the end of TMEM row t8 */
                    cw[0] = (int32_t)0x28000000u;
                    cw[1] = 0;
                    cw[2] = (int32_t)(settile_w0 |
                            ((t8e * line_words + dw) & 0x1ffu));
                    cw[3] = (int32_t)0x27000000u;
                    rdp_fifo_append(fifo, cw, 4);
                    cw[0] = (int32_t)settimg_w0;
                    cw[1] = (int32_t)src2;
                    cw[2] = (int32_t)0x26000000u;
                    cw[3] = 0;
                    cw[4] = (int32_t)0xf4000000u;
                    cw[5] = (int32_t)((7u << 24) |
                            ((((line_words - dw - 1u) << 4) & 0xfffu) << 12) |
                            ((cnt * 4u - 1u) & 0xfffu));
                    rdp_fifo_append(fifo, cw, 6);
                    /* row patch: the row past the main load into TMEM
                     * row t8 */
                    cw[0] = (int32_t)0x28000000u;
                    cw[1] = 0;
                    cw[2] = (int32_t)(settile_w0 |
                            ((t8e * line_words) & 0x1ffu));
                    cw[3] = (int32_t)0x27000000u;
                    rdp_fifo_append(fifo, cw, 4);
                    cw[0] = (int32_t)settimg_w0;
                    cw[1] = (int32_t)src3;
                    cw[2] = (int32_t)0x26000000u;
                    cw[3] = 0;
                    cw[4] = (int32_t)0xf4000000u;
                    cw[5] = (int32_t)((7u << 24) |
                            ((((dw - 1u) << 4) & 0xfffu) << 12) |
                            ((cnt * 4u - 1u) & 0xfffu));
                    rdp_fifo_append(fifo, cw, 6);
                }
                /* main final load (TILESYNC + SETTILE at TMEM 0; the load
                 * itself only when rows remain) */
                if ((int)t8 > 0)
                {
                    cw[0] = (int32_t)0x28000000u;
                    cw[1] = 0;
                    cw[2] = (int32_t)settile_w0;
                    cw[3] = (int32_t)0x27000000u;
                    rdp_fifo_append(fifo, cw, 4);
                    cw[0] = (int32_t)settimg_w0;
                    cw[1] = (int32_t)a1;
                    cw[2] = (int32_t)0x26000000u;
                    cw[3] = 0;
                    cw[4] = (int32_t)0xf4000000u;
                    cw[5] = (int32_t)((7u << 24) |
                            ((((line_words - 1u) << 4) & 0xfffu) << 12) |
                            ((t8 * 4u - 1u) & 0xfffu));
                    rdp_fifo_append(fifo, cw, 6);
                }
                else
                {
                    cw[0] = (int32_t)settile_w0;        /* IMEM 0xb10 */
                    cw[1] = (int32_t)0x27000000u;
                    rdp_fifo_append(fifo, cw, 2);
                }
                /* image exhausted: wrap back to the top (IMEM 0xb40-0xb74) */
                {
                    int rem = (int)v1 - (int)a0;
                    if (rem > 0)
                    {
                        v1 = (unsigned int)rem;
                        a1 += adv;
                    }
                    else
                    {
                        a1 = base + x_ofs + (unsigned int)(-rem) * bpr;
                        v1 = (unsigned int)rem + img_rows;
                    }
                }
            }

            /* IMEM 0xd98: PIPESYNC + TEXRECT */
            cw[0] = (int32_t)0x27000000u;
            cw[1] = 0;
            cw[2] = (int32_t)(0x24000000u | ((clip_lrx & 0xfffu) << 12) |
                              ((t2 * 4u) & 0xfffu));
            cw[3] = (int32_t)(((clip_ulx & 0xfffu) << 12) |
                              ((t1 * 4u) & 0xfffu));
            cw[4] = (int32_t)((s_coord << 16) | tc);
            cw[5] = (int32_t)((scale_w << 16) | scale_h);
            rdp_fifo_append(fifo, cw, 6);

            if ((int)t6 <= 0)                           /* IMEM 0xdd0 */
                break;
            t1  = t2;
            v0 += rows_q;                               /* IMEM 0xdd8 */
            a0  = rows_strip;                           /* DMEM 0x402 */
            adv = strip_adv;                            /* DMEM 0x408 */
            tc  = 0;                                    /* IMEM 0xde8 */
        }
    }
}
