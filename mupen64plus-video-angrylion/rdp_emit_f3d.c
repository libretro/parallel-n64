#include <stdio.h>
#include <stdlib.h>
/* rdp_emit_f3d.c -- F3D (Fast3D / "RSP SW 2.0X") display-list dispatcher for
 * the angrylion HLE path. See rdp_emit_f3d.h.
 *
 * Self-contained: keeps its own segment table / RDRAM pointer / other-modes
 * mirror so it never perturbs the F3DEX2 dispatcher's state (SM64 and the
 * F3DEX2 titles never run in the same task). Geometry is routed to the shared
 * frontend (gsp_*) and the RDP command FIFO (rdp_fifo_append) exactly as the
 * F3DEX2 path does; only the command decode differs.
 *
 * Opcode/argument layout follows the documented F3D microcode (the same decode
 * the in-tree gln64 F3D path uses); no external plugin code is linked.
 *
 * Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_f3d.h"
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_bridge.h"
#include "rdp_emit_frontend.h"
#include "rdp_emit_rsp.h"

/* ---- F3D command bytes (high byte of w0) ---------------------------------*/
#define F3D_MTX                0x01
#define F3D_MOVEMEM            0x03
#define F3D_VTX                0x04
#define F3D_DL                 0x06
#define F3D_TRI2               0xB1   /* Doom 64 ucode: gSP1Quadrangle, 2 tris */
#define F3D_RDPHALF_CONT       0xB2
#define F3D_RDPHALF_2          0xB3
#define F3D_RDPHALF_1          0xB4
#define F3D_LINE3D             0xB5   /* a.k.a. G_QUAD on F3D variants */
#define F3D_CLEARGEOMETRYMODE  0xB6
#define F3D_SETGEOMETRYMODE    0xB7
#define F3D_ENDDL              0xB8
#define F3D_SETOTHERMODE_L     0xB9
#define F3D_SETOTHERMODE_H     0xBA
#define F3D_TEXTURE            0xBB
#define F3D_MOVEWORD           0xBC
#define F3D_POPMTX             0xBD
#define F3D_CULLDL             0xBE
#define F3D_TRI1               0xBF

/* G_MTX param bits (F3D: NOT inverted, unlike F3DEX2's push bit) */
#define F3D_MTX_PROJECTION     0x01
#define F3D_MTX_LOAD           0x02
#define F3D_MTX_PUSH           0x04

/* G_DL sub-op (w0 >> 16) */
#define F3D_DL_PUSH            0x00
#define F3D_DL_NOPUSH          0x01

/* G_MOVEMEM index (w0 >> 16) */
#define F3D_MV_VIEWPORT        0x80
#define F3D_MV_LOOKATY         0x82
#define F3D_MV_LOOKATX         0x84
#define F3D_MV_L0              0x86
#define F3D_MV_MATRIX_1        0x9E   /* gSPForceMatrix: bytes  0..15 */
#define F3D_MV_MATRIX_2        0x98   /*                 bytes 16..31 */
#define F3D_MV_MATRIX_3        0x9A   /*                 bytes 32..47 */
#define F3D_MV_MATRIX_4        0x9C   /*                 bytes 48..63 */

/* G_MOVEWORD index (w0 & 0xff) */
#define F3D_MW_MATRIX          0x00
#define F3D_MW_NUMLIGHT        0x02
#define F3D_MW_CLIP            0x04
#define F3D_MW_SEGMENT         0x06
#define F3D_MW_FOG             0x08
#define F3D_MW_LIGHTCOL        0x0a
#define F3D_MW_POINTS          0x0c
#define F3D_MW_PERSPNORM       0x0e

#define F3D_DL_MAX_DEPTH       32

/* ---- self-contained state ------------------------------------------------*/
static unsigned char *s_rdram;
static unsigned int   s_rdram_size;
static unsigned int   s_seg[16];
static int            s_textured;
static int            s_zbuffered;
static unsigned int   s_othermode_h;
static unsigned int   s_othermode_l;
static int            s_dl_depth;
static unsigned int   s_geom;   /* geometry mode in native F3D bit layout */

/* ---- Wipeout 64 sprite microcode (custom F3DLX-derived ucode) -------------
 * Its 2D content (menu background, text, intro screens) is drawn by a custom
 * opcode triple that no standard walker decodes:
 *     09 0000<len> <addr>   load a <len>=0x18 (24) byte sprite struct @ <addr>
 *     BE 000000   <size>    texrect dsdx/dtdy (0x0400/0x0400)
 *     BD 000000   <pos>     screen position; emits the sprite
 * 0xBD/0xBE collide with F3D G_POPMTX/G_CULLDL, so the sprite path is gated on
 * having just seen the non-standard 0x09 (which no real F3D ucode emits).
 *
 * The struct fields drive the RDP commands (reverse-engineered from the ucode
 * text at ut=0x7d70): word0 = texture image address, word1 = palette/TLUT
 * address, +0x08/+0x0a = height/width, +0x0e = CI format. For the menu every
 * sprite is a 32x32 CI8 tile, so the tile/load/sync setup is identical across
 * sprites and only the two image addresses and the screen rectangle vary. The
 * ucode writes the addresses into SETTIMG raw (no segment indirection -- the
 * routine it calls after each command is the RDP-FIFO flush, not a resolver). */
static int          s_spr_have;          /* a 0x09 struct is loaded */
static unsigned int s_spr_struct[6];     /* the 24-byte struct */
static unsigned int s_spr_size;          /* BE operand (dsdx/dtdy) */
static unsigned int s_spr_flip;          /* BE w0 low 16: mirror flags */

static unsigned int seg_phys(unsigned int w1);

