/* Factor 5 / LucasArts Naboo-era microcode walker (Star Wars Episode I:
 * Battle for Naboo, Indiana Jones and the Infernal Machine) for the
 * angrylion HLE path.
 *
 * A later revision of the Rogue Squadron engine (see rdp_emit_rs.c): a
 * persistent streaming command server with the same libultra yield
 * protocol, feeding the RDP through a DMEM FIFO (0xe80) spilled to an
 * RDRAM ring driven directly into the DPC registers.  Grammar decoded
 * from the task text (BfN text sum 0x25c16, IJ 0x25c53; dispatch at
 * text 0x2c):
 *
 *   - w0 bits 30-31 select the command class; the handler table lives
 *     in the ucode data segment at +0xb6, halfword-indexed by
 *     (w0 >> 23) & 0x1fe, each entry overlay-id (bits 12-15) :
 *     IMEM offset (bits 0-11); overlays are DMA'd on demand from the
 *     task ucode blob (loader at text 0x15c, current-overlay id at
 *     DMEM 0x143);
 *   - class 3 (0xc0-0xff) passes through to the RDP verbatim
 *     (text 0x98); G_TEXRECT (0xe4) carries its two extra words
 *     inline (text 0xb4);
 *   - the custom class-0 set (0x00-0x1f) is aliased by GBI-numbered
 *     opcodes 0xa9-0xc8 through the same table (0xb8 EndDL = slot
 *     0x0f, 0xba SetOtherModeH = slot 0x11, ...), the same compaction
 *     Rogue Squadron uses;
 *   - triangles reject on the AND of per-vertex outcodes (struct+0x24)
 *     masked 0x7070, the Rogue Squadron outcode model.
 *
 * This walker is intentionally incremental: commands it does not yet
 * implement return NABOO_R_FALLBACK and the caller reruns the slice on
 * the LLE fallback, so titles stay fully playable while the command
 * set is filled in against cxd4 A/B references. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdp_emit_f3dex2.h"
#include "rdp_emit_naboo.h"

extern void rdp_fifo_append(RdpFifo *f, const int32_t *words, int count);

static unsigned char *s_rdram;
static unsigned int   s_rdram_size;

void naboo_set_rdram(unsigned char *rdram) { s_rdram = rdram; }
void naboo_set_rdram_size(unsigned int size) { s_rdram_size = size; }

static unsigned int nb_read_u32(unsigned int addr)
{
    unsigned int a = addr & 0x7ffffcu;
    if (!s_rdram || a + 4u > s_rdram_size)
        return 0u;
    return ((unsigned int)s_rdram[(a + 0u) ^ 3u] << 24)
         | ((unsigned int)s_rdram[(a + 1u) ^ 3u] << 16)
         | ((unsigned int)s_rdram[(a + 2u) ^ 3u] << 8)
         |  (unsigned int)s_rdram[(a + 3u) ^ 3u];
}

/* Walk state persists across slices of one task (the server yields and
 * resumes); a fresh task launch resets it. */
#define NB_DL_STACK 8
static struct {
    /* Display lists are chained 0x100-byte chunks, each prefixed by an
     * 8-byte header whose first word points at the next chunk (zero =
     * chain end).  The microcode reads them through the DMEM ring at
     * 0x270 (fetch loop text 0x60-0x8c, 0x108-byte refills); the
     * walker models the same chunked cursor over RDRAM directly. */
    unsigned int chunk;         /* current chunk base (RDRAM) */
    unsigned int off;           /* byte offset within chunk (8..0x108) */
    unsigned int dl;            /* current command address (chunk+off) */
    unsigned int active;
    unsigned int sp;            /* DL call depth (slot 0x06 / 0x0f) */
    unsigned int geom;          /* geometry-mode word (s5): slot 0x0e
                                   ORs w1 in, slot 0x0d ANDs w1
                                   (text 0xa68/0xa70) */
    unsigned int stack[NB_DL_STACK * 2];
    /* modeled microcode DMEM state: MoveWord (slot 0x13) writes land
     * here; render commands consume them (state words at 0x120-0x13c,
     * viewport/live-tail words, ...) */
    unsigned char dmem[0x1000];
} nb;

static void nb_dl_enter(unsigned int addr)
{
    nb.chunk = addr & 0x00fffff8u;
    nb.off = 8u;
    nb.dl = nb.chunk + nb.off;
}

/* Advance the chunked cursor by len bytes, following the chain link in
 * each chunk's header at the 0x108 boundary. */
static void nb_dl_step(unsigned int len)
{
    nb.off += len;
    while (nb.off >= 0x108u) {
        unsigned int rem = nb.off - 0x108u;
        unsigned int next = nb_read_u32(nb.chunk) & 0x00fffff8u;
        nb.chunk = next;
        nb.off = 8u + rem;
        if (next == 0u)
            break;
    }
    nb.dl = nb.chunk + nb.off;
}

void naboo_task_reset(unsigned int dl)
{
    nb_dl_enter(dl);
    nb.active = 1;
    nb.sp = 0;
}

/* Seed the DMEM shadow from the live DMEM.  The streaming server's
 * working state (data segment, matrices, live-tail words, stream
 * cursor) persists in physical DMEM across task slices -- the data
 * segment is DMA'd once at server start and never again -- so the
 * live DMEM at (re)launch IS the authoritative state. */
static int nb_emit_on;

