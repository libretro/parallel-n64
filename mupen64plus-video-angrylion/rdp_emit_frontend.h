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

#define GSP_MAX_VERTICES 64   /* N64 vertex buffer holds up to 32/64 */
#define GSP_MTX_STACK    16
#define GSP_MAX_LIGHTS   10   /* up to 8 directional + ambient (+ slack) */

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
} GSPState;

/* lifecycle */
void gsp_init(GSPState *s);

/* MOVEMEM G_MV_LIGHT slots 0 and 1: the texture-coordinate-generation
 * lookat X/Y direction vectors (s8 at bytes 8..10 of the Light struct). */
void gsp_set_lookat(GSPState *s, const unsigned char *rdram,
                    unsigned int addr, int index);

/* G_MW_FOG: fog multiplier (w1 >> 16) and offset (w1 & 0xffff), both s16. */
void gsp_set_fog(GSPState *s, int fm, int fo);

/* per-RSP-task reset: the microcode's DRAM matrix-stack pointer is
 * re-initialised at every task boot, so an unbalanced push/pop count within
 * one display list (which games rely on) cannot leak into the next frame. */
void gsp_task_reset(GSPState *s);

/* matrix ops (addr is an RDRAM byte address to a 4x4 N64 fixed-point matrix) */
void gsp_matrix_load(GSPState *s, const unsigned char *rdram, unsigned int addr,
                     int projection, int load, int push);
void gsp_matrix_pop(GSPState *s);
void gsp_combine_matrices(GSPState *s);

/* viewport (addr -> N64 Vp struct in RDRAM), and texture scale state */
void gsp_set_viewport(GSPState *s, const unsigned char *rdram, unsigned int addr);
void gsp_set_texture(GSPState *s, unsigned int scale_s, unsigned int scale_t,
                     int tile, int level, int tex_w, int tex_h);

/* load+transform n vertices starting at index v0 from RDRAM addr */
void gsp_vertex(GSPState *s, const unsigned char *rdram, unsigned int addr,
                int n, int v0);

/* emit a triangle from three cached vertex indices via the bridge.
 * Writes the RDP command words to cmd; returns the word count. */
/* F3DEX2 geometry mode (cull bits etc). Set by G_GEOMETRYMODE; consulted by
 * gsp_triangle to reject back/front-facing triangles the RSP would cull. */
void gsp_set_geometry_mode(GSPState *s, unsigned int mode);
void gsp_set_persp_norm(GSPState *s, unsigned int pn);
unsigned int gsp_get_geometry_mode(const GSPState *s);

/* Vertex lighting. gsp_set_num_lights sets the directional-light count (the
 * ambient color is the entry just past the last directional light, matching
 * the RSP layout). gsp_set_light loads one light structure (24 bytes) from
 * RDRAM at byte address `addr` into slot `index`. */
void gsp_set_num_lights(GSPState *s, int n);
void gsp_set_light(GSPState *s, const unsigned char *rdram,
                   unsigned int addr, int index);

int gsp_triangle(GSPState *s, int32_t *cmd, int i0, int i1, int i2,
                 int textured, int z_buffered);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_FRONTEND_H */
