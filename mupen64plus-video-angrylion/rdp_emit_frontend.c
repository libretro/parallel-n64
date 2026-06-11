/* rdp_emit_frontend.c -- self-contained geometry frontend for the angrylion
 * HLE path. Reimplements the N64 matrix stack and vertex transform in C89,
 * reading matrices/vertices from RDRAM and producing clip-space vertices for
 * rdp_emit_bridge. The algorithm mirrors the documented N64 RSP geometry
 * stage (and GLideN64's gSP.cpp), but uses no external plugin code.
 *
 * Inert until a microcode dispatch calls it. Build check:
 *   gcc -std=c89 -pedantic -Wall -Wdeclaration-after-statement -Werror
 */

#include "rdp_emit_frontend.h"
#include "rdp_emit_rsp.h"
#include <stdio.h>
#include <stdlib.h>
#include "rdp_emit_recip.h"

/* read a signed 16-bit big-endian halfword from RDRAM (N64 byte order) */
/* RDRAM is stored as host-native 32-bit words in this core, so a sub-word
 * read must undo the in-word byte order: on a little-endian host the N64
 * (big-endian) byte at offset a lives at (a ^ 2) for 16-bit and (a ^ 3) for
 * 8-bit, exactly as the RSP's u16()/u8() accessors do via S16/S8. */
#ifdef MSB_FIRST
#define RDRAM_S16 0u
#define RDRAM_S8  0u
#else
#define RDRAM_S16 2u
#define RDRAM_S8  3u
#endif

static int read_u8_n64(const unsigned char *rdram, unsigned int addr)
{
    return (int)rdram[addr ^ RDRAM_S8];
}

static int read_s16_be(const unsigned char *rdram, unsigned int addr)
{
    const unsigned short *p = (const unsigned short *)(rdram + (addr ^ RDRAM_S16));
    int v = (int)*p;
    if (v & 0x8000) v -= 0x10000;
    return v;
}

static int read_u16_be(const unsigned char *rdram, unsigned int addr)
{
    const unsigned short *p = (const unsigned short *)(rdram + (addr ^ RDRAM_S16));
    return (int)*p;
}

/* Matrices are s15.16 fixed point: element = (integer << 16) | fraction,
 * held in an int32. 1.0 is 0x00010000. This matches the RSP's representation
 * (it keeps the integer and fractional s16 halves separately; an s15.16 int32
 * is the concatenation of the two). */
#define FX_ONE 0x00010000

static void mtx_identity(int32_t m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? FX_ONE : 0;
}

static void mtx_copy(int32_t d[4][4], int32_t s[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            d[i][j] = s[i][j];
}

/* d = a * b (row-vector convention: v' = v * d, matching the N64 RSP).
 * Each product of two s15.16 values is a 64-bit s31.32 intermediate; the
 * column sum follows the microcode's mtx_multiply MAC chain exactly: the
 * frac*frac partial of every product is truncated to its high half before
 * accumulation (vmadl), unlike an exact 64-bit dot product which would keep
 * those low bits until a single final shift. The difference is only a few
 * LSBs per element, but the vertex w values -- and the 1/w reciprocals the
 * triangle write derives from them -- inherit it, so the exact behaviour is
 * required for bit-identical RDP commands. */
static void mtx_mul(int32_t d[4][4], int32_t a[4][4], int32_t b[4][4])
{
    int32_t r[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int64_t acc = 0;
            for (k = 0; k < 4; k++)
            {
                int32_t ai = a[i][k] >> 16;
                int32_t af = a[i][k] & 0xffff;
                int32_t bi = b[k][j] >> 16;
                int32_t bf = b[k][j] & 0xffff;
                acc += (int64_t)((uint32_t)((uint32_t)af * (uint32_t)bf) >> 16);
                acc += (int64_t)ai * bf;
                acc += (int64_t)af * bi;
                acc += ((int64_t)ai * bi) << 16;
            }
            /* vmadn/vmadh extraction: low half with the unsigned-low clamp,
             * high half signed-clamped. In-range sums pass through. */
            {
                int64_t hi = acc >> 16;
                int32_t out_i, out_f;
                if (hi < -32768)      { out_i = -32768; out_f = 0x0000; }
                else if (hi > 32767)  { out_i =  32767; out_f = 0xffff; }
                else                  { out_i = (int32_t)hi; out_f = (int32_t)(acc & 0xffff); }
                r[i][j] = (int32_t)(((uint32_t)out_i << 16) | (uint32_t)out_f);
            }
        }
    }
    mtx_copy(d, r);
}

/* Load an N64 fixed-point 4x4 matrix from RDRAM. The RSP stores the integer
 * s16 halves of all 16 elements first (offset +0), then the fractional u16
 * halves (offset +32); an element's s15.16 value is (int << 16) | frac. The
 * matrix is stored transposed for the row-vector transform, with the
 * translation in the last row, so element (i,j) is read from linear slot
 * (i*4 + j). read_s16_be/read_u16_be apply the in-word byte swap. */
static void load_n64_matrix(int32_t m[4][4], const unsigned char *rdram, unsigned int addr)
{
    int i, j;
    unsigned int int_base = addr;
    unsigned int frac_base = addr + 32;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int ofs = (i * 4 + j) * 2;
            int ip = read_s16_be(rdram, int_base + (unsigned int)ofs);
            int fp = read_u16_be(rdram, frac_base + (unsigned int)ofs);
            m[i][j] = (int32_t)(((uint32_t)(int16_t)ip << 16) | ((uint32_t)fp & 0xffffu));
        }
    }
}