/* Per-build emission gate: the triangle/attribute conventions are
 * verified bit-exact against the Battle for Naboo microcode build
 * (sum 0x25c16); other builds of the family (Indiana Jones, sum
 * 0x25c53) fall back at the first triangle until verified, keeping
 * their output on the LLE reference. */
void naboo_set_emit(int on)
{
    /* NABOO_NO_EMIT forces the fallback path on verified builds too:
     * the pixel-exactness instrument (same backend, same pacing --
     * emit vs LLE-rerun differ only by who rendered). */
    static int off = -1;
    if (off < 0)
        off = getenv("NABOO_NO_EMIT") != NULL;
    nb_emit_on = off ? 0 : on;
}

static unsigned int nb_dmem_r32(unsigned int off);
static void nb_dmem_w32(unsigned int off, unsigned int v);
static void nb_dmem_w16(unsigned int off, unsigned int v);
static void nb_dmem_w8(unsigned int off, unsigned int v);

void naboo_seed_dmem(const unsigned char *dmem)
{
    unsigned int i;
    for (i = 0; i < 0x1000u; i++)
        nb.dmem[i ^ 3u] = dmem[i ^ 3u];

    /* Boot init (overlay at IMEM 0xd60, also the op 0x80 handler),
     * skipped when the task flags carry the resume bit: reset the
     * overlay id and DL depth bytes, install the default attribute
     * routine, zero the live-tail scratch, seed the SET_OTHER_MODES
     * state pair, and clear the geometry mode. */
    if (!(nb_dmem_r32(0xfc4u) & 1u)) {
        nb_dmem_w8(0x143u, 0u);
        nb_dmem_w8(0x142u, 0u);
        nb_dmem_w32(0x16cu, nb_dmem_r32(0xff8u));
        nb_dmem_w32(0x5a0u, nb_dmem_r32(0xff0u));
        nb_dmem_w16(0x152u, 0x17b4u);
        nb_dmem_w32(0x120u, 0xef000000u);
        nb_dmem_w32(0x124u, 0u);
        nb_dmem_w32(0x5b0u, 0u);
        nb_dmem_w32(0x5b4u, 0u);
        nb.geom = 0;
    }
}

static void nb_watch(unsigned int a, unsigned int n, unsigned int v);

static unsigned int nb_dmem_r32(unsigned int off)
{
    unsigned int a = off & 0xffcu;
    return ((unsigned int)nb.dmem[(a + 0u) ^ 3u] << 24)
         | ((unsigned int)nb.dmem[(a + 1u) ^ 3u] << 16)
         | ((unsigned int)nb.dmem[(a + 2u) ^ 3u] << 8)
         |  (unsigned int)nb.dmem[(a + 3u) ^ 3u];
}

static int nb_dmem_s16(unsigned int off)
{
    unsigned int a = off & 0xffeu;
    return (int)(short)(((unsigned int)nb.dmem[a ^ 3u] << 8)
                       | (unsigned int)nb.dmem[(a + 1u) ^ 3u]);
}

static void nb_dmem_w16(unsigned int off, unsigned int v)
{
    unsigned int a = off & 0xffeu;
    nb_watch(a, 2u, v);
    nb.dmem[a ^ 3u]        = (unsigned char)(v >> 8);
    nb.dmem[(a + 1u) ^ 3u] = (unsigned char)v;
}

static void nb_dmem_w8(unsigned int off, unsigned int v)
{
    nb.dmem[(off & 0xfffu) ^ 3u] = (unsigned char)v;
}

static void nb_watch(unsigned int a, unsigned int n, unsigned int v)
{
    static int t = -1;
    if (t < 0) t = getenv("NB_WATCH") != NULL;
    if (t && a < 0x12cu && a + n > 0x128u)
        fprintf(stderr, "[W!] %03x len %u = %08x dl=%06x\n", a, n, v, nb.dl);
}

static void nb_dmem_w32(unsigned int off, unsigned int v)
{
    unsigned int a = off & 0xffcu;
    nb_watch(a, 4u, v);
    nb.dmem[(a + 0u) ^ 3u] = (unsigned char)(v >> 24);
    nb.dmem[(a + 1u) ^ 3u] = (unsigned char)(v >> 16);
    nb.dmem[(a + 2u) ^ 3u] = (unsigned char)(v >> 8);
    nb.dmem[(a + 3u) ^ 3u] = (unsigned char)v;
}

/* DMA (len bytes) from RDRAM into the DMEM shadow */
static void nb_load(unsigned int dmem_off, unsigned int addr, unsigned int len)
{
    unsigned int i;
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_TRACE") != NULL;
        if (t) fprintf(stderr, "[NBL] dmem=%03x dram=%06x len=%x dl=%06x\n",
                       dmem_off & 0xfffu, addr & 0xffffffu, len, nb.dl);

    }
    nb_watch(dmem_off & 0xfffu, len, 0x10ad);
    for (i = 0; i < len; i++)
        nb.dmem[((dmem_off + i) & 0xfffu) ^ 3u] =
            (unsigned char)(s_rdram ? s_rdram[((addr + i) & 0x7fffffu) ^ 3u] : 0u);
}

#include "rdp_emit_rsp.h"

/* RSP 48-bit accumulator helpers (vmudn/vmadh chain, zboss style) */
static int64_t nb_p(int a, int b) { return (int64_t)a * (int64_t)b; }
static int nb_acc_mid(int64_t acc)
{
    /* vmadh result register: clamped s16 of acc bits 16..47 */
    int64_t v = acc >> 16;
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int)v;
}

