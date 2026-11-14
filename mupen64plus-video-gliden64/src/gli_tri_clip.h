#ifndef GLI_TRI_CLIP_H
#define GLI_TRI_CLIP_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SPVertex;

/* The RSP clips a triangle reaching past the near plane or the guard band
 * into a polygon of new vertices, and the RDP interpolates its shade in
 * screen space. Clipped by the GPU instead, shade interpolated in screen
 * space comes out differently: across a vertex behind the eye it has no
 * screen position at all. */

/* Whether vertex @_v, in clip space, is outside what the RSP clips to */
u32 gli_tri_clip_vertex_outside(const struct SPVertex *_v);

/* The most triangles one triangle is clipped into */
#define GLI_TRI_CLIP_MAX 6

/* @_count indices (@_elements, or NULL for vertices in order) into
 * @_vertices, as triangles, clipped as the RSP clips them into @_out as a
 * list of triangles, at most GLI_TRI_CLIP_MAX times @_count vertices.
 * Screen space vertices (modify) pass as they are. Returns the vertices
 * written. */
u32 gli_tri_clip(const struct SPVertex *_vertices, const u16 *_elements, u32 _count,
	struct SPVertex *_out);

#ifdef __cplusplus
}
#endif

#endif