static void f3d_emit_sprite(RdpFifo *fifo, unsigned int pos)
{
    int32_t w[4];
    unsigned int texaddr = seg_phys(s_spr_struct[0]);    /* word0 */
    unsigned int paladdr = seg_phys(s_spr_struct[1]);    /* word1 */
    unsigned int texh  = (s_spr_struct[2] >> 16) & 0xffffu;  /* +0x08 texture height */
    unsigned int width =  s_spr_struct[2]        & 0xffffu;  /* +0x0a width */
    unsigned int disph = (s_spr_struct[3] >> 16) & 0xffffu;  /* +0x0c displayed height */
    unsigned int fmt   = (s_spr_struct[3] >>  8) & 0xffu;    /* +0x0e 2 = CI */
    unsigned int siz   =  s_spr_struct[3]        & 0xffu;    /* +0x0f size selector */
    unsigned int soff  = (s_spr_struct[4] >> 16) & 0xffffu;  /* +0x10 atlas S offset */
    unsigned int toff  =  s_spr_struct[4]        & 0xffffu;  /* +0x12 atlas T offset */

    unsigned int xh = (pos >> 16) & 0xffffu;               /* screen X (10.2) */
    unsigned int yh = pos & 0xffffu;                       /* screen Y (10.2) */
    unsigned int dsdx = (s_spr_size >> 16) & 0xffffu;
    unsigned int dtdy = s_spr_size & 0xffffu;
    /* 0xBE w0 low bytes mirror the sprite: the game-select menu draws its
     * full-screen rippling overlay twice, the second pass with 0x0101 --
     * both axes reversed -- so the two ripple layers interfere. Only the
     * combined form appears in Wipeout's display lists; byte 0 is taken as
     * the S mirror and byte 1 as the T mirror. */
    unsigned int sflip = s_spr_flip & 0x00ffu;
    unsigned int tflip = (s_spr_flip >> 8) & 0xffu;

    unsigned int timgw = (siz == 0u)
                       ? (((texh + 1u) >> 1) - 1u)          /* CI4: 2 texels/byte */
                       : ((texh << (siz - 1u)) - 1u);       /* CI8: 1 texel/byte (texh-1) */
    unsigned int t3    = (siz == 0u) ? 1u : 2u;              /* LOADTILE S pack shift */
    unsigned int s1    = toff;                              /* tile T low  (atlas) */
    unsigned int s2    = toff + disph - 1u;                 /* tile T high (atlas) */
    unsigned int loadsl = soff << t3;                       /* LOADTILE S low  */
    unsigned int loadsh = (soff + width - 1u) << t3;        /* LOADTILE S high */
    /* CI4 packs two texels per byte and the image is loaded through a CI8
     * tile, so an odd atlas S offset puts the tile origin one texel before
     * the glyph; the microcode compensates in the texrect S start (+1 texel),
     * matching the LLE RSP's 0x0020 on every odd-soff glyph. */
    unsigned int ssub = (siz == 0u && (soff & 1u)) ? 0x20u : 0u;
    unsigned int line;                                      /* tile line in 64b words */
    unsigned int fsbyte = ((fmt << 2) | 1u) << 3;           /* texture fmt/siz = 0x48 (CI8) */

    if (siz == 0u)      line = ((width << 2) + 63u) >> 6;    /* 32-bit pack path */
    else if (siz == 1u) line = (width + 7u) >> 3;           /* 8-bit path */
    else                line = ((width << 1) + 7u) >> 3;    /* 16-bit path */

    /* Space Station Silicon Valley's intro build feeds the same 24-byte
     * struct with fmt = 0 / siz = 2: a direct RGBA16 image (the boot
     * logos and intro stills, 320x240, palette word zero). Its routine
     * has no palette or other-modes preamble at all -- one SETTIMG of
     * the whole image and a strip loop of LOADTILE row windows, six
     * rows per strip for a 320-wide image (the full 4 KB of TMEM; no
     * TLUT resides in the high half), each strip drawn 1:1 with S = T =
     * 0 against a render tile whose size is just the strip height.
     * Transcribed from the cxd4 LLE stream of the Take-Two logo. */
    if (fmt == 0u)
    {
        unsigned int bpr = width << 1;                       /* RGBA16 */
        unsigned int max_rows = (bpr != 0u && bpr <= 4096u)
                              ? (4096u / bpr) : disph;
        unsigned int strw = (dsdx != 0u) ? ((width << 12) / dsdx)
                                         : (width << 2);
        unsigned int xl2 = xh + strw;
        unsigned int r0;

        if (max_rows == 0u)
            return;

        /* The routine opens by re-emitting the display list's current
         * other-modes shadow verbatim (no TEXTLUT force, no write-back
         * -- unlike the CI routine below). */
        w[0] = (int32_t)(0xef000000u | (s_othermode_h & 0x00ffffffu));
        w[1] = (int32_t)s_othermode_l;
        rdp_fifo_append(fifo, w, 2);

        for (r0 = 0u; r0 < disph; r0 += max_rows)
        {
            unsigned int h = (disph - r0 < max_rows) ? (disph - r0)
                                                     : max_rows;
            unsigned int t0 = toff + r0;
            unsigned int yh_b = yh + (unsigned int)(((uint64_t)r0 << 12)
                                     / (dtdy ? dtdy : 0x400u));
            unsigned int yl_b = yh + (unsigned int)(((uint64_t)(r0 + h) << 12)
                                     / (dtdy ? dtdy : 0x400u));

            /* SETTIMG + the load-tile SETTILE are re-emitted per strip */
            w[0] = (int32_t)(0xfd100000u | (width - 1u));
            w[1] = (int32_t)texaddr;                         rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xf5000000u | (2u << 19) | (line << 9));
            w[1] = (int32_t)0x07080200u;                     rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)0xe6000000u; w[1] = 0;           rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xf4000000u | (soff << 14) | (t0 << 2));
            w[1] = (int32_t)((7u << 24) | (((soff + width - 1u) << 2) << 12)
                             | ((t0 + h - 1u) << 2));
            rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)0xe7000000u; w[1] = 0;           rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xf5000000u | (2u << 19) | (line << 9));
            w[1] = (int32_t)0x00080200u;                     rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)0xf2000000u;
            w[1] = (int32_t)((((width - 1u) << 2) << 12) | ((h - 1u) << 2));
            rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xe4000000u | (xl2 << 12) | yl_b);
            w[1] = (int32_t)((xh << 12) | yh_b);
            w[2] = 0;
            w[3] = (int32_t)((dsdx << 16) | dtdy);
            rdp_fifo_append(fifo, w, 4);
        }
        return;
    }

    /* palette: SETTIMG -> SYNC_TILE -> SETTILE -> SYNC_LOAD -> LOADTLUT -> PIPESYNC */
    w[0] = (int32_t)0xfd100000u; w[1] = (int32_t)paladdr;    rdp_fifo_append(fifo, w, 2);
    w[0] = (int32_t)0xe8000000u; w[1] = 0;                   rdp_fifo_append(fifo, w, 2);
    w[0] = (int32_t)0xf5000100u; w[1] = (int32_t)0x07000000u;rdp_fifo_append(fifo, w, 2);
    w[0] = (int32_t)0xe6000000u; w[1] = 0;                   rdp_fifo_append(fifo, w, 2);
    w[0] = (int32_t)0xf0000000u;
    w[1] = (int32_t)(0x07000000u | (((siz == 0u) ? 15u : 255u) << 14));
    rdp_fifo_append(fifo, w, 2);
    w[0] = (int32_t)0xe7000000u; w[1] = 0;                   rdp_fifo_append(fifo, w, 2);
    /* render mode: the microcode's sprite routine emits the game's *current*
     * other-modes for every sprite -- the low word verbatim from the shadow
     * the display list accumulated through G_SETOTHERMODE_L, the high word
     * likewise but with TEXTLUT forced to G_TT_RGBA16 (these are CI textures;
     * the routine owns the palette load). Verified against the cxd4 LLE RSP
     * in both contexts that previously seemed to need different constants:
     *
     * - Race HUD: the game runs with 0x00504244 (z-compare off, point
     *   sampled), so every 1:1 atlas glyph -- CHECK / POSITION / LAP --
     *   composites over the near-plane geometry at the race-start camera pan.
     * - Game-select menu: the game sets 0x00504a50 (z_compare_en +
     *   z_mode.transparent) with G_SETOTHERMODE_L before the 32x30 backdrop
     *   tiles, then back for the text glyphs. The backdrop therefore z-fails
     *   over the spinning centre logo (drawn first, closer Z) and the logo
     *   stays solid red; emitting the glyph constant here instead let the
     *   tiles overwrite the logo, leaving a dark embossed silhouette.
     * - The stretched rippling overlay (dsdx=0x0c8) is drawn while the game's
     *   shadow holds 0x00504a54 / bilinear (othermode_h sample_type set), so
     *   the previous step-keyed |0x810 and sample-type selection fall out of
     *   the shadow naturally in both the menu and the race.
     *
     * Two ucode-fixed details remain: the 0xEF tag byte, and TEXTLUT --
     * bits 14-15 of the high word are cleared and set to 2 (G_TT_RGBA16). */
    w[0] = (int32_t)(0xef000000u | (s_othermode_h & 0x00ff3fffu) | 0x8000u);
    w[1] = (int32_t)s_othermode_l;
    rdp_fifo_append(fifo, w, 2);
    /* The routine writes this othermode back into the shared DMEM shadow, so
     * the game's later partial G_SETOTHERMODE_H/L merges re-emit with TEXTLUT
     * still enabled -- the LLE RSP shows 8cff/acff (not 0cff/2cff) on every
     * re-emission that follows a sprite. Mirror that into the walker shadow. */
    s_othermode_h = (s_othermode_h & ~0x0000c000u) | 0x8000u;
    /* texture: SETTIMG -> SETTILE(load), then a per-band SYNC_LOAD ->
     * LOADTILE -> PIPESYNC -> SETTILE(rend) -> SETTILESIZE -> TEXRECT.
     *
     * The screen rectangle is the texel extent divided by the texrect step:
     * an unscaled sprite (dsdx=dtdy=0x400) yields width<<2 / disph<<2 exactly
     * as before, but a stretched sprite (Wipeout's full-screen rippling
     * overlay is a 64x64 CI8 tile drawn with dsdx=0x0c8) expands to its true
     * on-screen size. A CI image larger than the 2 KB low TMEM half (the TLUT
     * occupies the high half) cannot be loaded in one shot, so it is split
     * into vertical bands of at most (2048 / bytes-per-row) rows -- the same
     * strip-tiling the microcode does -- with a one-row overlap for bilerp
     * continuity. Small atlas glyphs fit one band and keep the old output. */
    {
        unsigned int bpr = (siz == 0u) ? ((width + 1u) >> 1)   /* CI4 */
                         : (siz == 1u) ? width                  /* CI8 */
                         : (width << 1);                        /* 16-bit */
        unsigned int max_rows = (bpr != 0u && bpr <= 2048u)
                              ? (2048u / bpr) : disph;
        unsigned int strw = (dsdx != 0u) ? ((width << 12) / dsdx)
                                         : (width << 2);
        unsigned int full_h = (dtdy != 0u) ? ((disph << 12) / dtdy)
                                           : (disph << 2);
        unsigned int xl2 = xh + strw;


        /* SETTIMG + load-tile SETTILE are constant across bands */
        w[0] = (int32_t)(0xfd000000u | (fsbyte << 16) | timgw);
        w[1] = (int32_t)texaddr;                             rdp_fifo_append(fifo, w, 2);
        w[0] = (int32_t)(0xf5000000u | (fmt << 21) | (1u << 19) | (line << 9));
        w[1] = (int32_t)0x07080200u;                         rdp_fifo_append(fifo, w, 2);

        if (disph <= max_rows) {
            /* one band: the whole image fits the low TMEM half. No overlap;
             * th = s2 = toff+disph-1 keeps unscaled atlas glyphs byte-exact. */
            unsigned int yl2 = yh + full_h;
            w[0] = (int32_t)0xe6000000u; w[1] = 0;           rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xf4000000u | (loadsl << 12) | (s1 << 2));
            w[1] = (int32_t)((7u << 24) | (loadsh << 12) | (s2 << 2));
            rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)0xe7000000u; w[1] = 0;           rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xf5000000u | (fmt << 21) | (siz << 19) | (line << 9));
            w[1] = (int32_t)0x00080200u;                     rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)0xf2000000u;
            w[1] = (int32_t)((((width - 1u) << 2) << 12) | ((s2 - s1) << 2));
            rdp_fifo_append(fifo, w, 2);
            w[0] = (int32_t)(0xe4000000u | (xl2 << 12) | yl2);
            w[1] = (int32_t)((xh << 12) | yh);
            w[2] = (int32_t)(((sflip ? ((width - 1u) << 5) : ssub) << 16)
                             | (tflip ? ((s2 - s1) << 5) : 0u));
            w[3] = (int32_t)(((sflip ? ((0x10000u - dsdx) & 0xffffu) : dsdx) << 16)
                             | (tflip ? ((0x10000u - dtdy) & 0xffffu) : dtdy));
            rdp_fifo_append(fifo, w, 4);
        } else {
            /* TMEM-limited: split into vertical bands of (max_rows) texels with
             * a one-row overlap (advance = max_rows-1) for bilerp continuity,
             * exactly as the microcode does. Band height is a fixed ceil-to-
             * pixel of the advance; the last band fills to the true extent. The
             * accumulated sub-texel fraction lands in each band's T coordinate. */
            unsigned int adv   = (max_rows > 1u) ? (max_rows - 1u) : 1u;
            unsigned int dt4   = dtdy << 2;
            unsigned int bandh = (dtdy != 0u)
                               ? ((((adv << 12) + dt4 - 1u) / dt4) << 2)
                               : (adv << 2);
            unsigned int bfrac = ((bandh * dtdy) >> 7) - (adv << 5);
            /* T-mirrored pass: the microcode walks the same screen bands but
             * loads each band's mirrored row range [end-th, end-tl] and steps
             * T backwards from the far edge. Its descending accumulator is
             * the ceiling counterpart of the ascending pass's floor: each
             * band's global start is (end<<5) - b*ceil(bandh*dtdy/128),
             * reproduced bit-exactly from the LLE RSP's three overlay bands
             * (0x3e0 / 0x3dc / 0x038 with ults 33 / 2 / 0). */
            unsigned int tend  = s1 + disph;
            unsigned int cstep = ((bandh * dtdy) + 127u) >> 7;
            unsigned int b;
            for (b = 0u; b * adv < disph; b++) {
                unsigned int tl   = s1 + b * adv;
                unsigned int th   = (b * adv + adv < disph)
                                  ? (tl + adv) : (s1 + disph);
                unsigned int lrt  = th - tl;
                unsigned int yh_b = (b * bandh < full_h) ? (b * bandh) : full_h;
                unsigned int yl_b = ((b + 1u) * bandh < full_h)
                                  ? ((b + 1u) * bandh) : full_h;
                unsigned int tc   = b * bfrac;
                unsigned int ldl  = tflip ? (tend - th) : tl;
                unsigned int ldh  = tflip ? (tend - tl) : th;
                if (tflip)
                    tc = ((tend << 5) - b * cstep) - (ldl << 5);

                w[0] = (int32_t)0xe6000000u; w[1] = 0;       rdp_fifo_append(fifo, w, 2);
                w[0] = (int32_t)(0xf4000000u | (loadsl << 12) | (ldl << 2));
                w[1] = (int32_t)((7u << 24) | (loadsh << 12) | (ldh << 2));
                rdp_fifo_append(fifo, w, 2);
                w[0] = (int32_t)0xe7000000u; w[1] = 0;       rdp_fifo_append(fifo, w, 2);
                w[0] = (int32_t)(0xf5000000u | (fmt << 21) | (siz << 19) | (line << 9));
                w[1] = (int32_t)0x00080200u;                 rdp_fifo_append(fifo, w, 2);
                w[0] = (int32_t)0xf2000000u;
                w[1] = (int32_t)((((width - 1u) << 2) << 12) | (lrt << 2));
                rdp_fifo_append(fifo, w, 2);
                w[0] = (int32_t)(0xe4000000u | (xl2 << 12) | yl_b);
                w[1] = (int32_t)((xh << 12) | yh_b);
                w[2] = (int32_t)(((sflip ? ((width - 1u) << 5) : ssub) << 16) | tc);
                w[3] = (int32_t)(((sflip ? ((0x10000u - dsdx) & 0xffffu) : dsdx) << 16)
                                 | (tflip ? ((0x10000u - dtdy) & 0xffffu) : dtdy));
                rdp_fifo_append(fifo, w, 4);
            }
        }
    }
}

