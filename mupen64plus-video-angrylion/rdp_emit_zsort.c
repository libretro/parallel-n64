/* SGI ZSortp screen-space object walker. Grammar reference:
 * https://github.com/gonetz/GLideN64/blob/master/src/uCodes/ZSort.cpp
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdp_emit_zsort.h"
#include "rdp_emit_rsp.h"
extern void rdp_fifo_append(RdpFifo *, const int32_t *, int);
extern void rdp_fifo_fullsync_note(void);
#ifdef MSB_FIRST
#define ZBYTE 0u
#else
#define ZBYTE 3u
#endif

#define ZS_STATE_ONLY 0
#define ZS_SHADE_TRI  1
#define ZS_TEX_TRI    2
#define ZS_SHADE_QUAD 3
#define ZS_TEX_QUAD   4

typedef struct ZSortState
{
    unsigned char *rdram, *dmem;
    unsigned rdram_size;
    unsigned segments[16];
    unsigned othermode_h, othermode_l;
    unsigned command_budget;
    int trace;
    unsigned written_ranges[64][2];
    unsigned num_written_ranges;
    int32_t matrix[3][4][4];
    GSPState *gsp;
    RdpFifo *fifo;
} ZSortState;
/* ---- Guest memory and task validation ---- */

static unsigned zs_get32(const unsigned char *p, unsigned a)
{
    return (unsigned)p[a ^ ZBYTE] << 24 | (unsigned)p[(a + 1) ^ ZBYTE] << 16 |
           (unsigned)p[(a + 2) ^ ZBYTE] << 8 | p[(a + 3) ^ ZBYTE];
}

static int zs_get16(const unsigned char *p, unsigned a)
{
    return (int16_t)((unsigned)p[a ^ ZBYTE] << 8 | p[(a + 1) ^ ZBYTE]);
}

static void zs_put16(unsigned char *p, unsigned a, int v)
{
    p[a ^ ZBYTE] = (unsigned char)((unsigned)v >> 8);
    p[(a + 1) ^ ZBYTE] = (unsigned char)v;
}

static void zs_put32(unsigned char *p, unsigned a, unsigned v)
{
    zs_put16(p, a, v >> 16);
    zs_put16(p, a + 2, v);
}

static int zs_range(unsigned a, unsigned n, unsigned size)
{
    return a <= size && n <= size - a;
}

static int zs_ram_range(const ZSortState *z, unsigned a, unsigned n)
{
    unsigned i;
    if (!zs_range(a, n, z->rdram_size))
        return 0;
    /* Preflight does not commit DMA saves. If a later command consumes one
     * of those output ranges, leave the self-modifying task to the RSP. */
    for (i = 0; i < z->num_written_ranges; ++i)
        if (a < z->written_ranges[i][1] && a + n > z->written_ranges[i][0])
            return 0;
    return 1;
}

static unsigned zs_phys(const ZSortState *z, unsigned a)
{
    return ((a & 0xffffffu) + z->segments[(a >> 24) & 15]) & 0xffffffu;
}

static int zs_step(ZSortState *z)
{
    return z->command_budget && --z->command_budget;
}

static int zs_reject(const char *kind, unsigned a, unsigned value)
{
    static unsigned count;
    if (getenv("HLE_ZSORT_TRACE") && count++ < 12)
        fprintf(stderr, "ZSortp rejected %s at %06x: %08x\n", kind, a, value);
    return 0;
}

static int zs_dmfield(unsigned field, unsigned len, unsigned *out)
{
    if (field < 0x400 || !zs_range(field - 0x400, len, 4096))
        return 0;
    *out = field - 0x400;
    return 1;
}

int zsort_ucode_match(const unsigned char *ram, unsigned size, unsigned data,
                      unsigned bytes)
{
    static const char prefix[] = "RSP Gfx ucode ";
    unsigned i, j;
    if (!ram || !data || data >= size)
        return 0;
    if (!bytes || bytes > 0x1000)
        bytes = 0x1000;
    if (bytes > size - data)
        return 0;
    for (i = 0; i + sizeof(prefix) - 1 + 6 <= bytes; ++i)
    {
        for (j = 0; j < sizeof(prefix) - 1; ++j)
            if (ram[(data + i + j) ^ ZBYTE] != (unsigned char)prefix[j])
                break;
        if (j != sizeof(prefix) - 1)
            continue;
        /* The first signature names this task, not a bundled overlay. */
        for (j = 0; j < 6; ++j)
            if (ram[(data + i + sizeof(prefix) - 1 + j) ^ ZBYTE] !=
                (unsigned char)"ZSortp"[j])
                return 0;
        return 1;
    }
    return 0;
}
/* ---- Embedded RDP lists and screen-space objects ---- */

