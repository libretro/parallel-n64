#ifndef GLI_LLE_TRI_H
#define GLI_LLE_TRI_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SPVertex;

/* A corner of an RDP triangle as its edge walker has it: screen position
 * and the raw fixed point shade, depth, S, T and W there. */
typedef struct gli_lle_corner
{
	f32 x, y;
	s32 r, g, b, a;
	u32 z;
	s32 s, t, w;
} gli_lle_corner;

/* How the corners turn into vertices */
typedef struct gli_lle_mode
{
	int shade;    /* shade from the corners */
	int texture;  /* texture coordinates from the corners */
	int persp;    /* the texture divided by W */
	int zprim;    /* depth is @primZ, not the corners' */
	f32 primZ;
} gli_lle_mode;

/* The least W, in the RDP's s15.16, a vertex is drawn with */
#define GLI_LLE_MIN_W 0x100

/* The most vertices @_n strip corners turn into */
#define GLI_LLE_MAX_VERTICES(_n) (((_n) < 3 ? 0 : (_n) - 2) * 6)

/* The strip of @_n corners as vertices in @_out. The GPU interpolates
 * texture coordinates by perspective, from each vertex's W; where W
 * reaches zero or below inside the triangle, near the horizon, that is
 * no longer the RDP's S/W and T/W interpolated in screen space and the
 * texture warps across the whole triangle. Such a strip is cut, every
 * value along screen space as the RDP steps it, to where W is at least
 * GLI_LLE_MIN_W, and given as triangles. Returns the vertices written;
 * @_list says whether they are triangles rather than a strip. */
u32 gli_lle_vertices(const gli_lle_corner *_c, u32 _n, const gli_lle_mode *_mode,
	struct SPVertex *_out, int *_list);

#ifdef __cplusplus
}
#endif

#endif