void gsp_init(GSPState *s)
{
    int i;
    s->clip_ratio = 2;
    s->tri_dx_scale  = 0x4000;
    s->tri_idy_scale = 0x0008;
    s->tri_frac_mask = (int32_t)0xffff;
    s->tri_vcr_bound = 0x1cc;
    mtx_identity(s->projection);
    for (i = 0; i < GSP_MTX_STACK; i++)
        mtx_identity(s->modelview[i]);
    s->modelview_top = 0;
    mtx_identity(s->combined);
    s->combined_valid = 0;
    s->viewport.vscale_x = 160 << 16;
    s->viewport.vscale_y = 120 << 16;
    s->viewport.vtrans_x = 160 << 16;
    s->viewport.vtrans_y = 120 << 16;
    s->viewport.vscale_z = 511 << 16;
    s->viewport.vtrans_z = 511 << 16;
    s->tex_scale_s = 0x8000u;   /* G_TEXTURE scale, S0.16 with 0x8000 == 1.0 */
    s->tex_scale_t = 0x8000u;
    s->persp_norm = 0xffffu; /* default until gSPPerspNormalize sets it */
    s->fog_m = 0;
    s->fog_o = 0;
    s->viewport.persp_norm = 0xffffu;
    s->viewport.tri_dx_scale  = s->tri_dx_scale;
    s->viewport.tri_idy_scale = s->tri_idy_scale;
    s->viewport.tri_frac_mask = s->tri_frac_mask;
    s->viewport.tri_vcr_bound = s->tri_vcr_bound;

    /* default lighting: no directional lights, white ambient (255) so geometry
     * flagged G_LIGHTING before any light load is not pure black. */
    s->num_lights = 0;
    for (i = 0; i < GSP_MAX_LIGHTS; i++)
    {
        s->light_rgb[i][0] = 255;
        s->light_rgb[i][1] = 255;
        s->light_rgb[i][2] = 255;
        s->light_dir[i][0] = 0;
        s->light_dir[i][1] = 0;
        s->light_dir[i][2] = 0;
        s->light_raw[i][0] = 0;
        s->light_raw[i][1] = 0;
        s->light_raw[i][2] = 0;
        s->light_kc[i] = 0;
        s->light_kl[i] = 0;
        s->light_kq[i] = 0;
        s->light_pos[i][0] = 0;
        s->light_pos[i][1] = 0;
        s->light_pos[i][2] = 0;
        if (i < 2)
        {
            s->lookat[i][0] = 0;
            s->lookat[i][1] = 0;
            s->lookat[i][2] = 0;
            s->lookat_raw[i][0] = 0;
            s->lookat_raw[i][1] = 0;
            s->lookat_raw[i][2] = 0;
            s->lights_valid = 0;
        }
    }
    s->tex_tile = 0;
    s->tex_level = 0;
    s->tex_w = 32;
    s->tex_h = 32;
}

void gsp_matrix_load(GSPState *s, const unsigned char *rdram, unsigned int addr,
                     int projection, int load, int push)
{
    int32_t m[4][4];
    load_n64_matrix(m, rdram, addr);

    if (projection)
    {
        if (load)
            mtx_copy(s->projection, m);
        else
            mtx_mul(s->projection, m, s->projection);
    }
    else
    {
        if (push && s->modelview_top < GSP_MTX_STACK - 1)
        {
            mtx_copy(s->modelview[s->modelview_top + 1], s->modelview[s->modelview_top]);
            s->modelview_top++;
        }
        if (load)
            mtx_copy(s->modelview[s->modelview_top], m);
        else
            mtx_mul(s->modelview[s->modelview_top], m, s->modelview[s->modelview_top]);
    }
    /* load_mtx zeroes mvpValid with a word write that also covers
     * lightsValid, for projection loads as well as modelview. */
    s->lights_valid = 0;
    s->combined_valid = 0;
}

void gsp_matrix_pop(GSPState *s)
{
    /* do_popmtx only zeroes mvpValid/lightsValid when bytes were
     * actually popped from the matrix stack. */
    if (s->modelview_top > 0)
    {
        s->modelview_top--;
        s->combined_valid = 0;
        s->lights_valid = 0;
    }
}

void gsp_set_lookat(GSPState *s, const unsigned char *rdram,
                    unsigned int addr, int index)
{
    if (index < 0 || index > 1)
        return;
    /* Raw s8 lookat direction; transformed into model space together with
     * the directional lights under the same lightsValid cache. */
    s->lookat_raw[index][0] = (int)(signed char)read_u8_n64(rdram, addr + 8);
    s->lookat_raw[index][1] = (int)(signed char)read_u8_n64(rdram, addr + 9);
    s->lookat_raw[index][2] = (int)(signed char)read_u8_n64(rdram, addr + 10);
    /* G_MOVEMEM does not touch lightsValid: the RSP keeps using the
     * previously transformed directions until a matrix load, a real
     * matrix pop, or a G_MW_NUMLIGHT write invalidates them. */
}

/* Transform the lookat and directional light vectors from world space into
 * model space and normalize them, mirroring the RSP's lazy
 * continue_light_dir_xfrm pass: newDir = origDir * transpose(MV[0:2][0:2]),
 * normalized and stored as s8. Vertex shading then dots these unit s8
 * directions against the raw (untransformed, unnormalized) s8 vertex
 * normal, so authored normal magnitudes scale the contribution exactly as
 * on the RSP, while light vector magnitudes are normalized away. The pass
 * runs at most once per matrix/light change (lightsValid). */
static void gsp_light_dir_xfrm(GSPState *s)
{
    int32_t (*M)[4] = s->modelview[s->modelview_top];
    int k;
    for (k = -2; k < s->num_lights; k++)
    {
        const int32_t *raw = (k < 0) ? s->lookat_raw[k + 2] : s->light_raw[k];
        int32_t *out       = (k < 0) ? s->lookat[k + 2]     : s->light_dir[k];
        rsp_light_dir_xfrm_one((const int32_t (*)[4])M, raw, out);
    }
    s->lights_valid = 1;
}

void gsp_set_fog(GSPState *s, int fm, int fo)
{
    s->fog_m = fm;
    s->fog_o = fo;
}

void gsp_task_reset(GSPState *s)
{
    /* The RSP task boot resets the matrix-stack pointer; display lists are
     * free to leave pushes unbalanced. Without this per-task reset the HLE
     * stack ratchets up to its cap over a few frames and every pushed
     * (skeleton/nested) draw transforms with a stale top-of-stack matrix. */
    s->modelview_top = 0;
    s->combined_valid = 0;
    /* The task's DMEM data load restores the default guard-band clip
     * ratio; a list that wants another ratio re-sends G_MW_CLIP. */
    s->clip_ratio = 2;
}

void gsp_combine_matrices(GSPState *s)
{
    mtx_mul(s->combined, s->modelview[s->modelview_top], s->projection);
    s->combined_valid = 1;
}

