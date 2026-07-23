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
    int             sig0_taken;
    int             in_obj;
    unsigned int    obj_chain;
    unsigned int    obj_chain2;
    int             switch_req;
    unsigned int    updatemask[2];
    unsigned int    othermode_h;
    unsigned int    othermode_l;
    unsigned int    rdpcmds[3];
    short           audio_table[8][8];
    int             tri_tile;
    int             tri_level;
} ZbState;

static ZbState zb;

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;
static unsigned int  *s_sp_status;
static int            s_shadow;      /* free-run handshakes, no CPU coupling */
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

/* ---- RSP accumulator semantics (transcribed; see rdp_emit_rsp.c) ------ */

#define ZBU16(x) ((int32_t)((x) & 0xffff))
#define ZBS16(x) ((int32_t)(int16_t)((x) & 0xffff))

static int64_t zb_p_udl(int32_t a, int32_t b)
{ return (int64_t)((((int64_t)ZBU16(a) * (int64_t)ZBU16(b)) >> 16)); }
static int64_t zb_p_udm(int32_t a, int32_t b)
{ return (int64_t)ZBS16(a) * (int64_t)ZBU16(b); }
static int64_t zb_p_udn(int32_t a, int32_t b)
{ return (int64_t)ZBU16(a) * (int64_t)ZBS16(b); }
static int64_t zb_p_udh(int32_t a, int32_t b)
{ return ((int64_t)ZBS16(a) * (int64_t)ZBS16(b)) << 16; }

static int32_t zb_acc_mid(int64_t a)
{
    int64_t hi = a >> 16;
    if (hi < -32768) return -32768;
    if (hi >  32767) return  32767;
    return (int32_t)(hi & 0xffff);
}

static int32_t zb_acc_low(int64_t a)
{
    int64_t hi = a >> 16;
    if (hi < -32768) return 0x0000;
    if (hi >  32767) return 0xffff;
    return (int32_t)(a & 0xffff);
}

/* matrix element accessors: N64 packed layout at a DMEM slot -- 32 bytes
 * of s16 integer parts (row major) then 32 bytes of fraction parts */
static int zb_mi(unsigned int base, int i, int j)
{ return rd_s16(s_dmem, (base + (unsigned int)(i * 8 + j * 2)) & 0xfffu); }
static int zb_mf(unsigned int base, int i, int j)
{ return (int)(((unsigned int)s_dmem[(base + 32u + (unsigned int)(i * 8 + j * 2)) & 0xfffu ^ BO8] << 8)
              | s_dmem[((base + 32u + (unsigned int)(i * 8 + j * 2) + 1u) & 0xfffu) ^ BO8]); }
static void zb_mw(unsigned int base, int i, int j, int vi, int vf)
{
    wr_s16(s_dmem, (base + (unsigned int)(i * 8 + j * 2)) & 0xfffu, vi);
    s_dmem[((base + 32u + (unsigned int)(i * 8 + j * 2)) & 0xfffu) ^ BO8]
        = (unsigned char)((vf >> 8) & 0xff);
    s_dmem[((base + 32u + (unsigned int)(i * 8 + j * 2) + 1u) & 0xfffu) ^ BO8]
        = (unsigned char)(vf & 0xff);
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
            /* the microcode's generic state forward (IMEM 0x7dc, reached
             * for every op in this range through the RDP-op table at
             * DMEM 0xa1e) stores both command words verbatim into the
             * output staging -- image pointers included, KSEG0 bit and
             * all. The rasterizer masks addresses to the RDRAM window
             * itself, so resolving them here only made the emitted
             * words diverge from the oracle stream. */
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
    /* the microcode's OBJ handler polls SIG0 once per chain link
     * (IMEM 0x85c): when the CPU has granted the sub list, take the
     * signal and remember it; the switch itself happens at the next
     * sync (type-0) object below, mirroring the checkpoint at
     * IMEM 0x828/0x83c. */
    if (!s_shadow && !zb.sig0_taken && s_sp_status != 0
        && (*s_sp_status & 0x80u))
    {
        *s_sp_status &= ~0x80u;
        zb.sig0_taken = 1;
        s_dmem[0x940u ^ BO8] = 1;
    }
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
        else if (zb.sig0_taken && zb.pci == 0)
            zb.switch_req = 1;
    }
    return seg_phys(rd32(s_rdram, addr));
}