static void zs_append(ZSortState *z, const int32_t *w, int n)
{
    if (z->fifo)
        rdp_fifo_append(z->fifo, w, n);
}

static int zs_rdp(ZSortState *z, unsigned op, unsigned w0, unsigned w1)
{
    int32_t w[2];
    if (op == 0xe2 || op == 0xe3)
    {
        unsigned shift = (w0 >> 8) & 255, len = w0 & 255, mask;
        unsigned *mode = op == 0xe3 ? &z->othermode_h : &z->othermode_l;
        if (shift >= 32 || !len || len > 32 - shift)
            return 0;
        mask = len == 32 ? ~0u : ((1u << len) - 1) << shift;
        *mode = (*mode & ~mask) | w1;
        w0 = 0xef000000u | (z->othermode_h & 0xffffff);
        w1 = z->othermode_l;
    }
    else if (op == 0xef)
    {
        z->othermode_h = w0 & 0xffffff;
        z->othermode_l = w1;
    }
    else if (op == 0xfd || op == 0xfe || op == 0xff)
        w1 = zs_phys(z, w1);
    if (op == 0xe9 || op == 0x29)
    {
        if (z->fifo)
            rdp_fifo_fullsync_note();
        return 1;
    }
    w[0] = (int32_t)w0;
    w[1] = (int32_t)w1;
    zs_append(z, w, 2);
    return 1;
}

static int zs_rdplist(ZSortState *z, unsigned pointer)
{
    unsigned a = zs_phys(z, pointer);
    if (!a)
        return 1;
    while (zs_step(z))
    {
        unsigned w0, w1, op, n = 2, i;
        int32_t w[44];
        if ((a & 3) || !zs_ram_range(z, a, 8))
            return 0;
        w0 = zs_get32(z->rdram, a);
        w1 = zs_get32(z->rdram, a + 4);
        op = w0 >> 24;
        a += 8;
        if (op == 0xdf)
            return 1;
        if (op == 0)
            continue; /* Embedded SPNoOp/padding pairs. */
        if (op == 0xe4 || op == 0xe5)
        {
            if (!zs_ram_range(z, a, 16))
                return 0;
            w[0] = w0;
            w[1] = w1;
            w[2] = zs_get32(z->rdram, a + 4);
            w[3] = zs_get32(z->rdram, a + 12);
            a += 16;
            zs_append(z, w, 4);
            continue;
        }
        if ((op & 63) >= 8 && (op & 63) <= 15)
        {
            static const unsigned char lengths[8] = {8,  12, 24, 28,
                                                     24, 28, 40, 44};
            n = lengths[op & 7];
            if (!zs_ram_range(z, a, (n - 2) * 4))
                return 0;
            w[0] = w0;
            w[1] = w1;
            for (i = 2; i < n; ++i, a += 4)
                w[i] = zs_get32(z->rdram, a);
            zs_append(z, w, n);
            continue;
        }
        if (op < 0xe2 || !zs_rdp(z, op, w0, w1))
            return zs_reject("RDP", a - 8, w0);
    }
    return 0;
}

