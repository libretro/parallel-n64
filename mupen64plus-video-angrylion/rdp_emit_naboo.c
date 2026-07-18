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
    unsigned int dl;            /* command cursor (RDRAM) */
    unsigned int active;
    unsigned int sp;            /* DL call depth (slot 0x06 / 0x0f) */
    unsigned int geom;          /* geometry-mode word (s5): slot 0x0e
                                   ORs w1 in, slot 0x0d ANDs w1
                                   (text 0xa68/0xa70) */
    unsigned int stack[NB_DL_STACK];
    /* modeled microcode DMEM state: MoveWord (slot 0x13) writes land
     * here; render commands consume them (state words at 0x120-0x13c,
     * viewport/live-tail words, ...) */
    unsigned char dmem[0x1000];
} nb;

void naboo_task_reset(unsigned int dl)
{
    nb.dl = dl;
    nb.active = 1;
    nb.sp = 0;
}

/* Seed the DMEM shadow from the live DMEM.  The streaming server's
 * working state (data segment, matrices, live-tail words, stream
 * cursor) persists in physical DMEM across task slices -- the data
 * segment is DMA'd once at server start and never again -- so the
 * live DMEM at (re)launch IS the authoritative state. */
void naboo_seed_dmem(const unsigned char *dmem)
{
    unsigned int i;
    for (i = 0; i < 0x1000u; i++)
        nb.dmem[i ^ 3u] = dmem[i ^ 3u];
}

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
    nb.dmem[a ^ 3u]        = (unsigned char)(v >> 8);
    nb.dmem[(a + 1u) ^ 3u] = (unsigned char)v;
}

static void nb_dmem_w8(unsigned int off, unsigned int v)
{
    nb.dmem[(off & 0xfffu) ^ 3u] = (unsigned char)v;
}

static void nb_dmem_w32(unsigned int off, unsigned int v)
{
    unsigned int a = off & 0xffcu;
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

    /* translation from the state struct +0x18.  The struct base is
     * r18 = DMEM 0x558: slot 0x12 stores at +0x38 = the live-tail
     * word 0x590, and the EndDL depth byte at +0x32 = 0x58a, next to
     * the intensity byte 0x58b. */
    for (j = 0; j < 4; j++)
        tr[j] = nb_dmem_s16(0x570u + (unsigned int)j * 2u);
    {
        static int t = -1;
        if (t < 0) t = getenv("NB_TRACE") != NULL;
        if (t) fprintf(stderr, "[XFRM] n=%u tr=%04x %04x %04x %04x in0=%04x %04x %04x\n",
                       count, tr[0]&0xffff, tr[1]&0xffff, tr[2]&0xffff, tr[3]&0xffff,
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
    case 4:                                     /* color unpack: 565 ->
         * RGBA words through the parallel arrays at 0x480 (text
         * 0xbec-0xc30); modeled when the emitters consume it */
        break;
    case 6:                                     /* intensity byte */
        nb_dmem_w8(0x58bu, w1 >> 24);
        break;
    default:                                    /* load-only */
        break;
    }
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
        unsigned int cls = w0 >> 30;
        unsigned int op  = (w0 >> 24) & 0xffu;

        if (cls == 3u) {
            /* RDP passthrough (text 0x98).  G_TEXRECT carries two
             * extra inline words. */
            int32_t words[2];
            words[0] = (int32_t)w0;
            words[1] = (int32_t)w1;
            rdp_fifo_append(fifo, words, 2);
            nb.dl += 8;
            if (op == 0xe4u) {
                words[0] = (int32_t)nb_read_u32(nb.dl);
                words[1] = (int32_t)nb_read_u32(nb.dl + 4);
                rdp_fifo_append(fifo, words, 2);
                nb.dl += 8;
            }
            continue;
        }

        /* alias GBI-numbered opcodes onto the custom slot set */
        if (op >= 0xa9u && op <= 0xc8u)
            op -= 0xa9u;

        switch (op) {
        case 0x00:                              /* NOOP */
            nb.dl += 8;
            continue;
        case 0x06:                              /* DisplayList: call w1,
             * push the return cursor (text 0x754: stack at DMEM 0xfe0,
             * depth byte at struct+0x32) */
            if (nb.sp >= NB_DL_STACK) {
                nb.active = 0;
                return NABOO_R_FALLBACK;
            }
            nb.stack[nb.sp++] = nb.dl + 8;
            nb.dl = w1 & 0x00fffff8u;
            continue;
        case 0x0f:                              /* EndDL (GBI 0xb8):
             * pop a pushed cursor, or finish at top level (text 0x778).
             * At top level the server follows the live tail first: if
             * the word at DMEM 0x58c is nonzero, the CPU has appended
             * another list segment -- continue there and clear the
             * link (text 0x79c-0x7cc). */
            if (nb.sp) {
                nb.dl = nb.stack[--nb.sp];
                continue;
            }
            {
                unsigned int tail = nb_dmem_r32(0x58cu) & 0x00fffff8u;
                if (tail != 0u) {
                    nb_dmem_w32(0x58cu, 0u);
                    nb.dl = tail;
                    continue;
                }
            }
            nb.active = 0;
            return NABOO_R_DONE;
        case 0x02:                              /* open segmented
             * stream (text 0xd1c) */
            nb_op02(w0, w1);
            nb.dl += 8;
            continue;
        case 0x01:                              /* segmented load +
             * subtype processor (state modeling; RDP output comes from
             * the triangle path, which still falls back) */
            nb_op01(w0, w1);
            nb.dl += 8;
            continue;
        case 0x0d:                              /* GeometryMode &= w1
             * (text 0xa70) */
            nb.geom &= w1;
            nb.dl += 8;
            continue;
        case 0x0e:                              /* GeometryMode |= w1
             * (text 0xa68) */
            nb.geom |= w1;
            nb.dl += 8;
            continue;
        case 0x13:                              /* MoveWord (GBI 0xbc):
             * DMEM[w0 & 0xffc] = w1 (text 0xa78) */
            nb_dmem_w32(w0, w1);
            nb.dl += 8;
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
                    if (op == 0x0bu) {
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
                    nb.dl += step;
                    continue;
                }
            }
            nb.active = 0;
            return NABOO_R_FALLBACK;
        }
    }
}
