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
    unsigned int clip_poly[10]; /* clip fan resume state */
    unsigned int clip_n, clip_idx, clip_active;
    unsigned int tri_phase;     /* quad resume: 1 = first triangle
                                   already emitted before a splice */
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

unsigned int nb_task_ordinal;
void naboo_task_reset(unsigned int dl)
{
    nb_task_ordinal++;
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
static void nb_ovl1b(unsigned int w1);
struct rdp_fifo_s;
static int nb_emit_tri(RdpFifo *fifo, unsigned int ra, unsigned int rb,
                       unsigned int rc);
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
        nb_dmem_w32(0xfc4u, 0u);
        nb_dmem_w32(0x5b0u, 0u);
        nb_dmem_w32(0x5b4u, 0u);
        nb_dmem_w32(0x16cu, nb_dmem_r32(0xff8u));
        nb_dmem_w32(0x5a0u, nb_dmem_r32(0xff0u));
        nb_dmem_w8(0x142u, 0u);
        nb_dmem_w16(0x152u, 0x17b4u);
        nb_dmem_w32(0x120u, 0xef000000u);
        nb_dmem_w32(0x124u, 0u);
        nb.dmem[0x58au ^ 3u] = 0u;      /* batching countdown */
        nb_dmem_w32(0x11cu, 0u);        /* conditional-insert state */
        nb_dmem_w32(0x58cu, 0u);        /* splice link */
        nb_dmem_w16(0x168u, 0xffu);
        nb.dmem[0x37eu ^ 3u] = 0u;
        nb_dmem_w32(0x110u, nb_dmem_r32(0xfe8u));
        nb_dmem_w32(0x114u, nb_dmem_r32(0xfecu));
        nb_dmem_w32(0x118u, nb_dmem_r32(0xfecu));
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

/* Exact vch/vcl pair for the transform's clip tests (text 0x8cc-0x8dc):
 * 8 lanes, VS = position (int then frac pass), VT = per-vertex w
 * broadcast half-wise (element 3h).  Faithful to the RSP select-op
 * semantics including the carry/vce state vcl consumes from vch.
 * Returns the VCC halfword after the vcl (le bits 0-7, ge 8-15). */
static unsigned int nb_vch_vcl(const int16_t si[8], const int16_t sf[8],
                               const int16_t ti[8], const int16_t tf[8])
{
    int16_t sn[8], vce[8], ne[8], le1[8], ge1[8];
    unsigned int vcc = 0u;
    int i;

    /* vch on the integer lanes */
    for (i = 0; i < 8; i++) {
        int16_t vs = si[i], vt = ti[i], vc = vt;
        int cch = (vt == -32768);
        int s = ((vs ^ vt) < 0);
        int16_t eq, le, ge, diff;
        sn[i] = (int16_t)(s ? -1 : 0);
        if (s) vc = (int16_t)~vc;
        vce[i] = (int16_t)((vs == vc) && s);
        if (s && !cch) vc = (int16_t)(vc + 1);
        eq = (int16_t)(((vs == vc) && !cch) || vce[i]);
        diff = (int16_t)(sn[i] | vs);
        ge = (int16_t)(diff >= vt);
        diff = (int16_t)(vc - vs);
        le = (int16_t)(s ? (diff >= 0) : (vt < 0));
        ne[i] = (int16_t)(eq ^ 1);
        le1[i] = le; ge1[i] = ge;
    }
    /* vcl on the fraction lanes, consuming the vch flags */
    for (i = 0; i < 8; i++) {
        uint16_t vb = (uint16_t)sf[i], vc = (uint16_t)tf[i];
        int s = sn[i] != 0;
        int eq = (ne[i] == 0);
        int16_t diff; int uz, lz, gen, len, le, ge;
        if (s) vc = (uint16_t)(-(int16_t)vc);
        diff = (int16_t)(vb - vc);
        uz = (int)(((uint32_t)vb + (uint16_t)tf[i] - 65536u) >> 31);
        lz = (diff == 0);
        gen = lz | uz;
        len = lz & uz;
        gen = gen & (int)vce[i];
        len = len & (int)(vce[i] ^ 1);
        len = len | gen;
        gen = (vb >= vc);
        le = (eq && s)  ? len : (int)le1[i];
        ge = (eq && !s) ? gen : (int)ge1[i];
        if (le) vcc |= 1u << i;
        if (ge) vcc |= 1u << (8 + i);
    }
    return vcc;
}

/* Project one vertex record and compute its clip codes (text
 * 0x8c0-0x924): reads the clip-space position from the record
 * (+0x00 int, +0x08 frac), writes the screen triple (+0x18, z
 * fraction +0x1e), the doubled Newton-stepped reciprocal (+0x20),
 * and the outcode halfword (+0x24, sh flavor -- +0x26 preserved).
 * Factored from the batch transform for the clip overlay's
 * interpolated vertices. */
static unsigned int nb_project(unsigned int rec)
{
    int ci[4]; unsigned int cf[4];
    int j;
    for (j = 0; j < 4; j++) {
        ci[j] = nb_dmem_s16(rec + (unsigned int)j * 2u);
        cf[j] = (unsigned int)nb_dmem_s16(rec + 8u + (unsigned int)j * 2u) & 0xffffu;
    }
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
        nb_dmem_w16(rec + 0x20u, ((unsigned int)r >> 16) & 0xffffu);
        nb_dmem_w16(rec + 0x22u, (unsigned int)r & 0xffffu);
        for (j = 0; j < 3; j++) {
            t = (p32[j] * r) >> 16;
            t = (t * persp) >> 16;
            scl = nb_dmem_s16(0x130u + (unsigned int)j * 2u);
            ofs = nb_dmem_s16(0x138u + (unsigned int)j * 2u);
            sacc = ((int64_t)ofs << 16) + t * scl;
            scr = (int)(sacc >> 16);
            if (scr > 32767) scr = 32767;
            if (scr < -32768) scr = -32768;
            nb_dmem_w16(rec + 0x18u + (unsigned int)j * 2u,
                        (unsigned int)scr & 0xffffu);
            if (j == 2)
                nb_dmem_w16(rec + 0x1eu, (unsigned int)(sacc & 0xffff));
        }
    }
    {
        int16_t si[8], sf[8], ti[8], tf[8];
        int g = nb_dmem_s16(0x006u) & 0xffff;
        int64_t wg;
        unsigned int t1, t2, oc;
        for (j = 0; j < 4; j++) {
            si[j] = si[j + 4] = (int16_t)ci[j];
            sf[j] = sf[j + 4] = (int16_t)cf[j];
            ti[j] = ti[j + 4] = (int16_t)ci[3];
            tf[j] = tf[j + 4] = (int16_t)cf[3];
        }
        t1 = nb_vch_vcl(si, sf, ti, tf);
        wg = (int64_t)(uint16_t)cf[3] * g
           + (((int64_t)(int16_t)ci[3] * g) << 16);
        for (j = 0; j < 4; j++) {
            ti[j] = ti[j + 4] = (int16_t)((wg >> 16) & 0xffff);
            tf[j] = tf[j + 4] = (int16_t)(wg & 0xffff);
        }
        t2 = nb_vch_vcl(si, sf, ti, tf);
        oc = ((t1 & 0x707u) << 4) | (t2 & 0x707u);
        return oc;
    }
}

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
        /* projection and clip codes: identical to the standalone
         * record path, so share nb_project rather than carrying a
         * second transcription of text 0x8c0-0x9b8. */
        {
            unsigned int oc = nb_project(dst);
            static unsigned int oc_prev;
            {
                static int toc = -1;
                if (toc < 0) toc = getenv("NB_OC_TRACE") != NULL;
                if (toc) {
                    if (v & 1u)
                        fprintf(stderr, "[OC] sw r9=%03x v0=%08x\n",
                                dst - 0x28u, (oc << 16) | oc_prev);
                    else
                        fprintf(stderr, "[OC] sh r9=%03x v0=%08x\n",
                                dst, oc);
                }
            }
            if (v & 1u)
                nb_dmem_w32(dst + 0x24u, (oc << 16) | oc_prev);
            else
                nb_dmem_w16(dst + 0x24u, oc);
            oc_prev = oc;
        }
    }
}