static unsigned int rd32(const unsigned char *r, unsigned int a)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    if (a + 4u > limit)
        return 0u;
    /* RDRAM is stored host-native (byte-swapped on load); read native, the
     * same way the F3DEX2 walker and the rasterizer's command fetch do. */
    return *(const unsigned int *)(r + a);
}

/* Resolve a segmented/virtual address: seg_table[(w1>>24)&0xf] + low24. KSEG0
 * pointers (top byte 0x80/0xA0) map through segment 0 (base 0). */
static unsigned int seg_rsp(unsigned int w1)
{
    unsigned int seg = (w1 >> 24) & 0x0fu;
    return s_seg[seg] + (w1 & 0x00ffffffu);
}

static unsigned int seg_phys(unsigned int w1)
{
    return seg_rsp(w1) & 0x00ffffffu;
}

static int in_range(unsigned int a, unsigned int bytes)
{
    unsigned int limit = s_rdram_size ? s_rdram_size : (8u * 1024u * 1024u);
    return (a < limit) && (bytes <= limit) && (a + bytes <= limit);
}

/* ---- setup ---------------------------------------------------------------*/
/* F3D packs the cull / smooth-shade geometry-mode bits differently from
 * F3DEX2; the shared frontend interprets the mode in the F3DEX2 layout. Remap
 * the three bits that moved so culling and flat/smooth shading are correct:
 *   F3D G_SHADING_SMOOTH 0x000200 -> F3DEX2 0x200000
 *   F3D G_CULL_FRONT     0x001000 -> F3DEX2 0x000200
 *   F3D G_CULL_BACK      0x002000 -> F3DEX2 0x000400
 * All other bits (G_ZBUFFER/G_SHADE/G_FOG/G_LIGHTING/G_TEXTURE_GEN/...) share
 * the same position in both. */
static int s_variant_seta = 0;
void f3d_set_variant_seta(int v) { s_variant_seta = v ? 1 : 0; }

unsigned int f3d_xlate_geom(unsigned int m)
{
    unsigned int o = m & ~(0x000200u | 0x001000u | 0x002000u);
    if (m & 0x000200u) o |= 0x200000u;
    if (m & 0x001000u) o |= 0x000200u;
    if (m & 0x002000u) o |= 0x000400u;
    return o;
}

/* G_TEXTURE_PERSP (other-modes high bit 19) selects whether the microcode
 * runs its per-vertex perspective normalizer over the texture attributes.
 * With the bit clear the resident writer stores the texel shorts and a
 * 0x7fff W lane straight out; the normalized path halves every attribute
 * instead, and with no per-pixel divide to undo the halving the primitive
 * samples the wrong texels.  rsp_tri_write has carried the affine path
 * since Fighting Force 64 needed it, but nothing fed it the bit, so this
 * walker could never reach it. */
static void f3d_sync_persp_tex(void)
{
    rsp_set_affine_tex((s_othermode_h & 0x00080000u) ? 0 : 1);
}

void f3d_seg_reset(void)
{
    int i;
    for (i = 0; i < 16; i++)
        s_seg[i] = 0u;
    s_textured  = 0;
    s_zbuffered = 0;
    s_dl_depth  = 0;
    s_geom      = 0;
}

void f3d_set_rdram(unsigned char *rdram)        { s_rdram = rdram; }

/* Mid-list ucode-swap routing (Yoshi's Story title: an S2DEX 1.06 list that
 * hot-swaps to F3DEX.NoN via G_LOAD_UCODE/0xAF to draw its geometry, then
 * swaps back). When s_stop_uc is set, the walker treats a 0xAF as the end of
 * the F3DEX.NoN section, records the pc of that command so the S2DEX1 walker
 * can resume there, and returns. */
static int          s_stop_uc;
static unsigned int s_resume_pc;
static int          s_uc_stop_depth = -1;
static int          s_stopped_at_uc;
static unsigned int s_last_rdphalf1;
void f3d_set_stop_on_ucode(int on)
{
    s_stop_uc = on ? 1 : 0;
    if (on)
        s_stopped_at_uc = 0;
    else
        s_uc_stop_depth = -1;
}
unsigned int f3d_get_resume_pc(void)            { return s_resume_pc; }
int f3d_stopped_at_ucode(void)                  { return s_stopped_at_uc; }
void f3d_import_segments(const unsigned int *src)
{
    int i;
    for (i = 0; i < 16; i++)
        s_seg[i] = src[i];
}
void f3d_set_rdram_size(unsigned int size)      { s_rdram_size = size; }

void f3d_set_othermode_init(unsigned int h, unsigned int l)
{
    s_othermode_h = h | (0x2fu << 24);
    s_othermode_l = l;
    s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                   ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
}

/* SM64's F3D ucode signature: rd32(text+4) (native host word of the second
 * ucode-text word). F3DEX2/S2DEX2/L3DEX2 builds carry 0xc81f20xx here; the
 * Fast3D builds do not. Matching positively on the known F3D word keeps the
 * F3DEX2 titles on their existing path unchanged. */
int f3d_is_ucode(const unsigned char *rdram, unsigned int rdram_size,
                 unsigned int text)
{
    unsigned int w1;
    unsigned int limit = rdram_size ? rdram_size : (8u * 1024u * 1024u);
    if (rdram == 0 || text == 0 || text + 8u > limit)
        return 0;
    w1 = *(const unsigned int *)(rdram + text + 4u);
    if (w1 == 0x201d0110u)      /* RSP SW Version 2.0X (Super Mario 64) */
        return 1;
    /* The L3DEX line microcode boots differently and its word at text+4 is not
     * the SM64 version word, so it is not recognised here; the dispatcher
     * routes it onto the F3D walker by its data-segment family name instead
     * (see f3d_ucode_family), which also keeps Doom 64's and Hexen's automaps
     * off the F3DEX2 path. */
    return 0;
}

/* Fingerprint a microcode by a checksum over its text image, the way HLE
 * plugins routinely identify a build. Only Wave Race 64's plain Fast3D variant
 * still needs this (its data segment carries no "RSP Gfx ucode" name string, so
 * the family scanner cannot see it); every other family is recognised from that
 * name. The builds diverge within the first text block, so 0xc00 bytes suffice
 * and match the value computed for the USA cart. */
static unsigned int f3d_text_crc(const unsigned char *rdram,
                                 unsigned int rdram_size, unsigned int text)
{
    unsigned int i, cs = 0;
    for (i = 0; i < 0xc00u && (text + i) < rdram_size; i++)
        cs = cs * 131u + rdram[(text + i) ^ 3];
    return cs;
}

/* Wave Race 64's plain Fast3D build. It shares SM64's RSP version word but a
 * third opcode encoding: gSPVertex packs the count as n<<9 with the byte
 * length (16n-1) in the low bits and the destination as (v0)*5 in the param
 * byte, and gSP1Triangle/G_QUAD index vertices times five (SM64 times ten,
 * Doom 64 times two). Detected by text CRC so SM64 and Doom 64 keep their own
 * decodes. */
/* Body Harvest's custom F3DEX-derived build implements gSPLine3DW on
 * 0xB5 (stock F3DEX leaves it a no-op; F3DLX repurposes it as G_QUAD):
 * the enemies' plasma bolts are z-buffered shaded line segments.
 * Detected by text CRC so the fam-1 default keeps the quad decode. */
int f3d_is_bhline_ucode(const unsigned char *rdram, unsigned int rdram_size,
                        unsigned int text)
{
    if (rdram == 0 || text == 0)
        return 0;
    return f3d_text_crc(rdram, rdram_size, text) == 0x9c727d8eu;
}

static int s_variant_bhline = 0;
void f3d_set_variant_bhline(int bh) { s_variant_bhline = bh ? 1 : 0; }

int f3d_is_wr64_ucode(const unsigned char *rdram, unsigned int rdram_size,
                      unsigned int text)
{
    if (rdram == 0 || text == 0)
        return 0;
    return f3d_text_crc(rdram, rdram_size, text) == 0xc6d28214u;
}