/* Vertex transform (text 0x838-0x9ec): translate, 4x4 16.16 matrix,
 * dual guard-band clip codes, reciprocal divide, viewport, into
 * 0x28-byte records at DMEM 0x600.  First pass; calibrated against the
 * cxd4 oracle (goracle) on captured task slices. */
static void nb_xfrm(unsigned int count)
{
    unsigned int src = 0x170u;
    unsigned int dst = 0x600u;
    int tr[4];
    int mi[4][4], mf[4][4];
    int i, j;
    unsigned int v;

    /* translation from the state struct +0x18 (r18 = DMEM 0x110,
     * pinned by the op 0x80 init handler): DMEM 0x128, the MoveWord
     * targets in every frame prologue. */
    for (j = 0; j < 4; j++)
        tr[j] = nb_dmem_s16(0x128u + (unsigned int)j * 2u);
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_TRACE") != NULL;
        if (t) fprintf(stderr, "[XFRM] n=%u persp=%04x m30=%04x m31=%04x sc0=%04x of0=%04x tr=%04x %04x %04x %04x in0=%04x %04x %04x\n",
                       count,
                       (unsigned)nb_dmem_s16(0x14e)&0xffff,
                       (unsigned)nb_dmem_s16(0x5d8)&0xffff,
                       (unsigned)nb_dmem_s16(0x5da)&0xffff,
                       (unsigned)nb_dmem_s16(0x130)&0xffff,
                       (unsigned)nb_dmem_s16(0x138)&0xffff,
                       tr[0]&0xffff, tr[1]&0xffff, tr[2]&0xffff, tr[3]&0xffff,
                       (unsigned)nb_dmem_s16(0x170)&0xffff,
                       (unsigned)nb_dmem_s16(0x172)&0xffff,
                       (unsigned)nb_dmem_s16(0x174)&0xffff);
    }

    /* matrix at 0x5c0: 4 int rows then 4 frac rows, 4 lanes each */
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            mi[i][j] = nb_dmem_s16(0x5c0u + (unsigned int)(i * 8 + j * 2));
            /* fraction rows are unsigned (vmudn operand) */
            mf[i][j] = (int)(nb_dmem_s16(0x5e0u + (unsigned int)(i * 8 + j * 2)) & 0xffff);
        }

    for (v = 0; v < count; v++, src += 8u, dst += 0x28u) {
        int in[3], p[3];
        int ci[4]; unsigned int cf[4];
        int64_t acc;
        for (j = 0; j < 3; j++)
            in[j] = nb_dmem_s16(src + (unsigned int)j * 2u);
        /* vaddc pre-translate: unsigned per-lane add, no clamp */
        for (j = 0; j < 3; j++)
            p[j] = (int)(short)((unsigned short)((unsigned)in[j] + (unsigned)tr[j]));
        for (j = 0; j < 4; j++) {
            acc  = nb_p(mf[0][j], p[0]) + ((int64_t)nb_p(mi[0][j], p[0]) << 16);
            acc += nb_p(mf[1][j], p[1]) + ((int64_t)nb_p(mi[1][j], p[1]) << 16);
            acc += nb_p(mf[2][j], p[2]) + ((int64_t)nb_p(mi[2][j], p[2]) << 16);
            acc += nb_p(mf[3][j], 1)    + ((int64_t)nb_p(mi[3][j], 1) << 16);
            ci[j] = nb_acc_mid(acc);
            cf[j] = (unsigned int)(acc & 0xffffu);
        }
        /* store clip-space position (record +0x00 int, +0x08 frac) */
        for (j = 0; j < 4; j++) {
            nb_dmem_w16(dst + (unsigned int)j * 2u, (unsigned int)ci[j] & 0xffffu);
            nb_dmem_w16(dst + 8u + (unsigned int)j * 2u, cf[j]);
        }
        /* projection (text 0x8c0-0x9b8), oracle-exact: w' = w * persp
         * >> 16; rcp'd through the divide ROM, doubled, one Newton
         * step against 2; ratio = ((pos * rcp) >> 16 * persp) >> 16;
         * screen = viewport offset + (ratio * scale) >> 16 with the
         * s16 mid clamp. z keeps its fraction (record +0x1e). */
        {
            int persp = nb_dmem_s16(0x14eu) & 0xffff;
            int64_t p32[4], wp, r, wr, err, t, sacc;
            int scl, ofs, scr;
            for (j = 0; j < 4; j++)
                p32[j] = ((int64_t)ci[j] << 16) | cf[j];
            wp = (p32[3] * persp) >> 16;
            r  = rsp_rcp32_dp((int32_t)wp);
            r  = (int32_t)((uint32_t)r << 1);
            wr = (wp * r) >> 16;
            err = ((int64_t)2 << 16) - wr;
            r  = (int32_t)((r * err) >> 16);
            nb_dmem_w16(dst + 0x20u, ((unsigned int)r >> 16) & 0xffffu);
            nb_dmem_w16(dst + 0x22u, (unsigned int)r & 0xffffu);
            for (j = 0; j < 3; j++) {
                t = (p32[j] * r) >> 16;
                t = (t * persp) >> 16;
                scl = nb_dmem_s16(0x130u + (unsigned int)j * 2u);
                ofs = nb_dmem_s16(0x138u + (unsigned int)j * 2u);
                sacc = ((int64_t)ofs << 16) + t * scl;
                scr = (int)(sacc >> 16);
                if (scr > 32767) scr = 32767;
                if (scr < -32768) scr = -32768;
                nb_dmem_w16(dst + 0x18u + (unsigned int)j * 2u,
                            (unsigned int)scr & 0xffffu);
                if (j == 2)
                    nb_dmem_w16(dst + 0x1eu, (unsigned int)(sacc & 0xffff));
            }
        }
        /* clip codes (+0x24) and fog (+0x13): next calibration pass
         * (this batch exercises neither; the tri emitter needs them).
         * Store width follows the microcode's pair asymmetry: vertex A
         * of each pair uses sh (+0x24 only, +0x26 preserved), vertex B
         * uses sw (+0x24 and +0x26 cleared together). */
        if (v & 1u)
            nb_dmem_w32(dst + 0x24u, 0u);
        else
            nb_dmem_w16(dst + 0x24u, 0u);
    }
}