/* Overlay 0x0f (entry 0xcf0, its own IMEM page): the descriptor/setup
 * half of the 0x0f / 0x09 pair -- it configures the 0xda0 work area
 * that the vertex-morphing overlay 0x09 then consumes.  Entirely
 * scalar; a3 = r17 - 8, i.e. the payload is the command itself plus
 * the bytes that follow it in the display list.
 *
 * Three paths, keyed on w1 bit 6 and the one-shot gate at DMEM 0x37e:
 *   bit 6 set          -> mode 2 (0xf40): three state stores + an
 *                         IMEM DMA of the second mode routine.
 *   gate set, bit clear -> short path (0xec0): 8-byte command.
 *   gate clear          -> first-time path: 0x38-byte payload, two
 *                         DMAs, and the 0xd58 vertex-record pointer
 *                         table.
 */
static void nb_dl_fetch(unsigned char *dst, unsigned int len)
{
    unsigned int chunk = nb.chunk, off = nb.off, i;
    for (i = 0; i < len; i++) {
        unsigned int a;
        if (off >= 0x108u) {
            chunk = nb_read_u32(chunk) & 0x00fffff8u;
            off = 8u + (off - 0x108u);
        }
        a = (chunk + off) & 0x7fffffu;
        dst[i] = (unsigned char)((s_rdram && a < s_rdram_size)
                                 ? s_rdram[a ^ 3u] : 0u);
        off++;
    }
}

static int nb_pl_s16(const unsigned char *pl, unsigned int o)
{
    return (int)(short)(((unsigned int)pl[o] << 8) | pl[o + 1u]);
}

static unsigned int nb_pl_u32(const unsigned char *pl, unsigned int o)
{
    return ((unsigned int)pl[o] << 24) | ((unsigned int)pl[o + 1u] << 16)
         | ((unsigned int)pl[o + 2u] << 8) | (unsigned int)pl[o + 3u];
}