/* Walk the two object chains of an OBJ command. The microcode
 * interleaves the sub display list into this walk: SIG0 is polled per
 * link and the switch to the sub interpreter happens at the next
 * type-0 (sync) object, with the walk position checkpointed into DMEM
 * 0xf00 and resumed by ENDSUBDL. The chain state lives in zb so the
 * OBJ command can be re-entered after the sub list (the caller rewinds
 * the list pc, and in_obj makes the re-execution continue the chain).
 * Returns: 0 done, 1 switch to the sub list. */
static int zb_obj(unsigned int w0, unsigned int w1)
{
    int guard = 0;
    if (!zb.in_obj)
    {
        zb.obj_chain = seg_phys(w0);
        zb.obj_chain2 = seg_phys(w1);
        zb.in_obj = 1;
    }
    while (guard++ < 100000)
    {
        if (zb.obj_chain == 0)
        {
            if (zb.obj_chain2 == 0)
            {
                zb.in_obj = 0;
                return 0;
            }
            zb.obj_chain = zb.obj_chain2;
            zb.obj_chain2 = 0;
            continue;
        }
        zb.obj_chain = zb_load_object(zb.obj_chain);
        if (zb.switch_req)
        {
            zb.switch_req = 0;
            return 1;
        }
    }
    zb.in_obj = 0;
    return 0;
}

/* ---- compute commands -------------------------------------------------- */

static void zb_movemem(unsigned int w0, unsigned int w1)
{
    /* The microcode's MOVEMEM is a DMA in either direction between a
     * DMEM offset and RDRAM; matrix slots (0x830/0x870/0x8b0), the
     * viewport (0x0), and the fog table (0x730, accessed biased at
     * 0x7b0) are all plain DMEM regions to it. Keeping DMEM as the
     * single source of truth also makes the game's DMEM->RDRAM
     * readbacks byte-exact. */
    int flag = (int)((w0 >> 23) & 1u);
    unsigned int len = 1u + ((w0 >> 12) & 0x7ffu);
    unsigned int addr = seg_phys(w1);
    unsigned int off = w0 & 0xfffu;
    unsigned int i;

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
    /* 16.16 concatenation over the DMEM slots with the 48-bit
     * accumulator and the VMADH/VMADN clamp reads (IMEM 0x258) */
    unsigned int sb = (w1 >> 16) & 0xfffu;
    unsigned int tb = w0 & 0xfffu;
    unsigned int db = w1 & 0xfffu;
    int oi[4][4], of4[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            int64_t acc = 0;
            for (k = 0; k < 4; k++)
            {
                acc += zb_p_udl(zb_mf(sb, i, k), zb_mf(tb, k, j));
                acc += zb_p_udm(zb_mi(sb, i, k), zb_mf(tb, k, j));
                acc += zb_p_udn(zb_mf(sb, i, k), zb_mi(tb, k, j));
                acc += zb_p_udh(zb_mi(sb, i, k), zb_mi(tb, k, j));
            }
            oi[i][j] = zb_acc_mid(acc);
            of4[i][j] = zb_acc_low(acc);
        }
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            zb_mw(db, i, j, oi[i][j], of4[i][j]);
}

static void zb_transpose(unsigned int w1)
{
    unsigned int b = w1 & 0xfffu;
    int mi[3][3], mf[3][3];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
        {
            mi[i][j] = zb_mi(b, i, j);
            mf[i][j] = zb_mf(b, i, j);
        }
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            zb_mw(b, j, i, mi[i][j], mf[i][j]);
}