/* op 0x02 (text 0xd1c): open a segmented stream.  DMA the 8-byte
 * chain header at w1 into DMEM 0xfd8 (next-segment pointer), position
 * = w1 + 8, remaining = w0 & 0x1ff payload bytes. */
static void nb_stream_open(unsigned int base)
{
    nb_load(0xfd8u, base, 8u);
    nb_dmem_w32(0xfdcu, 0x100u);
    nb_dmem_w32(0xfd4u, base + 8u);
}

static void nb_op02(unsigned int w0, unsigned int w1)
{
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_TRACE") != NULL;
        if (t) fprintf(stderr, "[OP02] @%06x w0=%08x w1=%08x\n", nb.dl, w0, w1);
    }
    nb_stream_open(w1 & 0x00ffffffu);
    nb_dmem_w32(0xfdcu, w0 & 0x1ffu);
}

/* op 0x01: segmented data load + subtype dispatch (text 0xc94).
 * Direct when w1's low 24 bits are nonzero; otherwise consume the
 * open stream, chaining to the next segment (pre-fetched header at
 * DMEM 0xfd8) when the payload remainder runs out mid-load. */
static void nb_op01(unsigned int w0, unsigned int w1)
{
    unsigned int len  = (w0 & 0xffu) + 1u;
    unsigned int off  = (w0 >> 8) & 0xfffu;
    unsigned int sub  = (w0 >> 19) & 6u;
    unsigned int addr = w1 & 0x00ffffffu;

    if ((w0 & 0xffu) == 0u) {
        /* count 0: no load, dispatch only (text 0xc98) */
    } else if (addr != 0u) {
        nb_load(off, addr, len);
    } else {
        unsigned int pos  = nb_dmem_r32(0xfd4u) & 0x00ffffffu;
        unsigned int rem  = nb_dmem_r32(0xfdcu);
        if (rem >= len) {
            nb_load(off, pos, len);
            nb_dmem_w32(0xfdcu, rem - len);
            nb_dmem_w32(0xfd4u, pos + len);
        } else {
            /* split: drain this segment, chain, load the rest */
            unsigned int next;
            nb_load(off, pos, rem);
            next = nb_dmem_r32(0xfd8u) & 0x00ffffffu;
            nb_stream_open(next);
            pos = nb_dmem_r32(0xfd4u) & 0x00ffffffu;
            nb_load(off + rem, pos, len - rem);
            nb_dmem_w32(0xfdcu, 0x100u - (len - rem));
            nb_dmem_w32(0xfd4u, pos + (len - rem));
        }
    }

    switch (sub) {
    case 2:                                     /* vertex transform */
        nb_xfrm((w1 >> 24) & 0x7fu);
        break;
    case 4:                                     /* color processor
         * (text 0xbd4-0xc70): in-place top-down expansion of the
         * loaded RGB565 halfwords at 0x480+2n into RGBA words at
         * 0x480+4n, then -- when the count byte's sign OR the
         * intensity byte at 0x58b is negative -- a vmulf per-channel
         * scale by the 8 intensity bytes at DMEM 0x158 into the
         * array at 0xd40.  w0 bits 0/1 dispatch overlays 0x21/0x1b
         * afterwards (unimplemented: caller falls back). */
        {
            int cnt7 = (int)((w1 >> 24) & 0x7fu);
            int neg  = ((w1 >> 24) & 0x80u) != 0u;
            int i2;
            if ((w0 >> 23) & 1u) {
                for (i2 = cnt7 - 1; i2 >= 0; i2--) {
                    unsigned int c565 =
                        ((unsigned int)nb.dmem[(0x480u + (unsigned)i2*2u) ^ 3u] << 8)
                       | (unsigned int)nb.dmem[(0x481u + (unsigned)i2*2u) ^ 3u];
                    unsigned int rgba = ((c565 & 0xf800u) << 16)
                                      | ((c565 & 0x07e0u) << 13)
                                      | ((c565 & 0x001fu) << 11)
                                      | 0xffu;
                    nb_dmem_w32(0x480u + (unsigned)i2 * 4u, rgba);
                }
            }
            if (neg || (nb.dmem[0x58bu ^ 3u] & 0x80u)) {
                /* intensity scale into 0xd40; per-channel vmulf by the
                 * 8 bytes at 0x158, no overlay dispatch on this path
                 * (text 0xc44-0xc70) */
                for (i2 = 0; i2 < cnt7; i2++) {
                    unsigned int j2;
                    for (j2 = 0; j2 < 4u; j2++) {
                        int a2 = (int)nb.dmem[(0x480u + (unsigned)i2*4u + j2) ^ 3u] << 7;
                        int b2 = (int)nb.dmem[(0x158u + (j2 + ((unsigned)i2 & 1u) * 4u)) ^ 3u] << 7;
                        int64_t acc = (int64_t)a2 * b2 * 2 + 0x8000;
                        int r2 = (int)(acc >> 16);
                        if (r2 > 32767) r2 = 32767;
                        if (r2 < 0) r2 = 0;
                        nb.dmem[(0xd40u + (unsigned)i2*4u + j2) ^ 3u] =
                            (unsigned char)(r2 >> 7);
                    }
                }
            } else if (w0 & 3u) {
                /* non-scaled path dispatches overlays 0x21/0x1b on w0
                 * bits 0/1 (text 0xc74-0xc84): fallback */
                nb.active = 2u;
            }
        }
        break;
    case 6:                                     /* intensity byte */
        nb_dmem_w8(0x58bu, w1 >> 24);
        break;
    default:                                    /* load-only */
        break;
    }
}