static int zs_object(ZSortState *z, unsigned header, unsigned *cache,
                     unsigned *next)
{
    unsigned type = header & 7, a = header & 0x00fffff8u, nv, stride, prefix, i;
    RspTriVtx v[4];
    int32_t cmd[44];
    if (type > ZS_TEX_QUAD)
        return zs_reject("object type", a, type);
    prefix = (type == ZS_SHADE_TRI || type == ZS_SHADE_QUAD) ? 8 : 16;
    nv = type == ZS_STATE_ONLY ? 0 : type >= ZS_SHADE_QUAD ? 4 : 3;
    stride = (type == ZS_SHADE_TRI || type == ZS_SHADE_QUAD) ? 8 : 16;
    if (!zs_ram_range(z, a, prefix + nv * stride))
        return zs_reject("object bounds", a, type);
    *next = zs_phys(z, zs_get32(z->rdram, a));
    for (i = 0; i < prefix / 4 - 1; ++i)
    {
        unsigned p = zs_get32(z->rdram, a + 4 + i * 4);
        /* Unlike state/textured objects, the shaded handler has no null
         * guard. A null transition would read an RDP list at address zero. */
        if (stride == 8 && !p && cache[i])
            return 0;
        if (p && p != cache[i])
        {
            if (!zs_rdplist(z, p))
                return 0;
            cache[i] = p;
        }
    }
    if (!z->fifo || !nv)
        return 1;
    memset(v, 0, sizeof(v));
    for (i = 0; i < nv; ++i)
    {
        unsigned p = a + prefix + i * stride;
        v[i].x = zs_get16(z->rdram, p);
        v[i].y = zs_get16(z->rdram, p + 2);
        v[i].r = z->rdram[(p + 4) ^ ZBYTE];
        v[i].g = z->rdram[(p + 5) ^ ZBYTE];
        v[i].b = z->rdram[(p + 6) ^ ZBYTE];
        v[i].a = z->rdram[(p + 7) ^ ZBYTE];
        if (stride == 16)
        {
            v[i].s = zs_get16(z->rdram, p + 8);
            v[i].t = zs_get16(z->rdram, p + 10);
            v[i].invw = (int32_t)zs_get32(z->rdram, p + 12);
            v[i].pw = v[i].invw ? INT32_MAX / v[i].invw : INT32_MAX;
        }
    }
    rsp_tri_set_zboss_attr(1);
    /* ZSortp premultiplies S/T/W even when RDP perspective is disabled. */
    rsp_set_affine_tex(0);
    for (i = 0; i < nv - 2; ++i)
    {
        const RspTriVtx *v0, *v1, *v2;
        int n;
        /* The shaded network compares B/C first, then A (or D on the
         * second quad triangle). The textured network compares A/B first
         * and replaces A with D for the second triangle. Equal Y values
         * make these input permutations observable in the edge words. */
        if (stride == 8)
        {
            v0 = &v[1];
            v1 = &v[2];
            v2 = &v[i ? 3 : 0];
        }
        else
        {
            v0 = &v[i ? 3 : 0];
            v1 = &v[1];
            v2 = &v[2];
        }
        n = rsp_tri_write(cmd, v0, v1, v2, stride == 16, 0, 1, 1, 0, 0, 0x4000,
                          8, -1, 0x7fff);
        if (n > 0)
        {
            /* Both native writers explicitly store v0 into the shade-base
             * fraction pair (0x5ec / 0x914), rather than a half-unit bias. */
            cmd[12] = 0;
            cmd[13] = 0;
            zs_append(z, cmd, n);
        }
    }
    rsp_tri_set_zboss_attr(0);
    rsp_set_affine_tex(0);
    return 1;
}
/* ---- Matrix, viewport and scratch-memory commands ---- */

static int zs_movemem(ZSortState *z, unsigned w0, unsigned w1)
{
    unsigned idx = w0 & 0x3e, ofs = ((w0 >> 6) & 511) * 8,
             len = (1 + ((w0 >> 15) & 511)) * 8;
    unsigned a = zs_phys(z, w1), i, j;
    if (idx == 0 || idx == 2)
    {
        /* DMEM base table at 0x258: slot 0 = 0x400, slot 2 = 0x440. */
        ofs += idx == 2 ? 0x40 : 0;
        if (!zs_range(ofs, len, 4096) || !zs_range(a, len, z->rdram_size))
            return 0;
        if (!(w0 & 1) && !zs_ram_range(z, a, len))
            return 0;
        if ((w0 & 1) && !z->fifo)
        {
            if (z->num_written_ranges == 64)
                return 0;
            z->written_ranges[z->num_written_ranges][0] = a;
            z->written_ranges[z->num_written_ranges++][1] = a + len;
        }
        /* Copy by logical byte lane; unaligned lengths need not be a word. */
        for (i = 0; i < len; ++i)
            if (!(w0 & 1))
                z->dmem[(ofs + i) ^ ZBYTE] = z->rdram[(a + i) ^ ZBYTE];
            else if (z->fifo)
                z->rdram[(a + i) ^ ZBYTE] = z->dmem[(ofs + i) ^ ZBYTE];
        return 1;
    }
    if (idx == 4 || idx == 6 || idx == 8)
    {
        if ((w0 & 1) || ofs || len != 64)
            return 0;
        if (!zs_ram_range(z, a, 64))
            return 0;
        for (i = 0; i < 4; ++i)
            for (j = 0; j < 4; ++j)
            {
                unsigned p = (i * 4 + j) * 2;
                z->matrix[(idx - 4) / 2][i][j] =
                    (int32_t)((unsigned)zs_get16(z->rdram, a + p) << 16 |
                              (unsigned)(uint16_t)zs_get16(z->rdram,
                                                           a + 32 + p));
            }
        return 1;
    }
    if (idx == 12)
    {
        if ((w0 & 1) || ofs || len != 16)
            return 0;
        if (!zs_ram_range(z, a, 16))
            return 0;
        gsp_set_viewport(z->gsp, z->rdram, a);
        z->gsp->fog_m = zs_get16(z->rdram, a + 6);
        z->gsp->fog_o = zs_get16(z->rdram, a + 14);
        return 1;
    }
    return 0;
}