static unsigned int nb_ovl0f(unsigned int w1)
{
    unsigned char pl[0x40];
    unsigned int a2 = 0xda0u;
    int j;

    if (w1 & 0x40u) {
        /* 0xf40: mode 2.  The trailing DMA lands in IMEM, which the
         * walker does not model; the three state stores are the whole
         * DMEM-visible effect. */
        nb_dmem_w16(0x152u, 0x17b4u);
        nb_dmem_w8(0x37eu, 0u);
        nb_dmem_w8(0x143u, 0x3fu);
        return 0u;
    }

    nb_dl_fetch(pl, sizeof pl);

    if (nb.dmem[0x37eu ^ 3u] != 0u) {
        /* 0xec0: already initialised -- 8-byte command. */
        int r14, r15;
        nb_dmem_w16(0xdb8u, 0u);
        nb_dmem_w32(0x100u, 0u);
        nb_dmem_w16(0xdbcu, (unsigned int)nb_pl_s16(pl, 2u) & 0xffffu);
        r14 = (int)(signed char)pl[1];
        r15 = (int)(signed char)pl[4];
        nb_dmem_w32(a2 + 0x28u, (unsigned int)r14 << 24);
        nb_dmem_w32(a2 + 0x20u, ((unsigned int)r14 << 24) | 0xffffu);
        nb_dmem_w32(a2 + 0x24u, (unsigned int)(r14 + r14));
        nb_dmem_w32(a2 + 0x2cu, (unsigned int)r15);
        nb_dmem_w16(a2 + 0x36u, (unsigned int)(r15 + r15) & 0xffffu);
        for (j = 0; j < 4; j++) {
            unsigned int v = (unsigned int)nb_pl_s16(pl, 8u + (unsigned int)j * 2u)
                             & 0xffffu;
            nb_dmem_w16(a2 + 0x42u + (unsigned int)j * 0x10u, v);
            nb_dmem_w16(a2 + 0x4au + (unsigned int)j * 0x10u, v);
        }
        {
            unsigned int v = (unsigned int)nb_pl_s16(pl, 5u) & 0xffffu;
            nb_dmem_w16(a2 + 0x86u, v);
            nb_dmem_w16(a2 + 0x8eu, v);
        }
        return 8u;
    }

    /* first-time path (0xd08-0xebc), 0x38-byte payload */
    {
        int r9 = nb_pl_s16(pl, 8u), r10 = nb_pl_s16(pl, 10u);
        nb_dmem_w16(a2 + 0x30u, (unsigned int)r9 & 0xffffu);
        nb_dmem_w16(a2 + 0x38u, (unsigned int)r9 & 0xffffu);
        nb_dmem_w16(a2 + 0x34u, (unsigned int)r10 & 0xffffu);
        nb_dmem_w32(a2 + 0x3cu, ((unsigned int)r10 << 16) | 0x100u);
        nb_dmem_w16(a2 + 0x32u, (unsigned int)nb_pl_s16(pl, 0x38u) & 0xffffu);
        nb_dmem_w16(a2 + 0x3au, (unsigned int)nb_pl_s16(pl, 0x3au) & 0xffffu);
        nb_dmem_w32(0x0f8u, nb_pl_u32(pl, 12u));
        nb_dmem_w8(0x0fcu, pl[1]);
        nb_dmem_w8(0x0fdu, pl[0x2bu]);
        /* four halfword quads fanned across the 0x40..0x7f grid */
        for (j = 0; j < 4; j++) {
            unsigned int b = a2 + 0x40u + (unsigned int)j * 0x10u;
            nb_dmem_w16(b + 0u, (unsigned int)nb_pl_s16(pl, 0x10u + (unsigned int)j * 2u) & 0xffffu);
            nb_dmem_w16(b + 8u, (unsigned int)nb_pl_s16(pl, 0x10u + (unsigned int)j * 2u) & 0xffffu);
            nb_dmem_w16(b + 4u, (unsigned int)nb_pl_s16(pl, 0x18u + (unsigned int)j * 2u) & 0xffffu);
            nb_dmem_w16(b + 0xcu, (unsigned int)nb_pl_s16(pl, 0x18u + (unsigned int)j * 2u) & 0xffffu);
            nb_dmem_w16(b + 6u, (unsigned int)nb_pl_s16(pl, 0x20u + (unsigned int)j * 2u) & 0xffffu);
            nb_dmem_w16(b + 0xeu, (unsigned int)nb_pl_s16(pl, 0x20u + (unsigned int)j * 2u) & 0xffffu);
        }
        nb_dmem_w32(a2 + 0xa0u, nb_pl_u32(pl, 0x28u));
        nb_dmem_w32(a2 + 0xa4u, nb_pl_u32(pl, 0x28u));
        nb_dmem_w32(a2 + 0xa8u, nb_pl_u32(pl, 0x2cu));
        nb_dmem_w32(a2 + 0xacu, nb_pl_u32(pl, 0x2cu));
        nb_dmem_w16(a2 + 0x84u, (unsigned int)nb_pl_s16(pl, 0x3cu) & 0xffffu);
        nb_dmem_w16(a2 + 0x8cu, (unsigned int)nb_pl_s16(pl, 0x3eu) & 0xffffu);
        {
            int v0 = nb_pl_s16(pl, 2u);
            unsigned int v1 = ((unsigned int)(v0 + 0xff)) & 0xff00u;
            nb_dmem_w16(a2 + 0x80u, (unsigned int)v0 & 0xffffu);
            nb_dmem_w16(a2 + 0x88u, v1);
            nb_dmem_w16(a2 + 0x82u, (unsigned int)nb_pl_s16(pl, 0x34u) & 0xffffu);
            nb_dmem_w16(a2 + 0x8au, (unsigned int)(signed char)pl[0x2fu] & 0xffffu);
        }
        nb_dmem_w32(a2 + 0x90u, 0x7fff7fffu);
        nb_dmem_w32(a2 + 0x98u, 0x7fff7fffu);
        nb_dmem_w32(a2 + 0x94u, nb_pl_u32(pl, 0x30u));
        nb_dmem_w32(a2 + 0x9cu, nb_pl_u32(pl, 0x30u));
        /* DMA 0xf0 bytes from (w1 >> 8) into DMEM 0xc68 */
        nb_load(0xc68u, w1 >> 8, 0xf0u);
        nb_dmem_w8(0x37eu, 0xffu);
        /* The second DMA (0xe64-0xe90) reads a descriptor at DMEM
         * 0x7c and lands outside the data page: the oracle sandwich
         * shows no DMEM delta from it on any captured call, so it
         * targets IMEM and is not modeled here. */
        nb_dmem_w16(0x152u, 0x1cc8u);
        for (j = 0; j <= 0x1c; j++)
            nb_dmem_w16(0xd58u + (unsigned int)j * 2u,
                        0x600u + (unsigned int)j * 0x28u);
        return 0x38u;
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
            {
                static int t = -1;
                if (t < 0) t = getenv("NB_CP_TRACE") != NULL;
                if (t) fprintf(stderr, "[CP] w0=%08x w1=%08x 58b=%02x neg=%d\n",
                               w0, w1, nb.dmem[0x58bu ^ 3u], neg);
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
                 * bits 0/1 (text 0xc74-0xc84): fallback.
                 * NB_SKIP_COLOVL is a validation-only bypass that
                 * leaves the colors unlit so downstream paths can be
                 * verified in isolation. */
                static int skip = -1;
                if (skip < 0) skip = getenv("NB_SKIP_COLOVL") != NULL;
                if (!skip) {
                    /* the selector is the subtype field's low bit
                     * (text 0xc74: even -> overlay 0x1b, odd ->
                     * overlay 0x21, still unimplemented) */
                    if ((w0 >> 19) & 1u)
                        nb.active = 2u;
                    else
                        nb_ovl1b(w1);
                }
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
    /* perspNorm'd w = the projection's divide input (clip w at record
     * +6/+14 scaled by the persp halfword), which the shared writer's
     * texture normalizer divides through */
    {
        int64_t w32 = ((int64_t)nb_dmem_s16(rec + 6u) << 16)
                    | (nb_dmem_s16(rec + 0xeu) & 0xffff);
        v->pw = (int32_t)((w32 * (nb_dmem_s16(0x14eu) & 0xffff)) >> 16);
    }
    v->flat2d = 0;
}

/* Emit one triangle from three records: trivial reject on the ANDed
 * outcodes (0x7070), clip trigger on the ORed outcodes (0x4343 ->
 * caller falls back), winding cull against the geometry-mode bit,
 * then the shared RSP-exact edge/attribute writer.  Returns -1 when
 * the clip overlay would run. */


/* vmulf: signed Q15 multiply with rounding and mid clamp */
static int nb_vmulf(int a, int b)
{
    int64_t acc = (int64_t)(int16_t)a * (int16_t)b * 2 + 0x8000;
    int32_t r = (int32_t)(acc >> 16);
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    return r;
}

/* vmulf into the accumulator, then vmadh += v8<<16, result = clamped
 * mid: the lighting overlays' contribution chain. */
static int nb_mulf_addh(int a, int b, int addend)
{
    int64_t acc = (int64_t)(int16_t)a * (int16_t)b * 2 + 0x8000;
    acc += (int64_t)(int16_t)addend << 16;
    return nb_acc_mid(acc);
}

/* Overlay 0x1b (vertex lighting; even op-0x01 subtypes with w0 bit 0
 * of the subtype field clear dispatch here from the color processor).
 * r20w1 = the color command's w1: vertex count in the top byte.
 * Lights are 0x18-byte records at DMEM 0xb00, counted by the byte at
 * 0x58b: +0x13 type (0 flat, 1 directional with the per-vertex s8
 * normal quads at 0x380 and the 0x5b0/0x5b4 alternate-direction
 * select, else positional with the vertex index riding byte 3 of
 * each normal quad), +0x10 color, +0/+8 direction or position pair,
 * +0x14 the attenuation halfwords.  Contributions accumulate into
 * the color array at 0xd40 scaled by the global bytes at 0x158; the
 * alpha lane always carries the scaled vertex alpha. */
static void nb_ovl1b(unsigned int w1)
{
    unsigned int end = ((((w1 >> 24) & 0x7fu) << 2) + 4u) & 0xff8u;
    unsigned int nv = end >> 2;
    unsigned int lights = nb.dmem[0x58bu ^ 3u];
    unsigned int li, vi, k;

    for (vi = 0; vi < end; vi++)
        nb.dmem[(0xd40u + vi) ^ 3u] = 0u;
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_DOT_TRACE") != NULL;
        if (t) fprintf(stderr, "[L1B] nv=%u lights=%u types=%02x %02x %02x\n",
                       nv, lights,
                       nb.dmem[(0xb13u) ^ 3u],
                       nb.dmem[(0xb2bu) ^ 3u],
                       nb.dmem[(0xb43u) ^ 3u]);
    }
    if (lights == 0u)
        goto done;

    for (li = 0; li < lights; li++) {
        unsigned int lrec = 0xb00u + li * 0x18u;
        unsigned int type = nb.dmem[(lrec + 0x13u) ^ 3u];
        int lcol[4];
        for (k = 0; k < 3u; k++)
            lcol[k] = (int)nb.dmem[(lrec + 0x10u + k) ^ 3u] << 7;
        lcol[3] = 0;

        if (type == 0u) {
            for (vi = 0; vi < nv; vi++)
                for (k = 0; k < 4u; k++) {
                    unsigned int off = vi * 4u + k;
                    int c = (int)nb.dmem[(0x480u + off) ^ 3u] << 7;
                    int g = (int)nb.dmem[(0x158u + (k + (vi & 1u) * 4u)) ^ 3u] << 7;
                    int v5 = nb_vmulf(c, g);
                    int a8 = (int)nb.dmem[(0xd40u + off) ^ 3u] << 7;
                    int r = (k == 3u) ? v5 : nb_mulf_addh(v5, lcol[k], a8);
                    nb.dmem[(0xd40u + off) ^ 3u] =
                        (unsigned char)(((unsigned)(int16_t)r >> 7) & 0xffu);
                }
            continue;
        }

        if (type == 1u) {
            /* directional: per vertex pair, dot(normal, dir) */
            unsigned int selw0 = nb_dmem_r32(0x5b0u);
            unsigned int selw1 = nb_dmem_r32(0x5b4u);
            unsigned int have_sel = selw0 | selw1;
            unsigned int sbits = selw0, scount = 16u;
            for (vi = 0; vi < nv; vi += 2u) {
                int16_t dir[8], nrm[8];
                unsigned int vcc = 0u;
                int dot[2];
                unsigned int pair, lane;
                if (have_sel) {
                    vcc = ((sbits & 3u) << 3);
                    sbits >>= 2;
                    if (--scount == 0u) { sbits = selw1; scount = 16u; }
                }
                for (lane = 0; lane < 8u; lane++) {
                    unsigned int base = ((vcc >> lane) & 1u) ? 8u : 0u;
                    dir[lane] = (int16_t)nb_dmem_s16(lrec + base + (lane & 3u) * 2u);
                }
                for (lane = 0; lane < 8u; lane++) {
                    int8_t nb8 = (int8_t)nb.dmem[(0x380u + vi * 4u + lane) ^ 3u];
                    nrm[lane] = (int16_t)((int)nb8 << 8);
                    if ((lane & 3u) == 3u)
                        nrm[lane] = 0;      /* x v14 zeroes lane 3/7 */
                }
                for (pair = 0; pair < 2u; pair++) {
                    int d = 0;
                    for (k = 0; k < 3u; k++) {
                        int m = nb_vmulf(dir[pair * 4u + k], nrm[pair * 4u + k]);
                        d += m;             /* vadd folds, clamped */
                        if (d > 32767) d = 32767;
                        if (d < -32768) d = -32768;
                    }
                    if (d < 0) d = 0;       /* vge 0 */
                    dot[pair] = d;
                }
                {
                    static int t = -1;
                    if (t < 0) t = getenv("NB_DOT_TRACE") != NULL;
                    if (t) fprintf(stderr, "[DOT] %d %d\n", dot[0], dot[1]);
                }
                {
                    int outl[8];
                    for (lane = 0; lane < 8u; lane++) {
                        unsigned int v = vi + (lane >> 2);
                        unsigned int ch = lane & 3u;
                        unsigned int off = v * 4u + ch;
                        int c = (int)nb.dmem[(0x480u + off) ^ 3u] << 7;
                        int g = (int)nb.dmem[(0x158u + (ch + (v & 1u) * 4u)) ^ 3u] << 7;
                        int v5 = nb_vmulf(c, g);
                        int contrib = nb_vmulf(lcol[ch],
                                               (int)(unsigned)dot[lane >> 2]);
                        int a8 = (int)nb.dmem[(0xd40u + off) ^ 3u] << 7;
                        int r = (ch == 3u) ? v5 : nb_mulf_addh(v5, contrib, a8);
                        outl[lane] = r;
                        if (lane < 8u) {
                            static int t3 = -1;
                            static int cbuf[8];
                            if (t3 < 0) t3 = getenv("NB_DOT_TRACE") != NULL;
                            cbuf[lane] = contrib;
                            if (t3 && lane == 7u)
                                fprintf(stderr,
                                    "[CT1] %d %d %d %d %d %d %d %d\n",
                                    cbuf[0],cbuf[1],cbuf[2],cbuf[3],
                                    cbuf[4],cbuf[5],cbuf[6],cbuf[7]);
                        }
                        if (v < nv)
                            nb.dmem[(0xd40u + off) ^ 3u] =
                                (unsigned char)(((unsigned)(int16_t)r >> 7) & 0xffu);
                    }
                    {
                        static int t2 = -1;
                        if (t2 < 0) t2 = getenv("NB_DOT_TRACE") != NULL;
                        if (t2) fprintf(stderr,
                            "[ST1] %d %d %d %d %d %d %d %d\n",
                            outl[0],outl[1],outl[2],outl[3],
                            outl[4],outl[5],outl[6],outl[7]);
                    }
                }
            }
            continue;
        }

        /* positional: index rides byte 3 of the normal quad */
        {
            int att_i = nb_dmem_s16(lrec + 0x14u);
            int att_f = nb_dmem_s16(lrec + 0x16u) & 0xffff;
            for (vi = 0; vi < nv; vi++) {
                unsigned int idx = nb.dmem[(0x380u + vi * 4u + 3u) ^ 3u];
                unsigned int src = 0x170u + idx;
                int32_t sq_lo3 = 0; int64_t sq_mid3 = 0;
                int64_t acc;
                int attn;
                for (k = 0; k < 3u; k++) {
                    int p = nb_dmem_s16(src + k * 2u);
                    int lp = nb_dmem_s16(lrec + k * 2u);
                    int d = (int)(int16_t)(p - lp);          /* vsub clamps */
                    if (d > 32767) d = 32767;
                    if (d < -32768) d = -32768;
                    /* d << 8 as vmudm mid/low */
                    {
                        int di = (int)(((int64_t)d * 0x100) >> 16);
                        int df = (int)(((int64_t)d * 0x100) & 0xffff);
                        /* full square 32x32 */
                        acc  = nb_p(df, df);
                        acc += nb_p(di, df) << 16;
                        acc += nb_p(df, di) << 16;
                        acc += nb_p(di, di) << 32;
                        sq_lo3 += (int32_t)((acc >> 16) & 0xffff);
                        sq_mid3 += acc >> 32;
                    }
                }
                sq_mid3 += sq_lo3 >> 16;
                {
                    int lo = (int)(sq_lo3 & 0xffff);
                    int hi = (int)(sq_mid3 > 32767 ? 32767 :
                                   (sq_mid3 < -32768 ? -32768 : sq_mid3));
                    /* x attenuation, 32x32; keep low iff hi == 0xffff */
                    acc  = nb_p(lo, att_f);
                    acc += nb_p(hi, att_f) << 16;
                    acc += nb_p(lo, att_i) << 16;
                    acc += nb_p(hi, att_i) << 32;
                    {
                        int rhi = nb_acc_mid(acc >> 16 << 16) ;
                        int rlo;
                        rhi = (int)((acc >> 32) & 0xffff);
                        rlo = (int)((acc >> 16) & 0xffff);
                        attn = (rhi == 0xffff) ? rlo : 0;
                    }
                }
                for (k = 0; k < 4u; k++) {
                    unsigned int off = vi * 4u + k;
                    int c = (int)nb.dmem[(0x480u + off) ^ 3u] << 7;
                    int g = (int)nb.dmem[(0x158u + (k + (vi & 1u) * 4u)) ^ 3u] << 7;
                    int v5 = nb_vmulf(c, g);
                    int contrib = nb_vmulf(lcol[k], (int)(unsigned)attn);
                    int a8 = (int)nb.dmem[(0xd40u + off) ^ 3u] << 7;
                    int r = (k == 3u) ? v5 : nb_mulf_addh(v5, contrib, a8);
                    nb.dmem[(0xd40u + off) ^ 3u] =
                        (unsigned char)(((unsigned)(int16_t)r >> 7) & 0xffu);
                }
            }
        }
    }
done:
    nb_dmem_w32(0x5b0u, 0u);
    nb_dmem_w32(0x5b4u, 0u);
}

/* nb_vmulf fwd */

/* Overlay 0x2a (environment-mapped texture coordinates, w0 bit 11):
 * for each of the four vertex-index bytes (command bytes +19/+23/+27/
 * +31), the input vertex's normal (halfword elements 4-6 of the raw
 * record at 0x170 + idx) is transformed by the normal matrix at DMEM
 * 0xe40 (four int rows then four frac rows; the fourth row adds as a
 * translation), the integer lanes drop the bias vector at DMEM 0xf0
 * (vsubc: u16 wrap, fractions untouched), the result is normalized
 * through the squares fold and the reciprocal square root scaled by
 * the constant 0xab, and the S/T pair is the scale halfwords at DMEM
 * 0xec times the normalized x/y (vmudm/vmadh: the emitted value is
 * the integer lane).  Results go to out_st[i] packed S<<16|T. */
static void nb_ovl2a(unsigned int cmd, unsigned int out_st[4])
{
    int mi[4][3], mf[4][3];
    int bias[3], sc[2];
    int i, r, k;

    for (r = 0; r < 4; r++)
        for (k = 0; k < 3; k++) {
            mi[r][k] = nb_dmem_s16(0xe40u + (unsigned)r * 8u + (unsigned)k * 2u);
            mf[r][k] = nb_dmem_s16(0xe60u + (unsigned)r * 8u + (unsigned)k * 2u);
        }
    for (k = 0; k < 3; k++)
        bias[k] = nb_dmem_s16(0xf0u + (unsigned)k * 2u);
    for (k = 0; k < 2; k++)
        sc[k]   = nb_dmem_s16(0xecu + (unsigned)k * 2u);

    for (i = 0; i < 4; i++) {
        unsigned int idx = nb_read_u32(cmd + 16u + (unsigned)i * 4u) & 0xffu;
        unsigned int src = 0x170u + idx;
        int n[3], ti[3], tf[3];
        int32_t sq_lo, sq_mid;
        int64_t acc;
        int32_t rsq, r_i, r_f;
        int st[2];

        /* the ldv at overlay 0x050 loads EIGHT bytes at src+0 into
         * elements 0-3, and the half-selectors e4/e5/e6 on the
         * multiply chain therefore address elements 0/1/2 -- the
         * normal is at src+0/+2/+4, not at the element 4-6 offsets
         * the earlier end-state fit suggested. */
        for (k = 0; k < 3; k++)
            n[k] = nb_dmem_s16(src + (unsigned)k * 2u);

        /* normal x matrix + row-4 translation (vmudn frac + vmadh int
         * accumulated; mid = int lane, low = frac lane) */
        /* vmudn/vmadn take the FRACTION row UNSIGNED against the
         * signed normal (cxd4 multiply.c: _mm_mulhi_epu16 on vs with
         * the vt sign correction), while vmadh pairs the signed
         * integer row one slice up; the row-4 translation rides the
         * same pair against v30 element 1 (= 1). */
        for (k = 0; k < 3; k++) {
            acc  = nb_p((int)((unsigned short)mf[0][k]), n[0])
                 + (nb_p(mi[0][k], n[0]) << 16);
            acc += nb_p((int)((unsigned short)mf[1][k]), n[1])
                 + (nb_p(mi[1][k], n[1]) << 16);
            acc += nb_p((int)((unsigned short)mf[2][k]), n[2])
                 + (nb_p(mi[2][k], n[2]) << 16);
            acc += nb_p((int)((unsigned short)mf[3][k]), 1)
                 + (nb_p(mi[3][k], 1) << 16);
            ti[k] = nb_acc_mid(acc);
            tf[k] = (int)(acc & 0xffff);
        }
        /* vsubc bias -- the ldv at overlay 0x02c loads FOUR halfwords
         * into v12 and the vsubc in the jal's delay slot (0x078) is a
         * plain per-lane subtract, so lane 2 is biased as well; the
         * squared length below is taken AFTER this, which is why the
         * two-lane reading pushed the magnitude (and hence the
         * reciprocal square root's scale) badly off. */
        for (k = 0; k < 3; k++)
            ti[k] = (int)(int16_t)((unsigned short)((unsigned)ti[k] - (unsigned)bias[k]));

        /* squared length: full 32x32 per lane, folded */
        {
            /* per-lane square: the vmudl/vmadm/vmadn/vmadh quartet at
             * overlay 0x0ac-0x0b8 forms (x * x) >> 16 for the 32-bit
             * lane value x = ti:tf, keeping the low half in v17 and
             * the high half in v18.  The vaddc/vadd pairs then fold
             * lanes 1 and 2 into lane 0 (element 3 = quarter, element
             * 6 = half) as one 32-bit carry-propagating sum, lane 3
             * having been cleared by the mtc2 pair. */
            unsigned int lo[4]; int hi[4];
            for (k = 0; k < 4; k++) {
                if (k < 3) {
                    int32_t x = (int32_t)(((uint32_t)ti[k] << 16)
                                          | ((uint32_t)tf[k] & 0xffffu));
                    /* VMUDL shifts the product down 16, so the
                     * accumulator holds (x*x) >> 16; the vmadh at the
                     * end of the quartet writes its destination
                     * through the SIGNED MIDDLE CLAMP of acc[47:16],
                     * which for these squared lengths pegs at 0x7fff
                     * -- the reciprocal square root is genuinely
                     * taken of the clamped value. */
                    int64_t p = ((int64_t)x * x) >> 16;
                    int64_t m = p >> 16;
                    lo[k] = (unsigned int)(p & 0xffffu);
                    hi[k] = (int)(m > 32767 ? 32767 :
                                  (m < -32768 ? -32768 : m));
                } else {
                    lo[k] = 0u; hi[k] = 0;   /* mtc2 zero, v17/v18[6] */
                }
            }
            /* vaddc (unsigned, carry out) + vadd (SIGNED SATURATING)
             * pairs: element 3 folds lane 1 into lane 0, element 6
             * folds lane 2 into lane 0.  The saturation is load-
             * bearing -- these squared lengths routinely peg the high
             * half at 0x7fff and the reciprocal square root is taken
             * of the saturated value. */
            {
                unsigned int c;
                int t2;
                c = lo[0] + lo[1];
                lo[0] = c & 0xffffu;
                t2 = hi[0] + hi[1] + (int)(c >> 16);
                hi[0] = t2 > 32767 ? 32767 : (t2 < -32768 ? -32768 : t2);
                c = lo[2] + lo[3];
                lo[2] = c & 0xffffu;
                t2 = hi[2] + hi[3];
                hi[2] = t2 > 32767 ? 32767 : (t2 < -32768 ? -32768 : t2);
                c = lo[0] + lo[2];
                lo[0] = c & 0xffffu;
                t2 = hi[0] + hi[2] + (int)(c >> 16);
                hi[0] = t2 > 32767 ? 32767 : (t2 < -32768 ? -32768 : t2);
            }
            sq_lo  = (int32_t)lo[0];
            sq_mid = (int32_t)hi[0];
        }
        rsq = rsp_rsq32((int32_t)(((uint32_t)(sq_mid & 0xffff) << 16)
                                  | (uint32_t)(sq_lo & 0xffff)));
        /* x the 0xab constant: vmudl SHIFTS ITS PRODUCT DOWN 16 and
         * vmadm lands one slice up, so the pair forms
         * (rsq * 0xab) >> 16 across acc_md:acc_lo. */
        {
            int64_t r32 = (((int64_t)rsq * 0xab) >> 16);
            r_i = (int32_t)((r32 >> 16) & 0xffff);
            r_f = (int32_t)(r32 & 0xffff);
        }
        /* normalized = transformed x scaled rsq (32x32) */
        for (k = 0; k < 2; k++) {
            /* normalized = (transformed * refined) >> 16, the same
             * vmudl/vmadm/vmadn/vmadh quartet shape */
            int32_t x = (int32_t)(((uint32_t)ti[k] << 16)
                                  | ((uint32_t)tf[k] & 0xffffu));
            int32_t r32 = (int32_t)(((uint32_t)r_i << 16)
                                    | ((uint32_t)r_f & 0xffffu));
            int64_t n32 = ((int64_t)x * r32) >> 16;
            int nf = (int)(n32 & 0xffff);
            int ni = (int)((n32 >> 16) & 0xffff);
            /* scale: vmudm (>> 16) + vmadh one slice up, destination
             * taken through the signed middle clamp */
            /* vmudm lands its 32-bit product across acc_md:acc_lo and
             * its DESTINATION is acc_md, so the fraction contributes
             * (sc * nf) >> 16; vmadh then adds sc * ni into that same
             * middle slice and its destination is the stored S/T. */
            int64_t a2 = (((int64_t)sc[k] * (unsigned short)nf) >> 16)
                       + (int64_t)sc[k] * (short)ni;
            st[k] = (int)(a2 > 32767 ? 32767 : (a2 < -32768 ? -32768 : a2));
        }
        out_st[i] = ((unsigned int)(st[0] & 0xffff) << 16)
                  | ((unsigned int)st[1] & 0xffff);
    }
}



/* Naboo clip weight: t = (P.in) / (P.in - P.out) through the exact
 * fixed-point path (overlay text 0xe70-0xf24 + the resident refined
 * reciprocal at 0xa08): the coarse doubled reciprocal of D (negated
 * when D >= 0, saturated to a u16 fraction when its integer part is
 * nonzero), one Newton refinement of D x coarse against 2.0, and
 * the recombine N x refined x coarse over the v30 x 4 seed.
 * Returns the u16 weight applied to the INSIDE vertex (1 - t); the
 * outside weight is its two's complement. */
static unsigned int nb_clip_wt(const int32_t in4[4], const int32_t out4[4],
                               const int16_t P[4])
{
    int64_t n48 = 0, d48 = 0;
    int32_t n32, d32, coarse, norm, refined, wr;
    int64_t acc, err, t32;
    unsigned int cu16, w;
    int k;

    for (k = 0; k < 4; k++) {
        n48 += (int64_t)P[k] * in4[k];
        d48 += (int64_t)P[k] * out4[k];
    }
    d48 = n48 - d48;
    n32 = (int32_t)n48;
    d32 = (int32_t)d48;

    coarse = (int32_t)((uint32_t)rsp_rcp32_dp(d32) << 1);
    if (d32 < 0)
        coarse = -coarse;               /* magnitude */
    cu16 = (((unsigned)coarse >> 16) & 0xffffu) == 0u
         ? ((unsigned)coarse & 0xffffu) : 0xffffu;

    norm = (int32_t)(((int64_t)d32 * coarse) >> 16);
    refined = (int32_t)((uint32_t)rsp_rcp32_dp(norm) << 1);
    wr = (int32_t)(((int64_t)norm * refined) >> 16);
    err = ((int64_t)2 << 16) - wr;
    refined = (int32_t)(((int64_t)refined * err) >> 16);

    /* w_out = 1 + ((N x refined >> 16) x coarse) >> 16, fit exact
     * across the probe corpus */
    t32 = ((int64_t)n32 * refined) >> 16;
    acc = 1 + ((t32 * (int64_t)cu16) >> 16);
    w = (unsigned int)(acc & 0xffffu);
    if (w == 0u)
        w = 1u;
    return w;
}

/* Overlay 0x03: polygon clip (Sutherland-Hodgman against five
 * frustum planes), the Rogue Squadron algorithm on Naboo
 * addressing.  Ping-pong vertex-handle lists at DMEM 0x560/0x574
 * (zero-terminated), plane masks at DMEM 0x58 + 2k, plane
 * coefficients at DMEM (plane_sel << 2), interpolated vertices
 * staged at 0x380 with the record stride, each reprojected and
 * re-outcoded through the resident path; the surviving polygon
 * fans from its first vertex through the emitter's post-gate
 * entry.  Returns 0, or -1 to fall back. */
static int nb_clip(RdpFifo *fifo, unsigned int ra, unsigned int rb,
                   unsigned int rc)
{
    unsigned int lists[2][12];
    unsigned int n_in, n_out, cur;
    unsigned int stage = 0x380u;
    unsigned int r25;
    unsigned int i;

    nb.dmem[0x58au ^ 3u] = 0u;          /* batching countdown reset */
    nb_dmem_w16(0x588u, ra);            /* fan-centre handle */

    lists[0][0] = ra; lists[0][1] = rb; lists[0][2] = rc;
    n_in = 3u; cur = 0u;

    /* six planes: the microcode's bgtz tests before the delay-slot
     * decrement, so r25 walks a,8,6,4,2,0 (text 0xdbc) */
    for (r25 = 0xau; (int)r25 >= 0; r25 -= 2u) {
        unsigned int mask = (unsigned int)nb_dmem_s16(0x58u + r25) & 0xffffu;
        int16_t P[4];
        unsigned int k;
        for (k = 0; k < 4u; k++)
            P[k] = (int16_t)nb_dmem_s16((r25 << 2) + k * 2u);
        n_out = 0u;
        for (i = 0; i < n_in; i++) {
            unsigned int va = lists[cur][i];
            unsigned int vb = lists[cur][(i + 1u) % n_in];
            unsigned int sa = (unsigned int)nb_dmem_s16(va + 0x24u) & mask;
            unsigned int sb2 = (unsigned int)nb_dmem_s16(vb + 0x24u) & mask;
            if (sa == 0u)
                lists[cur ^ 1u][n_out++] = va;
            if (sa != sb2) {
                /* crossing: build the interpolated vertex */
                unsigned int vin  = sa ? vb : va;
                unsigned int vout = sa ? va : vb;
                int32_t in4[4], out4[4], wc, wt;
                for (k = 0; k < 4u; k++) {
                    in4[k]  = (int32_t)((nb_dmem_s16(vin + k * 2u) << 16)
                              | ((unsigned)nb_dmem_s16(vin + 8u + k * 2u) & 0xffffu));
                    out4[k] = (int32_t)((nb_dmem_s16(vout + k * 2u) << 16)
                              | ((unsigned)nb_dmem_s16(vout + 8u + k * 2u) & 0xffffu));
                }
                wt = (int32_t)nb_clip_wt(in4, out4, P);
                wc = (int32_t)((0x10000u - (unsigned)wt) & 0xffffu);
                {
                    static int t = -1;
                    if (t < 0) t = getenv("NB_CLIP_TRACE") != NULL;
                    if (t) fprintf(stderr,
                        "[CWT] in=%03x out=%03x wc=%d:%d wt=%d:%d\n",
                        vin, vout,
                        (int)(int16_t)(wc >> 16), (int)(int16_t)wc,
                        (int)(int16_t)(wt >> 16), (int)(int16_t)wt);
                }
                for (k = 0; k < 4u; k++) {
                    /* Q16 lerp: in x w + out x ~w (udl/adm/adl/adm) */
                    int64_t pa = ((int64_t)(in4[k] & 0xffff) * wc) >> 16;
                    int64_t p2;
                    pa += (int64_t)(in4[k] >> 16) * wc;
                    pa += ((int64_t)(out4[k] & 0xffff) * wt) >> 16;
                    pa += (int64_t)(out4[k] >> 16) * wt;
                    p2 = pa;            /* 32-bit position */
                    nb_dmem_w16(stage + k * 2u, ((uint64_t)p2 >> 16) & 0xffffu);
                    nb_dmem_w16(stage + 8u + k * 2u, (uint64_t)p2 & 0xffffu);
                }
                /* colors (bytes +0x10..13, u8 << 7 lanes) and the
                 * S/T halfwords (+0x14/+0x16) lerp with the same
                 * weight pair (text 0xf40-0xf60) */
                for (k = 0; k < 4u; k++) {
                    int a8 = (int)nb.dmem[((vin + 0x10u + k) & 0xfffu) ^ 3u] << 7;
                    int b8 = (int)nb.dmem[((vout + 0x10u + k) & 0xfffu) ^ 3u] << 7;
                    /* vmudm signed x u16-weight pair */
                    int r = (int)((((int64_t)a8 * wc) + ((int64_t)b8 * wt)) >> 16);
                    if (r > 32767) r = 32767;
                    if (r < -32768) r = -32768;
                    nb.dmem[((stage + 0x10u + k) & 0xfffu) ^ 3u] =
                        (unsigned char)(((unsigned)(int16_t)r >> 7) & 0xffu);
                }
                for (k = 0; k < 2u; k++) {
                    int a16 = nb_dmem_s16(vin + 0x14u + k * 2u);
                    int b16 = nb_dmem_s16(vout + 0x14u + k * 2u);
                    int r = (int)((((int64_t)a16 * wc) + ((int64_t)b16 * wt)) >> 16);
                    if (r > 32767) r = 32767;
                    if (r < -32768) r = -32768;
                    nb_dmem_w16(stage + 0x14u + k * 2u,
                                (unsigned int)r & 0xffffu);
                }
                nb_dmem_w16(stage + 0x24u, nb_project(stage));
                lists[cur ^ 1u][n_out++] = stage;
                stage += 0x28u;
                if (stage > 0x560u - 0x28u)
                    return -1;          /* staging overrun */
            }
            if (n_out >= 10u)
                return -1;
        }
        {
            static int t = -1;
            if (t < 0) t = getenv("NB_CLIP_TRACE") != NULL;
            if (t) {
                unsigned int q;
                fprintf(stderr, "[CLP] plane r25=%x mask=%04x -> n=%u:",
                        r25, mask, n_out);
                for (q = 0; q < n_out; q++)
                    fprintf(stderr, " %03x(%04x)",
                            lists[cur ^ 1u][q],
                            (unsigned)nb_dmem_s16(lists[cur ^ 1u][q] + 0x24u) & 0xffffu);
                fprintf(stderr, "\n");
            }
        }
        cur ^= 1u;
        n_in = n_out;
        if (n_in == 0u)
            return 0;                   /* clipped away entirely */
    }

    /* persist the surviving polygon for the fan (the microcode's
     * final ping-pong list is live DMEM state; write it back with
     * the zero terminator) */
    {
        unsigned int base = (cur == 0u) ? 0x560u : 0x574u;
        for (i = 0; i < n_in; i++) {
            nb.clip_poly[i] = lists[cur][i];
            nb_dmem_w16(base + i * 2u, lists[cur][i]);
        }
        nb_dmem_w16(base + n_in * 2u, 0u);
    }
    nb.clip_n = n_in;
    nb.clip_idx = 1u;
    nb.clip_active = 1u;
    return 0;
}

/* Fan emission for a built clip polygon: (poly[0], poly[i],
 * poly[i+1]) through the post-gate path -- the winding cull applies,
 * the outcode gates do not, and the splice check runs per triangle
 * exactly as it does for direct triangles (the jalr at text 0x2a8).
 * Returns 0 done, 1 suspended on a splice, -1 fallback. */
static int nb_clip_fan(RdpFifo *fifo)
{
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_CLIP_TRACE") != NULL;
        if (t) fprintf(stderr, "[CFAN] task=%u n=%u idx=%u active=%u\n",
                       nb_task_ordinal, nb.clip_n, nb.clip_idx,
                       nb.clip_active);
    }
    while (nb.clip_idx + 1u < nb.clip_n) {
        unsigned int va = nb.clip_poly[0];
        unsigned int vb = nb.clip_poly[nb.clip_idx];
        unsigned int vc = nb.clip_poly[nb.clip_idx + 1u];
        /* winding cull exactly as the emitter does it (text 0x230-
         * 0x244): cross = (C-A)x(B-A)y - (B-A)x(C-A)y from the
         * screen coordinates, culled when negative and geometry
         * mode bit 13 is set (geom << 18 puts bit 13 in the sign,
         * bltz exits through the jr r30 at 0x9e8).  Degenerate
         * zero-cross triangles fall out of the emitter naturally. */
        {
            int32_t ax = (int16_t)nb_dmem_s16(va + 0x18u);
            int32_t ay = (int16_t)nb_dmem_s16(va + 0x1au);
            int32_t bx = (int16_t)nb_dmem_s16(vb + 0x18u);
            int32_t by = (int16_t)nb_dmem_s16(vb + 0x1au);
            int32_t cx = (int16_t)nb_dmem_s16(vc + 0x18u);
            int32_t cy = (int16_t)nb_dmem_s16(vc + 0x1au);
            int64_t cross = (int64_t)(cx - ax) * (by - ay)
                          - (int64_t)(bx - ax) * (cy - ay);
            if (cross < 0 && (nb.geom & 0x2000u)) {
                nb.clip_idx++;
                continue;
            }
        }
        /* the staged-list check sits at text 0x2a8, which is PAST the
         * winding-cull exit at 0x244 (bltz -> 0x9e8, jr r30): a culled
         * triangle leaves the emitter before ever reaching it and so
         * does NOT consume a pending splice.  Checking ahead of the
         * cull made the walker swallow arms the microcode leaves
         * standing, which is what pulled a later fan into a list the
         * microcode never entered. */
        {
            unsigned int splice = nb_dmem_r32(0x58cu) & 0x00fffff8u;
            if (splice != 0u) {
                if (nb.sp >= NB_DL_STACK)
                    return -1;
                nb_dmem_w32(0x58cu, 0u);
                nb.stack[nb.sp * 2u] = nb.chunk;
                nb.stack[nb.sp * 2u + 1u] = nb.off;
                nb.sp++;
                nb_dl_enter(splice);
                return 1;
            }
        }
        if (nb_emit_tri(fifo, va, vb, vc) < 0)
            return -1;
        nb.clip_idx++;
    }
    nb.clip_active = 0u;
    return 0;
}