/* Triangle command (slot 0x0b, 16 or 32 bytes).  Layout:
 *   w0: opcode | flags (bit 9 = inline ST present, bit 11 = overlay
 *       0x2a extended path); low bits carry the record-base constant
 *   w1: vertex record addresses A (hi16) and B (lo16)
 *   +8..+11: face color-list byte offsets (byte +8 = the extra/fog
 *       slot, +9/+10/+11 = per-vertex offsets into the scaled color
 *       array at DMEM 0xd40)
 *   +12: vertex record address C; +14: staged slot for sharing
 *   +16..+31 (when w0 bit 9): packed S/T words, lanes 0/2/4 -> A/B/C
 *
 * Emission side effects modeled here (text 0xa94): when geometry-mode
 * bit 16 is set, each face's fog bytes (record +0x13) propagate into
 * the color array's alpha (0xd40 + off + 3); then the color words are
 * poked into the records at +0x10 and the inline STs at +0x14. */
/* Build an RspTriVtx from a vertex record in the DMEM shadow. */
static void nb_vtx(unsigned int rec, RspTriVtx *v)
{
    v->x = (int16_t)nb_dmem_s16(rec + 0x18u);
    v->y = (int16_t)nb_dmem_s16(rec + 0x1au);
    v->z = (int32_t)((nb_dmem_s16(rec + 0x1cu) << 16)
                     | (nb_dmem_s16(rec + 0x1eu) & 0xffff));
    v->r = nb.dmem[((rec + 0x10u) & 0xfffu) ^ 3u];
    v->g = nb.dmem[((rec + 0x11u) & 0xfffu) ^ 3u];
    v->b = nb.dmem[((rec + 0x12u) & 0xfffu) ^ 3u];
    v->a = nb.dmem[((rec + 0x13u) & 0xfffu) ^ 3u];
    v->s = nb_dmem_s16(rec + 0x14u);
    v->t = nb_dmem_s16(rec + 0x16u);
    v->invw = (int32_t)((nb_dmem_s16(rec + 0x20u) << 16)
                        | (nb_dmem_s16(rec + 0x22u) & 0xffff));
    v->pw = 0;
    v->flat2d = 0;
}

/* Emit one triangle from three records: trivial reject on the ANDed
 * outcodes (0x7070), clip trigger on the ORed outcodes (0x4343 ->
 * caller falls back), winding cull against the geometry-mode bit,
 * then the shared RSP-exact edge/attribute writer.  Returns -1 when
 * the clip overlay would run. */
static int nb_emit_tri(RdpFifo *fifo, unsigned int ra, unsigned int rb,
                       unsigned int rc)
{
    unsigned int oa = nb_dmem_s16(ra + 0x24u) & 0xffffu;
    unsigned int ob = nb_dmem_s16(rb + 0x24u) & 0xffffu;
    unsigned int oc = nb_dmem_s16(rc + 0x24u) & 0xffffu;
    RspTriVtx va, vb, vc;
    int32_t ew[64];
    int nw;
    int64_t cross;
    int tilebyte;

    if (oa & ob & oc & 0x7070u)
        return 0;                       /* trivial reject */
    if ((oa | ob | oc) & 0x4343u)
        return -1;                      /* clip overlay: fall back */

    nb_vtx(ra, &va); nb_vtx(rb, &vb); nb_vtx(rc, &vc);

    /* winding cull (10.2 screen, saturated deltas, cross vs the
     * geometry-mode cull bit -- Rogue Squadron convention) */
    {
        int32_t d1x = (vb.x - va.x), d1y = (vb.y - va.y);
        int32_t d2x = (vc.x - va.x), d2y = (vc.y - va.y);
        if (d1x > 32767) d1x = 32767; if (d1x < -32768) d1x = -32768;
        if (d1y > 32767) d1y = 32767; if (d1y < -32768) d1y = -32768;
        if (d2x > 32767) d2x = 32767; if (d2x < -32768) d2x = -32768;
        if (d2y > 32767) d2y = 32767; if (d2y < -32768) d2y = -32768;
        cross = (int64_t)d2x * d1y - (int64_t)d1x * d2y;
        if (cross == 0)
            return 0;
        if (cross < 0 && (nb.geom & 0x2000u))
            return 0;
    }

    tilebyte = (int)nb.dmem[0x14au ^ 3u];
    /* the Naboo emitter shares Rogue Squadron's edge-anchor and
     * attribute conventions; the shared writer's flags are global, so
     * assert them per call (another walker may have run) */
    rsp_set_tri_attr_rs(1);
    rsp_tri_set_rs_sort(1);
    nw = rsp_tri_write(ew, &va, &vb, &vc,
                       (int)(nb.geom >> 1) & 1,
                       (int)nb.geom & 1,
                       (int)(nb.geom >> 2) & 1, 1,
                       tilebyte & 7, (tilebyte >> 3) & 7,
                       0x1000, 0x20, (int32_t)0xfff8, 0);
    if (nw > 0)
        rdp_fifo_append(fifo, ew, nw);
    return 0;
}