void gsp_set_viewport(GSPState *s, const unsigned char *rdram, unsigned int addr)
{
    /* N64 Vp: vscale[0..3] then vtrans[0..3] (s16). X/Y are 10.2 fixed (the .2
     * sub-pixel), so the pixel value is raw/4 -> in s15.16 that is raw<<14. Z
     * (vscale[2]/vtrans[2] = G_MAXZ/2 = 511) is a plain integer -> raw<<16. All
     * stored as s15.16, integer-only (no float). */
    s->viewport.vscale_x = (int32_t)read_s16_be(rdram, addr + 0) << 14;
    s->viewport.vscale_y = (int32_t)read_s16_be(rdram, addr + 2) << 14;
    s->viewport.vscale_z = (int32_t)read_s16_be(rdram, addr + 4) << 16;
    s->viewport.vtrans_x = (int32_t)read_s16_be(rdram, addr + 8) << 14;
    s->viewport.vtrans_y = (int32_t)read_s16_be(rdram, addr + 10) << 14;
    s->viewport.vtrans_z = (int32_t)read_s16_be(rdram, addr + 12) << 16;
}

void gsp_set_texture(GSPState *s, unsigned int scale_s, unsigned int scale_t,
                     int tile, int level, int tex_w, int tex_h)
{
    s->tex_scale_s = scale_s;
    s->tex_scale_t = scale_t;
    s->tex_tile = tile;
    s->tex_level = level;
    s->tex_w = tex_w;
    s->tex_h = tex_h;
}

/* N64 Vertex struct in RDRAM (16 bytes), big-endian:
 *   s16 y, x;  u16 flag; s16 z;  s16 t, s;  color/normal[4] */
/* Snapshot the screen-space values for a freshly written vertex under the
 * currently active viewport/perspNorm, mirroring the microcode's
 * vertices_store (G_VTX and the clipper's generated vertices both pass
 * through it; the triangle write only reloads the stored results). */
static void gsp_vertex_screen(GSPState *s, GSPVertex *vt)
{
    BridgeVertex bv;
    bv.cx = vt->cx; bv.cy = vt->cy; bv.cz = vt->cz; bv.cw = vt->cw;
    bridge_compute_screen(&bv, &s->viewport,
                          &vt->scr_x, &vt->scr_y, &vt->scr_z,
                          &vt->w_raw, &vt->rsp_ok, &vt->rsp_invw);
}