/* Gate a triangle exactly as the emitter entry does BEFORE its jalr
 * (text 0x1a8-0x2a4): trivial reject on the ANDed outcodes, clip
 * trigger on the ORed outcodes, winding cull, degenerate skip.
 * Returns 0 = draw, 1 = silently skipped, -1 = clip overlay. */
static int nb_tri_gate(unsigned int ra, unsigned int rb, unsigned int rc)
{
    unsigned int oa = nb_dmem_s16(ra + 0x24u) & 0xffffu;
    unsigned int ob = nb_dmem_s16(rb + 0x24u) & 0xffffu;
    unsigned int oc = nb_dmem_s16(rc + 0x24u) & 0xffffu;
    int32_t ax, ay, bx, by, cx, cy;
    int32_t d1x, d1y, d2x, d2y;
    int64_t cross;

    if (oa & ob & oc & 0x7070u)
        return 1;                       /* trivial reject */
    if ((oa | ob | oc) & 0x4343u)
        return -1;                      /* clip overlay: fall back */

    ax = (int16_t)nb_dmem_s16(ra + 0x18u); ay = (int16_t)nb_dmem_s16(ra + 0x1au);
    bx = (int16_t)nb_dmem_s16(rb + 0x18u); by = (int16_t)nb_dmem_s16(rb + 0x1au);
    cx = (int16_t)nb_dmem_s16(rc + 0x18u); cy = (int16_t)nb_dmem_s16(rc + 0x1au);
    d1x = bx - ax; d1y = by - ay;
    d2x = cx - ax; d2y = cy - ay;
    if (d1x > 32767)  d1x = 32767;
    if (d1x < -32768) d1x = -32768;
    if (d1y > 32767)  d1y = 32767;
    if (d1y < -32768) d1y = -32768;
    if (d2x > 32767)  d2x = 32767;
    if (d2x < -32768) d2x = -32768;
    if (d2y > 32767)  d2y = 32767;
    if (d2y < -32768) d2y = -32768;
    cross = (int64_t)d2x * d1y - (int64_t)d1x * d2y;
    if (cross == 0)
        return 1;                       /* degenerate */
    if (cross < 0 && (nb.geom & 0x2000u))
        return 1;                       /* winding cull */
    return 0;
}