static int nb_tri(RdpFifo *fifo, unsigned int w0, unsigned int w1, int quad)
{
    if (!nb_emit_on)
        return -1;

    unsigned int va = (w1 >> 16) & 0xfff8u;
    unsigned int vb = w1 & 0xfff8u;
    unsigned int vc, vd, off_a, off_b, off_c, off_x;
    unsigned int cd = nb.dmem[0x58au ^ 3u];
    unsigned int len = 16u;

    /* batching countdown (text 0xb8c) */
    if (cd == 0u) {
        nb.dmem[0x58au ^ 3u] = (unsigned char)w0;
    } else {
        nb.dmem[0x58au ^ 3u] = (unsigned char)(cd - 1u);
        nb_dl_step((w0 & 0x200u) ? 32u : 16u);
        return 0;                       /* skipped by the countdown */
    }

    if (w0 & 0x800u)
        return -1;                      /* overlay 0x2a path: fallback */

    vc    = nb_read_u32(nb.dl + 12u) >> 16;
    vc   &= 0xfff8u;
    off_x = (nb_read_u32(nb.dl + 8u) >> 24) & 0xffu;
    off_a = (nb_read_u32(nb.dl + 8u) >> 16) & 0xffu;
    off_b = (nb_read_u32(nb.dl + 8u) >> 8) & 0xffu;
    off_c =  nb_read_u32(nb.dl + 8u) & 0xffu;

    vd = nb_read_u32(nb.dl + 12u) & 0xfff8u;    /* halfword +14 */

    if (nb.geom & 0x10000u) {
        /* fog-into-alpha propagation (text 0xab8-0xad4); the extra
         * slot pairs with the staged fourth vertex D on the quad op
         * (slot 0x0b, entry 1:b44) and with DMEM byte 0x13 on the
         * single-triangle op (slot 0x16, entry 1:b24, r15 = 0) */
        nb.dmem[(0xd43u + off_x) ^ 3u] = quad
            ? nb.dmem[((vd + 0x13u) & 0xfffu) ^ 3u]
            : nb.dmem[0x13u ^ 3u];
        nb.dmem[(0xd43u + off_a) ^ 3u] = nb.dmem[((va + 0x13u) & 0xfffu) ^ 3u];
        nb.dmem[(0xd43u + off_b) ^ 3u] = nb.dmem[((vb + 0x13u) & 0xfffu) ^ 3u];
        nb.dmem[(0xd43u + off_c) ^ 3u] = nb.dmem[((vc + 0x13u) & 0xfffu) ^ 3u];
    }

    {
        static int t = -1;
        if (t < 0) t = getenv("NB_TRI_TRACE") != NULL;
        if (t) fprintf(stderr, "[TRI] @%06x va=%03x vb=%03x vc=%03x off=%02x/%02x/%02x col_a=%08x\n",
                       nb.dl, va, vb, vc, off_a, off_b, off_c,
                       nb_dmem_r32(0xd40u + off_a));
    }
    nb_dmem_w32(va + 0x10u, nb_dmem_r32(0xd40u + off_a));
    nb_dmem_w32(vb + 0x10u, nb_dmem_r32(0xd40u + off_b));
    nb_dmem_w32(vc + 0x10u, nb_dmem_r32(0xd40u + off_c));
    /* every command is a quad: A/B/C poked by the parser, D by the
     * handler tail (text 0xb5c-0xb60), which then emits triangles
     * (A,B,C) and (A,C,D) */
    if (quad)
        nb_dmem_w32(vd + 0x10u, nb_dmem_r32(0xd40u + off_x));

    if (w0 & 0x200u) {
        len = 32u;
        /* inline S/T: slv elements e0/e4/e8/e12 map words +16/+20/
         * +24/+28 to A/B/C/D */
        nb_dmem_w32(va + 0x14u, nb_read_u32(nb.dl + 16u));
        nb_dmem_w32(vb + 0x14u, nb_read_u32(nb.dl + 20u));
        nb_dmem_w32(vc + 0x14u, nb_read_u32(nb.dl + 24u));
        if (quad)
            nb_dmem_w32(vd + 0x14u, nb_read_u32(nb.dl + 28u));
    }
    /* emit: (A,B,C), then (A,C,D) on the quad op */
    if (nb_emit_tri(fifo, va, vb, vc) < 0)
        return -1;
    if (quad && nb_emit_tri(fifo, va, vc, vd) < 0)
        return -1;

    nb_dl_step(len);
    return 0;
}

