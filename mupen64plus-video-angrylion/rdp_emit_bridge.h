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
} BridgeVertex;

int bridge_add_triangle(int32_t *cmd,
                        const BridgeVertex *v0, const BridgeVertex *v1,
                        const BridgeVertex *v2,
                        const BridgeViewport *vp,
                        int textured, int z_buffered, int smooth,
                        int tile, int max_level, int tex_w, int tex_h);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_BRIDGE_H */
