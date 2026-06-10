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

/* One projected vertex in the RSP's native fixed-point forms (no float). The
 * RSP/RDP are integer-only. x,y are s15.16 screen coordinates; z is s15.16
 * screen depth (0..0x3ffff range in the integer part path); w is the RDP
 * inverse-w coefficient (perspNorm-scaled 1/w); r,g,b,a are 0..255 color held
 * as s15.16; s,t are the S10.5 texel coordinate held as s15.16. */
typedef struct EmitVertex
{
    int32_t x, y, z, w;
    int32_t r, g, b, a;
    int32_t s, t;
    /* Filled when the RSP-exact vertex transform was in-domain: the 10.2
     * screen position is then exactly the microcode's, and rsp_invw is the
     * VTX_INV_W value its triangle write would read back. */
    int32_t rsp_invw;
    int rsp_ok;
} EmitVertex;

/* Optional wide-basis plane override for clip fragments: gradients are solved
 * over these three vertices (the unclipped parent triangle) instead of the
 * fragment's own sliver basis. Pass NULLs to clear. */

int emit_shaded_triangle(int32_t *ew, const EmitVertex *va,
                         const EmitVertex *vb, const EmitVertex *vc,
                         int tile, int max_level);
int emit_shaded_z_triangle(int32_t *ew, const EmitVertex *va,
                           const EmitVertex *vb, const EmitVertex *vc,
                           int tile, int max_level);
void emit_set_st_bias(int32_t bias_s, int32_t bias_t);
void emit_get_st_bias(int32_t *bias_s, int32_t *bias_t);

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
