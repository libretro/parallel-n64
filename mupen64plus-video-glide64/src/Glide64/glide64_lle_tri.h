#ifndef GLIDE64_LLE_TRI_H
#define GLIDE64_LLE_TRI_H

#ifdef __cplusplus
extern "C" {
#endif

/* A point of an RDP triangle: screen position and the shade, depth, S, T
 * and W there, in the units of the RDP's fixed point values */
typedef struct glide64_lle_point
{
   double x, y;
   double r, g, b, a, z, s, t, w;
} glide64_lle_point;

/* The RDP triangle's values as planes in screen space: each is given at
 * the major edge's x (@x0) on the triangle's first scanline (@y0), with
 * its step per pixel along x and per scanline along the major edge, whose
 * x moves @dxhdy per scanline. In the order r, g, b, a, z, s, t, w. */
typedef struct glide64_lle_plane
{
   double x0, y0, dxhdy;
   double v[8], dvdx[8], dvde[8];
} glide64_lle_plane;

/* The least W, in the RDP's s15.16, a point is drawn with */
#define GLIDE64_LLE_MIN_W 0x100

/* The most points @_n strip corners turn into */
#define GLIDE64_LLE_MAX_POINTS(_n) (((_n) < 3 ? 0 : (_n) - 2) * 6)

/* @_p at screen point @_x, @_y, every value from @_pl's planes */
void glide64_lle_point_at(const glide64_lle_plane *_pl, double _x, double _y,
      glide64_lle_point *_p);

/* The strip of @_n corners, as the points to draw in @_out. The GPU takes
 * texture coordinates by perspective from each vertex's W; where W
 * reaches zero or below inside the triangle, near the horizon, that is no
 * longer the RDP's S/W and T/W and the texture warps across the triangle.
 * With @_persp such a strip is cut, every value along screen space as the
 * RDP steps it, to where W is at least GLIDE64_LLE_MIN_W, and given as
 * triangles. Returns the points written; @_list says whether they are
 * triangles rather than the strip. */
unsigned glide64_lle_cut(const glide64_lle_point *_c, unsigned _n, int _persp,
      glide64_lle_point *_out, int *_list);

#ifdef __cplusplus
}
#endif

#endif
