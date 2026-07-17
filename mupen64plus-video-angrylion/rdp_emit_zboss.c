/* rdp_emit_zboss.c -- ZSortBOSS microcode walker for the angrylion HLE path.
 *
 * BOSS Game Studios' z-sort microcode (World Driver Championship, Stunt
 * Racer 64). The CPU z-sorts; the RSP is a command server: it transforms
 * point batches back into DMEM for the sorter (MULT_MPMTX), concatenates
 * matrices, runs the audio decoder/resampler, and draws pre-sorted chains
 * of screen-space objects. Grammar and per-command semantics transcribed
 * from GLideN64's uCodes/ZSortBOSS.cpp (same repository); the task/slice
 * protocol was audited from the microcode's live behavior:
 *
 *  - the OSTask carries TWO display lists: main (DMEM 0xff0) and sub
 *    (DMEM 0xff8); G_ZSBOSS_ENDMAINDL waits for the CPU to raise SIG0,
 *    clears it and continues on the sub list; G_ZSBOSS_ENDSUBDL ends the
 *    task (status SIG4|SIG2, observed 0xa43 at completion),
 *  - G_ZSBOSS_WAITSIGNAL raises SIG3 and suspends until the CPU clears
 *    it (observed spin sites IMEM 0xfc/0x13c under the LLE interpreter),
 *  - the task raises SIG4 at launch and keeps it through completion.
 *
 * The walk is sliced exactly like the Gauntlet streaming walker: on a
 * wait the interpreter returns to rsp-hle, which returns the task
 * incomplete (no HALT/BROKE/TASKDONE) so the core re-dispatches after
 * the CPU has run; rsp-hle owns every SP_STATUS bit.
 *
 * C89; build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include <string.h>
#include <math.h>
#include "rdp_emit_zboss.h"
#include "rdp_emit_rsp.h"

/* in-word byte order (see rdp_emit_frontend.c) */
#ifdef MSB_FIRST
#define BO16 0u
#define BO8  0u
#else
#define BO16 2u
#define BO8  3u
#endif

#define ZH_NULL   0
#define ZH_TXTRI  4
#define ZH_TXQUAD 8

/* wait kinds for the sliced protocol */
#define ZB_WAIT_NONE 0
#define ZB_WAIT_SIG3 1      /* WAITSIGNAL: CPU must clear SIG3 */
#define ZB_WAIT_SIG0 2      /* ENDMAINDL: CPU must raise SIG0 */

typedef struct ZbState
{
    int             active;
    int             wait_kind;
    unsigned int    pc[2];
    int             pci;
    int             maindl_done;
    int             subdl_done;
    unsigned int    updatemask[2];
    unsigned int    othermode_h;
    unsigned int    othermode_l;
    unsigned int    rdpcmds[3];
    float           mtx_model[4][4];
    float           mtx_proj[4][4];
    float           mtx_comb[4][4];
    float           view_scale[2];
    float           view_trans[2];
    float           invw_factor;
    int             fog_mult;
    int             fog_off;
    unsigned char   fogtable[256];
    short           audio_table[8][8];
    int             tri_tile;
    int             tri_level;
} ZbState;

static ZbState zb;

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;
static unsigned char *s_dmem;
static RdpFifo       *s_fifo;

/* ---- memory accessors -------------------------------------------------- */

static unsigned int rd32(const unsigned char *b, unsigned int a)
{
    return ((unsigned int)b[(a) ^ BO8] << 24)
         | ((unsigned int)b[(a + 1) ^ BO8] << 16)
         | ((unsigned int)b[(a + 2) ^ BO8] << 8)
         |  (unsigned int)b[(a + 3) ^ BO8];
}

static void wr32(unsigned char *b, unsigned int a, unsigned int v)
{
    b[(a) ^ BO8]     = (unsigned char)(v >> 24);
    b[(a + 1) ^ BO8] = (unsigned char)(v >> 16);
    b[(a + 2) ^ BO8] = (unsigned char)(v >> 8);
    b[(a + 3) ^ BO8] = (unsigned char)v;
}

static int rd_s16(const unsigned char *b, unsigned int a)
{
    int v = ((int)b[a ^ BO8] << 8) | (int)b[(a + 1) ^ BO8];
    if (v & 0x8000) v -= 0x10000;
    return v;
}

static void wr_s16(unsigned char *b, unsigned int a, int v)
{
    b[a ^ BO8]       = (unsigned char)((v >> 8) & 0xff);
    b[(a + 1) ^ BO8] = (unsigned char)(v & 0xff);
}

static int rd_s8(const unsigned char *b, unsigned int a)
{
    int v = (int)b[a ^ BO8];
    if (v & 0x80) v -= 0x100;
    return v;
}

static unsigned int seg_phys(unsigned int a)
{
    /* the BOSS GBI has no segment command; addresses are physical */
    return a & 0x00ffffffu;
}

static int addr_ok(unsigned int a, unsigned int len)
{
    return a < s_rdram_size && a + len <= s_rdram_size;
}