void gsp_vertex(GSPState *s, const unsigned char *rdram, unsigned int addr,
                int n, int v0)
{
    int i;
    if (!s->combined_valid)
        gsp_combine_matrices(s);

    for (i = 0; i < n; i++)
    {
        unsigned int base = addr + (unsigned int)i * 16;
        int idx = v0 + i;
        int32_t ox, oy, oz;
        int64_t cx, cy, cz, cw;
        int st_s, st_t;
        GSPVertex *vt;
        if (idx < 0 || idx >= GSP_MAX_VERTICES)
            continue;
        vt = &s->vtx[idx];

        /* Model-space position is an integer s16; the combined matrix is
         * s15.16. The product (integer * s15.16) is already an s15.16 value,
         * and the column sum is accumulated at 64-bit width like the RSP's
         * vector accumulator, then read back as s15.16 (>> 16 of the .16
         * product of integer*fixed cancels to leave the s15.16 result). */
        ox = read_s16_be(rdram, base + 0); /* x */
        oy = read_s16_be(rdram, base + 2); /* y */
        oz = read_s16_be(rdram, base + 4); /* z */

        cx = (int64_t)ox * s->combined[0][0] + (int64_t)oy * s->combined[1][0]
           + (int64_t)oz * s->combined[2][0] + (int64_t)s->combined[3][0];
        cy = (int64_t)ox * s->combined[0][1] + (int64_t)oy * s->combined[1][1]
           + (int64_t)oz * s->combined[2][1] + (int64_t)s->combined[3][1];
        cz = (int64_t)ox * s->combined[0][2] + (int64_t)oy * s->combined[1][2]
           + (int64_t)oz * s->combined[2][2] + (int64_t)s->combined[3][2];
        cw = (int64_t)ox * s->combined[0][3] + (int64_t)oy * s->combined[1][3]
           + (int64_t)oz * s->combined[2][3] + (int64_t)s->combined[3][3];

        vt->cx = (int32_t)cx; vt->cy = (int32_t)cy;
        vt->cz = (int32_t)cz; vt->cw = (int32_t)cw;

        st_s = read_s16_be(rdram, base + 8);  /* s */
        st_t = read_s16_be(rdram, base + 10); /* t */
        /* Carry the raw S10.5 texel coordinate (modulated by the G_TEXTURE
         * scale, an S0.16 fraction), NOT a [0,1]-normalized value. The
         * angrylion edgewalker and texel pipeline consume S10.5 texel units
         * directly; normalizing here (the GLideN64/GL-sampler convention)
         * collapsed the sampled coordinate toward 0 and drove the texture
         * combiner black. The encoder scales these into the fixed-point
         * inverse-w envelope angrylion expects. */
        /* The RSP texel coordinate is (raw S10.5 st * G_TEXTURE scale) >> 15:
         * the S0.16 scale field reads 0x8000 as 1.0 (so gSPTexture(0xFFFF)
         * roughly doubles). Calibrated against the cxd4 LLE oracle on the
         * pause menu: draws with scale 0x8000 match raw st exactly, draws
         * with scale 0xFFFF carry exactly twice the raw value; folding the
         * scale at >> 16 (or not at all) halves the sampled texel for the
         * 0xFFFF case. Result kept in s15.16:
         * (st << 16) * scale >> 15 == (st * scale) << 1. */
        /* (assigned below; G_TEXTURE_GEN overrides st_s/st_t first) */

        if (s->geometry_mode & 0x00020000u)     /* G_LIGHTING */
        {
            /* [12..14] are a signed s8 normal, used RAW: the RSP does not
             * transform or normalize vertex normals. Instead the light
             * (and lookat) directions are lazily rotated into model space
             * and normalized under the lightsValid cache, and the shade is
             * shade = ambient + S max(0, 2 * (n . L_i)) * rgb_i >> 15,
             * where n is the raw s8 normal and L_i the unit s8 model-space
             * direction. The factor 2 is the VMULU accumulator doubling;
             * for a full-length normal aligned with a light this reaches
             * ~32258/32768 of the light colour. Authored sub-length
             * normals dim exactly as on hardware, and non-orthonormal
             * model matrices (squash/stretch animation) light correctly
             * because the rotation is applied to the directions, not the
             * normal. */
            int nxb = (int)(signed char)read_u8_n64(rdram, base + 12);
            int nyb = (int)(signed char)read_u8_n64(rdram, base + 13);
            int nzb = (int)(signed char)read_u8_n64(rdram, base + 14);
            int li;
            if (!s->lights_valid)
                gsp_light_dir_xfrm(s);
            if (!(s->geometry_mode & 0x00400000u))
            {
                /* Pure directional lighting (no G_LIGHTING_POSITIONAL):
                 * the microcode's lights_dircoloraccum2 loop, bit-exact. */
                int32_t nrm[3], out[3];
                nrm[0] = nxb; nrm[1] = nyb; nrm[2] = nzb;
                rsp_light_vtx(nrm, s->light_rgb[s->num_lights],
                              (const int32_t (*)[3])s->light_rgb,
                              (const int32_t (*)[3])s->light_dir,
                              s->num_lights, out);
                vt->r = out[0] << 16;
                vt->g = out[1] << 16;
                vt->b = out[2] << 16;
                vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
            }
            else
            {
                /* Positional lighting (G_LIGHTING_POSITIONAL): the
                 * microcode's positional loop walks the lights one per
                 * iteration from the last down to the first, dispatching on
                 * the light's kc byte: kc != 0 takes the light_point chain,
                 * kc == 0 dots the prepass-normalized direction with the
                 * unsigned vmulu/vmacu clamp. Either factor folds into the
                 * running <<7-domain color with the per-light
                 * vmulf(color, 0x7FFF) + vmacf round. */
                int32_t lt[3], nrm[3], vtx3[3];
                int32_t (*MV)[4] = s->modelview[s->modelview_top];
                int32_t d;
                nrm[0] = nxb; nrm[1] = nyb; nrm[2] = nzb;
                vtx3[0] = ox; vtx3[1] = oy; vtx3[2] = oz;
                lt[0] = (s->light_rgb[s->num_lights][0] & 0xff) << 7;
                lt[1] = (s->light_rgb[s->num_lights][1] & 0xff) << 7;
                lt[2] = (s->light_rgb[s->num_lights][2] & 0xff) << 7;
                for (li = s->num_lights - 1; li >= 0; li--)
                {
                    if (s->light_kc[li] != 0)
                    {
                        d = rsp_light_point_factor((const int32_t (*)[4])MV,
                                                   nrm, vtx3,
                                                   s->light_pos[li],
                                                   s->light_kc[li],
                                                   s->light_kl[li],
                                                   s->light_kq[li]);
                    }
                    else
                        d = rsp_light_dirdot(nrm, s->light_dir[li]);
                    rsp_light_fold1(lt, s->light_rgb[li], d);
                    /* The loop suv-stores and luv-reloads the running
                     * color through DMEM every iteration, so each fold's
                     * result is truncated to its byte before the next
                     * light folds in. */
                    lt[0] = ((lt[0] >> 7) & 0xff) << 7;
                    lt[1] = ((lt[1] >> 7) & 0xff) << 7;
                    lt[2] = ((lt[2] >> 7) & 0xff) << 7;
                }
                vt->r = ((lt[0] >> 7) & 0xff) << 16;
                vt->g = ((lt[1] >> 7) & 0xff) << 16;
                vt->b = ((lt[2] >> 7) & 0xff) << 16;
                vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
            }

            if (s->geometry_mode & 0x00040000u) /* G_TEXTURE_GEN */
            {
                /* lights_texgenmain: generated coordinates from the raw s8
                 * normal against the two transformed lookat directions
                 * (MOVEMEM light slots 0/1), through the exact vmulf/vmacf
                 * accumulator chain; they replace the vertex-supplied lane
                 * values ahead of the same texture-scale multiply. */
                int linear = (s->geometry_mode & 0x00080000u) ? 1 : 0;
                int32_t nrm[3], gs, gt;
                nrm[0] = nxb; nrm[1] = nyb; nrm[2] = nzb;
                rsp_texgen(nrm, s->lookat[0], s->lookat[1], linear, &gs, &gt);
                st_s = gs;
                st_t = gt;
            }
        }
        else
        {
            vt->r = (int32_t)read_u8_n64(rdram, base + 12) << 16;
            vt->g = (int32_t)read_u8_n64(rdram, base + 13) << 16;
            vt->b = (int32_t)read_u8_n64(rdram, base + 14) << 16;
            vt->a = (int32_t)read_u8_n64(rdram, base + 15) << 16;
        }

        vt->s = (int32_t)(((int64_t)st_s * (int64_t)s->tex_scale_s) << 1);
        vt->t = (int32_t)(((int64_t)st_t * (int64_t)s->tex_scale_t) << 1);
        vt->sv = (int16_t)(((int64_t)st_s * (int64_t)s->tex_scale_s) >> 16);
        vt->tv = (int16_t)(((int64_t)st_t * (int64_t)s->tex_scale_t) >> 16);

        if (s->geometry_mode & 0x00010000u)     /* G_FOG */
        {
            /* With fog enabled the RSP replaces the shade alpha with the fog
             * factor: alpha = clamp(ndc_z * fog_m + fog_o, 0, 255), where
             * fog_m/fog_o come from the G_MW_FOG moveword (gSPFogPosition).
             * Verified against the cxd4 LLE oracle: indoors every actor
             * triangle carries shade alpha 0 (no fog at any visible depth);
             * passing the vertex alpha (255) through instead makes the
             * G_RM_FOG_SHADE_A blender output pure fog colour, which is what
             * rendered every lit actor as a black silhouette. ndc_z is the
             * exact s15.16 z/w; for w <= 0 the vertex is headed for the
             * clipper and the factor is irrelevant, so 0 is used. */
            int32_t fa = 0;
            if (vt->cw > 0)
            {
                /* RSP-exact fog: the microcode's vertex chain, so the
                 * triangle write's alpha lane is bit-identical to the LLE
                 * RSP. */
                fa = rsp_vtx_fog(vt->cz, vt->cw,
                                 (int32_t)(s->persp_norm ? s->persp_norm : 0xffffu),
                                 s->fog_m, s->fog_o);
            }
            vt->a = fa << 16;
        }

        /* Store the RSP's per-vertex clip outcode (the VCH sign-aware
         * screen-plane compare, bits N/P per x, y, z axis -- the SCRN half
         * of VTX_CLIP). G_CULLDL and the triangle trivial reject test the
         * AND of these flags, and unlike a plain w > 0 frustum test the
         * VCH rule also flags vertices behind the eye, which is what lets
         * the RSP reject the near geometry slivers a guard-band clipper
         * would otherwise rasterize. */
        {
            int32_t comps[4];
            int ax;
            unsigned int fl = 0;
            comps[0] = vt->cx; comps[1] = vt->cy;
            comps[2] = vt->cz; comps[3] = vt->cw;
            for (ax = 0; ax < 4; ax++)
            {
                int sn = ((comps[ax] ^ vt->cw) < 0);
                int nb = sn ? (comps[ax] <= -vt->cw) : (vt->cw < 0);
                int pb = sn ? (vt->cw < 0) : (comps[ax] >= vt->cw);
                if (nb) fl |= 1u << (4 + ax);   /* CLIP_NX..CLIP_NW */
                if (pb) fl |= 1u << (12 + ax);  /* CLIP_PX..CLIP_PW */
            }
            /* The scaled (clip-ratio) half of VTX_CLIP: the same VCH
             * sign-aware compare against ratio * w (the default ratio is
             * 2; the products need 33 bits). The clipper's x/y conditions
             * test these bits. */
            {
                int32_t w2 = rsp_clip_scale_w(vt->cw, s->clip_ratio);
                for (ax = 0; ax < 3; ax++)
                {
                    int32_t cp = comps[ax];
                    int sn = ((cp ^ w2) < 0);
                    int nb = sn ? (cp <= -w2) : (w2 < 0);
                    int pb = sn ? (w2 < 0) : (cp >= w2);
                    if (nb) fl |= 1u << (20 + ax);
                    if (pb) fl |= 1u << (28 + ax);
                }
            }
            vt->clip = (int)fl;
            gsp_vertex_screen(s, vt);
        }
    }
}

