/* rdp_emit_frontend.h -- self-contained geometry frontend for the angrylion
 * HLE path. Maintains the N64 matrix stack and a transformed-vertex cache,
 * reading matrices and vertices from RDRAM, and produces clip-space vertices
 * for rdp_emit_bridge. ISO C89 / MSVC-compatible.
 *
 * This is the angrylion-internal reimplementation of the geometry stage that
 * the cxd4 RSP performs in microcode (the algorithm mirrors GLideN64's
 * gSP.cpp, but no GLideN64 code is used -- this is standalone C89). */
#ifndef RDP_EMIT_FRONTEND_H
#define RDP_EMIT_FRONTEND_H

#include "rdp_emit.h"
#include "rdp_emit_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sized for the DMA-style vertex loaders, not just the 32/64-slot
 * F3DEX-family buffers: Turbo3D/T3DUX objects carry a u8 vertex count and
 * u8 triangle-record indices, so a single object can legitimately load up
 * to 255 vertices (Last Legion UX's mech uses 92-vertex objects; with a
 * 64-slot buffer every triangle referencing slots 64+ -- among them the
 * mech's feet -- was silently dropped at the index bound). */
#define GSP_MAX_VERTICES 256
#define GSP_MTX_STACK    16
#define GSP_MAX_LIGHTS   16   /* stock needs 8 directional + ambient; Conker's
                               * CBFD build loads up to 11 lights plus the
                               * ambient, so the array must hold at least 13 */

/* one loaded+transformed vertex, kept in the frontend's cache (no float) */
typedef struct GSPVertex
{
    int32_t cx, cy, cz, cw; /* clip-space s15.16 (exact, post combined transform) */
    int32_t r, g, b, a;     /* 0..255 color as s15.16 (vertex color or lighting) */
    int32_t s, t;           /* S10.5 texel coordinate as s15.16 */
    int16_t sv, tv;         /* the RSP's stored VTX_TC shorts (the vmudm mid
                               read of st * texscale): the exact values the
                               clip lerp interpolates. The s/t fields above
                               are these doubled (with int32 wrap for large
                               coordinates), which the emit path expects. */
    int     clip;           /* clip flags (outcode) */
    /* Store-time screen snapshot (see BridgeVertex): the microcode
     * computes screen coordinates and 1/w at G_VTX time under the
     * then-active viewport/perspNorm and the triangle write reloads
     * them, so a viewport change between the load and the draw must
     * not retroactively move already-stored vertices. */
    int32_t scr_x, scr_y, scr_z;
    int64_t w_raw;
    int     rsp_ok;
    int32_t rsp_invw;
    /* Fighting Force 64's 0xB2 overlay vertices: screen-space injected,
     * drawn by the microcode's simplified 2D triangle path with raw,
     * unnormalized texture/W attributes (no per-vertex perspective
     * normalizer, integer colours). */
    int     flat2d;
} GSPVertex;

/* VCH clip outcode bits, matching the F3DEX2 VTX_CLIP screen layout:
 * N flags at bits 4..7 (x, y, z, w), P flags at bits 12..15. The reject
 * mask is CLIP_ALL for the NoN microcode family: near clipping tests the
 * W lane (CLIP_NW), not NZ -- content drawn past the z near plane but in
 * front of the eye (ortho overlays, decals near the camera) must render.
 * For non-NoN F3DEX2 this under-culls only (the guard-band clipper still
 * handles the geometry), never over-culls. */
#define GSP_CLIP_NX 0x0010
#define GSP_CLIP_NY 0x0020
#define GSP_CLIP_NZ 0x0040
#define GSP_CLIP_NW 0x0080
#define GSP_CLIP_PX 0x1000
#define GSP_CLIP_PY 0x2000
#define GSP_CLIP_PZ 0x4000
#define GSP_CLIP_REJECT (GSP_CLIP_NX | GSP_CLIP_NY | GSP_CLIP_PX | \
                         GSP_CLIP_PY | GSP_CLIP_PZ | GSP_CLIP_NW)