int naboo_run_dl(RdpFifo *fifo, unsigned int dl_addr, int resume)
{
    if (!resume)
        naboo_task_reset(dl_addr);
    if (!nb.active)
        return NABOO_R_FALLBACK;

    for (;;) {
        unsigned int w0 = nb_read_u32(nb.dl);
        unsigned int w1 = nb_read_u32(nb.dl + 4);
        {
            static int t = -1;
            if (t < 0) t = getenv("NB_CMD_TRACE") != NULL;
            if (t) fprintf(stderr, "[NC] @%06x %08x %08x\n", nb.dl, w0, w1);
        }
        unsigned int cls = w0 >> 30;
        unsigned int op  = (w0 >> 24) & 0xffu;

        if (cls == 3u) {
            /* RDP passthrough (text 0x98).  G_TEXRECT carries two
             * extra inline words. */
            int32_t words[2];
            if (op == 0xe4u || op == 0xe5u) {
                /* G_TEXRECT is the compositor's splice point (text
                 * 0xb4, jal 0x7b4): when the CPU has staged a
                 * texture-setup list in the DMEM word 0x58c, the
                 * handler saves its position and walks that list
                 * first -- its top-level EndDL returns here and the
                 * texrect reprocesses with the link cleared.  The
                 * 'expanded' load blocks in the reference stream are
                 * these CPU-built lists, not generated commands. */
                unsigned int splice = nb_dmem_r32(0x58cu) & 0x00fffff8u;
                if (splice != 0u) {
                    if (nb.sp >= NB_DL_STACK) {
                        nb.active = 0;
                        return NABOO_R_FALLBACK;
                    }
                    nb_dmem_w32(0x58cu, 0u);
                    nb.stack[nb.sp * 2u] = nb.chunk;
                    nb.stack[nb.sp * 2u + 1u] = nb.off;
                    nb.sp++;
                    nb_dl_enter(splice);
                    continue;
                }
            }
            words[0] = (int32_t)w0;
            words[1] = (int32_t)w1;
            rdp_fifo_append(fifo, words, 2);
            nb_dl_step(8u);
            if (op == 0xe4u || op == 0xe5u) {
                words[0] = (int32_t)nb_read_u32(nb.dl);
                words[1] = (int32_t)nb_read_u32(nb.dl + 4);
                rdp_fifo_append(fifo, words, 2);
                nb_dl_step(8u);
            }
            continue;
        }

        /* alias GBI-numbered opcodes onto the custom slot set */
        if (op >= 0xa9u && op <= 0xc8u)
            op -= 0xa9u;

        switch (op) {
        case 0x00:                              /* NOOP */
            nb_dl_step(8u);
            continue;
        case 0x06:                              /* DisplayList: call w1,
             * push the return cursor (text 0x754: stack at DMEM 0xfe0,
             * depth byte at struct+0x32) */
            if (nb.sp >= NB_DL_STACK) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            nb.stack[nb.sp * 2u] = nb.chunk;
            nb.stack[nb.sp * 2u + 1u] = nb.off + 8u;
            nb.sp++;
            nb_dl_enter(w1);
            continue;
        case 0x0f:                              /* EndDL (GBI 0xb8):
             * pop a pushed cursor, or finish at top level (text 0x778).
             * At top level the server follows the live tail first: if
             * the word at DMEM 0x58c is nonzero, the CPU has appended
             * another list segment -- continue there and clear the
             * link (text 0x79c-0x7cc). */
            if (nb.sp) {
                nb.sp--;
                nb.chunk = nb.stack[nb.sp * 2u];
                nb.off = nb.stack[nb.sp * 2u + 1u];
                nb.dl = nb.chunk + nb.off;
                if (nb.off >= 0x108u) {
                    nb.off -= 8u;
                    nb.dl = nb.chunk + nb.off;
                    nb_dl_step(8u);
                }
                continue;
            }
            {
                unsigned int tail = nb_dmem_r32(0x58cu) & 0x00fffff8u;
                if (tail != 0u) {
                    nb_dmem_w32(0x58cu, 0u);
                    nb_dl_enter(tail);
                    continue;
                }
            }
            nb.active = 0;
            return NABOO_R_DONE;
        case 0x02:                              /* open segmented
             * stream (text 0xd1c) */
            nb_op02(w0, w1);
            nb_dl_step(8u);
            continue;
        case 0x01:                              /* segmented load +
             * subtype processor (state modeling; RDP output comes from
             * the triangle path, which still falls back) */
            nb_op01(w0, w1);
            if (nb.active == 2u) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            nb_dl_step(8u);
            continue;
        case 0x0b:                              /* quad: two
             * triangles (A,B,C) and (A,C,D) (entry 1:b44) */
        case 0x16:                              /* single triangle
             * (A,B,C) (entry 1:b24) */
            /* the triangle emitter's jalr through DMEM 0x152 lands on
             * the same splice-check helper as G_TEXRECT when the
             * default 0x17b4 routine is installed: a staged list in
             * 0x58c is walked first, then the triangle reprocesses */
            {
                unsigned int splice = nb_dmem_r32(0x58cu) & 0x00fffff8u;
                if (splice != 0u) {
                    if (nb.sp >= NB_DL_STACK) {
                        nb.active = 0;
                        return NABOO_R_FALLBACK;
                    }
                    nb_dmem_w32(0x58cu, 0u);
                    nb.stack[nb.sp * 2u] = nb.chunk;
                    nb.stack[nb.sp * 2u + 1u] = nb.off;
                    nb.sp++;
                    nb_dl_enter(splice);
                    continue;
                }
            }
            if (nb_tri(fifo, w0, w1, op == 0x0bu) < 0) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            continue;
        case 0x0c:                              /* NOP (entry 1:068 =
             * the fetch loop: consume and continue).  Gated with the
             * emitter: on unverified builds, completing slices that
             * previously fell back changes output ordering. */
            if (!nb_emit_on) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            nb_dl_step(8u);
            continue;
        case 0x0d:                              /* GeometryMode &= w1
             * (text 0xa70) */
            nb.geom &= w1;
            nb_dl_step(8u);
            continue;
        case 0x0e:                              /* GeometryMode |= w1
             * (text 0xa68) */
            nb.geom |= w1;
            nb_dl_step(8u);
            continue;
        case 0x10:                              /* conditional state
             * insert (text 0x7d0): compare w0 bit 23 against the
             * struct word +0xc; on mismatch skip, else perform the
             * slot 0x11 insert */
            if (((w0 >> 23) & 1u) != nb_dmem_r32(0x11cu)) {
                nb_dl_step(8u);
                continue;
            }
            /* fall through */
        case 0x11:                              /* state-word bitfield
             * insert (text 0x7e0): word = 0x120 + ((w0>>16)&7)*4;
             * clear a field of width (w0&31)+1 at shift (w0>>8)&31,
             * OR in w1 (pre-shifted by the CPU). Words 0/1 double as
             * the RDP SET_OTHER_MODES pair (emitted on the real
             * path); words 4-7 are the viewport block the transform
             * reads. */
            {
                /* the idx field is a BYTE offset into the state block
                 * (lw a1, 0x120(a0) with a0 = (w0>>16)&7): words at
                 * 0x120 and 0x124 only -- the RDP SET_OTHER_MODES
                 * pair. */
                unsigned int idx = (w0 >> 16) & 4u;
                unsigned int cur = nb_dmem_r32(0x120u + idx);
                unsigned int msk =
                    (unsigned int)((int32_t)0x80000000 >> (w0 & 31u));
                msk >>= (w0 >> 8) & 31u;
                cur = (cur & ~msk) | w1;
                nb_dmem_w32(0x120u + idx, cur);
            }
            /* forward the refreshed SET_OTHER_MODES pair, replacing
             * the pair at the fifo tail when it is already an EF
             * command (text 0x80c-0x834: consecutive mode inserts
             * collapse into one RDP command) */
            if (nb_emit_on) {
                int32_t words[2];
                words[0] = (int32_t)nb_dmem_r32(0x120u);
                words[1] = (int32_t)nb_dmem_r32(0x124u);
                if (fifo->used >= 8u &&
                    (*(const uint32_t *)(fifo->storage + fifo->used - 8u)
                     >> 24) == 0xefu)
                    fifo->used -= 8u;
                rdp_fifo_append(fifo, words, 2);
            }
            nb_dl_step(8u);
            continue;
        case 0x12:                              /* state toggle (text
             * 0xa80): store w0 at struct +0x38; flip geometry-mode
             * bit 1 when (geom ^ w0) & 2 */
            nb_dmem_w32(0x148u, w0);
            nb.geom ^= (nb.geom ^ w0) & 2u;
            nb_dl_step(8u);
            continue;
        case 0x13:                              /* MoveWord (GBI 0xbc):
             * DMEM[w0 & 0xffc] = w1 (text 0xa78) */
            {
                static int t = -1;
                if (t < 0) t = getenv("NB_MW_TRACE") != NULL;
                if (t && (w0 & 0xfe0u) == 0x120u)
                    fprintf(stderr, "[MW] %03x = %08x\n", w0 & 0xffcu, w1);
            }
            nb_dmem_w32(w0, w1);
            nb_dl_step(8u);
            continue;
        default:
            /* not yet implemented: rerun this slice on the LLE
             * fallback.  (Test harness: NB_PERMISSIVE skips unknown
             * commands so the loader/transform path can be
             * oracle-diffed in isolation; never set in production.) */
            {
                static int perm = -1;
                if (perm < 0)
                    perm = getenv("NB_PERMISSIVE") != NULL;
                if (perm) {
                    unsigned int step = 8u;
                    if (op == 0x05u) {
                        /* load+run overlay: never skippable (overlay
                         * code has arbitrary effects, and the oracle
                         * truncation technique patches list tails
                         * into overlay-0x1e exits) -- stop here */
                        nb.active = 0;
                        return NABOO_R_FALLBACK;
                    }
                    if (op == 0x0bu) {
                        /* empirical (goracle command trace, task 240,
                         * 3826 dispatches): triangles and slot 0x16
                         * are 32 bytes in this workload */
                        step = 32u;
                        nb_dl_step(step);
                        continue;
                    }
                    if (0) {
                        /* triangle batching (text 0xb8c): countdown
                         * byte at DMEM 0x58a, seeded from the first
                         * tri's w0; while active, each tri decrements
                         * it and consumes 16 bytes, or 32 when w0 bit
                         * 9 is set.  INCOMPLETE: triangles also carry
                         * inline per-vertex attribute payloads whose
                         * length depends on the mode routine at DMEM
                         * 0x152 (Rogue Squadron's inline S/T pattern),
                         * so the permissive walk still desyncs on
                         * attribute-bearing batches; exact stepping
                         * lands with the emitter. */
                        unsigned int cd = nb.dmem[0x58au ^ 3u];
                        step = 16u;
                        if (cd == 0u) {
                            nb.dmem[0x58au ^ 3u] = (unsigned char)w0;
                        } else {
                            nb.dmem[0x58au ^ 3u] = (unsigned char)(cd - 1u);
                            if (w0 & 0x200u)
                                step = 32u;
                        }
                    } else if (op == 0x15u) {
                        step = 16u;
                    }
                    nb_dl_step(step);
                    continue;
                }
            }
            nb.active = 0;
            return NABOO_R_FALLBACK;
        }
    }
}
