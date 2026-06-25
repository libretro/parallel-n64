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
#include "rdp_emit_frontend.h"

/* S2DEX sprite quad uses four scratch vertex slots at the top of the RSP
 * vertex buffer so it never clobbers geometry a surrounding display list
 * loaded into the low indices. */
#define S2DEX_SPR_V0 60

/* A screen-space sprite corner: position is s10.2 << 14 (the bridge reads
 * it back >> 14), texcoords are S10.5 in the high half (sv/tv keep the raw
 * short), shade is a flat white (0..255 as s15.16), and w is a constant so
 * the texture maps affinely (only the S:T:W ratio matters, and an equal w
 * on every corner removes perspective). */
static void s2dex_set_corner(GSPVertex *v, int sx, int sy,
                             int sval, int tval)
{
    /* S10.5 texel coordinates are a signed 16-bit attribute; the 4x extent of
     * a full-width (8192-texel) sprite reaches 0x8000, one past the positive
     * limit, which overflows the s15.16 store below (32768<<16 == INT32_MIN)
     * and flips the title's S negative (it samples off-texture and vanishes).
     * Saturate S to the representable range -- the real RSP stops the inclusive
     * right edge at texel 255 (S 0x1FE0), so clamping 0x8000 to 0x7FFF is
     * sub-texel and matches the LLE sample. T is left as-is: the affected
     * sprites here never exceed the T range, and tall-texture T saturation is a
     * separate, unverified case. */
    if (sval >  0x7fff) sval =  0x7fff;
    if (sval < -0x8000) sval = -0x8000;
    memset(v, 0, sizeof(*v));
    v->scr_x = (int32_t)sx << 14;
    v->scr_y = (int32_t)sy << 14;
    v->scr_z = 0;
    v->cw    = 0x00010000;          /* w == 1.0 (s15.16) */
    v->w_raw = (int64_t)0x40000000; /* normalised affine W */
    v->rsp_ok = 0;
    v->rsp_invw = 0;
    v->s  = (int32_t)sval << 16;
    v->t  = (int32_t)tval << 16;
    v->sv = (int16_t)sval;
    v->tv = (int16_t)tval;
    v->r = v->g = v->b = v->a = (int32_t)0xff << 16;
    v->clip = 0;
}

/* ---- latched RSP state ------------------------------------------------- */

/* DMEM 0x268: G_OBJ_RENDERMODE w1 low byte (handler at IMEM 0xe8). */
/* Index a u32 corrector table as little-endian s16[] (as the microcode does). */
#define S2DEX_S16AT(arr, i) \
    ((int)(short)(((i) & 1u) ? (unsigned short)((arr)[(i) >> 1] >> 16) \
                              : (unsigned short)((arr)[(i) >> 1] & 0xffffu)))

static unsigned int s_obj_rendermode;

/* DMEM 0x204/0x208: scissor fields split from the G_SETSCISSOR passthrough
 * (shared splitter at IMEM 0x158-0x18c): 10.2 ulx/lrx and uly/lry. */
static unsigned int s_scis_ulx, s_scis_uly, s_scis_lrx, s_scis_lry;

void s2dex_set_obj_rendermode(unsigned int w1)
{
    s_obj_rendermode = w1 & 0xffu;
}

/* the raw G_SETSCISSOR words, re-emitted in front of display-list
 * texture rectangles while the S2DEX2 microcode is loaded (its
 * background renderers may leave a narrowed scissor behind, so the
 * rectangle handler restores the list's scissor first). */
static unsigned int s_scis_w0, s_scis_w1;

void s2dex_set_scissor(unsigned int w0, unsigned int w1)
{
    s_scis_ulx = (w0 >> 12) & 0xfffu;
    s_scis_uly = w0 & 0xfffu;
    s_scis_lrx = (w1 >> 12) & 0xfffu;
    s_scis_lry = w1 & 0xfffu;
    s_scis_w0 = w0;
    s_scis_w1 = w1;
}

void s2dex_emit_scissor(RdpFifo *fifo)
{
    int32_t cw[2];
    cw[0] = (int32_t)s_scis_w0;
    cw[1] = (int32_t)s_scis_w1;
    rdp_fifo_append(fifo, cw, 2);
}

/* DMEM 0x8c-0x9b: the four texture-load status words used by the
 * G_OBJ_LOADTXTR dedup test: the load runs only when
 * (status[sid >> 2] & mask) != flag, and afterwards stores
 * status = (status & ~mask) | flag, so repeated loads of the texture
 * already resident in TMEM emit nothing. Reset on every microcode load
 * (the data segment, status words included, is re-DMAed). */
static unsigned int s_obj_status[4];

/* DMEM 0x2ae: the tile-7 history byte. G_OBJ_LOADTXTR emits a leading
 * TILESYNC only when the previous tile-7 user was also a texture load
 * (the byte holds 0x81; the background renderers store their render-tile
 * parity instead, and the data segment ships 0x80, so the first load
 * after a microcode reload never syncs). A deduped load skips the byte
 * entirely. Modeled as: set by loads and the BG renderers' strip loads,
 * cleared by the data reload; the load syncs when set. */
static int s_obj_tile7_used;

void s2dex_reset(void)
{
    /* Mirror the values the S2DEX2 data segment ships for these DMEM
     * slots: a microcode (re)load DMAs them in fresh. The default
     * scissor is the full 320x240 screen (0x204 = 0x00000500,
     * 0x208 = 0x000003c0), not zero -- Zelda's pre-rendered rooms set
     * the RDP scissor before gSPLoadUcodeL and never resend it, so a
     * zeroed shadow would clip every background strip away. */
    s_obj_rendermode = 0;
    s_scis_ulx = s_scis_uly = 0;
    s_scis_lrx = 0x500;
    s_scis_lry = 0x3c0;
    s_scis_w0 = 0xed000000u;
    s_scis_w1 = 0x005003c0u;
    s_obj_status[0] = s_obj_status[1] = 0;
    s_obj_status[2] = s_obj_status[3] = 0;
    s_obj_tile7_used = 0;
    s2dex1_reset();
}

/* ---- G_OBJ_LOADTXTR ----------------------------------------------------- */

