#ifndef RDP_EMIT_F3DTRI_H
#define RDP_EMIT_F3DTRI_H

#include <stdint.h>
#include "rdp_emit_rsp.h"

/* Fast3D (GBI1) triangle-setup revisions; see rdp_emit_f3dtri.c. */
enum
{
    F3D_TRI_NONE = 0,
    F3D_TRI_REV_A = 1,  /* Mario Kart 64 / Super Mario 64 era */
    F3D_TRI_REV_B = 2   /* GoldenEye / Perfect Dark */
};

typedef struct F3dTriState
{
    int      rev;
    uint32_t geometry_mode; /* F3D layout: 0x1 z, 0x2 tex, 0x4 shade,
                               0x200 smooth, 0x1000/0x2000 cull */
    uint32_t persp_norm;
    int      tile, level;
    int      zclamp;        /* G_TEXTURE w0 bits 16..23 (revision B) */
    int      flat_select;   /* build takes flat colour from flat_vtx */
    int      flat_vtx;      /* 0..2: command-order vertex */
    int      tex_scale_a;   /* revision A texture normalizer scale */
    /* clipper and vertex write */
    int16_t  vs[4], vt[4];  /* viewport scale/translate as stored */
    int      fog_m, fog_o;  /* G_MW_FOG multiplier and offset */
    int      guard;         /* gSPClipRatio: side-plane w lanes and the
                               vertex write guard factor (DMEM 0x76) */
    int      clamp_lo, clamp_hi; /* screen x/y clamp (data + 0x28) */
} F3dTriState;

/* Writes the coefficient block for the triangle (v1c, v2c, v3c in
 * command order) to ew. Returns the word count, 0 when the microcode
 * culls the triangle (face cull or zero area), or -1 for an unsupported
 * revision. */
int rsp_tri_write_f3d(int32_t *ew, const RspTriVtx *v1c,
                      const RspTriVtx *v2c, const RspTriVtx *v3c,
                      const F3dTriState *st);

/* The whole triangle command: the microcode's trivial reject on the
 * vertex clip flags, its guard-band clipper and fan, or the plain write.
 * Writes at most max_words; returns the word count. */
int rsp_tri_f3d(int32_t *ew, int max_words, const RspTriVtx *v1c,
                const RspTriVtx *v2c, const RspTriVtx *v3c,
                const F3dTriState *st);

/* 1 when the last rsp_tri_f3d call was culled on its face by the
 * unclipped write; the trivial reject, a zero area and the clipper's fan
 * leave it 0. */
int rsp_tri_f3d_face_culled(void);

/* Vertex clip flags for a pair of vertices transformed together (pos =
 * x, y, z, w clip-space s15.16); b is the pair's second vertex or 0 for
 * an unpaired last vertex. */
void f3d_vtx_flags(int rev, const int32_t a[4], const int32_t *b, int guard,
                   int32_t *fa, int32_t *fb);

#endif