typedef struct GSPState
{
    /* Matrices are stored in the RSP's fixed-point form: each element is an
     * s15.16 value held in an int32 (integer part in the high 16 bits, the
     * 1/65536 fraction in the low 16). The transform and multiply accumulate
     * these the way the RSP vector unit does, rather than in float, so the
     * emitted coordinates match the LLE path bit-for-bit. */
    int32_t projection[4][4];
    int32_t modelview[GSP_MTX_STACK][4][4];
    int   modelview_top;
    int32_t combined[4][4];
    int   combined_valid;

    BridgeViewport viewport;
    /* gSPClipRatio (G_MOVEWORD G_MW_CLIP): the guard-band multiplier for
     * the scaled clip planes. The microcode's DMEM defaults to 2 on task
     * load; OoT's pause screen sets 1 (clip exactly at the screen edges)
     * while the 3D scene restores 2. */
    int clip_ratio;
    /* z coefficient of the near clip-plane row (microcode data + 0x1a8):
     * 0 = NoN microcode (near plane is w >= 0 only), 1 = standard near
     * clipping (z + w >= 0). Selects the near plane the polygon clipper
     * uses and the VCH outcode bit that gates and rejects against it. */
    int clip_near_z;
    /* The reduced line microcodes (gspL3DEX and Blast Corps' Fast3D line
     * build) have no lighting or fog block: vertex alpha stays the display
     * list's raw byte even with G_FOG set in the geometry mode. */
    int fog_off;
    int pd_ci;                /* Perfect Dark colour-indexed vertex format */
    unsigned int pd_cbase;    /* physical base of the vertex colour table */
    int cbfd;                 /* Conker CBFD vertex/lighting model */
    unsigned int cbfd_nbase;  /* physical base of the per-vertex normal table */
    int t3d_zbuffered;        /* T3DUX: depth-test state sniffed from othermode */
    int32_t cbfd_lpos[GSP_MAX_LIGHTS][3]; /* CBFD point-light s16 positions */
    int32_t cbfd_lca[GSP_MAX_LIGHTS];     /* CBFD point-light ca numerator (light byte = ca*16) */
    int32_t cbfd_cmod[16];    /* G_MW_COORD_MOD offset/scale rows (12..15 in 16.16) */
    int dkr_shade_alpha_zero; /* DKR: RSP zeroes shade alpha under the fog blender P mux */
    unsigned int mtx_stack_ptr;
    unsigned int mtx_stack_base;
    unsigned int mtx_stack_limit;
    int line_z;            /* Body Harvest's line build: z-buffered lines
                              (emit the triangle's z block, interpolated
                              along the segment) */
    int line_alpha_mask;   /* gspL3DEX: shade alpha & 0xfc on line commands */
    int line_clip_3d;      /* gspL3DEX: 3D-clip flagged line segments; Blast
                              Corps' Fast3D line build draws them raw */
    /* current RDP scissor, tracked for the line microcodes' per-line
     * scissor emission */
    int32_t scis_w0, scis_w1;
    int scis_valid;
    /* the line microcodes' XL/DxL DMEM slot: written by every x-major
     * line, carried inert into y-major and transposed commands */
    int32_t line_xl[2];
    /* Fan pivot of the polygon clipper's triangulation: 0 = fan from the
     * last polygon vertex with ascending pairs (F3DEX2 2.05+/F3DZEX2),
     * 1 = fan from the first vertex with descending pairs (2.04H). */
    int clip_fan_first;
    int clip_reject;  /* F3DLX.Rej/F3DZEX.Rej: whole-tri reject, no clipper */
    int no_texgen;    /* F3DFLX.Rej: texgen drives reflection alpha, not S/T */
    int reflect_valid;            /* F3DFLX: reflection LUT captured this task */
    unsigned char reflect_lut[256]; /* 1D reflection ramp DMA'd to DMEM by the
                                       F3DFLX racer draw (gSPDmaRead): the lookat
                                       dot product indexes it to a fog factor */
    /* G_BRANCH_Z (F3DEX2: compare 32-bit screen z) vs G_BRANCH_W
     * (F3DZEX2: compare s16 clip-w integer) for opcode 0x04. */
    int branch_z_mode;
    unsigned int   tex_scale_s, tex_scale_t; /* raw S0.16 from G_TEXTURE */
    unsigned int   persp_norm;               /* G_MW_PERSPNORM u16 (gSPPerspNormalize) */
    int            fog_m, fog_o;             /* G_MW_FOG multiplier/offset (s16 each) */
    int            tex_tile, tex_level, tex_w, tex_h;
    unsigned char  tile_mask_s[8], tile_mask_t[8];

    unsigned int   geometry_mode;

    /* Vertex lighting state (integer, like the RSP). lights[0..num_lights-1]
     * are directional lights; lights[num_lights] holds the ambient color.
     * light_rgb is 0..255 per channel; light_dir is the normalized direction
     * as s.15 fixed (i.e. value/32768). Used when G_LIGHTING is set. */
    int   num_lights;
    int32_t light_rgb[GSP_MAX_LIGHTS][3];
    int32_t light_dir[GSP_MAX_LIGHTS][3]; /* cached model-space unit dirs (s8) */
    int32_t light_raw[GSP_MAX_LIGHTS][3]; /* raw s8 dirs as loaded by MOVEMEM */
    int32_t light_kc[GSP_MAX_LIGHTS];     /* point light constant attenuation (0 = directional) */
    int32_t light_kl[GSP_MAX_LIGHTS];     /* point light linear attenuation */
    int32_t light_kq[GSP_MAX_LIGHTS];     /* point light quadratic attenuation */
    int32_t light_pos[GSP_MAX_LIGHTS][3]; /* point light position, s16 camera-space */
    int32_t lookat[2][3];     /* cached model-space lookat X/Y dirs (s8) */
    int32_t lookat_raw[2][3]; /* raw s8 lookat dirs as loaded */
    int     lights_valid;     /* cache flag, mirrors the RSP's lightsValid */

    GSPVertex vtx[GSP_MAX_VERTICES];
    /* per-microcode triangle-setup scale constants from the ucode data
     * segment's v30 vector (DMEM 0x1C0): lane 2 scales the edge dX, lane 7
     * scales the inverse-dY reciprocal. F3DEX2 2.08 ships 0x4000/0x0008;
     * F3DZEX2 ships 0x1000/0x0020. The intermediate vmudl/vmadm clamp
     * points depend on the split, so the pair is not interchangeable even
     * though the products agree. */
    int32_t tri_dx_scale;
    int32_t tri_idy_scale;
    int32_t tri_frac_mask;
    int32_t tri_vcr_bound;
} GSPState;

