/* Fast3D triangle setup (rdp_emit_f3dtri.c) against the cxd4 LLE RSP.
 *
 * The vectors in angrylion_f3dtri_expected.h are vertex records, state
 * and RDP words captured from LLE runs of Perfect Dark (revision B) and
 * Mario Kart 64 (revision A). Checks:
 *   - the revision B and revision A triangle writes, culls included and
 *     with Mario Kart 64's flat-shade vertex choice;
 *   - the revision B clipper: trivial reject, the six-plane clip with its
 *     generated vertices, and the fan, word for word;
 *   - the Fast3D vertex write model (rsp_vtx_screen): screen x, y, z and
 *     1/w of the clipper's input vertices from their clip positions;
 *   - the frontend route: the same clip cases through gsp_triangle.
 * Before this model GoldenEye and Perfect Dark drew through the F3DEX
 * writer, the stock vertex reciprocal and the generic guard-band clipper,
 * none of which reproduce these words. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rdp_emit_rsp.h"
#include "rdp_emit_f3dtri.h"
#include "rdp_emit_frontend.h"

typedef struct F3dWriterCase
{
    unsigned char rec[0x78];    /* three DMEM vertex records */
    unsigned char sp[0x20];     /* DMEM 0x110 state block */
    int rev, flat_select, flat_vtx, nwords;
    uint32_t words[44];
} F3dWriterCase;

typedef struct F3dClipCase
{
    unsigned char rec[0x78];
    unsigned char sp[0x20];
    unsigned char vp[0x20];     /* DMEM 0x260: viewport and fog rows */
    int nwords;
    uint32_t words[176];
} F3dClipCase;

#include "angrylion_f3dtri_expected.h"