static void zb_mult_mpmtx(unsigned int w0, unsigned int w1)
{
    /* Integer transcription of the microcode's point-transform service
     * (IMEM 0x2fc): 16.16 matrix accumulate with the RSP's 48-bit
     * accumulator and clamp reads, DIV-table reciprocal of
     * w * invw_factor (only the low half of the DMEM 0x10 factor is
     * used, per the [e9] broadcast), the VGE/VMRG overflow clamp that
     * pins a negative reciprocal high half to 0x7fff before the screen
     * multiply, the fog ramp fogO + (w_i * fogM >> 16) looked up in
     * the DMEM fog table biased at 0x7b0, and the VCH/VCL clip codes.
     * The viewport scale is pre-folded with 2*invw_factor (saturated
     * s16 lane add), cancelling the factor in the reciprocal domain. */
    unsigned int mb = w0 & 0xfffu;
    unsigned int num = 1u + ((w1 >> 24) & 0xffu);
    unsigned int src = (w1 >> 12) & 0xfffu;
    unsigned int dst = w1 & 0xfffu;
    int ivf  = (int)(rd32(s_dmem, 0x10u) & 0xffffu);
    int ivf2;
    int fogm = rd_s16(s_dmem, 6u);
    int fogo = rd_s16(s_dmem, 14u);
    int scale[2], trans[2], pre_i[2], pre_f[2];
    int j;
    unsigned int p;

    {
        long t2 = (long)(short)ivf + (long)(short)ivf;   /* vadd lane sat */
        if (t2 > 32767) t2 = 32767;
        if (t2 < -32768) t2 = -32768;
        ivf2 = (int)t2 & 0xffff;
    }
    for (j = 0; j < 2; j++)
    {
        int64_t acc;
        scale[j] = rd_s16(s_dmem, (unsigned int)(j * 2));
        trans[j] = rd_s16(s_dmem, 8u + (unsigned int)(j * 2));
        acc = zb_p_udm(scale[j], ivf2);
        pre_i[j] = zb_acc_mid(acc);
        pre_f[j] = zb_acc_low(acc);
    }

    for (p = 0; p < num; p++)
    {
        int co[3];
        int ri[4], rf[4];
        int32_t v32[4];
        int rcp, rcp_hi, rcp_lo, hi_c;
        int div_i, div_f;
        int32_t div32;
        int scr[2];
        int fog;
        unsigned int cc = 0;
        int64_t acc;

        co[0] = rd_s16(s_dmem, src); src = (src + 2u) & 0xfffu;
        co[1] = rd_s16(s_dmem, src); src = (src + 2u) & 0xfffu;
        co[2] = rd_s16(s_dmem, src); src = (src + 2u) & 0xfffu;

        for (j = 0; j < 4; j++)
        {
            acc  = zb_p_udn(zb_mf(mb, 3, j), 1) + zb_p_udh(zb_mi(mb, 3, j), 1);
            acc += zb_p_udn(zb_mf(mb, 1, j), co[1])
                 + zb_p_udh(zb_mi(mb, 1, j), co[1]);
            acc += zb_p_udn(zb_mf(mb, 2, j), co[2])
                 + zb_p_udh(zb_mi(mb, 2, j), co[2]);
            acc += zb_p_udn(zb_mf(mb, 0, j), co[0])
                 + zb_p_udh(zb_mi(mb, 0, j), co[0]);
            ri[j] = zb_acc_mid(acc);
            rf[j] = zb_acc_low(acc);
            v32[j] = (int32_t)(((uint32_t)ZBU16(ri[j]) << 16)
                               | (uint32_t)ZBU16(rf[j]));
        }

        /* DIV input: w * invw_factor low half (vmudl/vmadm) */
        acc = zb_p_udl(rf[3], ivf) + zb_p_udm(ri[3], ivf);
        div_i = zb_acc_mid(acc);
        div_f = zb_acc_low(acc);
        div32 = (int32_t)(((uint32_t)ZBU16(div_i) << 16)
                          | (uint32_t)ZBU16(div_f));
        rcp = (int)rsp_rcp32(div32);
        rcp_hi = (rcp >> 16) & 0xffff;
        rcp_lo = rcp & 0xffff;
        hi_c = (ZBS16(rcp_hi) >= 0) ? rcp_hi : 0x7fff;

        for (j = 0; j < 2; j++)
        {
            int ndc_i, ndc_f;
            acc  = zb_p_udl(rf[j], rcp_lo) + zb_p_udm(ri[j], rcp_lo);
            acc += zb_p_udn(rf[j], hi_c)   + zb_p_udh(ri[j], hi_c);
            ndc_i = zb_acc_mid(acc);
            ndc_f = zb_acc_low(acc);
            acc  = zb_p_udh(trans[j], 1);
            acc += zb_p_udl(pre_f[j], ndc_f) + zb_p_udm(pre_i[j], ndc_f);
            acc += zb_p_udn(pre_f[j], ndc_i) + zb_p_udh(pre_i[j], ndc_i);
            scr[j] = zb_acc_mid(acc);
        }

        acc = zb_p_udh(fogo, 1) + zb_p_udm(ri[3], fogm);
        fog = zb_acc_mid(acc);
        {
            unsigned int fa = (0x7b0u + (unsigned int)fog) & 0xfffu;
            fog = s_dmem[fa ^ BO8];
        }

        if (v32[0] >= v32[3])  cc |= 0x10u;
        if (v32[1] >= v32[3])  cc |= 0x20u;
        if (v32[2] >= v32[3])  cc |= 0x40u;
        if (v32[0] <= -v32[3]) cc |= 0x01u;
        if (v32[1] <= -v32[3]) cc |= 0x02u;
        if (v32[2] <= -v32[3]) cc |= 0x04u;

        /* zSortVDest, 16 bytes: sy sx | invw | yi xi wi | fog cc */
        wr_s16(s_dmem, dst, scr[1]);           dst = (dst + 2u) & 0xfffu;
        wr_s16(s_dmem, dst, scr[0]);           dst = (dst + 2u) & 0xfffu;
        wr32(s_dmem, dst, (unsigned int)(((unsigned int)rcp_hi << 16)
                                         | (unsigned int)rcp_lo));
        dst = (dst + 4u) & 0xfffu;
        wr_s16(s_dmem, dst, ri[1]);            dst = (dst + 2u) & 0xfffu;
        wr_s16(s_dmem, dst, ri[0]);            dst = (dst + 2u) & 0xfffu;
        wr_s16(s_dmem, dst, ri[3]);            dst = (dst + 2u) & 0xfffu;
        s_dmem[dst ^ BO8] = (unsigned char)fog;          dst = (dst + 1u) & 0xfffu;
        s_dmem[dst ^ BO8] = (unsigned char)cc;           dst = (dst + 1u) & 0xfffu;
    }
}