#define GEOM_ZBUFFER    0x00000001u

void gsp_set_tri_scales(GSPState *s, int32_t dx_scale, int32_t idy_scale,
                        int32_t frac_mask, int32_t vcr_bound)
{
    if (dx_scale > 0 && idy_scale > 0)
    {
        s->tri_dx_scale = dx_scale;
        s->tri_idy_scale = idy_scale;
        s->tri_frac_mask = frac_mask & 0xffff;
        if (vcr_bound > 0)
            s->tri_vcr_bound = vcr_bound;
        s->viewport.tri_dx_scale = dx_scale;
        s->viewport.tri_idy_scale = idy_scale;
        s->viewport.tri_frac_mask = s->tri_frac_mask;
        s->viewport.tri_vcr_bound = s->tri_vcr_bound;
    }
}

void gsp_set_persp_norm(GSPState *s, unsigned int pn)
{
    s->persp_norm = pn & 0xffffu;
    s->viewport.persp_norm = pn & 0xffffu;
}

void gsp_set_geometry_mode(GSPState *s, unsigned int mode)
{
    s->geometry_mode = mode;
}

unsigned int gsp_get_geometry_mode(const GSPState *s)
{
    return s->geometry_mode;
}

void gsp_set_num_lights(GSPState *s, int n)
{
    if (n < 0) n = 0;
    if (n > GSP_MAX_LIGHTS - 1) n = GSP_MAX_LIGHTS - 1;
    s->num_lights = n;
    /* The RSP zeroes lightsValid whenever numLightsx18 is written. */
    s->lights_valid = 0;
}

void gsp_set_light(GSPState *s, const unsigned char *rdram,
                   unsigned int addr, int index)
{
    if (index < 0 || index >= GSP_MAX_LIGHTS)
        return;

    /* Light struct: bytes [0..2] = r,g,b (0..255); the direction is a signed
     * s8 vector at bytes [8..10]. The raw direction is cached as loaded; the
     * RSP transforms all directions into model space (modelview transpose)
     * and normalizes them lazily at vertex-processing time, gated by
     * lightsValid -- see gsp_light_dir_xfrm(). */
    s->light_rgb[index][0] = (int32_t)read_u8_n64(rdram, addr + 0);
    s->light_rgb[index][1] = (int32_t)read_u8_n64(rdram, addr + 1);
    s->light_rgb[index][2] = (int32_t)read_u8_n64(rdram, addr + 2);

    s->light_raw[index][0] = (int)(signed char)read_u8_n64(rdram, addr + 8);
    s->light_raw[index][1] = (int)(signed char)read_u8_n64(rdram, addr + 9);
    s->light_raw[index][2] = (int)(signed char)read_u8_n64(rdram, addr + 10);

    /* F3DZEX point light fields. A nonzero byte at +3 (constant attenuation
     * kc) marks the light as positional: the struct then carries an s16
     * camera-space position at +8 (overlapping the s8 direction of the
     * directional layout), a linear factor kl at +7 and a quadratic factor
     * kq at +14. kc == 0 keeps the plain directional interpretation. */
    s->light_kc[index] = (int32_t)read_u8_n64(rdram, addr + 3);
    s->light_kl[index] = (int32_t)read_u8_n64(rdram, addr + 7);
    s->light_kq[index] = (int32_t)read_u8_n64(rdram, addr + 14);
    s->light_pos[index][0] = (int32_t)(short)((read_u8_n64(rdram, addr + 8) << 8)
                                             | read_u8_n64(rdram, addr + 9));
    s->light_pos[index][1] = (int32_t)(short)((read_u8_n64(rdram, addr + 10) << 8)
                                             | read_u8_n64(rdram, addr + 11));
    s->light_pos[index][2] = (int32_t)(short)((read_u8_n64(rdram, addr + 12) << 8)
                                             | read_u8_n64(rdram, addr + 13));
    /* G_MOVEMEM does not touch lightsValid (see gsp_set_lookat). */
}

void gsp_set_light_color(GSPState *s, int index,
                         int32_t rr, int32_t gg, int32_t bb)
{
    if (index < 0 || index >= GSP_MAX_LIGHTS)
        return;
    s->light_rgb[index][0] = rr;
    s->light_rgb[index][1] = gg;
    s->light_rgb[index][2] = bb;
    /* direction (and the lightsValid transform cache) intentionally
     * untouched: gSPLightColor documents itself as a color-only update */
}