/* Eikou no Saint Andrews (SETA, 1996): a custom graphics microcode on the
 * "RSP SW Version: 2.0D, 04-01-96" base whose display lists use the stock
 * GBI 1 command encoding. Its text boots through a zeroed jump slot (word 0
 * is 0, the code proper starts at +0x10) so every boot-word probe misses,
 * and its data segment is a custom table with neither the SGI name string
 * nor the F3D othermode-default pair, so the family and GBI 1 data probes
 * miss too -- the task fell through to the F3DEX2 walker, which decodes
 * GBI 1 words as garbage (the title screen drew as scattered gray blocks).
 * Detect it by text CRC like the other name-stripped builds. */
int f3d_is_seta_ucode(const unsigned char *rdram, unsigned int rdram_size,
                      unsigned int text)
{
    unsigned int cs;
    if (rdram == 0 || text == 0)
        return 0;
    cs = f3d_text_crc(rdram, rdram_size, text);
    /* 0x99b3.. = the "2.0D, 04-01-96" build (Eikou no Saint Andrews);
     * 0x8eb6.. = the "2.0G, 09-30-96" revision (Morita Shougi 64) -- same
     * zeroed boot slot, data layout, and display-list conventions. */
    return cs == 0x99b3558au || cs == 0x8eb6aa3fu;
}

/* Blast Corps' line-capable Fast3D build (the J-Bomb bonus-stage select and
 * carrier scenes): a name-stripped Rare Fast3D variant whose 0xB5 opcode is
 * gSPLine3D -- (v0*10)<<16 | (v1*10)<<8 | width -- not G_QUAD. Decoded as a
 * quad it emits a shaded fan across consecutive polyline vertices (the giant
 * white/red wedge over the planet). Detected by text CRC like Wave Race 64:
 * the build carries no "RSP Gfx ucode" name string for the family scanner. */
int f3d_is_bcline_ucode(const unsigned char *rdram, unsigned int rdram_size,
                        unsigned int text)
{
    if (rdram == 0 || text == 0)
        return 0;
    return f3d_text_crc(rdram, rdram_size, text) == 0xa96befadu;
}

/* Fighting Force 64's F3D build draws its HUD (the health-bar gradient)
 * through a 2D overlay extension on the 0xB2 opcode: sub-op 0x18 writes a
 * vertex slot's screen position directly (two 10.2 coordinates in w1,
 * bypassing the transform), sub-op 0x14 the slot's S10.5 texel
 * coordinates, sub-op 0x10 its RGBA colour, and
 * an ordinary G_TRI/quad then draws from those slots. Stock Fast3D uses
 * 0xB2 as G_RDPHALF_CONT, so the extension is gated on the build's text
 * CRC (it carries no ucode name string). Without it the quad draws stale
 * transformed vertices: a white triangle floating mid-screen and a health
 * bar left black. */
int f3d_is_ff2d_ucode(const unsigned char *rdram, unsigned int rdram_size,
                      unsigned int text)
{
    if (rdram == 0 || text == 0)
        return 0;
    return f3d_text_crc(rdram, rdram_size, text) == 0x89bf13b3u;
}

static int s_variant_ff2d = 0;
void f3d_set_variant_ff2d(int ff2d) { s_variant_ff2d = ff2d ? 1 : 0; }

/* 0 = plain Fast3D (Super Mario 64); 1 = Doom 64's variant. Set once per task
 * before the top-level walk; the recursive F3D_DL descent inherits it. */
static int s_variant_d64 = 0;
static int s_variant_line = 0;   /* 1 => Doom 64 automap line ucode (gspL3DEX) */
static int s_variant_wr64 = 0;   /* 1 => Wave Race 64 (n<<9 vtx, x5 indices) */
static int s_variant_f3dex = 0;  /* 1 => F3DEX GBI1 (GoldenEye/Perfect Dark): x2
                                  * vertex indices and 0xB1 = G_TRI2 (two tris) */
void f3d_set_variant_f3dex(int v) { s_variant_f3dex = v ? 1 : 0; }
/* Perfect Dark: the F3DEX GBI1 decode plus the colour-indexed 12-byte
 * vertex record and the 0x07 G_VTXCOLORBASE command. */
static int s_variant_pd = 0;
void f3d_set_variant_pd(int v) { s_variant_pd = v ? 1 : 0; }
void f3d_set_variant(int doom64)
{
    s_variant_d64 = doom64 ? 1 : 0;
    rsp_set_vtx_invw_2rd(s_variant_d64);
    rsp_set_clip_lerp_l3dex(s_variant_d64);
}
void f3d_set_line_variant(int line) { s_variant_line = line ? 1 : 0; }
void f3d_set_variant_wr64(int wr64) { s_variant_wr64 = wr64 ? 1 : 0; }
static int s_variant_wo64 = 0;
void f3d_set_variant_wo64(int wo64)
{
    s_variant_wo64 = wo64 ? 1 : 0;
    rsp_set_vtx_y_round(wo64);
    bridge_set_clip_degenerate_cull(wo64);
    if (wo64)
        rsp_set_clip_lerp_wo64(1);
}


/* ---- RDP pass-through (microcode-independent), mirrors rdp_emit_f3dex2.c --*/
static void rdp_passthrough(GSPState *gsp, RdpFifo *fifo, int cmd,
                            unsigned int w0, unsigned int w1)
{
    int rdp_id = cmd & 0x3f;

    /* Set Other Modes (0xEF -> 0x2f): depth enables select the Z tri variant */
    if (rdp_id == 0x2f)
    {
        int zc = (int)((w1 >> 4) & 1);
        int zu = (int)((w1 >> 5) & 1);
        s_zbuffered = (zc || zu) ? 1 : 0;
    }
    /* Set Scissor (0xED -> 0x2d): the line microcodes emit a scissor before
     * every line command (see gsp_line), restoring this tracked value for
     * the y-major form. */
    if (rdp_id == 0x2d)
    {
        gsp->scis_w0 = (int32_t)w0;
        gsp->scis_w1 = (int32_t)w1;
        gsp->scis_valid = 1;
    }
    /* Set Tile (0xF5 -> 0x35): cache the wrap mask exponents the texcoord fold
     * in gsp_triangle needs. */
    if (rdp_id == 0x35)
    {
        unsigned int ti = (w1 >> 24) & 7;
        gsp->tile_mask_t[ti] = (unsigned char)((w1 >> 14) & 0xf);
        gsp->tile_mask_s[ti] = (unsigned char)((w1 >> 4) & 0xf);
    }
    /* 0x24..0x3f are the RDP non-triangle commands angrylion implements; skip
     * 0x31 (G_SETKEY*, unimplemented) and 0x29 (SYNC_FULL, the activation
     * appends exactly one frame terminator itself, but records that the list
     * requested one). */
    if (rdp_id == 0x29)
        rdp_fifo_fullsync_note();
    if (rdp_id >= 0x24 && rdp_id <= 0x3f && rdp_id != 0x31 && rdp_id != 0x29)
    {
        int32_t two[2];
        two[0] = (int32_t)w0;
        /* SET_COLOR/Z/TEXTURE_IMAGE carry a (possibly segmented) DRAM pointer
         * in w1; resolve it the way the RSP does before the RDP sees it. */
        if (rdp_id == 0x3f || rdp_id == 0x3e || rdp_id == 0x3d)
            two[1] = (int32_t)seg_rsp(w1);
        else
            two[1] = (int32_t)w1;
        rdp_fifo_append(fifo, two, 2);
    }
}