/* lifecycle */
void gsp_init(GSPState *s);
void gsp_detect_ucode_params(GSPState *st, const unsigned char *rdram,
                             unsigned int rdram_size,
                             unsigned int ud, unsigned int ut);

/* MOVEMEM G_MV_LIGHT slots 0 and 1: the texture-coordinate-generation
 * lookat X/Y direction vectors (s8 at bytes 8..10 of the Light struct). */
void gsp_set_alpha_light(GSPState *s, const unsigned char *rdram, unsigned int addr, int index);
void gsp_set_lookat(GSPState *s, const unsigned char *rdram,
                    unsigned int addr, int index);

/* G_MW_FOG: fog multiplier (w1 >> 16) and offset (w1 & 0xffff), both s16. */
void gsp_set_vertex_color_base(GSPState *s, unsigned int base);
void gsp_set_fog(GSPState *s, int fm, int fo);
void gsp_set_dkr_shade_alpha_zero(GSPState *s, int on);
void gsp_set_rsp_screen_model(GSPState *s, int on);

/* per-RSP-task reset: the microcode's DRAM matrix-stack pointer is
 * re-initialised at every task boot, so an unbalanced push/pop count within
 * one display list (which games rely on) cannot leak into the next frame. */
void gsp_task_reset(GSPState *s);

