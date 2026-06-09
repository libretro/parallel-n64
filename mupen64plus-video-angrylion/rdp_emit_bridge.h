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
    float vscale_x, vscale_y, vscale_z;
    float vtrans_x, vtrans_y, vtrans_z;
    unsigned int persp_norm; /* G_MW_PERSPNORM u16, applied to 1/w */
} BridgeViewport;

int bridge_add_triangle(int32_t *cmd,
                        const float *v0, const float *v1, const float *v2,
                        const BridgeViewport *vp,
                        int textured, int z_buffered,
                        int tile, int max_level, int tex_w, int tex_h);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_BRIDGE_H */