/* uObjTxtr (24 bytes, big-endian):
 *   +0x00 u32 type     G_OBJLT_TXTRBLOCK 0x1033 / TXTRTILE 0xfc1034 /
 *                      TLUT 0x30
 *   +0x04 u32 image    source address (segmented)
 *   +0x08 u16 tmem     TMEM word address (TLUT: palette head)
 *   +0x0a u16 tsize    LOADBLOCK texel count (TLUT: entry count - 1)
 *   +0x0c u16 tline    LOADBLOCK dxt (TLUT: unused)
 *   +0x0e u16 sid      status id (0/4/8/12)
 *   +0x10 u32 flag     dedup tag
 *   +0x14 u32 mask     dedup mask
 * Returns 1 when the command was consumed (always). The emitted command
 * words mirror the S2DEX2 RSP output verbatim, including the junk upper
 * bits the microcode leaves in the tile words (0x27000000), so the
 * stream compares byte-exact against the LLE. */
int s2dex_obj_loadtxtr(const unsigned char *rdram, unsigned int rdram_bytes,
                       unsigned int ta, RdpFifo *fifo,
                       unsigned int (*segfn)(unsigned int))
{
    unsigned int type, image, tmem, tsize, tline, sid, flag, mask;
    int32_t cw[6];
    if (ta + 24 > rdram_bytes)
        return 1;
    type  = ((unsigned int)rdram[(ta + 0) ^ 3] << 24)
          | ((unsigned int)rdram[(ta + 1) ^ 3] << 16)
          | ((unsigned int)rdram[(ta + 2) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 3) ^ 3];
    image = ((unsigned int)rdram[(ta + 4) ^ 3] << 24)
          | ((unsigned int)rdram[(ta + 5) ^ 3] << 16)
          | ((unsigned int)rdram[(ta + 6) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 7) ^ 3];
    tmem  = ((unsigned int)rdram[(ta + 8) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 9) ^ 3];
    tsize = ((unsigned int)rdram[(ta + 10) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 11) ^ 3];
    tline = ((unsigned int)rdram[(ta + 12) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 13) ^ 3];
    sid   = ((unsigned int)rdram[(ta + 14) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 15) ^ 3];
    flag  = ((unsigned int)rdram[(ta + 16) ^ 3] << 24)
          | ((unsigned int)rdram[(ta + 17) ^ 3] << 16)
          | ((unsigned int)rdram[(ta + 18) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 19) ^ 3];
    mask  = ((unsigned int)rdram[(ta + 20) ^ 3] << 24)
          | ((unsigned int)rdram[(ta + 21) ^ 3] << 16)
          | ((unsigned int)rdram[(ta + 22) ^ 3] << 8)
          |  (unsigned int)rdram[(ta + 23) ^ 3];

    sid = (sid >> 2) & 3u;
    if ((s_obj_status[sid] & mask) == flag)
        return 1;                       /* already resident */
    s_obj_status[sid] = (s_obj_status[sid] & ~mask) | flag;

    if (s_obj_tile7_used)
    {
        cw[0] = (int32_t)0x28000000u;   /* tilesync against the last load */
        cw[1] = 0;
        rdp_fifo_append(fifo, cw, 2);
    }
    s_obj_tile7_used = 1;

    image = segfn(image);

    if (type == 0x00000030u)            /* G_OBJLT_TLUT */
    {
        /* SETTIMG(RGBA, 16b, width = pnum), SETTILE(tmem = phead,
         * tile 7), LOADTLUT(tile 7, pnum << 14). */
        cw[0] = (int32_t)(0x3d100000u | (tsize & 0xfffu));
        cw[1] = (int32_t)image;
        cw[2] = (int32_t)(0x35000000u | (tmem & 0x1ffu));
        cw[3] = (int32_t)0x27000000u;
        cw[4] = (int32_t)0x30000000u;
        cw[5] = (int32_t)(0x27000000u | ((tsize & 0x3ffu) << 14));
        rdp_fifo_append(fifo, cw, 6);
    }
    else if (type == 0x00001033u)       /* G_OBJLT_TXTRBLOCK */
    {
        /* SETTIMG width-1 = tsize>>2 (the block's TMEM word span scaled
         * back to the 16-bit load pitch); without it the LOADBLOCK dxt
         * walks the wrong DRAM lines and nothing reaches TMEM. tsize is the
         * 64-bit-TMEM-word count-1 (GS_TB_TSIZE = GS_PIX2TMEM(pix,siz)-1);
         * the LOADBLOCK lrs is in 16-bit texels, and each TMEM word holds
         * four of them, so the block span is tsize<<2 (cxd4-verified: 399
         * words -> lrs 1596, filling the full tile rather than a quarter). */
        cw[0] = (int32_t)(0x3d100000u | ((tsize >> 2) & 0xfffu));
        cw[1] = (int32_t)image;
        cw[2] = (int32_t)(0x35100000u | (tmem & 0x1ffu));
        cw[3] = (int32_t)0x27000000u;
        cw[4] = (int32_t)0x33000000u;
        cw[5] = (int32_t)(0x27000000u | (((tsize << 2) & 0xfffu) << 12)
                          | (tline & 0xfffu));
        rdp_fifo_append(fifo, cw, 6);
    }
    else if (type == 0x00fc1034u)       /* G_OBJLT_TXTRTILE */
    {
        /* G_OBJLT_TXTRTILE loads the tile with a SETTIMG/SETTILE/LOADTILE
         * triple. The fields decode as: SETTIMG image width-1 = tsize (the
         * GS_TT_TWIDTH texel count); the load tile's line stride in 64-bit
         * TMEM words = (tsize >> 2) + 1; and the LOAD_TILE lower-right S is in
         * 10.2 texel units, so tsize is shifted left 2 (GS_TT_THEIGHT, tline,
         * is already 10.2 and passes through). The original code put tsize>>2
         * in the SETTIMG width, left the load tile's line at 0 and forwarded
         * tsize raw as lrs, so the LOAD_TILE never strode between rows and read
         * a quarter-width strip -- every OBJ sprite sampling the tile drew
         * garbage (Yoshi's Story's level border frame is ~60 of them).
         * cxd4-matched: tsize 11 / tline 83 -> SETTIMG 0x3d10000b,
         * SETTILE 0x35100600, LOAD_TILE 0x2702c053. */
        cw[0] = (int32_t)(0x3d100000u | (tsize & 0xfffu));
        cw[1] = (int32_t)image;
        cw[2] = (int32_t)(0x35100000u | ((((tsize >> 2) + 1u) & 0x1ffu) << 9)
                          | (tmem & 0x1ffu));
        cw[3] = (int32_t)0x27000000u;
        cw[4] = (int32_t)0x34000000u;
        cw[5] = (int32_t)(0x27000000u | (((tsize << 2) & 0xfffu) << 12)
                          | (tline & 0xfffu));
        rdp_fifo_append(fifo, cw, 6);
    }
    return 1;
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
 * 512 8-byte TMEM words). Only CI is halved -- the upper TMEM bank holds
 * its palette. The shipped segment reads 0400 0400 0200 0400 0400; the
 * earlier transcription also halved I, which split Kirby 64's I4 dialog
 * panels into twice as many half-height strips as the RSP draws. */