/* Guard-band plane distance for clip-space vertex v (s15.16) against plane p:
 * p0: 2w - x >= 0   (x <=  2w)
 * p1: 2w + x >= 0   (x >= -2w)
 * p2: 2w - y >= 0   (y <=  2w)
 * p3: 2w + y >= 0   (y >= -2w)
 * The clip ratio of 2 matches the RSP guard band measured from the cxd4 LLE
 * stream (clipped skybox vertices land exactly on screen = vtrans +- 2*vscale).
 * The four side planes jointly bound w > 0, so Sutherland-Hodgman against
 * them alone also disposes of behind-the-eye (w <= 0) vertices, which is the
 * NoN ("no near clip") microcode behaviour. Evaluated in 64-bit: |2w| + |x|
 * can exceed 31 bits. */
/* The microcode's polygon clipper (ovl3). Conditions run in the order
 * near, far, +y, +x, -y, -x; each pass walks the polygon edges starting
 * from the last vertex and subdivides edges whose endpoints differ in the
 * condition's outcode bit, producing the boundary vertex with the
 * RSP-exact rsp_clip_lerp. The new vertex's outcodes are recomputed from
 * the lerped clip-space position with the same VCH rules the vertex
 * pipeline uses, since later conditions test them. Attribute lanes are
 * lerped in the stored domains: colors as byte << 7, texture coordinates
 * as the raw S10.5 shorts. */
#define GSP_CLIP_MAX 16

static const int16_t gsp_clip_ratio_rows[6][4] = {
    { 0, 0, 0,  1 },   /* near (NoN: w)        */
    { 0, 0, 1, -1 },   /* far  (z - w)         */
    { 0, 1, 0, -2 },   /* +y (w lane = -clip_ratio, patched at use) */
    { 1, 0, 0, -2 },   /* +x                   */
    { 0, 1, 0,  2 },   /* -y (w lane = +clip_ratio, patched at use) */
    { 1, 0, 0,  2 }    /* -x                   */
};

static const unsigned int gsp_clip_cond_mask[6] = {
    1u << 7,            /* CLIP_NEAR = CLIP_NW << SCRN (NoN) */
    1u << 14,           /* CLIP_FAR  = CLIP_PZ << SCRN       */
    1u << 29,           /* CLIP_PY << SCAL                   */
    1u << 28,           /* CLIP_PX << SCAL                   */
    1u << 21,           /* CLIP_NY << SCAL                   */
    1u << 20            /* CLIP_NX << SCAL                   */
};

/* Recompute the stored VCH outcodes for a clip-generated vertex. */
static void gsp_clip_vertex_flags(const GSPState *st, GSPVertex *vt)
{
    int32_t comps[4];
    int ax;
    unsigned int fl = 0;
    int32_t w2;
    comps[0] = vt->cx; comps[1] = vt->cy;
    comps[2] = vt->cz; comps[3] = vt->cw;
    for (ax = 0; ax < 4; ax++)
    {
        int sn = ((comps[ax] ^ vt->cw) < 0);
        int nb = sn ? (comps[ax] <= -vt->cw) : (vt->cw < 0);
        int pb = sn ? (vt->cw < 0) : (comps[ax] >= vt->cw);
        if (nb) fl |= 1u << (4 + ax);
        if (pb) fl |= 1u << (12 + ax);
    }
    w2 = rsp_clip_scale_w(vt->cw, st->clip_ratio);
    for (ax = 0; ax < 3; ax++)
    {
        int sn = ((comps[ax] ^ w2) < 0);
        int nb = sn ? (comps[ax] <= -w2) : (w2 < 0);
        int pb = sn ? (w2 < 0) : (comps[ax] >= w2);
        if (nb) fl |= 1u << (20 + ax);
        if (pb) fl |= 1u << (28 + ax);
    }
    vt->clip = (int)fl;
}

static void gsp_clip_subdivide(GSPState *st, GSPVertex *out,
                               const GSPVertex *onv,
                               const GSPVertex *offv, const int16_t cr[4])
{
    int32_t on_pos[4], off_pos[4], out_pos[4];
    int16_t on_attr[8], off_attr[8], out_attr[8];
    on_pos[0] = onv->cx;  on_pos[1] = onv->cy;
    on_pos[2] = onv->cz;  on_pos[3] = onv->cw;
    off_pos[0] = offv->cx; off_pos[1] = offv->cy;
    off_pos[2] = offv->cz; off_pos[3] = offv->cw;
    on_attr[0] = (int16_t)(((onv->r >> 16) & 0xff) << 7);
    on_attr[1] = (int16_t)(((onv->g >> 16) & 0xff) << 7);
    on_attr[2] = (int16_t)(((onv->b >> 16) & 0xff) << 7);
    on_attr[3] = (int16_t)(((onv->a >> 16) & 0xff) << 7);
    /* The vertex pipeline stores the texture coordinates as the vmudm mid
     * read of st * texscale -- HALF the value this pipeline carries (the
     * doubling the triangle write needs is applied at the bridge). The
     * RSP's clip lerp interpolates the stored halves; lerping the doubled
     * values truncates differently in the low bit, so convert to the
     * stored domain here (>> 17 is exact: the stored mid read always fits
     * 15 bits plus sign) and back after. */
    on_attr[4] = onv->sv;
    on_attr[5] = onv->tv;
    on_attr[6] = 0; on_attr[7] = 0;
    off_attr[0] = (int16_t)(((offv->r >> 16) & 0xff) << 7);
    off_attr[1] = (int16_t)(((offv->g >> 16) & 0xff) << 7);
    off_attr[2] = (int16_t)(((offv->b >> 16) & 0xff) << 7);
    off_attr[3] = (int16_t)(((offv->a >> 16) & 0xff) << 7);
    off_attr[4] = offv->sv;
    off_attr[5] = offv->tv;
    off_attr[6] = 0; off_attr[7] = 0;
    rsp_clip_lerp(on_pos, off_pos, cr, on_attr, off_attr, out_pos, out_attr);
    out->cx = out_pos[0]; out->cy = out_pos[1];
    out->cz = out_pos[2]; out->cw = out_pos[3];
    /* suv truncates the lerped color lanes to bytes (>> 7). */
    out->r = (int32_t)(((out_attr[0] >> 7) & 0xff) << 16);
    out->g = (int32_t)(((out_attr[1] >> 7) & 0xff) << 16);
    out->b = (int32_t)(((out_attr[2] >> 7) & 0xff) << 16);
    out->a = (int32_t)(((out_attr[3] >> 7) & 0xff) << 16);
    out->sv = out_attr[4];
    out->tv = out_attr[5];
    out->s = (int32_t)((uint32_t)(int32_t)out_attr[4] << 17);
    out->t = (int32_t)((uint32_t)(int32_t)out_attr[5] << 17);
    gsp_clip_vertex_flags(st, out);
    gsp_vertex_screen(st, out);
}