/* ---- matrices (s15.16 in RDRAM, float mirrors, per the reference) ------ */

static void zb_load_matrix(float m[4][4], unsigned int addr)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            int hi = rd_s16(s_rdram, addr + (unsigned int)(i * 8 + j * 2));
            int lo = ((int)s_rdram[(addr + 32u + (unsigned int)(i * 8 + j * 2)) ^ BO8] << 8)
                   |  (int)s_rdram[(addr + 32u + (unsigned int)(i * 8 + j * 2) + 1u) ^ BO8];
            m[i][j] = (float)hi + (float)lo * (1.0f / 65536.0f);
        }
}

static void zb_store_matrix(const float m[4][4], unsigned int addr)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            float el = m[i][j];
            int hi, lo;
            if (el > 32767.0f) el = 32767.0f;
            if (el < -32768.0f) el = -32768.0f;
            hi = (int)floor((double)el);
            lo = (int)((el - (float)hi) * 65536.0f) & 0xffff;
            wr_s16(s_rdram, addr + (unsigned int)(i * 8 + j * 2), hi);
            s_rdram[(addr + 32u + (unsigned int)(i * 8 + j * 2)) ^ BO8]
                = (unsigned char)((lo >> 8) & 0xff);
            s_rdram[(addr + 32u + (unsigned int)(i * 8 + j * 2) + 1u) ^ BO8]
                = (unsigned char)(lo & 0xff);
        }
}

static float *zb_mtx_slot(unsigned int dmem_off)
{
    switch (dmem_off)
    {
    case 0x830: return &zb.mtx_model[0][0];
    case 0x870: return &zb.mtx_proj[0][0];
    case 0x8b0: return &zb.mtx_comb[0][0];
    }
    return 0;
}