/* matrix ops (addr is an RDRAM byte address to a 4x4 N64 fixed-point matrix) */
void gsp_matrix_load(GSPState *s, const unsigned char *rdram, unsigned int addr,
                     int projection, int load, int push);
void gsp_matrix_pop(GSPState *s, const unsigned char *rdram);
void gsp_set_matrix_stack(GSPState *s, unsigned char *rdram,
                          unsigned int base, unsigned int size);
int gsp_culldl_test(const GSPState *s, int v0, int vn);
/* DKR (F3DDKR) indexed matrix load + active-slot select. */
void gsp_matrix_dkr(GSPState *s, const unsigned char *rdram, unsigned int addr,
                    int index, int multiply);
void gsp_select_matrix_dkr(GSPState *s, int index);
void gsp_combine_matrices(GSPState *s);
void gsp_force_matrix_chunk(GSPState *s, const unsigned char *rdram,
                            unsigned int addr, unsigned int offset);

/* viewport (addr -> N64 Vp struct in RDRAM), and texture scale state */
void gsp_set_viewport(GSPState *s, const unsigned char *rdram, unsigned int addr);
void gsp_set_texture(GSPState *s, unsigned int scale_s, unsigned int scale_t,
                     int tile, int level, int tex_w, int tex_h);

/* load+transform n vertices starting at index v0 from RDRAM addr */
void gsp_vertex(GSPState *s, const unsigned char *rdram, unsigned int addr,
                int n, int v0);

/* DKR (F3DDKR) 10-byte pos+RGBA vertex load (see rdp_emit_frontend.c). */
void gsp_vertex_dkr(GSPState *s, const unsigned char *rdram, unsigned int addr,
                    int n, int v0, int billboard);
/* T3DUX (Turbo3D UX) vertex load: 8-byte y,x,flag,z positions from vaddr and
 * a separate a,b,g,r colour array from caddr, n vertices into slots 0..n-1. */
void gsp_vertex_t3dux(GSPState *s, const unsigned char *rdram,
                      unsigned int vaddr, unsigned int caddr, int n);
/* DKR: set a cached vertex's per-vertex S/T from a gSPPolygon entry. */
void gsp_set_vertex_st(GSPState *s, int idx, int st_s, int st_t);

/* emit a triangle from three cached vertex indices via the bridge.
 * Writes the RDP command words to cmd; returns the word count. */
/* F3DEX2 geometry mode (cull bits etc). Set by G_GEOMETRYMODE; consulted by
 * gsp_triangle to reject back/front-facing triangles the RSP would cull. */
void gsp_set_geometry_mode(GSPState *s, unsigned int mode);
void gsp_set_tri_scales(GSPState *s, int32_t dx_scale, int32_t idy_scale,
                        int32_t frac_mask, int32_t vcr_bound);
void gsp_set_persp_norm(GSPState *s, unsigned int pn);
unsigned int gsp_get_geometry_mode(const GSPState *s);

/* Vertex lighting. gsp_set_num_lights sets the directional-light count (the
 * ambient color is the entry just past the last directional light, matching
 * the RSP layout). gsp_set_light loads one light structure (24 bytes) from
 * RDRAM at byte address `addr` into slot `index`. */
void gsp_set_num_lights(GSPState *s, int n);
void gsp_set_light(GSPState *s, const unsigned char *rdram,
                   unsigned int addr, int index);
void gsp_set_light_color(GSPState *s, int index,
                         int32_t rr, int32_t gg, int32_t bb);
void gsp_modify_vertex(GSPState *s, int vtx, unsigned int where,
                       unsigned int w1);

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered);
/* Expand a G_LINE3D (gSPLine3D) between two stored vertices into a thin
 * screen-space quad and emit it; used by the Doom 64 automap line ucode. */
int gsp_line(GSPState *s, int32_t *cmd, int i0, int i1, int width_q);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_FRONTEND_H */