/* ---- the F3D walker ------------------------------------------------------*/
void f3d_run_dl(GSPState *gsp, RdpFifo *fifo, unsigned int addr,
                int textured, int z_buffered)
{
    int guard = 0;
    unsigned int pc = addr;
    int running = 1;
    const unsigned char *r = s_rdram;

    if (textured)   s_textured  = textured;
    if (z_buffered) s_zbuffered = z_buffered;
    if (r == 0)
        return;
    if (s_dl_depth == 0)
    {
        /* SM64-era plain Fast3D clips triangles against the near plane
         * (z + w >= 0); enabling the frontend's near-plane clip keeps its
         * large camera-straddling quads (the water/mist sheets in Jolly Roger
         * Bay) from being disposed by the guard band. The Doom 64 / Turok
         * build, by contrast, is a NoN ("no near clip") F3DEX-family
         * microcode: it lets behind-the-eye geometry fall out through the
         * guard band rather than clipping it at z + w = 0. Forcing the near
         * clip on that variant severed wall quads the moment the camera
         * pressed against them (Turok, standing point-blank to a wall) along
         * z + w = 0 and resampled the surviving remainder, so the wall showed
         * sharp mid-range texels where the LLE RSP shows the blurred
         * point-blank surface. Gate the near clip to the plain-Fast3D variant.
         * Both keep FRUSTRATIO_1 (clip-to-screen) as the default clip ratio
         * rather than the guard-band ratio of the wider F3DEX family; an
         * explicit gSPClipRatio still overrides it. */
        /* Perfect Dark's microcode is a No-Near-clipping (".NoN") build --
         * the "NoN" tag sits in its text image -- so, like Doom 64's Fast3D,
         * it has no near-plane clip: geometry crossing z is passed to the
         * rasterizer and the scissor trims it. Keeping the near clip on
         * dropped Perfect Dark's 2D HUD/pause quads whole (they sit right at
         * the ortho near plane, so every one tripped the near-Z reject that
         * the real RSP does not run): the pause overlay's scanning panels and
         * the health-bar backing vanished. GoldenEye's build keeps the near
         * clip, so gate this on the PD variant. */
        gsp->clip_near_z = (s_variant_d64 || s_variant_pd) ? 0 : 1;
        /* Perfect Dark uses the wider guard-band clip ratio (FRUSTRATIO_2),
         * not the clip-to-screen ratio the plain F3D/F3DEX builds default to.
         * With the narrow ratio the walker subdivided every door- and
         * wall-edge triangle that reached just past the screen edge into a
         * guard-band fan, and the extra seams rasterised as vertical texel
         * slivers down otherwise-solid surfaces (visible on the retail
         * cart's sliding doors). The real microcode passes those triangles
         * to the rasterizer whole, so widen the band to match; GoldenEye
         * keeps the narrow ratio. */
        gsp->clip_ratio = s_variant_pd ? 2 : 1;
        /* The Doom 64 lineage (Doom 64, Rayman 2, ...) keeps the 2.04H-era
         * clip fan: pivot on the FIRST polygon vertex and walk the pairs
         * downward from the end -- (0,n-2,n-1), (0,n-3,n-2), ..., (0,1,2).
         * The default last-vertex fan tiles the same clipped polygon with a
         * different triangle set, which is visible against the cxd4 LLE
         * oracle wherever a clipped polygon has more than three vertices
         * (Rayman 2's pause-menu nebula: 30/52 -> 38/52 oracle-exact
         * triangles with the correct fan). */
        if (s_variant_d64)
            gsp->clip_fan_first = 1;
        rsp_tri_set_d64_sort(s_variant_d64 ? 1 : 0);
        if (s_variant_wo64)
        {
            /* Wipeout 64's data-segment plane table (data + 0x70):
             * -x, -y, +x, +y, far, z + w near -- the reverse of the
             * F3DEX iteration order, with the near clip on and no
             * guard band. The iteration order decides the clip fan's
             * vertex sequence, hence the triangulation. */
            gsp->clip_near_z = 1;
            gsp->clip_fan_first = 2;
            gsp->clip_ratio = 1;
        }
        gsp->fog_off = (s_variant_line && s_variant_d64
                        && !s_variant_bhline) ? 1 : 0;
        gsp->line_alpha_mask = (s_variant_line && s_variant_d64
                                && !s_variant_bhline) ? 1 : 0;
        gsp->line_clip_3d = (s_variant_line && s_variant_d64
                             && !s_variant_bhline) ? 1 : 0;
        gsp->line_z = s_variant_bhline;
        gsp->line_xl[0] = 0;
        gsp->line_xl[1] = 0;
        s_spr_have = 0;
    }
    if (s_dl_depth >= F3D_DL_MAX_DEPTH)
        return;
    s_dl_depth++;

    /* Only the outermost F3DEX.NoN walk (the one entered from the S2DEX1
     * host list) ends at a G_LOAD_UCODE; a 0xAF reached inside a nested
     * G_DL must not hijack the resume point (that aliased the outer section
     * to a backward branch and span-looped the walker). */
    if (s_stop_uc && s_uc_stop_depth < 0)
        s_uc_stop_depth = s_dl_depth;

    while (running && guard++ < 100000)
    {
        unsigned int w0 = rd32(r, pc);
        unsigned int w1 = rd32(r, pc + 4);
        int cmd = (int)((w0 >> 24) & 0xff);
        pc += 8;

        if (s_stop_uc && cmd == 0xAF && s_dl_depth == s_uc_stop_depth)
        {
            s_stopped_at_uc = 1;
            /* End of the F3DEX.NoN section: hand control back to the S2DEX1
             * walker at this G_LOAD_UCODE so it can swap interpretation. */
            s_resume_pc = pc - 8u;
            running = 0;
            continue;
        }

        switch (cmd)
        {
        case F3D_MTX:
        {
            int projection = (int)((w0 >> 16) & F3D_MTX_PROJECTION) ? 1 : 0;
            int load       = (int)((w0 >> 16) & F3D_MTX_LOAD)       ? 1 : 0;
            int push       = (int)((w0 >> 16) & F3D_MTX_PUSH)       ? 1 : 0;
            unsigned int ma = seg_phys(w1);
            if ((w0 & 0xffffu) == 64u && in_range(ma, 64u))
                gsp_matrix_load(gsp, r, ma, projection, load, push);
            break;
        }

        case 0x09:
            /* Wipeout sprite ucode: load the 24-byte sprite struct from w1
             * (w0&0x1ff is the byte length, always 0x18). Marks the sprite
             * path active so the following 0xBE/0xBD act as size/draw. */
            {
                unsigned int sa = seg_phys(w1);
                if (in_range(sa, 24u))
                {
                    int i;
                    for (i = 0; i < 6; i++)
                        s_spr_struct[i] = rd32(r, sa + (unsigned int)i * 4u);
                    s_spr_have = 1;
                }
            }
            break;

        case F3D_POPMTX:
            /* Wipeout sprite ucode: 0xBD carries the screen position and emits
             * the sprite (gated on a preceding 0x09 struct load). */
            if (s_spr_have)
            {
                f3d_emit_sprite(fifo, w1);
                break;
            }
            /* F3D pops one modelview level (w1 selects modelview/projection). */
            gsp_matrix_pop(gsp, r);
            break;

        case F3D_VTX:
        {
            int n, v0;
            unsigned int va = seg_phys(w1);
            if (s_variant_wr64)
            {
                /* Wave Race 64's plain Fast3D gSPVertex packs the count as
                 * n<<9 with the byte length (16n-1) in the low bits, and the
                 * destination as (v0)*5 in the param byte (bits 16..23). */
                n  = (int)((w0 >> 9) & 0x7fu);
                v0 = (int)(((w0 >> 16) & 0xffu) / 5u);
            }
            else if (s_variant_d64)
            {
                /* gSPVertex(v, n, v0) on this Fast3D/F3DEX-family microcode
                 * (shared by Doom 64 and Turok): w0 packs the count as n<<10
                 * with the DMA byte length minus one (n*16-1) in the low bits,
                 * and the destination index as (v0 << 1) in the param byte
                 * (bits 16..23). Decode n from the count field, NOT from the
                 * low byte: the low byte holds (n*16-1)&0xff, which aliases
                 * once n>16 (e.g. n=6 and n=22 both yield low byte 0x5f),
                 * truncating large batches to 6 and leaving the unloaded high
                 * slots stale -- the source of Doom 64's stretched stray
                 * triangles. Doom 64 always loads to slot 0 (param byte 0), but
                 * Turok's title logo streams later batches into the upper half
                 * of the vertex buffer (v0=16, param byte 0x20) while keeping
                 * the lower half live; forcing v0=0 clobbered slots 0..15 and
                 * left the quads that index 16..31 reading stale vertices,
                 * which sheared the logo into stray triangles. */
                n  = (int)((w0 >> 10) & 0x3fu);
                v0 = (int)(((w0 >> 16) & 0xffu) >> 1);
            }
            else
            {
                n  = (int)((w0 >> 20) & 0x0f) + 1;
                v0 = (int)((w0 >> 16) & 0x0f);
            }
            if (n > 0 && in_range(va,
                                  (unsigned int)n * (s_variant_pd ? 12u : 16u)))
                gsp_vertex(gsp, r, va, n, v0);
            break;
        }

        case 0x07:
            /* Perfect Dark G_VTXCOLORBASE: the base of the separate vertex
             * colour/normal table its 12-byte colour-indexed vertices point
             * into (segmented or physical). Other builds never emit 0x07
             * (stock GBI1 reserves it), so it is gated on the PD variant. */
            if (s_variant_pd)
                gsp_set_vertex_color_base(gsp, seg_rsp(w1) & 0x00ffffffu);
            break;

        case F3D_TRI1:
        {
            f3d_sync_persp_tex();
            int32_t cw[GSP_TRI_CMD_WORDS];
            int a, b, c, nc;
            /* GoldenEye/Perfect Dark (Rare's F3DEX-derived GBI1) store the
             * G_TRI1 vertex index as slot*10 -- the value the microcode's
             * 0x2d0 index->offset table emits -- so the slot is byte/10.
             * Other F3D-family ucodes keep their byte/divisor form. */
            if (s_variant_f3dex)
            {
                a = (int)((w1 >> 16) & 0xff) / 10;
                b = (int)((w1 >>  8) & 0xff) / 10;
                c = (int)((w1 >>  0) & 0xff) / 10;
            }
            else
            {
                int s = s_variant_wr64 ? 5 : (s_variant_d64 ? 2 : 10);
                a = (int)((w1 >> 16) & 0xff) / s;
                b = (int)((w1 >>  8) & 0xff) / s;
                c = (int)((w1 >>  0) & 0xff) / s;
            }
            nc = gsp_triangle(gsp, cw, a, b, c, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            break;
        }

        case F3D_TRI2:
        {
            f3d_sync_persp_tex();
            int32_t cw[GSP_TRI_CMD_WORDS];
            int nc;
            if (s_variant_f3dex)
            {
                /* GoldenEye/Perfect Dark reuse 0xB1 as Rare's G_TRI4: four
                 * triangles packed with 4-bit vertex indices. The third index
                 * of each triangle comes from a nibble of w0, the first two
                 * from a byte of w1:
                 *   w0: [0xB1][i2_4 i2_3 i2_2 i2_1][unused]
                 *   w1: [t4.b t4.a][t3.b t3.a][t2.b t2.a][t1.b t1.a]  (nibbles)
                 * Degenerate triangles (all three indices equal) are culled,
                 * matching the microcode. SM64's Fast3D never emits 0xB1. */
                int tri[4][3];
                int i;
                tri[0][0] = (int)((w1 >>  0) & 0xf);
                tri[0][1] = (int)((w1 >>  4) & 0xf);
                tri[0][2] = (int)((w0 >>  0) & 0xf);
                tri[1][0] = (int)((w1 >>  8) & 0xf);
                tri[1][1] = (int)((w1 >> 12) & 0xf);
                tri[1][2] = (int)((w0 >>  4) & 0xf);
                tri[2][0] = (int)((w1 >> 16) & 0xf);
                tri[2][1] = (int)((w1 >> 20) & 0xf);
                tri[2][2] = (int)((w0 >>  8) & 0xf);
                tri[3][0] = (int)((w1 >> 24) & 0xf);
                tri[3][1] = (int)((w1 >> 28) & 0xf);
                tri[3][2] = (int)((w0 >> 12) & 0xf);
                for (i = 0; i < 4; i++)
                {
                    if (tri[i][0] == tri[i][1] && tri[i][0] == tri[i][2])
                        continue;
                    nc = gsp_triangle(gsp, cw, tri[i][0], tri[i][1], tri[i][2],
                                      s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                }
            }
            else if (s_variant_d64)
            {
                /* Doom 64's gSP1Quadrangle (0xB1): two triangles, indices in
                 * w0's and w1's low 24 bits, each byte a vertex index times
                 * two. SM64's Fast3D never emits 0xB1, so it is untouched. */
                int a0 = (int)((w0 >> 16) & 0xff) / 2;
                int b0 = (int)((w0 >>  8) & 0xff) / 2;
                int c0 = (int)((w0 >>  0) & 0xff) / 2;
                int a1 = (int)((w1 >> 16) & 0xff) / 2;
                int b1 = (int)((w1 >>  8) & 0xff) / 2;
                int c1 = (int)((w1 >>  0) & 0xff) / 2;
                if (s_variant_ff2d
                    && a1 >= 0 && a1 < GSP_MAX_VERTICES
                    && gsp->vtx[a1].rsp_invw == (int32_t)0x7fff0000
                    && gsp->vtx[a1].cw == 0x10000)
                {
                    /* Fighting Force 64's 2D overlay quads (vertex slots
                     * injected by the 0xB2 extension above): the microcode
                     * emits the lower half first and forces both halves
                     * left-major, so the shared diagonal is walked with
                     * identical coefficients by both commands and the
                     * antialias coverage tiles without a seam. Mirror the
                     * order and flip the second triangle's winding; drawn
                     * with the walker's order the diagonal double-blends
                     * into a dim streak across the glyphs. */
                    nc = gsp_triangle(gsp, cw, c1, b1, a1, s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                    nc = gsp_triangle(gsp, cw, a0, b0, c0, s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                }
                else
                {
                    /* The pair goes out second-triangle-first.  The
                     * microcode stores the second triangle's indices and
                     * calls the triangle writer before falling into the
                     * single-triangle path for the first, which is the
                     * same order F3DEX2's G_TRI2 handler already follows;
                     * this path had it the other way round, and only the
                     * Fighting Force branch above happened to be right.
                     *
                     * Fed Quake's particle pass, every Y coordinate in
                     * the frame moves from disagreeing with the
                     * interpreter to agreeing with it, and Hexen's title
                     * screen goes from 192 differing pixels to none. */
                    nc = gsp_triangle(gsp, cw, a1, b1, c1, s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                    nc = gsp_triangle(gsp, cw, a0, b0, c0, s_textured, s_zbuffered);
                    if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                }
            }
            break;
        }

        case F3D_LINE3D:
        {
            f3d_sync_persp_tex();
            if (s_variant_line)
            {
                /* gSPLine3D(v0,v1,flag): the two vertex indices live in w1
                 * -- (v0*k)<<16 | (v1*k)<<8 with the family's index scale k
                 * (Doom 64 gspL3DEX x2, Blast Corps' Fast3D line build x10)
                 * -- and the low byte carries the line width. Draw the
                 * segment as a width-expanded screen-space quad. */
                int s = s_variant_wr64 ? 5 : (s_variant_d64 ? 2 : 10);
                int a = (int)((w1 >> 16) & 0xff) / s;
                int b = (int)((w1 >>  8) & 0xff) / s;
                int wd = (int)(w1 & 0xff);
                int32_t cw[GSP_TRI_CMD_WORDS];
                int nc = gsp_line(gsp, cw, a, b, wd);
                if (nc > 0) rdp_fifo_append(fifo, cw, nc);
                break;
            }
            /* G_QUAD on F3D builds that use 0xB5 for quads: four indices in w1,
             * emitted as two triangles (a,b,c) + (a,c,d). */
            {
            int s = s_variant_wr64 ? 5 : (s_variant_d64 ? 2 : 10);
            int a = (int)((w1 >> 24) & 0xff) / s;
            int b = (int)((w1 >> 16) & 0xff) / s;
            int c = (int)((w1 >>  8) & 0xff) / s;
            int d = (int)((w1 >>  0) & 0xff) / s;
            int32_t cw[GSP_TRI_CMD_WORDS];
            int nc;
            nc = gsp_triangle(gsp, cw, a, b, c, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            nc = gsp_triangle(gsp, cw, a, c, d, s_textured, s_zbuffered);
            if (nc > 0) rdp_fifo_append(fifo, cw, nc);
            }
            break;
        }

        case F3D_DL:
        {
            int nopush = (int)((w0 >> 16) & 0xff) == F3D_DL_NOPUSH;
            unsigned int da = seg_phys(w1);
            if (nopush)
            {
                /* gSPBranchList is a jump, not a call: the DL pointer moves to
                 * the target and the current list continues there, with no
                 * return and no push onto the DL stack. Recursing here (like a
                 * pushed gSPDisplayList) inflated s_dl_depth by one per branch.
                 * Shadows of the Empire chains its 2D overlay / HUD lists
                 * through dozens of branches, so the depth ran past
                 * F3D_DL_MAX_DEPTH and the walk aborted before the 560
                 * briefing/HUD texrects, dropping every one of them. Jump
                 * instead; the guard below still bounds runaway branch loops. */
                if (in_range(da, 8u))
                {
                    pc = da;
                    continue;
                }
                running = 0;   /* unresolvable target: end this list */
            }
            else if (in_range(da, 8u))
            {
                f3d_run_dl(gsp, fifo, da, s_textured, s_zbuffered);
            }
            break;
        }

        case F3D_ENDDL:
            running = 0;
            break;

        case F3D_SETGEOMETRYMODE:
            s_geom |= w1;
            s_textured = (int)((s_geom >> 1) & 1u);
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case F3D_CLEARGEOMETRYMODE:
            s_geom &= ~w1;
            s_textured = (int)((s_geom >> 1) & 1u);
            gsp_set_geometry_mode(gsp, f3d_xlate_geom(s_geom));
            break;

        case F3D_TEXTURE:
        {
            int on    = (int)(w0 & 0xff);
            int level = (int)((w0 >> 11) & 0x07);
            int tile  = (int)((w0 >> 8) & 0x07);
            unsigned int ss = (unsigned int)((w1 >> 16) & 0xffff);
            unsigned int ts = (unsigned int)(w1 & 0xffff);
            /* The microcode's G_TEXTURE handler folds the on flag into
             * bit 1 of the geometry-mode word (G_TEXTURE_ENABLE) --
             * Silicon Valley's build at 0x1248: lh geom+2; andi 0xfffd;
             * on<<1; or; sh -- and the triangle write forms its RDP
             * opcode as 0xC8 | (geometry mode & 0xff) (its 0x1a78/0x1a8c:
             * lb geom.byte3; ori 0xc8). A gSPClearGeometryMode covering
             * bit 1 therefore turns texturing off with no further
             * G_TEXTURE command; Silicon Valley clears 0xFFFFFFFF per
             * world cell and re-enables selectively. Track the bit in
             * the shadow word and derive the textured flag from it. */
            s_geom = (s_geom & ~2u) | ((on != 0) ? 2u : 0u);
            s_textured = (int)((s_geom >> 1) & 1u);
            /* A G_TEXTURE carrying a zero S/T scale must not overwrite the
             * active scale that bakes vertex texcoords. Wipeout 64's custom
             * ucode emits 0xBB commands with w1==0 (both on==0 and on==1)
             * between vertex loads; taking the zero literally made every
             * subsequently-transformed vertex bake tv = (st*scale)>>16 = 0,
             * flattening the affected texture to a single texel. A zero
             * texture scale is never valid for a textured draw (it collapses
             * all texels to one), and the LLE RSP retains the last real scale,
             * so preserve it when the command provides none. */
            if (ss == 0 && ts == 0)
            {
                ss = gsp->tex_scale_s;
                ts = gsp->tex_scale_t;
            }
            gsp_set_texture(gsp, ss, ts, tile, level, gsp->tex_w, gsp->tex_h);
            break;
        }

        case F3D_SETOTHERMODE_H:
        case F3D_SETOTHERMODE_L:
        {
            /* F3D partial other-modes write: shift and bit-length are direct
             * (not the F3DEX2 32-shift-length inverted form). w1 holds the new
             * field bits already positioned. Merge and re-emit a full RDP
             * SET_OTHER_MODES so angrylion picks up the cycle type / render
             * mode. */
            unsigned int shift = (w0 >> 8) & 0xffu;
            unsigned int length = w0 & 0xffu;
            unsigned int mask;
            int32_t two[2];
            if (length >= 32u || shift >= 32u)
                mask = 0xffffffffu;
            else
                mask = ((1u << length) - 1u) << shift;
            /* The microcode's handler clears the field with the mask but
             * ORs in the *whole* w1: any bits the display list carries
             * outside the addressed field stick. Top Gear Rally relies on
             * this, shipping its dust render mode as
             * gsSPSetOtherMode(..., 3, 29, 0x0F0A0233): the two low bits
             * (alpha-compare dither) ride below the shifted field and only
             * reach the RDP because the ucode does not mask the source
             * word. Masking w1 here left the dust and tire-smoke sprites
             * alpha-thresholded: solid grey card-board slabs instead of
             * dissolved puffs. */
            if (cmd == F3D_SETOTHERMODE_H)
                s_othermode_h = (s_othermode_h & ~mask)
                              | (unsigned int)w1
                              | (0x2fu << 24);
            else
                s_othermode_l = (s_othermode_l & ~mask)
                              | (unsigned int)w1;
            s_zbuffered = (((s_othermode_l >> 4) & 1u) ||
                           ((s_othermode_l >> 5) & 1u)) ? 1 : 0;
            two[0] = (int32_t)(s_othermode_h | (0x2fu << 24));
            two[1] = (int32_t)s_othermode_l;
            rdp_fifo_append(fifo, two, 2);
            break;
        }

        case F3D_MOVEMEM:
        {
            unsigned int idx = (w0 >> 16) & 0xffu;
            unsigned int ma  = seg_phys(w1);
            if (idx == F3D_MV_VIEWPORT)
            {
                if (in_range(ma, 16u))
                {
                    gsp_set_viewport(gsp, r, ma);
                    /* SETA's custom transform maps NDC +Y down the screen:
                     * the attract fly-over's triangles land mirrored around
                     * vtrans.y against the LLE RSP (y 88.75 vs 151.25 about
                     * the 120-line centre) with X exact. Negate the loaded
                     * Y scale so the shared vertex pipeline flips to match. */
                    if (s_variant_seta)
                        gsp->viewport.vscale_y = -gsp->viewport.vscale_y;
                }
            }
            else if (idx == F3D_MV_LOOKATX)
            {
                if (in_range(ma, 16u))
                    gsp_set_lookat(gsp, r, ma, 0);
            }
            else if (idx == F3D_MV_LOOKATY)
            {
                if (in_range(ma, 16u))
                    gsp_set_lookat(gsp, r, ma, 1);
            }
            else if (idx == F3D_MV_MATRIX_1 || idx == F3D_MV_MATRIX_2
                     || idx == F3D_MV_MATRIX_3 || idx == F3D_MV_MATRIX_4)
            {
                /* gSPForceMatrix (Top Gear Rally loads a combined matrix
                 * per object): four 16-byte chunks addressed by index. */
                static const unsigned char moff[4] = { 16, 32, 48, 0 };
                if (in_range(ma, 16u))
                    gsp_force_matrix_chunk(gsp, r, ma,
                                           moff[(idx - F3D_MV_MATRIX_2) >> 1]);
            }
            else if (idx >= F3D_MV_L0)
            {
                /* G_MV_L0/L1/...: directional/ambient lights, step 2 per slot */
                int li = (int)((idx - F3D_MV_L0) >> 1);
                if (li >= 0 && li < GSP_MAX_LIGHTS && in_range(ma, 16u))
                    gsp_set_light(gsp, r, ma, li);
            }
            break;
        }

        case F3D_MOVEWORD:
        {
            unsigned int index = w0 & 0xffu;
            switch (index)
            {
            case F3D_MW_SEGMENT:
                s_seg[(w0 >> 10) & 0x0fu] = w1;
                break;
            case F3D_MW_NUMLIGHT:
                gsp_set_num_lights(gsp,
                    (int)(((w1 - 0x80000000u) >> 5) - 1u));
                break;
            case F3D_MW_FOG:
                gsp_set_fog(gsp, (int)(int16_t)(w1 >> 16),
                                 (int)(int16_t)(w1 & 0xffff));
                break;
            case F3D_MW_CLIP:
            {
                /* gSPClipRatio: four moveword writes update the scaled clip
                 * planes; the positive-side words (offsets 0x04 and 0x0c)
                 * carry the ratio. The ratio is runtime state, not a ucode
                 * build property -- Last Legion UX runs the same F3DLX
                 * build as Wipeout 64 but sends gSPClipRatio(FRUSTRATIO_2),
                 * and with the variant's ratio-1 default its guard band
                 * clipped terrain triangles the real microcode passes to
                 * the rasterizer whole; the reshaped clip fans z-buffered
                 * differently and the T3DUX mech partially z-failed against
                 * the mis-clipped ground. */
                unsigned int off = (w0 >> 8) & 0xffffu;
                if (off == 0x04u || off == 0x0cu)
                {
                    int rv = (int)(int16_t)(w1 & 0xffffu);
                    if (rv > 0)
                        gsp->clip_ratio = rv;
                }
                break;
            }
            case F3D_MW_PERSPNORM:
                gsp_set_persp_norm(gsp, w1 & 0xffffu);
                break;
            case F3D_MW_POINTS:
            {
                unsigned int val = (w0 >> 8) & 0xffffu;
                gsp_modify_vertex(gsp, (int)(val / 40u), val % 40u, w1);
                break;
            }
            case F3D_MW_MATRIX:
            {
                /* gSPInsertMatrix: patch one 32-bit word of the combined MVP
                 * (two s16 halves). Offsets 0x00-0x1f the integer halves,
                 * 0x20-0x3f the fractions. */
                unsigned int off = (w0 >> 8) & 0xffffu;
                if (off < 0x40u && (off & 3u) == 0u)
                {
                    int mr = (int)((off & 0x1fu) >> 3);
                    int mc = (int)((off & 7u) >> 1);
                    int32_t *e0 = &gsp->combined[mr][mc];
                    int32_t *e1 = &gsp->combined[mr][mc + 1];
                    if (off < 0x20u)
                    {
                        *e0 = (int32_t)((w1 & 0xffff0000u)
                                        | ((uint32_t)*e0 & 0xffffu));
                        *e1 = (int32_t)(((w1 & 0xffffu) << 16)
                                        | ((uint32_t)*e1 & 0xffffu));
                    }
                    else
                    {
                        *e0 = (int32_t)(((uint32_t)*e0 & 0xffff0000u)
                                        | ((w1 >> 16) & 0xffffu));
                        *e1 = (int32_t)(((uint32_t)*e1 & 0xffff0000u)
                                        | (w1 & 0xffffu));
                    }
                }
                break;
            }
            case F3D_MW_LIGHTCOL:
            {
                /* gSPLightColor: overwrite a light's RGB in place (packed
                 * RGBA in w1); the offset selects the light (aLIGHT_1=0x00,
                 * aLIGHT_2=0x20, ...). Previously dropped, which left the
                 * stale G_MOVEMEM color -- e.g. Robotron 64's title head,
                 * whose flesh tint is applied via LIGHTCOL over a
                 * gray-loaded light. */
                unsigned int loff = (w0 >> 8) & 0xffffu;
                gsp_set_light_color(gsp, (int)(loff / 0x20u),
                                    (int32_t)((w1 >> 24) & 0xffu),
                                    (int32_t)((w1 >> 16) & 0xffu),
                                    (int32_t)((w1 >>  8) & 0xffu));
                break;
            }
            default:
                break;
            }
            break;
        }

        case F3D_CULLDL:
            /* Wipeout sprite ucode: 0xBE carries the texrect dsdx/dtdy. */
            if (s_spr_have)
            {
                s_spr_size = w1;
                s_spr_flip = (unsigned int)w0 & 0xffffu;
                break;
            }
            /* gSPCullDisplayList: the microcode ANDs the span's stored
             * screen outcodes and, when every vertex is outside the
             * same plane, returns from the current display list
             * immediately through the G_ENDDL code. Ending the list is
             * a state matter, not just a speed-up: the skipped tail can
             * carry matrix and other state commands the microcode never
             * executes. */
            {
                int v0 = (int)(((w0 >> 16) & 0xffu) / 2u);
                int vn = (int)((w1 & 0xffu) / 2u);
                if (gsp_culldl_test(gsp, v0, vn))
                    running = 0;
            }
            break;

        case 0xAF:
            /* G_LOAD_UCODE (gsSPLoadUcodeEx): the RSP reloads the microcode
             * named by w1 with the data segment staged through the preceding
             * G_RDPHALF_1, and the new microcode's DMEM initialisation
             * replaces the RDP other-modes with the defaults stored in its
             * data segment (the EF command image at data + 0x118) -- the same
             * words the per-task setup reads for the primary microcode.
             * Fighting Force 64 swaps microcodes mid-list around its menu
             * text; without the reload the walker's accumulated other-modes
             * leak into the overlay's glyph rectangles (a TLUT mode left
             * enabled turns the IA font invisible). The overlay command sets
             * used by known titles stay F3D-compatible, so the walk simply
             * continues. An armed S2DEX handoff stops at 0xAF before this
             * case is reached. */
            {
                unsigned int ud  = seg_phys(s_last_rdphalf1);
                unsigned int uds = (w0 & 0xffffu) + 1u;
                /* SimCity 2000 (Japan) hot-swaps its F3DEX.NoN / F3DLX.Rej
                 * vertex lists to S2DEX 1.03 mid-list to blit the composed
                 * terrain map to the framebuffer (G_BG_1CYC splits it into
                 * full-width TMEM strips). S2DEX's command set is not
                 * F3D-compatible -- its low opcodes collide with G_MTX/
                 * G_MOVEMEM -- so walking on eats the background draw and the
                 * terrain never appears. Detect the staged data segment by
                 * its S2DEX 1.xx name and hand the rest of the list to the
                 * S2DEX GBI 1 walker; its own G_LOAD_UCODE handler routes any
                 * later F3D vertex section back through this walker, so the
                 * alternation unwinds naturally and this invocation is done. */
                if (s_last_rdphalf1 != 0 && ud != 0 &&
                    s2dex1_ucode_match(s_rdram, s_rdram_size, ud, uds))
                {
                    f3dex2_set_rdram(s_rdram);
                    f3dex2_set_rdram_size(s_rdram_size);
                    f3dex2_set_task_ucode(s_rdram, seg_phys(w1));
                    f3dex2_force_class_s2dex1();
                    f3dex2_import_segments(s_seg);
                    /* The RSP's gSPLoadUcode preserves the other-modes state
                     * across the swap (per-task seeding notes in
                     * rdp_emit_hle.c, validated pixel-exact on Kirby 64's
                     * round-trips), so hand the walker's accumulated shadow
                     * to the S2DEX side: its G_BG_1CYC compositor emits
                     * SETOTHERMODE from that base, and a stale shadow left
                     * G_TC_CONV selected -- every CI8 background texel then
                     * went through the YUV converter and drew black. */
                    f3dex2_set_othermode_init(s_othermode_h, s_othermode_l);
                    f3dex2_run_dl(gsp, fifo, pc, s_textured, s_zbuffered);
                    running = 0;
                    break;
                }
                if (ud != 0 && in_range(ud, 0x120u))
                {
                    unsigned int oh = ((unsigned int)r[(ud + 0x118) ^ 3] << 24)
                                    | ((unsigned int)r[(ud + 0x119) ^ 3] << 16)
                                    | ((unsigned int)r[(ud + 0x11a) ^ 3] << 8)
                                    |  (unsigned int)r[(ud + 0x11b) ^ 3];
                    unsigned int ol = ((unsigned int)r[(ud + 0x11c) ^ 3] << 24)
                                    | ((unsigned int)r[(ud + 0x11d) ^ 3] << 16)
                                    | ((unsigned int)r[(ud + 0x11e) ^ 3] << 8)
                                    |  (unsigned int)r[(ud + 0x11f) ^ 3];
                    /* The reload is silent: the microcode's next
                     * SETOTHERMODE emits from the fresh base. */
                    if ((oh >> 24) == 0xefu)
                        f3d_set_othermode_init(oh, ol);
                }
            }
            break;

        case F3D_RDPHALF_CONT:
            /* The Doom 64 lineage's 3D use of the same 0xB2 vertex-patch
             * extension: Rayman 2's pause-menu nebula is a quad grid whose
             * shared vertex slots are re-textured per cell with sub-op 0x14
             * between gSP1Quadrangle calls (the scroll never re-runs the
             * transform).  The patch stores the S10.5 texel shorts through
             * the same G_TEXTURE scale multiply as the vertex path's TC
             * transform (one vmudm mid read); the LLE oracle's implied
             * per-vertex record S for the 0x0800 patches is exactly
             * (0x800 * 0xFFFF) >> 16 == 2047. */
            if (s_variant_d64 && ((w0 >> 16) & 0xff) == 0x14)
            {
                int slot = (int)(w0 & 0xffff) >> 1;
                if (slot >= 0 && slot < GSP_MAX_VERTICES)
                {
                    GSPVertex *v = &gsp->vtx[slot];
                    v->sv = (int16_t)((w1 >> 16) & 0xffffu);
                    v->tv = (int16_t)(w1 & 0xffffu);
                    v->s = (int32_t)v->sv << 1;
                    v->t = (int32_t)v->tv << 1;
                }
                break;
            }
            /* Fighting Force 64's 2D overlay extension (see
             * f3d_is_ff2d_ucode above). */
            if (s_variant_ff2d)
            {
                int sub  = (int)((w0 >> 16) & 0xff);
                int slot = (int)(w0 & 0xffff) >> 1;
                if (slot >= 0 && slot < GSP_MAX_VERTICES)
                {
                    GSPVertex *v = &gsp->vtx[slot];
                    if (sub == 0x18)
                    {
                        /* two 10.2 screen coordinates; snapshots are s15.16 */
                        v->scr_x = (int32_t)((w1 >> 16) & 0xffffu) << 14;
                        v->scr_y = (int32_t)(w1 & 0xffffu) << 14;
                        v->scr_z = 0;
                        v->clip = 0;
                        v->cx = 0; v->cy = 0; v->cz = 0;
                        v->cw = 0x10000;
                        /* Texture coordinates are staged by a preceding
                         * sub-op 0x14 write; do not disturb them here. */
                        v->w_raw = 0x10000;
                        v->rsp_ok = 1;
                        v->rsp_invw = 0x7fff0000;
                        v->flat2d = 1;
                    }
                    else if (sub == 0x14)
                    {
                        /* two S10.5 texel coordinates (0x20 == 1 texel); the
                         * title-screen glyph quads map 1:1. Store the RSP's
                         * VTX_TC shorts and their doubled form the way the
                         * transform path does. */
                        v->sv = (int16_t)((w1 >> 16) & 0xffffu);
                        v->tv = (int16_t)(w1 & 0xffffu);
                        v->s = (int32_t)v->sv << 1;
                        v->t = (int32_t)v->tv << 1;
                    }
                    else if (sub == 0x10)
                    {
                        v->r = (int32_t)((w1 >> 24) & 0xffu) << 16;
                        v->g = (int32_t)((w1 >> 16) & 0xffu) << 16;
                        v->b = (int32_t)((w1 >>  8) & 0xffu) << 16;
                        v->a = (int32_t)(w1 & 0xffu) << 16;
                    }
                    /* sub 0x1c carries a per-vertex W; every observed use is
                     * zero (flat 2D) and the injected vertices are already
                     * flat, so it needs no action. */
                }
                break;
            }
            /* fall through: stock Fast3D stages texrect words here */
        case F3D_RDPHALF_2:
            /* Staged texrect coordinates; consumed by the G_TEXRECT handler
             * below. A stray RDPHALF outside a texrect is a no-op. */
            break;

        case 0xB0:
        {
            /* G_BRANCHZ (gsSPBranchLessZ / gsSPBranchLessZraw, GBI 1): branch
             * -- not call -- to the display list staged by the preceding
             * G_RDPHALF_1 when the referenced vertex's 32-bit screen-Z word
             * is below the command's threshold. w0 packs the vertex index
             * twice (bits 12-23 as index*5, bits 0-11 as index*2); the
             * F3DEX 1.x line compares the stored screen Z, the same
             * vertex+0x1c word its F3DEX2 successor loads. Choro Q 64
             * LOD-selects every racer with it -- the chase-cam car sits
             * closest, so dropping the opcode left the player's own car
             * invisible (its hi-LOD sub-list was never entered). */
            int bv = (int)((w0 & 0xfffu) >> 1);
            unsigned int ba = seg_rsp(s_last_rdphalf1) & 0x00ffffffu;
            unsigned int pw = (pc >= 16u) ? rd32(r, pc - 16u) : 0u;
            /* The gbi macros always emit the pair G_RDPHALF_1 + G_BRANCHZ
             * back to back, so require the staged address to come from the
             * immediately preceding command; a fork that repurposes the
             * 0xB0 byte then never takes a branch off a stale slot. */
            if (((pw >> 24) & 0xffu) == F3D_RDPHALF_1
                && bv >= 0 && bv < GSP_MAX_VERTICES && in_range(ba, 8u)
                && gsp->vtx[bv].scr_z < (int32_t)w1)
                pc = ba;
            break;
        }

        case F3D_RDPHALF_1:
            /* gsSPLoadUcode stages the new microcode's data address through
             * G_RDPHALF_1 immediately before G_LOAD_UCODE; remember it for
             * the 0xAF handler below. */
            s_last_rdphalf1 = w1;
            /* GoldenEye and Perfect Dark draw the level backdrop (the gradient
             * sky and horizon) by streaming a complete RDP edge-triangle
             * command through the G_RDPHALF channel: the first G_RDPHALF_1
             * carries the triangle command word (an edge-triangle opcode,
             * 0x08..0x0f, in its high byte) and each following G_RDPHALF_1 /
             * G_RDPHALF_CONT carries one more command word. The RSP accumulates
             * them and forwards the finished command to the RDP. Reassemble the
             * stream and forward it. Gated on the F3DEX (GoldenEye/Perfect Dark)
             * variant and the edge-triangle signature so the SM64 perspnorm use
             * of a bare G_RDPHALF_1 below, and the texrect coordinate tail words
             * (consumed inline by the 0xE4/0xE5 assembler, which never reach
             * this case), are both untouched. */
            if (s_variant_f3dex && ((w1 >> 24) & 0x3f) >= 0x08
                                && ((w1 >> 24) & 0x3f) <= 0x0f)
            {
                int32_t raw[40];
                int wi = 0;
                raw[wi++] = (int32_t)w1;
                while (wi < 40)
                {
                    unsigned int nx = rd32(r, pc);
                    int nhc = (int)((nx >> 24) & 0xff);
                    if (nhc != F3D_RDPHALF_1 && nhc != F3D_RDPHALF_CONT &&
                        nhc != F3D_RDPHALF_2)
                        break;
                    raw[wi++] = (int32_t)rd32(r, pc + 4);
                    pc += 8;
                }
                while (wi < 40)
                    raw[wi++] = 0;
                rdp_fifo_append(fifo, raw, 40);
                break;
            }
            /* On the old F3D microcode (retail Super Mario 64) gSPPerspNormalize
             * is not a G_MOVEWORD: it stores the perspective-normalize factor in
             * the RDPHALF_1 slot with a bare G_RDPHALF_1 word, and the vertex
             * transform reads perspNorm from that slot. (The newer GBI / F3DEX2
             * path uses G_MOVEWORD/G_MW_PERSPNORM, handled above.) The texrect
             * tail RDPHALF words are consumed inline by the 0xE4/0xE5 assembler
             * and never reach here, so a G_RDPHALF_1 seen at this point is the
             * perspective-normalize write; mirror the slot into perspNorm.
             * Doom 64's gspL3DEX repurposes the RDPHALF_1 slot for packed
             * segment parameters (18 per automap task); mirroring those
             * would clobber the real perspNorm between the vertex loads and
             * the draws, which moves the clip lerp's reprojection of
             * generated vertices (bottom-edge clips land at 240.0 instead
             * of the microcode's 239.75). Blast Corps' Fast3D line build
             * keeps the old-F3D bare-RDPHALF_1 perspNorm convention. */
            if (!s_variant_d64)
                gsp_set_persp_norm(gsp, w1 & 0xffffu);
            break;

        default:
            
            /* TEXTURE_RECTANGLE (0xE4) / _FLIP (0xE5) is a 4-word RDP command,
             * but F3D delivers it split: the G_TEXRECT word pair carries the
             * rectangle + tile, and the following two G_RDPHALF commands carry
             * the texture coordinates -- RDPHALF_2 (0xB3) the s/t pair and
             * RDPHALF_1 (0xB4) the dsdx/dtdy pair. Forwarding only the first
             * two words leaves angrylion to consume the next command as the
             * rectangle's tail, corrupting the HUD and sky tiles. Assemble the
             * full command and skip the two RDPHALF words consumed. */
            if (cmd == 0xe4 || cmd == 0xe5)
            {
                unsigned int h_st = 0u, h_dsdt = 0u;
                int32_t tr[4];
                int k;
                /* The two coordinate words follow as RDPHALF-family commands.
                 * SM64 uses RDPHALF_2 (0xB3) for the s/t pair and RDPHALF_CONT
                 * (0xB2) for the dsdx/dtdy pair, but assign by position (first
                 * tail word -> s/t = RDP word2, second -> dsdx/dtdy = word3) so
                 * the exact opcode the build chose does not matter. */
                for (k = 0; k < 2; k++)
                {
                    unsigned int c0 = rd32(r, pc);
                    int hc = (int)((c0 >> 24) & 0xff);
                    if (hc != F3D_RDPHALF_CONT && hc != F3D_RDPHALF_2 &&
                        hc != F3D_RDPHALF_1)
                        break;
                    if (k == 0)
                        h_st = rd32(r, pc + 4);
                    else
                        h_dsdt = rd32(r, pc + 4);
                    pc += 8;
                }
                tr[0] = (int32_t)w0;
                tr[1] = (int32_t)w1;
                tr[2] = (int32_t)h_st;
                tr[3] = (int32_t)h_dsdt;
                rdp_fifo_append(fifo, tr, 4);
            }
            else if (cmd >= 0xc8)
                rdp_passthrough(gsp, fifo, cmd, w0, w1);
            break;
        }
    }

    s_dl_depth--;
}