static int zs_transform(ZSortState *z, unsigned w1)
{
    unsigned n = 1 + (w1 >> 24), src, dst, i, k;
    if (!zs_dmfield((w1 >> 12) & 4095, n * 6, &src) ||
        !zs_dmfield(w1 & 4095, n * 16, &dst))
        return 0;
    for (i = 0; i < n; ++i, src += 6, dst += 16)
    {
        int x = zs_get16(z->dmem, src), y = zs_get16(z->dmem, src + 2),
            zz = zs_get16(z->dmem, src + 4);
        int64_t p[4];
        int sx, sy, fog, cc = 0;
        for (k = 0; k < 4; ++k)
            p[k] = (int64_t)x * z->matrix[2][0][k] +
                   (int64_t)y * z->matrix[2][1][k] +
                   (int64_t)zz * z->matrix[2][2][k] + z->matrix[2][3][k];
        sx = z->gsp->viewport.vtrans_x / 16384;
        sy = z->gsp->viewport.vtrans_y / 16384;
        if (p[3])
        {
            sx += (int)(p[0] * (z->gsp->viewport.vscale_x / 16384) / p[3]);
            sy += (int)(p[1] * (z->gsp->viewport.vscale_y / 16384) / p[3]);
        }
        zs_put16(z->dmem, dst, sx);
        zs_put16(z->dmem, dst + 2, sy);
        {
            int64_t w = p[3] * 31 / 65536;
            zs_put32(z->dmem, dst + 4,
                     w ? (unsigned)(INT32_MAX / w) : INT32_MAX);
        }
        zs_put16(z->dmem, dst + 8, (int)(p[0] / 65536));
        zs_put16(z->dmem, dst + 10, (int)(p[1] / 65536));
        fog = p[3] > 0 ? (int)(p[2] * z->gsp->fog_m / p[3]) + z->gsp->fog_o : 0;
        if (fog < 0)
            fog = 0;
        if (fog > 255)
            fog = 255;
        if (p[0] < -p[3])
            cc |= 16;
        if (p[0] > p[3])
            cc |= 1;
        if (p[1] < -p[3])
            cc |= 32;
        if (p[1] > p[3])
            cc |= 2;
        if (p[3] < 6554)
            cc |= 4;
        z->dmem[(dst + 12) ^ ZBYTE] = cc;
        z->dmem[(dst + 13) ^ ZBYTE] = fog;
        zs_put16(z->dmem, dst + 14, (int)(p[3] / 65536));
    }
    return 1;
}
/* ---- Display-list dispatch ---- */