static void zb_mult_matrix(const float a[4][4], const float b[4][4],
                           float d[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            d[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j]
                    + a[i][2] * b[2][j] + a[i][3] * b[3][j];
}

/* ---- RDP output -------------------------------------------------------- */

static void zb_emit_othermode(void)
{
    int32_t two[2];
    two[0] = (int32_t)(0xef000000u | (zb.othermode_h & 0x00ffffffu));
    two[1] = (int32_t)zb.othermode_l;
    rdp_fifo_append(s_fifo, two, 2);
}

/* the RDP-side commands the microcode interprets rather than forwards:
 * partial/masked other-mode merges, the update mask, and the triangle
 * render-state command. Shared between the display-list interpreter and
 * the RDPCMD chains, which run through the same command table (the
 * reference's ZSort_RDPCMD dispatches every entry through GBI.cmd). */
static int zb_dispatch_ucode_rdp(unsigned int cmd, unsigned int w0,
                                 unsigned int w1)
{
    switch (cmd)
    {
    case 0xdd:                              /* UPDATEMASK */
        zb.updatemask[0] = w0 | 0xff000000u;
        zb.updatemask[1] = w1;
        return 1;
    case 0xde:                              /* TRIANGLECOMMAND */
        zb.tri_level = (int)((w1 >> 3) & 7u);
        zb.tri_tile  = (int)(w1 & 7u);
        return 1;
    case 0xdf:                              /* FLUSHRDPCMDBUFFER */
    case 0xe1:                              /* RDPHALF_1 */
    case 0xf1:                              /* RDPHALF_2 */
        return 1;
    case 0xe2:                              /* SETOTHERMODE_L */
    {
        unsigned int mask =
            (unsigned int)((int)0x80000000 >> (int)(w0 & 0x1fu));
        mask >>= (w0 >> 8) & 0x1fu;
        zb.othermode_l = (zb.othermode_l & ~mask) | w1;
        zb_emit_othermode();
        return 1;
    }
    case 0xe3:                              /* SETOTHERMODE_H */
    {
        unsigned int mask =
            (unsigned int)((int)0x80000000 >> (int)(w0 & 0x1fu));
        mask >>= (w0 >> 8) & 0x1fu;
        zb.othermode_h = (zb.othermode_h & ~mask) | w1;
        zb_emit_othermode();
        return 1;
    }
    case 0xef:                              /* RDPSETOTHERMODE (masked) */
        zb.othermode_h = (w0 & zb.updatemask[0])
                       | (zb.othermode_h & ~zb.updatemask[0]);
        zb.othermode_l = (w1 & zb.updatemask[1])
                       | (zb.othermode_l & ~zb.updatemask[1]);
        zb_emit_othermode();
        return 1;
    }
    return 0;
}

/* run a chain of RDP commands at an RDRAM address until 0xDF, through
 * the command table (transcribed from ZSort_RDPCMD: TEXRECT spans three
 * command pairs) */
static void zb_rdpcmd(unsigned int w1)
{
    unsigned int addr = seg_phys(w1);
    int guard = 0;
    if (addr == 0)
        return;
    while (guard++ < 65536)
    {
        unsigned int c0, c1, op;
        if (!addr_ok(addr, 8u))
            return;
        c0 = rd32(s_rdram, addr);
        op = (c0 >> 24) & 0xffu;
        if (op == 0xdfu)
            return;
        c1 = rd32(s_rdram, addr + 4u);
        addr += 8u;
        if (zb_dispatch_ucode_rdp(op, c0, c1))
            continue;
        if (op >= 0xc8u && op <= 0xcfu)
        {
            /* raw RDP triangle built by the CPU: forward the full
             * command (8..44 words). The LLE game path renders this
             * way (CPU-computed coefficients in the chains); a
             * two-word forward would misparse the coefficients as
             * commands. */
            static const unsigned char tri_words[8] =
                { 8, 12, 24, 28, 24, 28, 40, 44 };
            unsigned int clen = tri_words[op & 7u];
            int32_t buf[44];
            unsigned int q;
            buf[0] = (int32_t)c0;
            buf[1] = (int32_t)c1;
            if (!addr_ok(addr, (clen - 2u) * 4u))
                return;
            for (q = 2; q < clen; q++)
            {
                buf[q] = (int32_t)rd32(s_rdram, addr);
                addr += 4u;
            }
            rdp_fifo_append(s_fifo, buf, (int)clen);
            continue;
        }
        if (op == 0xe4u || op == 0xe5u)
        {
            /* rectangle pair + the s/t and dsdx/dtdy halves from the two
             * following command pairs (second word of each) */
            int32_t tr[4];
            unsigned int h0 = 0u, h1 = 0u;
            if (addr_ok(addr, 16u))
            {
                h0 = rd32(s_rdram, addr + 4u);
                h1 = rd32(s_rdram, addr + 12u);
                addr += 16u;
            }
            tr[0] = (int32_t)c0;
            tr[1] = (int32_t)c1;
            tr[2] = (int32_t)h0;
            tr[3] = (int32_t)h1;
            rdp_fifo_append(s_fifo, tr, 4);
        }
        else if (op == 0x29u)
        {
            rdp_fifo_fullsync_note();
        }
        else if (op >= 0xc8u)
        {
            int32_t two[2];
            two[0] = (int32_t)c0;
            /* image pointers may not be segmented here (physical GBI),
             * but mask to the RDRAM window as the RSP does */
            if (op == 0xffu || op == 0xfeu || op == 0xfdu)
                two[1] = (int32_t)seg_phys(c1);
            else
                two[1] = (int32_t)c1;
            rdp_fifo_append(s_fifo, two, 2);
        }
    }
}

/* ---- object drawing ---------------------------------------------------- */

static int zb_calc_invw(int w)
{
    if (w == 0)
        return 0x7fffffff;
    return (int)(0x7fffffffl / (long)w);
}

/* emit one triangle through the RSP edge writer, guarding the two ways
 * a screen-space sliver can poison the software rasterizer: zero area
 * feeds the reciprocal chain garbage, and the resulting coefficients
 * can carry y spans thousands of lines outside the input bounding box.
 * Such triangles cover no pixels the fill rules would keep; drop them. */
static void zb_emit_tri(const RspTriVtx *a, const RspTriVtx *b,
                        const RspTriVtx *c)
{
    int32_t cmd[GSP_ZB_TRI_WORDS];
    int nw;
    int ymin_v, ymax_v, yl, yh;
    long area2 = (long)(b->x - a->x) * (long)(c->y - a->y)
               - (long)(b->y - a->y) * (long)(c->x - a->x);
    if (area2 == 0)
        return;
    nw = rsp_tri_write(cmd, a, b, c,
                       1 /*textured*/, 0 /*z*/, 1 /*shaded*/, 1 /*smooth*/,
                       zb.tri_tile, zb.tri_level,
                       0x4000, 0x0008, (int32_t)0xffffffff /*frac*/, 0x01cc);
    if (nw <= 0)
        return;
    ymin_v = a->y; if (b->y < ymin_v) ymin_v = b->y;
    if (c->y < ymin_v) ymin_v = c->y;
    ymax_v = a->y; if (b->y > ymax_v) ymax_v = b->y;
    if (c->y > ymax_v) ymax_v = c->y;
    yl = (int)(cmd[0] & 0x3fff);  if (yl & 0x2000) yl -= 0x4000;
    yh = (int)(cmd[1] & 0x3fff);  if (yh & 0x2000) yh -= 0x4000;
    if (yh < ymin_v - 4 || yl > ymax_v + 4)
        return;
    rdp_fifo_append(s_fifo, cmd, nw);
}

static void zb_draw_object(unsigned int addr, unsigned int type)
{
    RspTriVtx v[4];
    unsigned int vnum = (type == ZH_TXQUAD) ? 4u : 3u;
    unsigned int i;

    for (i = 0; i < vnum; i++)
    {
        unsigned int va = addr + i * 16u;
        int invw_rec, w_rec;
        if (!addr_ok(va, 16u))
            return;
        v[i].x = (int16_t)rd_s16(s_rdram, va);          /* s13.2 */
        v[i].y = (int16_t)rd_s16(s_rdram, va + 2u);
        v[i].z = 0;
        v[i].r = s_rdram[(va + 4u) ^ BO8];
        v[i].g = s_rdram[(va + 5u) ^ BO8];
        v[i].b = s_rdram[(va + 6u) ^ BO8];
        v[i].a = s_rdram[(va + 7u) ^ BO8];
        v[i].s = rd_s16(s_rdram, va + 8u);              /* S10.5 tc */
        v[i].t = rd_s16(s_rdram, va + 10u);
        invw_rec = (int)rd32(s_rdram, va + 12u);
        /* the reference treats invw == rgba or invw < 0 as "no
         * perspective" (w = 1) */
        if (invw_rec == (int)rd32(s_rdram, va + 4u) || invw_rec < 0)
        {
            v[i].invw = 0x7fffffff;
            v[i].pw   = 1;
        }
        else
        {
            /* invw_rec = 0x7fffffff / (w * invw_factor); the texture
             * normalizer in rsp_tri_write only uses invw/pw relatively
             * (norm_v = invw_v * min(pw)), so a consistent reciprocal
             * pair in the record's own domain is sufficient */
            w_rec = zb_calc_invw(invw_rec);
            v[i].invw = invw_rec;
            v[i].pw   = w_rec;
        }
        v[i].flat2d = 0;
    }

    /* The CPU hands the microcode unclipped screen-space objects whose
     * far vertices run to the s13.2 extremes; the F3DEX2 edge writer's
     * fixed point (y*0x4000 anchor products, slope reciprocals) assumes
     * the microcode's own post-clip domain and saturates on such input,
     * emitting commands with ym/yh pinned at 0x8001 whose slopes sweep
     * thousands of on-screen pixels (observed 9.4k px per triangle,
     * 2.7x total triangle fill). BOSS's own triangle setup tolerates
     * the wide domain; ours must not see it. Clip the polygon to a
     * guard band around the frame in screen space before emission --
     * attributes interpolate linearly in screen space, which is the
     * plane equation the RDP evaluates, so this is render-equivalent
     * inside the scissor. TXQUAD vertices arrive in strip order; the
     * polygon ring is 0,1,3,2 and the fan diagonal matches the strip's
     * shared edge. */
    {
        static const int gmin = -(64 << 2);
        static const int gmax_x = (704 << 2);
        static const int gmax_y = (544 << 2);
        RspTriVtx poly[10], tmp[10];
        unsigned int pn, tn, k2;
        int pass;
        poly[0] = v[0];
        poly[1] = v[1];
        if (vnum == 4u)
        {
            poly[2] = v[3];
            poly[3] = v[2];
        }
        else
            poly[2] = v[2];
        pn = vnum;
        for (pass = 0; pass < 4 && pn >= 3u; pass++)
        {
            int lim  = (pass == 0 || pass == 2) ? gmin
                     : (pass == 1) ? gmax_y : gmax_x;
            int usey = (pass < 2);
            tn = 0;
            for (k2 = 0; k2 < pn; k2++)
            {
                const RspTriVtx *a = &poly[k2];
                const RspTriVtx *b = &poly[(k2 + 1u) % pn];
                int ca = usey ? a->y : a->x;
                int cb = usey ? b->y : b->x;
                int ina = (pass == 0 || pass == 2) ? (ca >= lim) : (ca <= lim);
                int inb = (pass == 0 || pass == 2) ? (cb >= lim) : (cb <= lim);
                if (ina)
                    tmp[tn++] = *a;
                if (ina != inb)
                {
                    RspTriVtx c;
                    long num = (long)(lim - ca);
                    long den = (long)(cb - ca);
                    long t2 = den ? ((num << 16) / den) : 0;
                    c.x = (int16_t)(a->x + (int)(((long)(b->x - a->x) * t2) >> 16));
                    c.y = (int16_t)(a->y + (int)(((long)(b->y - a->y) * t2) >> 16));
                    if (usey) c.y = (int16_t)lim; else c.x = (int16_t)lim;
                    c.z = 0;
                    c.r = a->r + (int32_t)(((long)(b->r - a->r) * t2) >> 16);
                    c.g = a->g + (int32_t)(((long)(b->g - a->g) * t2) >> 16);
                    c.b = a->b + (int32_t)(((long)(b->b - a->b) * t2) >> 16);
                    c.a = a->a + (int32_t)(((long)(b->a - a->a) * t2) >> 16);
                    c.s = a->s + (int32_t)(((long)(b->s - a->s) * t2) >> 16);
                    c.t = a->t + (int32_t)(((long)(b->t - a->t) * t2) >> 16);
                    c.invw = a->invw
                           + (int32_t)(((long)(b->invw - a->invw) * t2) >> 16);
                    c.pw = zb_calc_invw(c.invw);
                    c.flat2d = 0;
                    if (tn < 10u)
                        tmp[tn++] = c;
                }
            }
            for (k2 = 0; k2 < tn && k2 < 10u; k2++)
                poly[k2] = tmp[k2];
            pn = (tn <= 10u) ? tn : 10u;
        }
        for (k2 = 1; k2 + 1u < pn; k2++)
            zb_emit_tri(&poly[0], &poly[k2], &poly[k2 + 1u]);
    }
}

static unsigned int zb_load_object(unsigned int zheader)
{
    unsigned int type = (zheader & 7u) << 1;
    unsigned int addr = zheader & 0xfffffff8u;
    unsigned int w1;
    if (!addr_ok(addr, 16u))
        return 0;
    if (type == ZH_NULL || type == ZH_TXTRI || type == ZH_TXQUAD)
    {
        w1 = rd32(s_rdram, addr + 4u);
        if (w1 != zb.rdpcmds[0]) { zb.rdpcmds[0] = w1; zb_rdpcmd(w1); }
        w1 = rd32(s_rdram, addr + 8u);
        if (w1 != zb.rdpcmds[1]) { zb_rdpcmd(w1); zb.rdpcmds[1] = w1; }
        w1 = rd32(s_rdram, addr + 12u);
        if (w1 != zb.rdpcmds[2]) { zb_rdpcmd(w1); zb.rdpcmds[2] = w1; }
        if (type != ZH_NULL)
            zb_draw_object(addr + 16u, type);
    }
    return seg_phys(rd32(s_rdram, addr));
}

static void zb_obj(unsigned int w0, unsigned int w1)
{
    unsigned int zheader = seg_phys(w0);
    int guard = 0;
    while (zheader && guard++ < 100000)
        zheader = zb_load_object(zheader);
    zheader = seg_phys(w1);
    while (zheader && guard++ < 100000)
        zheader = zb_load_object(zheader);
}

/* ---- compute commands -------------------------------------------------- */

static void zb_movemem(unsigned int w0, unsigned int w1)
{
    int flag = (int)((w0 >> 23) & 1u);
    unsigned int len = 1u + ((w0 >> 12) & 0x7ffu);
    unsigned int addr = seg_phys(w1);
    unsigned int off = w0 & 0xfffu;
    unsigned int i;

    if (off == 0x830u && !flag) { zb_load_matrix(zb.mtx_model, addr); return; }
    if (off == 0x870u && !flag) { zb_load_matrix(zb.mtx_proj, addr); return; }
    if (off == 0x8b0u)
    {
        if (!flag) zb_load_matrix(zb.mtx_comb, addr);
        else       zb_store_matrix(zb.mtx_comb, addr);
        return;
    }
    if (off == 0x0u && !flag)
    {
        /* viewport: 8 s16 -- scale x/y (s13.2), z, fog mult; trans
         * x/y (s13.2), z, fog offset */
        int sx = rd_s16(s_rdram, addr);
        int sy = rd_s16(s_rdram, addr + 2u);
        int fm = rd_s16(s_rdram, addr + 6u);
        int tx = rd_s16(s_rdram, addr + 8u);
        int ty = rd_s16(s_rdram, addr + 10u);
        int fo = rd_s16(s_rdram, addr + 14u);
        zb.view_scale[0] = (float)sx * 0.25f * 4.0f;
        zb.view_scale[1] = (float)sy * 0.25f * 4.0f;
        zb.view_trans[0] = (float)tx * 0.25f * 4.0f;
        zb.view_trans[1] = (float)ty * 0.25f * 4.0f;
        zb.fog_mult = fm;
        zb.fog_off  = fo;
        /* fall through to the DMEM copy as the microcode also stages it */
    }
    if (off == 0x730u && len == 256u && addr_ok(addr, 256u))
        for (i = 0; i < 256u; i++)
            zb.fogtable[i] = s_rdram[(addr + i) ^ BO8];

    if (!addr_ok(addr, len) || off + len > 0x1000u)
        return;
    if (!flag)
        for (i = 0; i < len; i++)
            s_dmem[(off + i) ^ BO8] = s_rdram[(addr + i) ^ BO8];
    else
        for (i = 0; i < len; i++)
            s_rdram[(addr + i) ^ BO8] = s_dmem[(off + i) ^ BO8];
}

static void zb_mtxcat(unsigned int w0, unsigned int w1)
{
    float *sm = zb_mtx_slot((w1 >> 16) & 0xfffu);
    float *tm = zb_mtx_slot(w0 & 0xfffu);
    float *dm = zb_mtx_slot(w1 & 0xfffu);
    float m[4][4];
    if (sm == 0 || tm == 0 || dm == 0)
        return;
    zb_mult_matrix((const float (*)[4])sm, (const float (*)[4])tm, m);
    memcpy(dm, m, sizeof(m));
}

static void zb_transpose(unsigned int w1)
{
    float *mp = zb_mtx_slot(w1 & 0xfffu);
    float m[4][4];
    int i, j;
    if (mp == 0)
        return;
    memcpy(m, mp, sizeof(m));
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            ((float (*)[4])mp)[j][i] = m[i][j];
}

static void zb_mult_mpmtx(unsigned int w0, unsigned int w1)
{
    unsigned int num = 1u + ((w1 >> 24) & 0xffu);
    unsigned int src = (w1 >> 12) & 0xfffu;
    unsigned int dst = w1 & 0xfffu;
    unsigned int i;
    const float (*c)[4] = (const float (*)[4])zb.mtx_comb;
    (void)w0;   /* always the combined matrix (0x8b0) */

    for (i = 0; i < num; i++)
    {
        int sx = rd_s16(s_dmem, src); src += 2u;
        int sy = rd_s16(s_dmem, src); src += 2u;
        int sz = rd_s16(s_dmem, src); src += 2u;
        float x = (float)sx * c[0][0] + (float)sy * c[1][0]
                + (float)sz * c[2][0] + c[3][0];
        float y = (float)sx * c[0][1] + (float)sy * c[1][1]
                + (float)sz * c[2][1] + c[3][1];
        float z = (float)sx * c[0][2] + (float)sy * c[1][2]
                + (float)sz * c[2][2] + c[3][2];
        float w = (float)sx * c[0][3] + (float)sy * c[1][3]
                + (float)sz * c[2][3] + c[3][3];
        float invw, x_w, y_w;
        int fog, ssx, ssy;
        unsigned int cc = 0;

        invw = (w <= 0.0f) ? zb.invw_factor : (1.0f / w);
        x_w = x * invw;
        y_w = y * invw;
        if (x_w >  zb.invw_factor) x_w =  zb.invw_factor;
        if (x_w < -zb.invw_factor) x_w = -zb.invw_factor;
        if (y_w >  zb.invw_factor) y_w =  zb.invw_factor;
        if (y_w < -zb.invw_factor) y_w = -zb.invw_factor;

        ssx = (int)(zb.view_trans[0] + x_w * zb.view_scale[0]);
        ssy = (int)(zb.view_trans[1] + y_w * zb.view_scale[1]);

        fog = (int)(w * ((float)zb.fog_mult * (1.0f / 65536.0f))
                    + (float)zb.fog_off);
        if (fog > 127) fog = 127;
        if (fog < -128) fog = -128;

        if (x >=  w) cc |= 0x10u;
        if (y >=  w) cc |= 0x20u;
        if (z >=  w) cc |= 0x40u;
        if (x <= -w) cc |= 0x01u;
        if (y <= -w) cc |= 0x02u;
        if (z <= -w) cc |= 0x04u;

        /* zSortVDest, 16 bytes: sy sx | invw | yi xi wi | fog cc */
        wr_s16(s_dmem, dst, ssy);              dst += 2u;
        wr_s16(s_dmem, dst, ssx);              dst += 2u;
        wr32(s_dmem, dst,
             (unsigned int)zb_calc_invw((int)(w * zb.invw_factor)));
        dst += 4u;
        wr_s16(s_dmem, dst, (int)(short)(int)y); dst += 2u;
        wr_s16(s_dmem, dst, (int)(short)(int)x); dst += 2u;
        wr_s16(s_dmem, dst, (int)(short)(int)w); dst += 2u;
        s_dmem[dst ^ BO8] = zb.fogtable[(fog + 128) & 0xff]; dst += 1u;
        s_dmem[dst ^ BO8] = (unsigned char)cc;               dst += 1u;
    }
}

static void zb_lighting(unsigned int w0, unsigned int w1)
{
    /* texgen only: writes lookat-projected s/t shorts back to DMEM.
     * The reference keeps lookat at fixed DMEM slots via
     * TRANSFORMLIGHTS; WDC ships no positional lights (numLights 0,
     * lookat only). */
    unsigned int num = 1u + ((w1 >> 24) & 0xffu);
    unsigned int nsrs = w0 & 0xfffu;
    unsigned int tdest = w1 & 0xfffu;
    unsigned int i;
    for (i = 0; i < num; i++)
    {
        (void)rd_s8(s_dmem, nsrs);
        nsrs += 3u;
        /* no lookat state is populated by WDC's lists in the reference
         * (TransformLights asserts numLights == 0); emit centered texgen */
        wr_s16(s_dmem, tdest, 0x200); tdest += 2u;
        wr_s16(s_dmem, tdest, 0x200); tdest += 2u;
    }
}

/* audio: exact integer transcriptions of the reference */

static void zb_audio1(unsigned int w0, unsigned int w1)
{
    unsigned int addr = seg_phys(w1);
    unsigned int val = rd32(s_dmem, w0 & 0xffcu);
    unsigned int i;
    wr32(s_dmem, 0u, val);
    if (!addr_ok(addr, 8u))
        return;
    for (i = 0; i < 8u; i++)
        s_rdram[(addr + i) ^ BO8] = s_dmem[i ^ BO8];
}

static void zb_audio2(unsigned int w0, unsigned int w1)
{
    int len = (int)(w1 >> 24);
    unsigned int dst = rd32(s_dmem, 0x10u);
    float f1 = (float)((w0 >> 16) & 0xffu) + (float)(w0 & 0xffffu) / 65536.0f;
    float f2 = (float)((w1 >> 16) & 0xffu) + (float)(w1 & 0xffffu) / 65536.0f;
    int v11_0 = (int)(((unsigned int)s_dmem[0x904u ^ BO8] << 8)
                      | s_dmem[0x905u ^ BO8]);
    int v11_1 = (int)(((unsigned int)s_dmem[0x906u ^ BO8] << 8)
                      | s_dmem[0x907u ^ BO8]);
    int i, j, k;
    for (i = 0; i < len; i += 4)
    {
        for (j = 0; j < 4; j++)
        {
            float val = (float)i * f1 + (float)j * f1 + f2;
            float ip;
            float fp = (float)fabs((double)modff(val, &ip));
            int index = ((int)ip) << 1;
            int v9  = rd_s16(s_dmem, 0x30u + (unsigned int)index);
            int v10 = rd_s16(s_dmem, 0x32u + (unsigned int)index);
            int v12 = (short)(v10 - v9);
            int v13 = rd_s16(s_dmem, dst);
            for (k = 0; k < 2; k++)
            {
                int vk = (k == 0) ? v11_0 : v11_1;
                long res1 = (long)v12 * (long)(unsigned short)(fp * 65536.0f);
                long res2 = (long)v9 << 16;
                int res3 = (int)(short)((res2 + res1) >> 16);
                res1 = (long)vk * (long)res3;
                res2 = (long)v13 << 16;
                res3 = (int)(short)((res2 + res1) >> 16);
                wr_s16(s_dmem, dst, res3);
                dst += 2u;
            }
        }
    }
}

static void zb_audio3(unsigned int w0, unsigned int w1)
{
    unsigned int addr = seg_phys(w0);
    int i, j;
    if (addr_ok(addr, 128u))
        for (i = 0; i < 8; i++)
            for (j = 0; j < 8; j++)
                zb.audio_table[i][j] = (short)rd_s16(s_rdram,
                    addr + (unsigned int)((i << 4) + (j << 1)));
    addr = seg_phys(w1);
    if (addr_ok(addr, 8u))
    {
        unsigned int i2;
        for (i2 = 0; i2 < 8u; i2++)
            s_dmem[i2 ^ BO8] = s_rdram[(addr + i2) ^ BO8];
        wr32(s_dmem, 8u, addr);
    }
}

static void zb_audio4(unsigned int w0, unsigned int w1)
{
    unsigned int addr = seg_phys(w1);
    unsigned int src = ((w0 & 0xf000u) >> 12) + addr;
    unsigned int dst = 0x30u;
    int len = (int)(w0 & 0xfffu);
    int v1 = rd_s16(s_dmem, 0u);
    int v2 = rd_s16(s_dmem, 2u);
    int l1, l2, i, j, k;
    for (l1 = len; l1 > 0; l1 -= 9)
    {
        unsigned int r9;
        int index, c1;
        if (!addr_ok(src, 9u))
            return;
        r9 = s_rdram[src ^ BO8]; src += 1u;
        index = (int)((r9 & 0xfu) << 1);
        if (index > 6)
            return;
        c1 = 1 << ((r9 >> 4) & 0x1fu);
        for (l2 = 0; l2 < 2; l2++)
        {
            int a = rd_s8(s_rdram, src); src += 1u;
            int b = rd_s8(s_rdram, src); src += 1u;
            int c = rd_s8(s_rdram, src); src += 1u;
            int d = rd_s8(s_rdram, src); src += 1u;
            int coeff[8];
            coeff[0] = a >> 4;  coeff[1] = (int)((a << 28) >> 28);
            coeff[2] = b >> 4;  coeff[3] = (int)((b << 28) >> 28);
            coeff[4] = c >> 4;  coeff[5] = (int)((c << 28) >> 28);
            coeff[6] = d >> 4;  coeff[7] = (int)((d << 28) >> 28);
            for (i = 0; i < 8; i++)
            {
                long res1 = 0;
                long res2;
                int out;
                for (j = 0, k = i; j < i; j++, k--)
                    res1 += (long)zb.audio_table[index + 1][k - 1]
                          * (long)coeff[j];
                res1 += (long)coeff[i] * 0x0800l;
                res1 *= (long)c1;
                res2 = (long)v1 * (long)zb.audio_table[index][i]
                     + (long)v2 * (long)zb.audio_table[index + 1][i];
                out = (int)((res1 * 0x20l + res2 * 0x20l) >> 16);
                wr_s16(s_dmem, dst + (unsigned int)((i ^ 1) << 1),
                       (int)(short)out);
            }
            v1 = rd_s16(s_dmem, dst + (unsigned int)((0x6 ^ 0x1) << 1));
            v2 = rd_s16(s_dmem, dst + (unsigned int)((0x7 ^ 0x1) << 1));
            dst += 16u;
        }
    }
}

/* ---- interpreter ------------------------------------------------------- */

int zboss_run(unsigned char *rdram, unsigned int rdram_size,
              unsigned char *dmem, RdpFifo *fifo, int op)
{
    int guard = 0;

    s_rdram = rdram;
    s_rdram_size = rdram_size;
    s_dmem = dmem;
    s_fifo = fifo;

    if (op == ZBOSS_OP_FRESH)
    {
        /* Per-task reset covers the walk state only. Matrices, viewport,
         * fog and audio tables, the other-mode shadow, the update mask
         * and the RDP-command dedup pointers all persist across tasks,
         * as the microcode's DMEM does (and as the reference's gstate,
         * cleared once per session, does). */
        static int session_init;
        if (!session_init)
        {
            memset(&zb, 0, sizeof(zb));
            zb.invw_factor = 10.0f;
            session_init = 1;
        }
        zb.active = 1;
        zb.wait_kind = ZB_WAIT_NONE;
        zb.maindl_done = 0;
        zb.subdl_done = 0;
        zb.pc[0] = rd32(dmem, 0xff0u) & 0x00ffffffu;
        zb.pc[1] = rd32(dmem, 0xff8u) & 0x00ffffffu;
        zb.pci = 0;
    }
    else
    {
        if (!zb.active)
            return -1;
        /* the wait condition was satisfied by the CPU (rsp-hle only
         * resumes once it is) */
        if (zb.wait_kind == ZB_WAIT_SIG0)
            zb.pci = 1;
        zb.wait_kind = ZB_WAIT_NONE;
    }

    while (guard++ < 200000)
    {
        unsigned int pc = zb.pc[zb.pci];
        unsigned int w0, w1, cmd;
        if (!addr_ok(pc, 8u))
            return -1;
        w0 = rd32(s_rdram, pc);
        w1 = rd32(s_rdram, pc + 4u);
        zb.pc[zb.pci] = pc + 8u;
        cmd = (w0 >> 24) & 0xffu;

        switch (cmd)
        {
        case 0x00:                              /* SPNOOP */
            break;
        case 0x02:                              /* ENDMAINDL */
            if (zb.subdl_done)
            {
                zb.active = 0;
                return ZBOSS_R_DONE;
            }
            zb.maindl_done = 1;
            zb.wait_kind = ZB_WAIT_SIG0;
            return ZBOSS_R_WAIT_SIG0;
        case 0x1a:                              /* ENDSUBDL */
            if (zb.maindl_done)
            {
                zb.active = 0;
                return ZBOSS_R_DONE;
            }
            zb.subdl_done = 1;
            zb.pci = 0;
            break;
        case 0x04:                              /* MOVEMEM */
            zb_movemem(w0, w1);
            break;
        case 0x06:                              /* MOVEWORD (DMEM write) */
            if ((w0 & 0xfffu) <= 0xffcu)
                wr32(s_dmem, w0 & 0xfffu, w1);
            break;
        case 0x08:                              /* TRANSPOSEMTX */
            zb_transpose(w1);
            break;
        case 0x0a:                              /* MTXCAT */
            zb_mtxcat(w0, w1);
            break;
        case 0x0c:                              /* MULT_MPMTX */
            zb_mult_mpmtx(w0, w1);
            break;
        case 0x0e:                              /* RDPCMD */
            zb_rdpcmd(w1);
            break;
        case 0x10:                              /* OBJ */
            zb_obj(w0, w1);
            break;
        case 0x12:                              /* WAITSIGNAL */
            zb.wait_kind = ZB_WAIT_SIG3;
            return ZBOSS_R_WAIT_SIG3;
        case 0x14:                              /* LIGHTING */
            zb_lighting(w0, w1);
            break;
        case 0x18:                              /* TRANSFORMLIGHTS */
            /* WDC ships lookat-only light state (reference: numLights
             * == 0); the texgen consumer is stubbed in zb_lighting */
            break;
        case 0x1c:                              /* AUDIO2 */
            zb_audio2(w0, w1);
            break;
        case 0x1e:                              /* CLEARBUFFER */
        {
            unsigned int i;
            for (i = 0; i < 512u; i++)
                s_dmem[(0xc20u + i) ^ BO8] = 0;
            break;
        }
        case 0x22:                              /* AUDIO3 */
            zb_audio3(w0, w1);
            break;
        case 0x24:                              /* AUDIO4 */
            zb_audio4(w0, w1);
            break;
        case 0x26:                              /* AUDIO1 */
            zb_audio1(w0, w1);
            break;
        default:
            if (zb_dispatch_ucode_rdp(cmd, w0, w1))
                break;
            /* unknown command: skip (transcription surface is the
             * reference's full table; anything else is a list error) */
            break;
        }
    }
    return -1;
}
