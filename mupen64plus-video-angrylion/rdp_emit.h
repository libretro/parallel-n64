/* rdp_emit.h -- RDP triangle-command encoder for the angrylion HLE path.
 * See rdp_emit.c. ISO C89 / MSVC-compatible. */
#ifndef RDP_EMIT_H
#define RDP_EMIT_H

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef signed   __int32 rdp_emit_int32_t;
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmitVertex
{
    float x, y, z, w;
    float r, g, b, a;
    float s, t;
} EmitVertex;

int emit_shaded_triangle(int32_t *ew, const EmitVertex *va,
                         const EmitVertex *vb, const EmitVertex *vc,
                         int tile, int max_level);
int emit_shaded_z_triangle(int32_t *ew, const EmitVertex *va,
                           const EmitVertex *vb, const EmitVertex *vc,
                           int tile, int max_level);
int emit_texshade_triangle(int32_t *ew,
                           const EmitVertex *va, const EmitVertex *vb,
                           const EmitVertex *vc, int tex_w, int tex_h,
                           int tile, int max_level);
int emit_texshade_z_triangle(int32_t *ew,
                             const EmitVertex *va, const EmitVertex *vb,
                             const EmitVertex *vc, int tex_w, int tex_h,
                             int tile, int max_level);

#ifdef __cplusplus
}
#endif

#endif /* RDP_EMIT_H */