static int zs_walk(ZSortState *z, unsigned start)
{
    unsigned pc[10], depth = 0;
    pc[0] = start;
    while (zs_step(z))
    {
        unsigned w0, w1, op, a = pc[depth];
        if ((a & 3) || !zs_ram_range(z, a, 8))
            return 0;
        w0 = zs_get32(z->rdram, a);
        w1 = zs_get32(z->rdram, a + 4);
        op = w0 >> 24;
        pc[depth] += 8;
        if (z->trace)
        {
            static unsigned char seen[256];
            if (!seen[op])
            {
                seen[op] = 1;
                fprintf(stderr, "ZSortp op %02x: %08x %08x at %06x\n", op, w0,
                        w1, a);
            }
        }
        if (op == 0xdf)
        {
            if (!depth)
                return 1;
            --depth;
        }
        else if (op == 0xde)
        {
            if (!((w0 >> 16) & 255))
            {
                if (depth == 9)
                    return 0;
                ++depth;
            }
            pc[depth] = zs_phys(z, w1);
        }
        else if (op == 0x80)
        {
            unsigned cache[3] = {0}, h = zs_phys(z, w0), chain;
            for (chain = 0; chain < 2; ++chain)
            {
                while (h)
                {
                    if (!zs_step(z) || !zs_object(z, h, cache, &h))
                        return 0;
                }
                h = zs_phys(z, w1);
            }
        }
        else if (op == 0x81)
        {
            if (!zs_rdplist(z, w1))
                return 0;
        }
        else if (op == 0xdc)
        {
            if (!zs_movemem(z, w0, w1))
                return 0;
        }
        else if (op == 0xdb)
        {
            unsigned idx = w0 & 255, off = (w0 >> 8) & 65535;
            if (idx == 6)
            {
                if (off & 3 || off >= 64)
                    return 0;
                z->segments[off / 4] = w1 & 0xffffff;
            }
            else if (idx == 8)
            {
                z->gsp->fog_m = (int16_t)(w1 >> 16);
                z->gsp->fog_o = (int16_t)w1;
            }
            else if (idx == 0x0e)
                z->gsp->persp_norm = w1 & 65535;
            else
                return 0;
        }
        else if (op == 0xd6)
        {
            if (!zs_transform(z, w1))
                return 0;
        }
        else if (op == 0xd5)
        {
            unsigned s = w0 & 15, t = (w1 >> 16) & 15, d = w1 & 15, i, j, k;
            int32_t m[4][4];
            if (s < 4 || s > 8 || s & 1 || d < 4 || d > 8 || d & 1)
                return 0;
            if (t < 4 || t > 8 || t & 1)
                return 0;
            for (i = 0; i < 4; ++i)
                for (j = 0; j < 4; ++j)
                {
                    int64_t sum = 0;
                    for (k = 0; k < 4; ++k)
                        sum += ((int64_t)z->matrix[(s - 4) / 2][i][k] *
                                z->matrix[(t - 4) / 2][k][j]) >>
                               16;
                    m[i][j] = (int32_t)sum;
                }
            memcpy(z->matrix[(d - 4) / 2], m, sizeof(m));
        }
        /* The main and embedded-RDP tables have different biases (DMEM
         * 0x2dc versus 0x2e2). Raw RDP commands belong in the 0x81 list;
         * do not interpret unknown main opcodes as F3D geometry commands. */
        else
        {
            if (z->trace)
                zs_reject("opcode", a, op);
            return 0;
        }
    }
    return 0;
}
/* ---- Walker entry points ---- */

static int zs_run(GSPState *g, RdpFifo *f, unsigned char *ram, unsigned size,
                  unsigned char *dm, unsigned dl)
{
    ZSortState z;
    unsigned i, j;
    if (!g || !ram || !dm || !size || (size & 3))
        return 0;
    memset(&z, 0, sizeof(z));
    z.rdram = ram;
    z.rdram_size = size;
    z.dmem = dm;
    z.gsp = g;
    z.fifo = f;
    z.command_budget = 200000;
    z.trace = getenv("HLE_ZSORT_TRACE") != NULL;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 4; ++j)
            z.matrix[i][j][j] = 65536;
    return zs_walk(&z, dl);
}

int zsort_validate(unsigned char *ram, unsigned size, const unsigned char *dm,
                   unsigned dl)
{
    GSPState g;
    unsigned char scratch[4096];
    if (!ram || !dm)
        return 0;
    memset(&g, 0, sizeof(g));
    gsp_init(&g);
    memcpy(scratch, dm, sizeof(scratch));
    return zs_run(&g, NULL, ram, size, scratch, dl);
}

int zsort_run_dl(GSPState *g, RdpFifo *f, unsigned char *ram, unsigned size,
                 unsigned char *dm, unsigned dl)
{
    return zs_run(g, f, ram, size, dm, dl);
}