/* Light-struct handlers (IMEM 0x570 TRANSFORMLIGHTS, 0x444 LIGHTING):
 * lane-exact transcriptions validated against a standalone cxd4 oracle
 * running the captured microcode (corpus: 13 matrix/lookat cases, a
 * 12-step magnitude sweep, and 10 randomised texgen runs, all
 * bit-identical).  DMEM is the single source of truth: the transformed
 * lookat vectors live in the light struct itself at each slot's
 * raw-direction offset + 8, packed by SPV (bits 15..8 of the final
 * lanes) and duplicated across two words. */

static void zb_lighting(unsigned int w0, unsigned int w1)
{
    /* Texgen per record: s/t = ((sat16(sum 2*n*l) + 0x8000) & 0xffff)
     * >> 6 against the two transformed lookat vectors.  The dot is a
     * saturating s16 sum of per-component vmulf terms (each clamped);
     * the scale is the handler's vaddc 0x8000 / vmudl 0x0400 pair.
     * Colors: acc = ambient (luv u8<<7 domain), then for each of the
     * (struct_w1 >> 12) + 1 slots acc += max(vmulf(slot color, dot), 0),
     * finally scaled by the w0>>12 table and packed with suv; alpha
     * bytes come from the same table (bytes 3/7 of each 8-byte lane
     * group), advancing by the stride byte at DMEM 0x944. */
    unsigned int num = 1u + ((w1 >> 24) & 0xffu);
    unsigned int nsrs = w0 & 0xfffu;
    unsigned int cdest = (w1 >> 12) & 0xfffu;
    unsigned int tdest = w1 & 0xfffu;
    unsigned int lw1 = rd32(s_dmem, 0x948u);
    unsigned int lbase = lw1 & 0xfffu;
    unsigned int lcount = (lw1 >> 12) + 1u;
    unsigned int atab = (w0 >> 12) & 0xfffu;
    unsigned int stride = s_dmem[0x944u ^ BO8];
    unsigned int i, li;
    int k;

    if (lcount > 8u)
        lcount = 8u;

    for (i = 0; i < num; i++)
    {
        int n[3];
        int32_t cacc[3];
        int32_t d[8];
        for (k = 0; k < 3; k++)
        {
            n[k] = rd_s8(s_dmem, nsrs);
            nsrs = (nsrs + 1u) & 0xfffu;
        }
        /* ambient preload, u8<<7 domain */
        for (k = 0; k < 3; k++)
            cacc[k] = (int32_t)s_dmem[((lbase + (unsigned int)k) & 0xfffu) ^ BO8] << 7;
        for (li = 0; li < lcount; li++)
        {
            unsigned int slot = (lbase + li * 24u) & 0xfffu;
            int32_t acc = 0;
            for (k = 0; k < 3; k++)
            {
                int lv = rd_s8(s_dmem, (slot + 24u + (unsigned int)k) & 0xfffu);
                int32_t t = 2 * n[k] * lv;
                if (t > 32767)  t = 32767;
                if (t < -32768) t = -32768;
                acc += t;
                if (acc > 32767)  acc = 32767;
                if (acc < -32768) acc = -32768;
            }
            d[li] = acc;
            for (k = 0; k < 3; k++)
            {
                int32_t cs = (int32_t)s_dmem[((slot + 8u + (unsigned int)k) & 0xfffu) ^ BO8] << 7;
                int32_t p = (int32_t)(((int64_t)cs * (int64_t)acc * 2 + 0x8000) >> 16);
                if (p < 0)
                    p = 0;
                cacc[k] += p;
                if (cacc[k] > 32767) cacc[k] = 32767;
            }
        }
        /* texgen: the last two slots walked are the lookat pair */
        if (lcount >= 2u)
        {
            wr_s16(s_dmem, tdest,
                   (int)(((unsigned int)(d[lcount - 2u] + 0x8000) & 0xffffu) >> 6));
            tdest = (tdest + 2u) & 0xfffu;
            wr_s16(s_dmem, tdest,
                   (int)(((unsigned int)(d[lcount - 1u] + 0x8000) & 0xffffu) >> 6));
            tdest = (tdest + 2u) & 0xfffu;
        }
        /* color scale table + pack (suv: lane >> 7); the table is
         * consumed four records per block (the two luv/ldv pairs),
         * scale bytes at block + (i % 4) * 4, alpha at that + 3, and
         * the block pointer advances by the DMEM 0x944 stride byte
         * after every fourth record */
        {
            unsigned int arec = (atab + (i & 3u) * 4u) & 0xfffu;
            for (k = 0; k < 3; k++)
            {
                int32_t sc = (int32_t)s_dmem[((arec + (unsigned int)k) & 0xfffu) ^ BO8] << 7;
                int32_t p = (int32_t)(((int64_t)cacc[k] * (int64_t)sc * 2 + 0x8000) >> 16);
                if (p < 0)      p = 0;
                if (p > 32767)  p = 32767;
                s_dmem[((cdest + (unsigned int)k) & 0xfffu) ^ BO8]
                    = (unsigned char)((p >> 7) & 0xff);
            }
            s_dmem[((cdest + 3u) & 0xfffu) ^ BO8]
                = s_dmem[((arec + 3u) & 0xfffu) ^ BO8];
            cdest = (cdest + 4u) & 0xfffu;
            if ((i & 3u) == 3u)
                atab = (atab + stride) & 0xfffu;
        }
    }
    (void)li;
}