static int clip_polygon_guard(GSPState *st, GSPVertex *poly, int n)
{
    GSPVertex tmp[GSP_CLIP_MAX];
    int cond;
    for (cond = 0; cond < 6 && n > 0; cond++)
    {
        unsigned int mask = gsp_clip_cond_mask[cond];
        int16_t cr[4];
        int i, m = 0;
        unsigned int f3;
        int i3;
        cr[0] = gsp_clip_ratio_rows[cond][0];
        cr[1] = gsp_clip_ratio_rows[cond][1];
        cr[2] = gsp_clip_ratio_rows[cond][2];
        cr[3] = gsp_clip_ratio_rows[cond][3];
        if (cond >= 2)  /* the +-x/+-y guard-band planes scale with the ratio */
            cr[3] = (int16_t)((cr[3] < 0) ? -st->clip_ratio : st->clip_ratio);
        f3 = (unsigned int)poly[n - 1].clip & mask;
        i3 = n - 1;
        for (i = 0; i < n; i++)
        {
            unsigned int f2 = (unsigned int)poly[i].clip & mask;
            if (f2 != f3)
            {
                const GSPVertex *onv  = f2 ? &poly[i3] : &poly[i];
                const GSPVertex *offv = f2 ? &poly[i]  : &poly[i3];
                if (m < GSP_CLIP_MAX)
                {
                    gsp_clip_subdivide(st, &tmp[m], onv, offv, cr);
                    m++;
                }
            }
            if (!f2 && m < GSP_CLIP_MAX)
                tmp[m++] = poly[i];
            f3 = f2;
            i3 = i;
        }
        for (i = 0; i < m; i++)
            poly[i] = tmp[i];
        n = m;
        if (n < 3)
            return 0;
    }
    return n;
}

/* Fold and recenter the S10.5 texture coordinates of one triangle onto a
 * coherent wrap branch, and emit the bias. Runs per emitted triangle,
 * after clipping, like the rasterizer-facing coordinate handling: the
 * RSP's clip lerp itself operates on the raw stored S10.5 values. */