static const uint16_t s2dex_fmt_tab[5] = {
    0x0400, 0x0400, 0x0200, 0x0400, 0x0400
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
    image_ptr  = gsp_seg_addr_rsp(bg_rd_u32(rdram, bg_addr + 0x10));
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
    s_obj_tile7_used = 1;
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

/* ---- G_BG_COPY ----------------------------------------------------------
 * Transcribed from the S2DEX2 copy-mode handler (IMEM 0x210-0x4d0 in the
 * resident main text). Unlike BG_1CYC, the strip parameters come from the
 * guS2DInitBg-precomputed tmem fields of uObjBg, the loads use LOADBLOCK
 * or LOADTILE per imageLoad, and the texrects are COPY-cycle (inclusive
 * coordinates, dsdx 4.0 from the v30 lane template, 0xE4 opcode byte). */

void s2dex_bg_copy(const unsigned char *rdram, unsigned int rdram_bytes,
                   unsigned int bg_addr, RdpFifo *fifo)
{

    unsigned int image_x, image_w, image_y, image_h;
    int          frame_x, frame_y;
    unsigned int frame_w, frame_h;
    unsigned int image_ptr, image_load, image_fmt, image_siz, image_pal;
    unsigned int tmem_w, tmem_h, tmem_load_sh, tmem_load_th;
    unsigned int tmem_size_w, tmem_size;
    unsigned int image_flip_c;
    unsigned int clip_ulx, clip_uly, draw_w_q, draw_h_q;
    unsigned int copy_clip_t_q;             /* top clip, 10.2 */
    unsigned int settimg_w0, load_w0, load_w1, line_field;
    unsigned int x_phase_s;         /* sub-chunk X offset, s10.5 texels */
    unsigned int siz_add_c;
    unsigned int ptr, t1, rows, adv, s_flip_coord;
    unsigned int clip_skip, x_units;
    int          a0;
    int32_t cw[16];

    if (bg_addr + 0x28u > rdram_bytes)
        return;

    image_x    = bg_rd_u16(rdram, bg_addr + 0x00);
    image_w    = bg_rd_u16(rdram, bg_addr + 0x02);
    frame_x    = (int)(short)bg_rd_u16(rdram, bg_addr + 0x04);
    frame_w    = bg_rd_u16(rdram, bg_addr + 0x06);
    image_y    = bg_rd_u16(rdram, bg_addr + 0x08);
    image_h    = bg_rd_u16(rdram, bg_addr + 0x0a);
    frame_y    = (int)(short)bg_rd_u16(rdram, bg_addr + 0x0c);
    frame_h    = bg_rd_u16(rdram, bg_addr + 0x0e);
    image_ptr  = gsp_seg_addr_rsp(bg_rd_u32(rdram, bg_addr + 0x10));
    image_load = bg_rd_u16(rdram, bg_addr + 0x14);
    image_fmt  = rdram[(bg_addr + 0x16) ^ 3];
    image_siz  = rdram[(bg_addr + 0x17) ^ 3];
    image_pal  = bg_rd_u16(rdram, bg_addr + 0x18);
    image_flip_c = bg_rd_u16(rdram, bg_addr + 0x1a);
    tmem_w       = bg_rd_u16(rdram, bg_addr + 0x1c);
    tmem_h       = bg_rd_u16(rdram, bg_addr + 0x1e);
    tmem_load_sh = bg_rd_u16(rdram, bg_addr + 0x20);
    tmem_load_th = bg_rd_u16(rdram, bg_addr + 0x22);
    tmem_size_w  = bg_rd_u16(rdram, bg_addr + 0x24);
    tmem_size    = bg_rd_u16(rdram, bg_addr + 0x26);

    s_obj_tile7_used = 1;
    if (tmem_h == 0u || tmem_w == 0u)
        return;
    siz_add_c = s2dex_siz_tab[image_siz][0];

    /* scissor intersection (IMEM 0x1b4-0x204); copy mode is 1:1 so the
     * clip works in 10.2 directly. */
    {
        int clip_l, clip_t, clip_r, clip_b, draw_w, draw_h;
        clip_l = (int)s_scis_ulx - frame_x;
        if (clip_l < 0) clip_l = 0;
        clip_t = (int)s_scis_uly - frame_y;
        if (clip_t < 0) clip_t = 0;
        clip_r = frame_x + (int)frame_w - (int)s_scis_lrx;
        if (clip_r < 0) clip_r = 0;
        clip_b = frame_y + (int)frame_h - (int)s_scis_lry;
        if (clip_b < 0) clip_b = 0;
        draw_w = (int)frame_w - clip_l - clip_r;
        draw_h = (int)frame_h - clip_t - clip_b;
        if (draw_w < 1 || draw_h < 1)
            return;
        clip_ulx = (unsigned int)(frame_x + clip_l) & 0xfffu;
        clip_uly = (unsigned int)(frame_y + clip_t) & 0xfffu;
        draw_w_q = (unsigned int)draw_w & 0xfffcu;
        draw_h_q = (unsigned int)draw_h & 0xfffcu;

        /* image start offset from the scissored top-left (IMEM 0x278-0x2c4):
         * rows via tmemSizeW * clipped pixel rows, the X part via the
         * per-size 0x800<<siz lane fold, then halved and 8-byte scaled. */
        copy_clip_t_q = (unsigned int)clip_t;
        /* X units (4-byte) from the scissored left clip plus the imageX
         * scroll (u10.5): both advance the pointer, skew the row loads,
         * and so both contribute to the seam reservation. */
        x_units = ((((unsigned int)clip_l) << image_siz) >> 5)
                + (((unsigned int)image_x << image_siz) >> 8);
        /* the sub-chunk part of the X offset that the 8-byte-aligned
         * pointer skip can't absorb becomes the strips' S coordinate,
         * and the row loads widen by the same amount: a 3-texel left
         * clip of an RGBA16 frame copy loads texels 0..302 and draws
         * with S = 3.0 (lrs 0x4bb / s 0x60 against the RSP). */
        x_phase_s = ((((unsigned int)clip_l) << 3)
                     + (unsigned int)image_x) & siz_add_c;
        {
            /* the imageY scroll (u10.5, integral rows) adds to the
             * clipped top: it advances the start pointer and shrinks the
             * image-side row budget identically (traced: a2 = rows<<2,
             * v0 = base + tmemSizeW*rows*4) */
            unsigned int y_rows = (image_y >> 5) & 0x3ffu;
            unsigned int raw = tmem_size_w
                * ((((unsigned int)clip_t) >> 2) + y_rows)
                + x_units;
            clip_skip = (raw >> 1) << 3;
            copy_clip_t_q += y_rows << 2;
        }
    }

    /* preamble (IMEM 0x208-0x264): SETTILE 7 (line only in LOADTILE mode),
     * SETTILESIZE 0, SETTILE 0 with the render line and palette. */
    line_field = (tmem_w << 9) & 0x3fe00u;
    if (!(image_load & 0x8000u))
        line_field = 0u;                    /* LOADBLOCK: linear, line 0 */
    else
        line_field = (tmem_w << 9) & 0x3fe00u;
    /* the AND against the sign-extended imageLoad high byte: 0xff keeps
     * the line (LOADTILE 0xfff4), 0x00 clears it (LOADBLOCK 0x0033) */
    {
        unsigned int hib = (image_load >> 8) & 0xffu;
        line_field = ((tmem_w << 9) & ((hib & 0x80u) ? 0xffffffffu : 0u))
                   & 0x3fe00u;
    }
    cw[0] = (int32_t)(0x35100000u | line_field);
    cw[1] = (int32_t)0x27000000u;
    cw[2] = (int32_t)0x32000000u;
    cw[3] = 0;
    cw[4] = (int32_t)(0x35000000u | ((image_fmt & 7u) << 21) |
                      ((image_siz & 3u) << 19) | ((tmem_w << 9) & 0x3fe00u));
    cw[5] = (int32_t)(((image_pal & 0xfu) << 20) | S2DEX_SETTILE_W1);
    rdp_fifo_append(fifo, cw, 6);

    /* per-strip constants (IMEM 0x324-0x37c). The strip loads always
     * address the image as 16-bit RGBA texels regardless of the image's
     * own format (the render tile set above carries the real fmt/siz);
     * the drawn width therefore folds through the texel size into
     * 16-bit units for the LOADTILE right edge. Verified against the
     * RSP for the CI8 backdrops in Kirby 64 (settimg 3d100097 /
     * lrs 0x257 for a 300px CI8 strip, not the image-sized 3d580097 /
     * 0x4af). */
    settimg_w0 = 0x3d100000u | ((tmem_size_w * 2u - 1u) & 0xfffu);
    if (image_load & 0x8000u)
    {
        /* LOADTILE: lrs is computed from the drawn width (IMEM 0x338-0x340,
         * v10[4]*4-1) in 16-bit texel units, lrt comes from the struct */
        load_w0 = 0xf4000000u;
        load_w1 = (((((((draw_w_q + (x_phase_s >> 3)) << image_siz) >> 2)
                      - 1u) & 0xfffu)
                    | 0x7000u) << 12)
                | tmem_load_th;
        adv     = (tmem_size << 16) | tmem_load_sh;
    }
    else
    {
        /* LOADBLOCK */
        load_w0 = 0x33000000u;
        load_w1 = (((tmem_load_sh | 0x7000u) << 12) | tmem_load_th);
        adv     = tmem_size;
    }

    /* horizontal flip mirrors about the inclusive right edge: S becomes
     * -(drawW-0.25px) in s10.5 into the always-mirrored render tile */
    s_flip_coord = (image_flip_c & 1u)
                 ? ((0u - (draw_w_q - 1u) * 8u) & 0xffffu)
                 : (x_phase_s & 0xffffu);

    ptr  = image_ptr + clip_skip;
    t1   = clip_uly;
    rows = tmem_h;

    /* row budget (IMEM image 0x2f4-0x364, live +0x80): a0 = rows the image
     * can supply past the clipped start, capped by the frame's drawn rows;
     * the difference wraps. An X skip makes every row load cross into the
     * next row, so the final supplied row would read past the image -- it
     * is reserved (a0 -= 4) and drawn by the seam block below. */
    a0 = (int)(image_h & 0xfffcu) - (int)copy_clip_t_q;
    if (a0 <= 0)
        return;
    {
        int at = (int)draw_h_q;
        if (x_units != 0u)
            a0 -= 4;
        if (a0 - at > 0)
            a0 = at;
        at -= a0;

    while (a0 > 0)
    {
        unsigned int n = rows;
        unsigned int w1 = load_w1;
        unsigned int gp;
        a0 -= (int)rows;
        if (a0 < 0)
        {
            /* final partial strip (IMEM 0x384-0x3c8): shrink the rows by
             * the 10.2 overshoot and rebuild the load. LOADBLOCK rebuilds
             * lrs from the shrunk byte advance ((adv-2 | 0xe000) << 11
             * carries the tile-7 bits); LOADTILE only shortens lrt. */
            int32_t over = (int32_t)tmem_size_w * a0;   /* bytes, negative */
            n = (unsigned int)((int)rows + a0);
            if (n == 0u)
                break;
            adv = (unsigned int)((int32_t)adv + over);
            if (image_load & 0x8000u)
                w1 = (load_w1 & 0xfffff000u) | ((n - 1u) & 0xfffu);
            else
                w1 = (((adv - 2u) | 0xe000u) << 11) | tmem_load_th;
        }
        gp = t1 + n - 1u;

        cw[0] = (int32_t)0x26000000u;       /* loadsync */
        cw[1] = 0;
        cw[2] = (int32_t)settimg_w0;
        cw[3] = (int32_t)ptr;
        cw[4] = (int32_t)load_w0;
        cw[5] = (int32_t)w1;
        cw[6] = (int32_t)0x27000000u;       /* pipesync */
        cw[7] = 0;
        cw[8] = (int32_t)(0xe4000000u |
                (((clip_ulx + draw_w_q - 1u) & 0xfffu) << 12) |
                (gp & 0xfffu));
        cw[9] = (int32_t)(((clip_ulx & 0xfffu) << 12) | (t1 & 0xfffu));
        cw[10] = (int32_t)(s_flip_coord << 16);     /* s, t */
        cw[11] = (int32_t)0x10000400u;       /* copy dsdx 4.0 */
        rdp_fifo_append(fifo, cw, 12);

        t1   = gp + 1u;
        ptr += adv;
    }

    /* Y-wrap seam (IMEM image 0x418-0x4d0): while frame rows remain, draw
     * the reserved row with a split load -- the in-bounds piece from the
     * current pointer into TMEM 0, and x_units words from the image base
     * appended after it -- then continue from the wrapped image top. */
    if (at > 0)
    {
        if (x_units != 0u)
        {
        unsigned int s5a = (tmem_size_w >> 1) - (x_units >> 1);
        cw[0]  = (int32_t)0x26000000u;      /* loadsync */
        cw[1]  = 0;
        cw[2]  = (int32_t)settimg_w0;
        cw[3]  = (int32_t)ptr;
        cw[4]  = (int32_t)0x35100000u;      /* settile 7 at TMEM 0 */
        cw[5]  = (int32_t)0x06000000u;
        cw[6]  = (int32_t)0x33000000u;
        cw[7]  = (int32_t)((((s5a << 14) - 0x1000u) | 0x06000000u));
        cw[8]  = (int32_t)0x27000000u;      /* pipesync */
        cw[9]  = 0;
        rdp_fifo_append(fifo, cw, 10);
        cw[0]  = (int32_t)settimg_w0;
        cw[1]  = (int32_t)image_ptr;
        cw[2]  = (int32_t)(0x35100000u | (s5a & 0x1ffu));
        cw[3]  = (int32_t)0x06000000u;
        cw[4]  = (int32_t)0x33000000u;
        cw[5]  = (int32_t)(((((x_units >> 1) << 14) - 0x1000u)
                            | 0x06000000u));
        cw[6]  = (int32_t)0x27000000u;      /* pipesync */
        cw[7]  = 0;
        cw[8]  = (int32_t)(0xe4000000u |
                (((clip_ulx + draw_w_q - 1u) & 0xfffu) << 12) |
                (t1 & 0xfffu));
        cw[9]  = (int32_t)(((clip_ulx & 0xfffu) << 12) | (t1 & 0xfffu));
        cw[10] = (int32_t)(s_flip_coord << 16);
        cw[11] = (int32_t)0x10000400u;
        rdp_fifo_append(fifo, cw, 12);
        t1 += 4u;
        at -= 4;
        }
        if (at <= 0)
            goto wrap_done;
        /* continue from the wrapped image top: the seam block halved the
         * X units (image 0x444 srl), and the rejoin restages the advance
         * from the struct (image 0x368 lhu) */
        ptr = image_ptr + (x_units >> 1) * 8u;
        adv = (image_load & 0x8000u) ? ((tmem_size << 16) | tmem_load_sh)
                                     : tmem_size;
        a0  = at;
        while (a0 > 0)
        {
            unsigned int n2 = rows;
            unsigned int w12 = load_w1;
            unsigned int gp2;
            a0 -= (int)rows;
            if (a0 < 0)
            {
                int32_t over2 = (int32_t)tmem_size_w * a0;
                n2 = (unsigned int)((int)rows + a0);
                if (n2 == 0u)
                    break;
                adv = (unsigned int)((int32_t)adv + over2);
                if (image_load & 0x8000u)
                    w12 = (load_w1 & 0xfffff000u) | ((n2 - 1u) & 0xfffu);
                else
                    w12 = (((adv - 2u) | 0xe000u) << 11) | tmem_load_th;
            }
            gp2 = t1 + n2 - 1u;
            cw[0] = (int32_t)0x26000000u;
            cw[1] = 0;
            cw[2] = (int32_t)settimg_w0;
            cw[3] = (int32_t)ptr;
            cw[4] = (int32_t)load_w0;
            cw[5] = (int32_t)w12;
            cw[6] = (int32_t)0x27000000u;
            cw[7] = 0;
            cw[8] = (int32_t)(0xe4000000u |
                    (((clip_ulx + draw_w_q - 1u) & 0xfffu) << 12) |
                    (gp2 & 0xfffu));
            cw[9] = (int32_t)(((clip_ulx & 0xfffu) << 12) | (t1 & 0xfffu));
            cw[10] = (int32_t)(s_flip_coord << 16);
            cw[11] = (int32_t)0x10000400u;
            rdp_fifo_append(fifo, cw, 12);
            t1   = gp2 + 1u;
            ptr += adv;
        }
        /* a single wrap pass, matching the LLE stream */
    }
wrap_done:;
    }
}

/* ======================================================================== *
 *  S2DEX (GBI 1) object matrix + sprite/rectangle rendering.
 *
 *  Standalone S2DEX 1.xx games (e.g. Yoshi's Story) drive the whole frame
 *  with the sprite command set rather than switching to it for a single BG
 *  rect the way the Zelda engine does. The object commands draw a uObjSprite
 *  (gs2dex.h, 24 bytes) as a screen-space textured quad transformed by the
 *  2D object matrix (G_OBJ_MOVEMEM, default identity). With an identity (or
 *  pure-scale) matrix the quad is axis aligned, so it maps directly onto a
 *  single RDP TEXTURE_RECTANGLE; the render tile is set from the sprite's
 *  own fmt/siz/stride/TMEM-address/palette (gSPSetSpriteTile).
 *
 *  Coordinate math follows the decode in GLideN64's gSPObjSprite (constants
 *  from S2DEXCoordCorrector, the v1.06 / non-1.3 default-rendermode lane:
 *  A1 = 0, A3 = -2, B0 = 0xfffffffc, B3 = 1):
 *      ulx = objX + A3 ; uly = objY + A3
 *      lrx = (((imageW - A1) << 8) * (0x80007fff / scaleW)) >> 32 + ulx
 *      lry = (((imageH - A1) << 8) * (0x80007fff / scaleH)) >> 32 + uly
 *      x0  = (mtxX + B3) & B0   (0 for identity) ; screen = x0 + mtx*corner
 *  TEXRECT s/t start at 0; the per-pixel texel step is the sprite scale
 *  itself (dsdx = scaleW, dtdy = scaleH; u5.10 == s5.10 at 1.0 = 0x400).
 * ======================================================================== */

/* DMEM object matrix (uObjMtx_t): A,B,C,D s15.16, X,Y s10.2, BaseScale u5.10.
 * Reset to identity at task start; G_OBJ_MOVEMEM (0xdc) overwrites it. */
static int          s_objmtx_a = 0x00010000, s_objmtx_b = 0;
static int          s_objmtx_c = 0,          s_objmtx_d = 0x00010000;
static int          s_objmtx_x = 0,          s_objmtx_y = 0;
static int          s_objmtx_bsx = 0x0400,   s_objmtx_bsy = 0x0400;

void s2dex1_reset(void)
{
    s_objmtx_a = 0x00010000; s_objmtx_b = 0;
    s_objmtx_c = 0;          s_objmtx_d = 0x00010000;
    s_objmtx_x = 0;          s_objmtx_y = 0;
    s_objmtx_bsx = 0x0400;   s_objmtx_bsy = 0x0400;
}

void s2dex_obj_movemem(const unsigned char *r, unsigned int rdram_bytes,
                       unsigned int w0, unsigned int addr)
{
    /* gSPObjMatrix encodes w0 = (cmd<<24)|(dmasize-1<<16)|(dmem_offset):
     * a full uObjMtx load targets DMEM offset 0; a sub-matrix update
     * (gSPObjSubMatrix) targets offset 0x10 and ships only X,Y,BaseScale. */
    unsigned int dmem_off = w0 & 0xffffu;
    if (addr + 24u > rdram_bytes)
        return;
    if (dmem_off == 0u)
    {
        s_objmtx_a = (int)bg_rd_u32(r, addr + 0x00);
        s_objmtx_b = (int)bg_rd_u32(r, addr + 0x04);
        s_objmtx_c = (int)bg_rd_u32(r, addr + 0x08);
        s_objmtx_d = (int)bg_rd_u32(r, addr + 0x0c);
        s_objmtx_x   = (int)(short)bg_rd_u16(r, addr + 0x10);
        s_objmtx_y   = (int)(short)bg_rd_u16(r, addr + 0x12);
        s_objmtx_bsx = (int)bg_rd_u16(r, addr + 0x14);
        s_objmtx_bsy = (int)bg_rd_u16(r, addr + 0x16);
    }
    else
    {
        s_objmtx_x   = (int)(short)bg_rd_u16(r, addr + 0x00);
        s_objmtx_y   = (int)(short)bg_rd_u16(r, addr + 0x02);
        s_objmtx_bsx = (int)bg_rd_u16(r, addr + 0x04);
        s_objmtx_bsy = (int)bg_rd_u16(r, addr + 0x06);
    }
}

/* OBJ screen/texture coordinates, per olivieryuyu's decoding of the S2DEX
 * microcode. Both gSPObjRectangle(_R) and the BaseScale form of gSPObjSprite
 * build their rect this way: a render-mode corrector biases the edges and the
 * per-axis reciprocal is 0x80007FFF/scale. Outputs are s10.2 screen (xh/yh/
 * xl/yl) and s10.5 texture origin (sh/th); dsdx/dtdy are BaseScaleX/Y. */
static void s2dex_obj_coords(int use_matrix, int objX, int objY,
                             unsigned int imageW, unsigned int imageH,
                             unsigned int scaleW, unsigned int scaleH,
                             short *xh_o, short *yh_o, short *xl_o, short *yl_o,
                             short *sh_o, short *th_o,
                             unsigned int *bsx_o, unsigned int *bsy_o)
{
    static const unsigned int A01[8] = {
        0x00000000u, 0x00100020u, 0x00200040u, 0x00300060u,
        0x0000FFF4u, 0x00100014u, 0x00200034u, 0x00300054u };
    static const unsigned int A23[4] = {
        0x0001FFFEu, 0xFFFEFFFEu, 0x00010000u, 0x00000000u };
    static const unsigned int B03[4] = {
        0xFFFC0000u, 0x00000001u, 0xFFFF0003u, 0xFFF00000u };
    unsigned int rm = s_obj_rendermode;
    unsigned int O1 = (rm & 0x70u) >> 3;   /* SHRINK1|SHRINK2|WIDEN */
    unsigned int O2 = (rm & 0x18u) >> 2;   /* SHRINK1|BILERP */
    unsigned int O3 = (rm & 0x08u) >> 1;   /* BILERP */
    int A0 = S2DEX_S16AT(A01, (0u + O1) ^ 1u);
    int A1 = S2DEX_S16AT(A01, (1u + O1) ^ 1u);
    int A2 = S2DEX_S16AT(A23, (0u + O2) ^ 1u);
    int B0 = S2DEX_S16AT(B03, (0u + O3) ^ 1u);
    int B2 = S2DEX_S16AT(B03, (2u + O3) ^ 1u);
    unsigned int sprW = scaleW ? scaleW : 1u;
    unsigned int sprH = scaleH ? scaleH : 1u;
    unsigned int bsx  = (unsigned int)(s_objmtx_bsx ? s_objmtx_bsx : 1);
    unsigned int bsy  = (unsigned int)(s_objmtx_bsy ? s_objmtx_bsy : 1);
    short xh, xl, yh, yl, sh, th;

    if (use_matrix)
    {
        unsigned int swe = ((unsigned int)bsx * 0x40u * sprW) >> 16;
        unsigned int she = ((unsigned int)bsy * 0x40u * sprH) >> 16;
        int xhp, xlp, yhp, ylp;
        if (!swe) swe = 1u;
        if (!she) she = 1u;
        xhp = (int)((((long long)objX << 16) * 0x0800
                    * (0x80007FFFu / bsx)) >> 32)
            + (((s_objmtx_x + A2) & B0) << 16);
        xh  = (short)(xhp >> 16);
        xlp = xhp + (int)((((long long)(imageW - A1) << 24)
                    * (0x80007FFFu / swe)) >> 32);
        xl  = (short)(xlp >> 16);
        yhp = (int)((((long long)objY << 16) * 0x0800
                    * (0x80007FFFu / bsy)) >> 32)
            + (((s_objmtx_y + A2) & B0) << 16);
        yh  = (short)(yhp >> 16);
        ylp = yhp + (int)((((long long)(imageH - A1) << 24)
                    * (0x80007FFFu / she)) >> 32);
        yl  = (short)(ylp >> 16);
        sh  = (short)(A0 + B2);
        th  = (short)(sh - (int)(((yh & 3) * 0x0200 * she) >> 16));
    }
    else
    {
        xh = (short)((objX + A2) & B0);
        xl = (short)((int)((((unsigned long long)(imageW - A1) << 24)
              * (0x80007FFFu / sprW)) >> 48) + xh);
        yh = (short)((objY + A2) & B0);
        yl = (short)((int)((((unsigned long long)(imageH - A1) << 24)
              * (0x80007FFFu / sprH)) >> 48) + yh);
        sh = (short)(A0 + B2);
        th = (short)(sh - (int)(((yh & 3) * 0x0200 * sprH) >> 16));
    }

    *xh_o = xh; *yh_o = yh; *xl_o = xl; *yl_o = yl;
    *sh_o = sh; *th_o = th; *bsx_o = bsx; *bsy_o = bsy;
}

/* draw a uObjSprite at addr as a TEXTURE_RECTANGLE. use_matrix applies the
 * object matrix translation/scale (gSPObjSprite); gSPObjRectangle passes 0. */
static void s2dex_draw_obj(GSPState *gsp, const unsigned char *r,
                           unsigned int rdram_bytes,
                           unsigned int addr, int use_matrix, int as_rect,
                           RdpFifo *fifo)
{
    int          objX, objY;
    unsigned int scaleW, scaleH, imageW, imageH, imageStride, imageAdrs;
    unsigned int imageFmt, imageSiz, imagePal;
    int          ulx, uly, lrx, lry;
    int          sax, say, w_q, h_q;
    unsigned int tw, th, lrs, lrt;
    unsigned int settile_w0, settile_w1, settsz_w1;
    int32_t      cw[6];
    int32_t      tribuf[220];
    int          nc;

    if (addr + 24u > rdram_bytes)
        return;

    objX        = (int)(short)bg_rd_u16(r, addr + 0x00);
    scaleW      =            bg_rd_u16(r, addr + 0x02);
    imageW      =            bg_rd_u16(r, addr + 0x04);
    objY        = (int)(short)bg_rd_u16(r, addr + 0x08);
    scaleH      =            bg_rd_u16(r, addr + 0x0a);
    imageH      =            bg_rd_u16(r, addr + 0x0c);
    imageStride =            bg_rd_u16(r, addr + 0x10);
    imageAdrs   =            bg_rd_u16(r, addr + 0x12);
    imageFmt    = r[(addr + 0x14) ^ 3];
    imageSiz    = r[(addr + 0x15) ^ 3];
    imagePal    = r[(addr + 0x16) ^ 3];
    /* imageFlags (FLIPS/FLIPT) at +0x17 -- TODO */

    if (scaleW == 0u) scaleW = 1u;
    if (scaleH == 0u) scaleH = 1u;

    /* cxd4-derived screen mapping for an S2DEX OBJ sprite (verified
     * bit-exact against the LLE RSP for Yoshi's Story: the "Nintendo
     * PRESENTS" logo lands at xh=384, xl=892, yh=383, yl=579).
     *
     * The object matrix translation (X,Y) is the sprite's BOTTOM-LEFT
     * screen anchor in s10.2. objX runs right; objY runs UP -- the OBJ
     * Y axis is inverted relative to the screen -- so objY subtracts from
     * the anchor. The drawn extent is the sprite's texel size scaled by
     * its own u5.10 scale: extent_s10.2 = (imageU10.5 * scaleU5.10) >> 13,
     * less one screen pixel (the RSP's inclusive right/bottom edge).
     *
     * The 2x2 part (A) carries the (square, unrotated) zoom Yoshi uses;
     * B/C shear and a distinct D are not exercised by the test content and
     * are left unmodelled (identity A == 0x00010000 reduces to objX/objY).
     */
    if (use_matrix)
    {
        int say_d = (s_objmtx_d < 0) ? -s_objmtx_d : s_objmtx_d;
        sax = s_objmtx_x + (int)(((long long)objX * s_objmtx_a) >> 16);
        say = s_objmtx_y - (int)(((long long)objY * say_d) >> 16);
    }
    else
    {
        sax = objX;
        say = -objY;
    }

    /* The OBJ sprite's scaleW/scaleH are an inverse texel step (u5.10 dsdx),
     * not a forward multiplier: a SMALLER scale draws a LARGER sprite. The
     * drawn span is (texels - 1) (the RDP's inclusive right/bottom edge drops
     * the last texel) divided by that step: screen_px = (texels-1)*1024/scale,
     * texels = imageW>>5; in 10.2 screen units that is ((imageW>>5)-1)*4096
     * /scale. This reduces to the old (imageW*scale>>13)-4 at scale==0x400
     * (unity), so unity-scale sprites -- the scene, titles, the matrix-zoomed
     * enemies -- stay stream-identical, while sub-unity fills (Yoshi's Story's
     * pause panel, built from 0x44/0x55-scale sprites) take their true extent
     * instead of collapsing to a few pixels. cxd4-derived: imageW 512, scaleW
     * 0x44, A unity -> 232 px; scaleH 0x55, |D| 0.449 -> 81 px (both exact). */
    {
        int wt = ((int)imageW >> 5) - 1;
        int ht = ((int)imageH >> 5) - 1;
        w_q = (wt > 0) ? (wt * 4096) / (int)scaleW : 0;
        h_q = (ht > 0) ? (ht * 4096) / (int)scaleH : 0;
    }
    /* The object matrix scales the sprite's extent, not just its anchor: a
     * gSPObjSprite zoomed by A/D (e.g. Yoshi's Story enemies at A=0x5999,
     * D=-0x5999 ~ 0.35x) must shrink the drawn quad by the same factor, or
     * the full-size texel span is stretched across the zoomed screen rect
     * and the sprite smears. Titles that draw at unity (A=0x10000) are
     * unaffected. Use |A| for width and |D| for height (B/C unmodelled). */
    if (use_matrix)
    {
        int sa = (s_objmtx_a < 0) ? -s_objmtx_a : s_objmtx_a;
        int sd = (s_objmtx_d < 0) ? -s_objmtx_d : s_objmtx_d;
        w_q = (int)(((long long)w_q * sa) >> 16);
        h_q = (int)(((long long)h_q * sd) >> 16);
    }
    if (w_q < 0) w_q = 0;
    if (h_q < 0) h_q = 0;

    ulx = sax;                  /* left   */
    lrx = sax + w_q;            /* right  */
    lry = say;                  /* bottom */
    uly = say - h_q;            /* top    */

    if (lrx <= ulx || lry <= uly)
        return;


    /* Screen-space sprite corners are passed straight to the triangle
     * rasterizer as s10.2; off-screen-negative upper-left corners are left
     * unclamped (a TEXRECT's 12-bit unsigned field would have to clamp, but
     * a triangle's signed edge coordinates do not, and the RDP scissor trims
     * the off-screen part). */

    tw = (imageW >> 5); if (tw == 0u) tw = 1u;
    th = (imageH >> 5); if (th == 0u) th = 1u;
    lrs = (tw - 1u) << 2;
    lrt = (th - 1u) << 2;

    /* SET_TILE (render tile 0) from the sprite's image attributes. */
    settile_w0 = 0xf5000000u | ((imageFmt & 7u) << 21) | ((imageSiz & 3u) << 19)
               | ((imageStride & 0x1ffu) << 9) | (imageAdrs & 0x1ffu);
    /* tile 0, palette, clamp S and T (cm = G_TX_CLAMP = 2). */
    settile_w1 = ((imagePal & 0xfu) << 20) | (2u << 18) | (2u << 8);
    /* SET_TILE_SIZE w1: tile 0, lrs/lrt (10.2). */
    settsz_w1  = ((lrs & 0xfffu) << 12) | (lrt & 0xfffu);

    cw[0] = (int32_t)settile_w0;
    cw[1] = (int32_t)settile_w1;
    cw[2] = (int32_t)0xf2000000u;               /* SET_TILE_SIZE w0 (uls=ult=0) */
    cw[3] = (int32_t)settsz_w1;
    rdp_fifo_append(fifo, cw, 4);

    /* gSPObjRectangle / gSPObjRectangleR build a TextureRectangle from the
     * uObjSprite and hand it straight to the RDP (per the S2DEX manual): in
     * copy / 1-2 cycle mode the rectangle is a TEXRECT, not a shaded quad.
     * Yoshi's Story's pause dialog draws its panel text this way (RECTANGLE_R
     * with a per-line OBJ matrix). The texel step per screen pixel is the
     * sprite scale divided by the matrix zoom: dsdx = scaleW / |A| in s5.10,
     * matching the LLE stream (scaleW 0x400 / A 0xb3ca -> 0x05b2). */
    if (as_rect)
    {
        short xh, xl, yh, yl, sh, th_t;
        unsigned int bsx, bsy, u_ulx, u_uly, u_lrx, u_lry;
        s2dex_obj_coords(use_matrix, objX, objY, imageW, imageH,
                         scaleW, scaleH, &xh, &yh, &xl, &yl, &sh, &th_t,
                         &bsx, &bsy);
        u_ulx = (unsigned int)xh & 0xfffu;
        u_uly = (unsigned int)yh & 0xfffu;
        u_lrx = (unsigned int)xl & 0xfffu;
        u_lry = (unsigned int)yl & 0xfffu;
        cw[0] = (int32_t)(0x24000000u | (u_lrx << 12) | u_lry);
        cw[1] = (int32_t)((u_ulx << 12) | u_uly);   /* tile 0 */
        cw[2] = (int32_t)((((unsigned int)sh & 0xffffu) << 16)
                          | ((unsigned int)th_t & 0xffffu));
        cw[3] = (int32_t)(((bsx & 0xffffu) << 16) | (bsy & 0xffffu));
        rdp_fifo_append(fifo, cw, 4);
        return;
    }

    /* Emit the sprite as two shaded textured triangles, the way the S2DEX
     * RSP microcode does (a TEXRECT cannot supply the SHADE the sprite's
     * combiner multiplies by, so a texrect comes out black). Texcoords run
     * 0..imageW / 0..imageH in S10.5 across the quad; the shade is flat
     * white so TEXEL*SHADE == TEXEL. */
    /* The triangle bridge's affine-W normalisation lands the per-pixel S/T
     * gradient at a quarter of the texel rate (the I4 sampler advances one
     * texel every four pixels with the raw U10.5 extent), mapping only the
     * texture's left/top quarter across the quad. Scaling the extent by 4
     * brings the sample rate to 1 texel/pixel, cxd4-matched (dsdx 8 -> 32). */
    /* The object Y axis is inverted (the matrix anchor is the sprite's
     * bottom, objY counts up), so the screen-top corner samples the image
     * bottom and the screen-bottom corner the image top: T runs t_ext at
     * uly down to 0 at lry. Mapping T=0 to the screen top instead drew the
     * sprite upside down. */
    {
        int s_ext = (int)(imageW << 2);
        int t_ext = (int)(imageH << 2);
        s2dex_set_corner(&gsp->vtx[S2DEX_SPR_V0 + 0], ulx, uly, 0,     t_ext);
        s2dex_set_corner(&gsp->vtx[S2DEX_SPR_V0 + 1], lrx, uly, s_ext, t_ext);
        s2dex_set_corner(&gsp->vtx[S2DEX_SPR_V0 + 2], ulx, lry, 0,     0);
        s2dex_set_corner(&gsp->vtx[S2DEX_SPR_V0 + 3], lrx, lry, s_ext, 0);
    }

    nc = gsp_triangle(gsp, tribuf, S2DEX_SPR_V0 + 0, S2DEX_SPR_V0 + 2,
                      S2DEX_SPR_V0 + 1, 1, 0);
    if (nc > 0) rdp_fifo_append(fifo, tribuf, nc);
    nc = gsp_triangle(gsp, tribuf, S2DEX_SPR_V0 + 1, S2DEX_SPR_V0 + 2,
                      S2DEX_SPR_V0 + 3, 1, 0);
    if (nc > 0) rdp_fifo_append(fifo, tribuf, nc);
}

void s2dex_obj_sprite(GSPState *gsp, const unsigned char *r,
                      unsigned int rdram_bytes,
                      unsigned int addr, RdpFifo *fifo)
{
    s2dex_draw_obj(gsp, r, rdram_bytes, addr, 1, 0, fifo);
}

void s2dex_obj_rectangle_r(GSPState *gsp, const unsigned char *r,
                           unsigned int rdram_bytes,
                           unsigned int addr, RdpFifo *fifo)
{
    s2dex_draw_obj(gsp, r, rdram_bytes, addr, 1, 1, fifo);
}

void s2dex_obj_rectangle(GSPState *gsp, const unsigned char *r,
                         unsigned int rdram_bytes,
                         unsigned int addr, RdpFifo *fifo)
{
    s2dex_draw_obj(gsp, r, rdram_bytes, addr, 0, 0, fifo);
}