/* the walker objects reference the HLE glue, which the test leaves out */
int s2dex1_ucode_match(const unsigned char *r, unsigned int size,
                       unsigned int ud, unsigned int ut)
{
    (void)r; (void)size; (void)ud; (void)ut;
    return 0;
}
void gsp_detect_ucode_params(GSPState *st, const unsigned char *r,
                             unsigned int size, unsigned int ud,
                             unsigned int ut)
{
    (void)st; (void)r; (void)size; (void)ud; (void)ut;
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int s16(const unsigned char *p) { return (int16_t)((p[0] << 8) | p[1]); }
static int u16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static int32_t i32(const unsigned char *r, int io, int fo)
{
    return (int32_t)(((uint32_t)(uint16_t)s16(r + io) << 16) | (uint32_t)u16(r + fo));
}

static void mkv(RspTriVtx *v, const unsigned char *r)
{
    memset(v, 0, sizeof(*v));
    v->cx = i32(r, 0x00, 0x08);
    v->cy = i32(r, 0x02, 0x0a);
    v->cz = i32(r, 0x04, 0x0c);
    v->cw = i32(r, 0x06, 0x0e);
    v->r = r[0x10]; v->g = r[0x11]; v->b = r[0x12]; v->a = r[0x13];
    v->s = s16(r + 0x14);
    v->t = s16(r + 0x16);
    v->x = (int16_t)s16(r + 0x18);
    v->y = (int16_t)s16(r + 0x1a);
    v->z = i32(r, 0x1c, 0x1e);
    v->invw = i32(r, 0x20, 0x22);
    v->f3d_flags = u16(r + 0x24);
}

static void mkstate(F3dTriState *st, const unsigned char *sp, int rev)
{
    memset(st, 0, sizeof(*st));
    st->rev = rev;
    st->persp_norm = (uint32_t)u16(sp + 0x02);
    st->geometry_mode = ((uint32_t)u16(sp + 0x04) << 16) | (uint32_t)u16(sp + 0x06);
    st->zclamp = sp[0x11];
    st->level = sp[0x12] >> 3;
    st->tile = sp[0x12] & 7;
    st->tex_scale_a = 0x40;
    st->guard = 2;
    st->clamp_lo = -4090;
    st->clamp_hi = 4090;
}

static int check_words(const char *what, int idx, const int32_t *got, int n,
                       const uint32_t *exp, int en)
{
    int i;
    if (n != en)
    {
        printf("FAIL %s %d: %d words, expected %d\n", what, idx, n, en);
        return 1;
    }
    for (i = 0; i < n; i++)
        if ((uint32_t)got[i] != exp[i])
        {
            printf("FAIL %s %d: word %d %08x, expected %08x\n", what, idx, i,
                   (unsigned)got[i], (unsigned)exp[i]);
            return 1;
        }
    return 0;
}

static int test_writer(const char *what, const F3dWriterCase *c, int n)
{
    int i, fails = 0;
    for (i = 0; i < n; i++)
    {
        RspTriVtx v[3];
        F3dTriState st;
        int32_t ew[64];
        int w;
        mkv(&v[0], c[i].rec);
        mkv(&v[1], c[i].rec + 0x28);
        mkv(&v[2], c[i].rec + 0x50);
        mkstate(&st, c[i].sp, c[i].rev);
        st.flat_select = c[i].flat_select;
        st.flat_vtx = c[i].flat_vtx;
        w = rsp_tri_write_f3d(ew, &v[0], &v[1], &v[2], &st);
        fails += check_words(what, i, ew, w, c[i].words, c[i].nwords);
    }
    printf("%s: %d/%d\n", what, n - fails, n);
    return fails;
}

static void clip_state(F3dTriState *st, const F3dClipCase *c)
{
    int k;
    mkstate(st, c->sp, F3D_TRI_REV_B);
    for (k = 0; k < 4; k++)
    {
        st->vs[k] = (int16_t)s16(c->vp + 2 * k);
        st->vt[k] = (int16_t)s16(c->vp + 8 + 2 * k);
    }
    st->fog_m = s16(c->vp + 0x10);
    st->fog_o = s16(c->vp + 0x12);
}

static int test_clip(void)
{
    int i, fails = 0;
    for (i = 0; i < N(k_pd_clip); i++)
    {
        const F3dClipCase *c = &k_pd_clip[i];
        RspTriVtx v[3];
        F3dTriState st;
        int32_t ew[512];
        int w;
        mkv(&v[0], c->rec);
        mkv(&v[1], c->rec + 0x28);
        mkv(&v[2], c->rec + 0x50);
        clip_state(&st, c);
        w = rsp_tri_rare(ew, 512, &v[0], &v[1], &v[2], &st);
        fails += check_words("clip", i, ew, w, c->words, c->nwords);
    }
    printf("revision B clipper: %d/%d\n", N(k_pd_clip) - fails, N(k_pd_clip));
    return fails;
}

static int test_vertex(void)
{
    int i, k, n = 0, fails = 0;
    rsp_set_vtx_f3d(1, -4090, 4090);
    for (i = 0; i < N(k_pd_clip); i++)
    {
        const F3dClipCase *c = &k_pd_clip[i];
        for (k = 0; k < 3; k++)
        {
            RspTriVtx v;
            int32_t sx, sy, sz, iw;
            mkv(&v, c->rec + 0x28 * k);
            rsp_vtx_screen(v.cx, v.cy, v.cz, v.cw, u16(c->sp + 2),
                           s16(c->vp + 0), s16(c->vp + 2), s16(c->vp + 4),
                           s16(c->vp + 8), s16(c->vp + 10), s16(c->vp + 12),
                           &sx, &sy, &sz, &iw);
            n++;
            if (sx != v.x || sy != v.y || sz != v.z || iw != v.invw)
            {
                printf("FAIL vertex %d.%d: %d %d %08x %08x, expected %d %d %08x %08x\n",
                       i, k, (int)sx, (int)sy, (unsigned)sz, (unsigned)iw,
                       v.x, v.y, (unsigned)v.z, (unsigned)v.invw);
                fails++;
            }
        }
    }
    rsp_set_vtx_f3d(0, -4090, 4090);
    printf("vertex write: %d/%d\n", n - fails, n);
    return fails;
}

/* the clip cases through the frontend: GSPState vertices and modes */
static int test_frontend(void)
{
    static GSPState s;
    int i, k, fails = 0;
    for (i = 0; i < N(k_pd_clip); i++)
    {
        const F3dClipCase *c = &k_pd_clip[i];
        F3dTriState st;
        int32_t cmd[GSP_TRI_CMD_WORDS];
        uint32_t gm;
        int n;
        gsp_init(&s);
        clip_state(&st, c);
        gm = st.geometry_mode;
        s.viewport.f3d_tri_rev = F3D_TRI_REV_B;
        s.viewport.f3d_clamp_lo = -4090;
        s.viewport.f3d_clamp_hi = 4090;
        s.viewport.persp_norm = st.persp_norm;
        s.viewport.f3d_zclamp = st.zclamp;
        s.viewport.vscale_x = (int32_t)st.vs[0] << 14;
        s.viewport.vscale_y = (int32_t)st.vs[1] << 14;
        s.viewport.vscale_z = (int32_t)st.vs[2] << 16;
        s.viewport.vtrans_x = (int32_t)st.vt[0] << 14;
        s.viewport.vtrans_y = (int32_t)st.vt[1] << 14;
        s.viewport.vtrans_z = (int32_t)st.vt[2] << 16;
        s.clip_ratio = 2;
        s.fog_m = st.fog_m;
        s.fog_o = st.fog_o;
        s.tex_tile = st.tile;
        s.tex_level = st.level;
        /* the frontend keeps the F3DEX2 geometry-mode layout */
        s.geometry_mode = (gm & 0x10005u) | ((gm & 0x200u) ? 0x00200000u : 0u)
                        | (((gm >> 12) & 3u) << 9);
        for (k = 0; k < 3; k++)
        {
            RspTriVtx v;
            GSPVertex *g = &s.vtx[k];
            mkv(&v, c->rec + 0x28 * k);
            memset(g, 0, sizeof(*g));
            g->cx = v.cx; g->cy = v.cy; g->cz = v.cz; g->cw = v.cw;
            g->r = v.r << 16; g->g = v.g << 16; g->b = v.b << 16; g->a = v.a << 16;
            g->sv = (int16_t)v.s; g->tv = (int16_t)v.t;
            g->scr_x = (int32_t)v.x << 14;
            g->scr_y = (int32_t)v.y << 14;
            g->scr_z = v.z;
            g->rsp_invw = v.invw;
            g->rsp_ok = 1;
            g->f3d_flags = v.f3d_flags;
        }
        n = gsp_triangle(&s, cmd, 0, 1, 2, (gm & 2u) ? 1 : 0, (gm & 1u) ? 1 : 0);
        fails += check_words("frontend", i, cmd, n, c->words, c->nwords);
    }
    printf("frontend route: %d/%d\n", N(k_pd_clip) - fails, N(k_pd_clip));
    return fails;
}

int main(void)
{
    int fails = 0;
    fails += test_writer("revision B write", k_pd_writer, N(k_pd_writer));
    fails += test_writer("revision A write", k_mk_writer, N(k_mk_writer));
    fails += test_clip();
    fails += test_vertex();
    fails += test_frontend();
    if (fails)
    {
        printf("FAILED (%d)\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