static void gsp_fold_st(GSPState *s, GSPVertex *v)
{
    /* Fold the S10.5 texture coordinates of b and c onto a's wrap branch.
     * The 16-bit S10.5 texel coordinates are only meaningful modulo 65536:
     * games author vertices on either side of the signed wrap (e.g. +29976
     * and -30145 on the Kokiri Forest ground, truly 5415 apart) and rely on
     * the hardware's 16-bit attribute deltas taking the short way around;
     * the per-pixel tile mask absorbs the absolute offset (65536 S10.5 =
     * 2048 texels, a multiple of every power-of-two mask). Differencing the
     * values in wider arithmetic instead takes the long way -- here it made
     * the S plane sweep ~60000 instead of ~5400 across the giant clipped
     * ground triangles, painting the floor texture at a ~10x wrong
     * frequency (the scanline-streak artifact). Folding must happen before
     * the guard-band clip so the clip lerp interpolates on the coherent
     * branch as the RSP's 16-bit lerp does. */
    {
        int32_t sref = v[0].s >> 16;
        int32_t tref = v[0].t >> 16;
        int k2;
        int32_t sv[3], tv[3], mn, mx;

        sv[0] = sref;
        tv[0] = tref;
        for (k2 = 1; k2 < 3; k2++)
        {
            sv[k2] = sref + (((((v[k2].s >> 16) - sref) + 0x8000) & 0xffff) - 0x8000);
            tv[k2] = tref + (((((v[k2].t >> 16) - tref) + 0x8000) & 0xffff) - 0x8000);
        }
        /* The folded branch can leave the representable S10.5 range (e.g.
         * folding -27658 onto +29976's branch gives +37878, whose s15.16
         * form overflows int32), and the coherent interval can even cross
         * the +/-32768 boundary so that no 65536-aligned shift fits.
         * Recenter the whole triangle around zero in steps of the tile's
         * wrap period (mask texels * 32 in S10.5; 65536 when the tile
         * geometry is unavailable or not a power of two): shifting all
         * three vertices by a multiple of the mask period lands on the
         * same texels after the per-pixel mask, so the offset is
         * invisible. Coordinates needing the fold only occur on
         * wrap-reliant content, where the mask is active by construction.
         * If recentring still cannot fit the values, leave the originals
         * untouched (the pre-fold behaviour). */
        {
            int32_t pers = 0, pert = 0, sh;
            int32_t sh_s = 0, sh_t = 0;
            int fit = 1;
            int ti = s->tex_tile & 7;
            if (s->tile_mask_s[ti])
                pers = 32 << s->tile_mask_s[ti];
            if (s->tile_mask_t[ti])
                pert = 32 << s->tile_mask_t[ti];
            mn = sv[0]; mx = sv[0];
            for (k2 = 1; k2 < 3; k2++)
            {
                if (sv[k2] < mn) mn = sv[k2];
                if (sv[k2] > mx) mx = sv[k2];
            }
            sh = pers ? (int32_t)((((int64_t)mn + mx) / 2) / pers) * pers : 0;
            if (mn - sh < -32768 || mx - sh > 32767)
                fit = 0;
            else
            {
                for (k2 = 0; k2 < 3; k2++) sv[k2] -= sh;
                sh_s = sh;
            }
            mn = tv[0]; mx = tv[0];
            for (k2 = 1; k2 < 3; k2++)
            {
                if (tv[k2] < mn) mn = tv[k2];
                if (tv[k2] > mx) mx = tv[k2];
            }
            sh = pert ? (int32_t)((((int64_t)mn + mx) / 2) / pert) * pert : 0;
            if (mn - sh < -32768 || mx - sh > 32767)
                fit = 0;
            else
            {
                for (k2 = 0; k2 < 3; k2++) tv[k2] -= sh;
                sh_t = sh;
            }
            /* The recenter shift feeds only the legacy emitters, which
             * rebuild the absolute coordinate branch from it; the RSP
             * triangle write consumes the exact stored shorts instead. */
            emit_set_st_bias(fit ? sh_s : 0, fit ? sh_t : 0);
            if (fit)
                for (k2 = 0; k2 < 3; k2++)
                {
                    v[k2].s = sv[k2] << 16;
                    v[k2].t = tv[k2] << 16;
                }
        }
    }
}

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered)
{
    GSPVertex poly[GSP_CLIP_MAX];
    int np;
    const GSPVertex *a, *b, *c;
    unsigned int oa, ob, oc;
    int k;

    if (i0 < 0 || i0 >= GSP_MAX_VERTICES ||
        i1 < 0 || i1 >= GSP_MAX_VERTICES ||
        i2 < 0 || i2 >= GSP_MAX_VERTICES)
        return 0;

    a = &s->vtx[i0]; b = &s->vtx[i1]; c = &s->vtx[i2];

    /* Z bit comes from the G_ZBUFFER geometry-mode bit, not the othermode. */
    z_buffered = (s->geometry_mode & GEOM_ZBUFFER) ? 1 : 0;

    /* Trivial reject: the RSP tests the AND of the three vertices' stored
     * VCH clip outcodes against CLIP_ALL_SCRN -- if every vertex lies
     * outside the same screen plane (including the z lanes, and including
     * the behind-the-eye encodings the VCH compare produces for w <= 0),
     * the triangle is dropped before any clipping. The guard-band clipper
     * below only sees triangles that survive this. */
    if (a->clip & b->clip & c->clip & GSP_CLIP_REJECT)
        return 0;

    /* The RSP enters the clipper when any vertex's stored outcode has any
     * bit of the scaled x/y conditions or the screen near/far conditions
     * set; otherwise the triangle is drawn directly. */
    oa = (unsigned int)a->clip;
    ob = (unsigned int)b->clip;
    oc = (unsigned int)c->clip;

    poly[0] = *a;
    poly[1] = *b;
    poly[2] = *c;

    np = 3;
#define GSP_CLIP_TRIGGER ((1u << 20) | (1u << 21) | (1u << 28) | (1u << 29) \
                        | (1u << 14) | (1u << 7))
    if ((oa | ob | oc) & GSP_CLIP_TRIGGER)
    {
        /* Polygon clip against the guard band the way the RSP microcode
         * does: triangles that straddle w <= 0 cannot be passed through to
         * the rasterizer (the 1/w attribute planes of such a triangle are
         * not the planes of its visible portion, so scissoring is not
         * equivalent), and steep overhang past the guard band loses
         * coefficient precision. */
        np = clip_polygon_guard(s, poly, np);
        if (np < 3)
            return 0;
    }

    /* Fan-triangulate the (possibly clipped) polygon and emit. */
    {
        int total = 0;
        int i;
        for (i = 0; i + 2 < np; i++)
        {
            BridgeVertex v0, v1, v2;
            GSPVertex tv[3];
            int nc;
            /* The microcode's subdivision draws (0,1,n-1), (1,2,n-1),
             * (2,3,n-1): consecutive vertex pairs fanned from the LAST
             * polygon vertex, not from vertex 0. */
            tv[0] = poly[i];
            tv[1] = poly[i + 1];
            tv[2] = poly[np - 1];
            /* The fan loop routes each sub-triangle through the full
             * triangle write, whose AND trivial-reject against the screen
             * outcodes (the CLIP_ALL_SCRN constant, not the temporarily
             * cleared activeClipPlanes) still applies: a guard-band
             * polygon's fan can contain sub-triangles entirely past one
             * screen plane, and the microcode drops those. */
            if (tv[0].clip & tv[1].clip & tv[2].clip & GSP_CLIP_REJECT)
                continue;
            gsp_fold_st(s, tv);
            v0.cx = tv[0].cx; v0.cy = tv[0].cy; v0.cz = tv[0].cz; v0.cw = tv[0].cw;
            v0.r = tv[0].r; v0.g = tv[0].g; v0.b = tv[0].b; v0.a = tv[0].a;
            v0.s = tv[0].s; v0.t = tv[0].t;
            v0.sv = tv[0].sv; v0.tv = tv[0].tv;
            v0.scr_valid = 1;
            v0.scr_x = tv[0].scr_x; v0.scr_y = tv[0].scr_y;
            v0.scr_z = tv[0].scr_z; v0.w_raw = tv[0].w_raw;
            v0.rsp_ok = tv[0].rsp_ok; v0.rsp_invw = tv[0].rsp_invw;
            v1.cx = tv[1].cx; v1.cy = tv[1].cy; v1.cz = tv[1].cz; v1.cw = tv[1].cw;
            v1.r = tv[1].r; v1.g = tv[1].g; v1.b = tv[1].b; v1.a = tv[1].a;
            v1.s = tv[1].s; v1.t = tv[1].t;
            v1.sv = tv[1].sv; v1.tv = tv[1].tv;
            v1.scr_valid = 1;
            v1.scr_x = tv[1].scr_x; v1.scr_y = tv[1].scr_y;
            v1.scr_z = tv[1].scr_z; v1.w_raw = tv[1].w_raw;
            v1.rsp_ok = tv[1].rsp_ok; v1.rsp_invw = tv[1].rsp_invw;
            v2.cx = tv[2].cx; v2.cy = tv[2].cy; v2.cz = tv[2].cz; v2.cw = tv[2].cw;
            v2.r = tv[2].r; v2.g = tv[2].g; v2.b = tv[2].b; v2.a = tv[2].a;
            v2.s = tv[2].s; v2.t = tv[2].t;
            v2.sv = tv[2].sv; v2.tv = tv[2].tv;
            v2.scr_valid = 1;
            v2.scr_x = tv[2].scr_x; v2.scr_y = tv[2].scr_y;
            v2.scr_z = tv[2].scr_z; v2.w_raw = tv[2].w_raw;
            v2.rsp_ok = tv[2].rsp_ok; v2.rsp_invw = tv[2].rsp_invw;
            nc = bridge_add_triangle(cmd + total, &v0, &v1, &v2, &s->viewport,
                                     textured, z_buffered,
                                     (s->geometry_mode & 0x00000004u) ? 1 : 0,
                                     (s->geometry_mode & 0x00200000u) ? 1 : 0,
                                     (int)((s->geometry_mode >> 9) & 3u),
                                     s->tex_tile, s->tex_level, s->tex_w, s->tex_h);
            if (nc > 0)
                total += nc;
        }
        return total;
    }
}