static int nb_emit_tri(RdpFifo *fifo, unsigned int ra, unsigned int rb,
                       unsigned int rc)
{
    RspTriVtx va, vb, vc;
    int32_t ew[64];
    int nw;
    int tilebyte;

    nb_vtx(ra, &va); nb_vtx(rb, &vb); nb_vtx(rc, &vc);
    /* text 0x3fc-0x400: `andi v0, r21, 0x1000` / `bne v0, zero, 0x474`
     * jumps clear over the whole 1/w block (0x404-0x470), so with the
     * geometry word's bit 12 set the writer stores the texel shorts as
     * loaded, with no per-vertex perspective normalizer. */
    rsp_set_persp_skip((nb.geom & 0x1000u) ? 1 : 0);
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_EMIT_TRACE") != NULL;
        if (t) fprintf(stderr, "[EV] a=(%d,%d) b=(%d,%d) c=(%d,%d)\n",
                       va.x, va.y, vb.x, vb.y, vc.x, vc.y);
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
                       0x1000, 0x20, (int32_t)0xfff8, 0x1cc);
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
    unsigned int cd;
    unsigned int len = 16u;

    /* batching countdown (text 0xb8c); on a post-splice reprocess
     * (tri_phase nonzero) the countdown was already consumed.
     * Phases: 0 fresh, 2 countdown consumed / first triangle pending,
     * 1 first triangle emitted / second pending. */
    if (nb.tri_phase == 0u) {
        cd = nb.dmem[0x58au ^ 3u];
        if (cd == 0u) {
            nb.dmem[0x58au ^ 3u] = (unsigned char)w0;
        } else {
            nb.dmem[0x58au ^ 3u] = (unsigned char)(cd - 1u);
            nb_dl_step((w0 & 0x200u) ? 32u : 16u);
            return 0;                   /* skipped by the countdown */
        }
        nb.tri_phase = 2u;
    }


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

    if (w0 & 0x800u) {
        /* overlay 0x2a: environment-mapped S/T computed from the
         * vertex normals; the four index bytes ride the low byte of
         * each would-be inline S/T word (+19/+23/+27/+31), and the
         * results replace the inline pokes (rejoin text 0xb10 for
         * A/B/C, the quad tail 0xb60 lane e12 for D) */
        unsigned int est[4];
        len = 32u;
        nb_ovl2a(nb.dl, est);
        nb_dmem_w32(va + 0x14u, est[0]);
        nb_dmem_w32(vb + 0x14u, est[1]);
        nb_dmem_w32(vc + 0x14u, est[2]);
        if (quad)
            nb_dmem_w32(vd + 0x14u, est[3]);
    } else if (w0 & 0x200u) {
        len = 32u;
        /* inline S/T: slv elements e0/e4/e8/e12 map words +16/+20/
         * +24/+28 to A/B/C/D */
        nb_dmem_w32(va + 0x14u, nb_read_u32(nb.dl + 16u));
        nb_dmem_w32(vb + 0x14u, nb_read_u32(nb.dl + 20u));
        nb_dmem_w32(vc + 0x14u, nb_read_u32(nb.dl + 24u));
        if (quad)
            nb_dmem_w32(vd + 0x14u, nb_read_u32(nb.dl + 28u));
    }
    /* emit: (A,B,C), then (A,C,D) on the quad op.  The emitter's
     * jalr through DMEM 0x152 (the default 0x17b4 splice-check) runs
     * PER TRIANGLE: a staged list in 0x58c is walked before that
     * triangle, and the microcode resumes mid-quad (state at
     * 0x590-0x594).  The walker models the resume with tri_phase:
     * on reprocess after a splice, already-emitted triangles are not
     * re-emitted (the pokes above are idempotent). */
    if (nb.tri_phase == 2u) {
        int g = nb_tri_gate(va, vb, vc);
        if (g < 0) {
            /* outcode 0x4343: the clip overlay */
            if (!nb.clip_active) {
                if (nb_clip(fifo, va, vb, vc) < 0)
                    return -1;
            }
            if (nb.clip_active) {
                int cr = nb_clip_fan(fifo);
                if (cr < 0)
                    return -1;
                if (cr > 0)
                    return 1;           /* splice: reprocess */
            }
            g = 1;                      /* handled; skip direct emit */
        }
        if (g == 0) {
            unsigned int splice = nb_dmem_r32(0x58cu) & 0x00fffff8u;
            if (splice != 0u) {
                if (nb.sp >= NB_DL_STACK)
                    return -1;
                nb_dmem_w32(0x58cu, 0u);
                nb.stack[nb.sp * 2u] = nb.chunk;
                nb.stack[nb.sp * 2u + 1u] = nb.off;
                nb.sp++;
                nb_dl_enter(splice);
                return 1;               /* re-enter after the list */
            }
            if (nb_emit_tri(fifo, va, vb, vc) < 0)
                return -1;
        }
        nb.tri_phase = 1u;
    }
    if (quad && nb.tri_phase == 1u) {
        int g = nb_tri_gate(va, vc, vd);
        if (g < 0) {
            /* outcode 0x4343: the clip overlay */
            if (!nb.clip_active) {
                if (nb_clip(fifo, va, vc, vd) < 0)
                    return -1;
            }
            if (nb.clip_active) {
                int cr = nb_clip_fan(fifo);
                if (cr < 0)
                    return -1;
                if (cr > 0)
                    return 1;           /* splice: reprocess */
            }
            g = 1;                      /* handled; skip direct emit */
        }
        if (g == 0) {
            unsigned int splice = nb_dmem_r32(0x58cu) & 0x00fffff8u;
            if (splice != 0u) {
                if (nb.sp >= NB_DL_STACK)
                    return -1;
                nb_dmem_w32(0x58cu, 0u);
                nb.stack[nb.sp * 2u] = nb.chunk;
                nb.stack[nb.sp * 2u + 1u] = nb.off;
                nb.sp++;
                nb_dl_enter(splice);
                return 1;
            }
            if (nb_emit_tri(fifo, va, vc, vd) < 0)
                return -1;
        }
    }
    nb.tri_phase = 0u;
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
        case 0x05:                              /* load+run overlay */
            if ((w1 & 0x3fu) == 0x0fu) {
                /* descriptor/setup for the vertex-morphing overlay
                 * 0x09.  The dispatcher has already consumed the
                 * 8-byte command (the overlay reads its payload from
                 * r17 - 8), so the extra length it reports is on top
                 * of that. */
                nb_dl_step(8u + nb_ovl0f(w1));
                continue;
            }
            nb.active = 0;
            return NABOO_R_FALLBACK;
        case 0x07:                              /* DL jump (entry
             * 1:76c): enter the chunk-formatted list at w1 with no
             * stack push -- the current position is abandoned */
            if (!nb_emit_on) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            nb_dl_enter(w1 & 0x00fffff8u);
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
             * On the pop path a zero w1 selects the plain resume and
             * a nonzero one the splice-aware resume (text 0x790); the
             * walker's resume covers both.  At top level the server
             * follows the
             * live tail first: if the word at DMEM 0x58c is nonzero,
             * the CPU has appended another list segment -- continue
             * there and clear the link (text 0x79c-0x7cc). */
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
                /* the zero-w1 pop resumes through the boot vector
                 * (text 0x790 -> 0x010), where the server follows a
                 * pending live tail and otherwise polls the CPU's
                 * status signal, yielding when it is set.  The signal
                 * is live state a static capture cannot carry, so the
                 * walker continues unconditionally; against a
                 * captured task this can run a few commands past the
                 * microcode's yield point into the next task's work,
                 * which the live baselines show to be a benign
                 * boundary shift. */
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
            {
                int tr = nb_tri(fifo, w0, w1, op == 0x0bu);
                if (tr < 0) {
                    nb.active = 0;
                    return NABOO_R_FALLBACK;
                }
            }
            continue;
        case 0x0c:                              /* end-of-chunk marker:
             * the dispatch entry (1:068) is the chunk-refill routine
             * itself -- it reloads the ring window from the chain
             * pointer and restarts the fetch at the new chunk's first
             * command.  Bytes after this op in the current chunk are
             * dead padding the microcode never walks.  Gated with the
             * emitter on unverified builds. */
            if (!nb_emit_on) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            {
                unsigned int next = nb_read_u32(nb.chunk) & 0x00fffff8u;
                if (next == 0u) {
                    nb.active = 0;
                    return NABOO_R_FALLBACK;
                }
                nb.chunk = next;
                nb.off = 8u;
                nb.dl = nb.chunk + nb.off;
            }
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
            {
                static int t = -1;
                if (t < 0) t = getenv("NB_MW_TRACE") != NULL;
                if (t) fprintf(stderr, "[B9] @%06x bit=%u 11c=%08x\n",
                               nb.dl, (w0 >> 23) & 1u, nb_dmem_r32(0x11cu));
            }
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

/* Unit-test hook for the clip overlay: seed DMEM via
 * naboo_seed_dmem first, then call with the triangle handles.
 * Emits into the given fifo. */
int naboo_clip_unit(void *fifo, unsigned int ra, unsigned int rb,
                    unsigned int rc)
{
    int r = nb_clip((RdpFifo *)fifo, ra, rb, rc);
    if (r == 0 && nb.clip_active)
        r = nb_clip_fan((RdpFifo *)fifo);
    return r;
}