static void zb_transformlights(unsigned int w0, unsigned int w1)
{
    /* Rotate (w1 >> 12) + 1 light-struct direction vectors by the model
     * matrix at w0 & 0xfff and renormalise (rsp_zsort_light_xfrm; exact
     * IMEM 0x570 chain).  Each slot's SPV-packed result lands at its
     * raw-direction offset + 8, with the first word duplicated in the
     * following word, and the raw w1 is stored at DMEM 0x948 from the
     * jal delay slot. */
    unsigned int mbase = w0 & 0xfffu;
    unsigned int count = (w1 >> 12) + 1u;
    unsigned int addr = w1 & 0xfffu;
    unsigned int li;
    int j, i;

    wr32(s_dmem, 0x948u, w1);
    if (count > 8u)
        count = 8u;

    for (li = 0; li < count; li++)
    {
        uint16_t mi[3][4], mf[3][4];
        int32_t dir[3];
        unsigned char outb[4];

        for (i = 0; i < 3; i++)
            for (j = 0; j < 4; j++)
            {
                mi[i][j] = ZBU16(zb_mi(mbase, i, j));
                mf[i][j] = ZBU16(zb_mf(mbase, i, j));
            }
        for (i = 0; i < 3; i++)
            dir[i] = rd_s8(s_dmem, (addr + 16u + (unsigned int)i) & 0xfffu);

        rsp_zsort_light_xfrm(mi, mf, dir, outb);

        for (j = 0; j < 4; j++)
        {
            s_dmem[((addr + 24u + (unsigned int)j) & 0xfffu) ^ BO8] = outb[j];
            s_dmem[((addr + 28u + (unsigned int)j) & 0xfffu) ^ BO8] = outb[j];
        }
        addr = (addr + 24u) & 0xfffu;
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

void zboss_set_shadow(int on)
{
    s_shadow = on ? 1 : 0;
}

int zboss_run(unsigned char *rdram, unsigned int rdram_size,
              unsigned char *dmem, RdpFifo *fifo, int op,
              unsigned int *sp_status)
{
    int guard = 0;

    s_rdram = rdram;
    s_rdram_size = rdram_size;
    s_dmem = dmem;
    s_fifo = fifo;
    s_sp_status = sp_status;

    if (op == ZBOSS_OP_FRESH)
    {
        /* Per-task reset covers the walk state only. Matrices, viewport,
         * fog and audio tables, the other-mode shadow and the update
         * mask persist across tasks, as the microcode's DMEM does (and
         * as the reference's gstate, cleared once per session, does).
         * The RDP-command dedup pointers are the exception: the
         * microcode's cache lives at DMEM 0xef4, inside the range the
         * task-data DMA overwrites on every launch (the OBJ handler at
         * IMEM 0x894 compares the object's three list pointers against
         * it), so the real cache starts cold each task and the first
         * object's lists are always re-run. */
        static int session_init;
        if (!session_init)
        {
            memset(&zb, 0, sizeof(zb));
            session_init = 1;
        }
        zb.rdpcmds[0] = 0;
        zb.rdpcmds[1] = 0;
        zb.rdpcmds[2] = 0;
        zb.sig0_taken = 0;
        zb.in_obj = 0;
        zb.obj_chain = 0;
        zb.obj_chain2 = 0;
        zb.switch_req = 0;
        zb.active = 1;
        zb.wait_kind = ZB_WAIT_NONE;
        zb.maindl_done = 0;
        zb.subdl_done = 0;
        zb.pc[0] = rd32(dmem, 0xff0u) & 0x00ffffffu;
        zb.pc[1] = rd32(dmem, 0xff8u) & 0x00ffffffu;
        zb.pci = 0;
        /* interpreter progress flags the microcode keeps in DMEM:
         * 0x940 sub-list state (1 after the SIG0 grant, -1 when the
         * sub list finished first), 0x941 main-list-done (-1) */
        dmem[0x940u ^ BO8] = 0;
        dmem[0x941u ^ BO8] = 0;
    }
    else
    {
        if (!zb.active)
            return -1;
        /* the wait condition was satisfied by the CPU (rsp-hle only
         * resumes once it is) */
        if (zb.wait_kind == ZB_WAIT_SIG0)
        {
            zb.pci = 1;
            s_dmem = dmem;
            dmem[0x940u ^ BO8] = 1;     /* SIG0 granted: sub list live */
        }
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
            if (zb.sig0_taken || s_shadow)
            {
                /* the grant already arrived mid-walk: service the sub
                 * list now (microcode: bgtz on DMEM 0x940 at 0x1bc) */
                zb.pci = 1;
                break;
            }
            zb.maindl_done = 1;
            s_dmem[0x941u ^ BO8] = 0xff;    /* main list done */
            zb.wait_kind = ZB_WAIT_SIG0;
            return ZBOSS_R_WAIT_SIG0;
        case 0x1a:                              /* ENDSUBDL */
            if (zb.maindl_done)
            {
                zb.active = 0;
                return ZBOSS_R_DONE;
            }
            zb.subdl_done = 1;
            zb.sig0_taken = 0;
            s_dmem[0x940u ^ BO8] = 0xff;    /* sub list finished first */
            zb.pci = 0;                     /* resume the main walk at
                                             * its OBJ checkpoint */
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
        {
            if (zb_obj(w0, w1))
            {
                /* re-execute this OBJ command on the way back so the
                 * chain walk continues from its checkpoint (in_obj),
                 * the way the microcode resumes into the middle of the
                 * OBJ handler through the DMEM 0xf00 continuation */
                zb.pc[zb.pci] = pc;
                zb.pci = 1;                 /* service the sub list */
            }
            break;
        }
        case 0x12:                              /* WAITSIGNAL */
            if (s_shadow)
                break;              /* free-run: records are task-stable */
            zb.wait_kind = ZB_WAIT_SIG3;
            return ZBOSS_R_WAIT_SIG3;
        case 0x14:                              /* LIGHTING */
        case 0x16:                              /* LIGHTING alias: the
            dispatch table at DMEM 0xbb0 is byte-indexed by the opcode
            and maps 0x16 to the same IMEM 0x444 handler (verified
            against the cxd4 oracle: identical texgen output) */
            zb_lighting(w0, w1);
            break;
        case 0x18:                              /* TRANSFORMLIGHTS */
            zb_transformlights(w0, w1);
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
