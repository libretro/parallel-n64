/* rdp_emit_bridge.h -- frontend->encoder bridge for the angrylion HLE path.
 * See rdp_emit_bridge.c. ISO C89 / MSVC-compatible. */
#ifndef RDP_EMIT_BRIDGE_H
#define RDP_EMIT_BRIDGE_H

#include "rdp_emit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BridgeViewport
{
    int32_t vscale_x, vscale_y, vscale_z;   /* s15.16 */
    int32_t vtrans_x, vtrans_y, vtrans_z;   /* s15.16 */
    unsigned int persp_norm; /* G_MW_PERSPNORM u16, applied to 1/w */
} BridgeViewport;

/* Clip-space vertex inputs to the bridge: exact s15.16 clip coords (cx,cy,cz,cw)
 * plus the per-vertex attributes already in their fixed forms (r,g,b,a 0..255 as
 * s15.16; s,t the S10.5 texel as s15.16). */
typedef struct BridgeVertex
{
    int32_t cx, cy, cz, cw;
    int32_t r, g, b, a;
    int32_t s, t;
    int16_t sv, tv;         /* the RSP's stored VTX_TC shorts, exact */
    /* Screen-space snapshot taken when the vertex was stored (the
     * microcode computes and stores screen coordinates and 1/w at
     * G_VTX time, under the viewport and perspNorm active THEN; the
     * triangle write only reloads the stored values). scr_valid set
     * means the fields below are authoritative and the emit path must
     * not recompute them from the clip-space position. */
    int     scr_valid;
    int32_t scr_x, scr_y, scr_z;    /* s15.16, x/y quantized to .25 */
    int64_t w_raw;                  /* (pn << 41) / cw, 0 for w <= 0 */
    int     rsp_ok;
    int32_t rsp_invw;
} BridgeVertex;

/* cull_mode: bit 0 = G_CULL_FRONT, bit 1 = G_CULL_BACK (the geometry-mode
 * cull bits shifted down). */
/* Compute the per-vertex screen-space values (fallback s15.16 screen
 * position, raw 1/w, and the RSP-exact transform results) from a
 * clip-space position under the given viewport. Used by the frontend at
 * vertex-store and clip-subdivision time to snapshot the values the way
 * the microcode does. */
void bridge_compute_screen(const BridgeVertex *v, const BridgeViewport *vp,
                           int32_t *x, int32_t *y, int32_t *z,
                           int64_t *w_raw, int *rsp_ok, int32_t *rsp_invw);

int bridge_add_triangle(int32_t *cmd,
                        const BridgeVertex *v0, const BridgeVertex *v1,
                        const BridgeVertex *v2,
                        const BridgeViewport *vp,
                        int textured, int z_buffered, int smooth,
                        int cull_mode,
                        int tile, int max_level, int tex_w, int tex_h);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_BRIDGE_H */
