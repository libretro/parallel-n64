/* Copyright (c) 2026 The RetroArch team
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef RDP_TEMPORAL_UPSCALER_H
#define RDP_TEMPORAL_UPSCALER_H

/* The geometry side of a frontend temporal upscaler: the jitter each
 * frame is drawn under and the motion vectors of what it drew.
 *
 * The RDP gets screen-space triangles and no camera. Each triangle drawn
 * into a framebuffer the VI shows is kept, unjittered, and matched with
 * the triangle of the frame shown before that was drawn under the same
 * state with the same ordinal among those; the motion vector pass draws
 * the difference: where its corners went, or, for a triangle the RSP
 * cut at the edge of the screen, whose corners are not its vertices,
 * where its texture (or shade) says each pixel was. A triangle without
 * a match, or one that went further than anything moves in a frame,
 * stands still, as do rectangles.
 *
 * Runs on the thread that processes RDP commands. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

typedef struct rdp_upscaler rdp_upscaler_t;

/* A triangle as triangle setup decodes it: x 16.15 (32768 a pixel),
 * x slopes per quarter line, y in quarter lines; attributes at the
 * major edge, their change per line down it and per pixel across. */
typedef struct rdp_upscaler_triangle
{
   int32_t xh, xm, xl;
   int32_t dxhdy, dxmdy, dxldy;
   int32_t yh, ym, yl;
   int32_t attr[8];    /* r g b a s t w z */
   int32_t attr_de[8];
   int32_t attr_dx[8];
} rdp_upscaler_triangle_t;

#define RDP_UPSCALER_DEPTH_TEST     (1 << 0)
#define RDP_UPSCALER_DEPTH_WRITE    (1 << 1)
#define RDP_UPSCALER_RECT           (1 << 2)
/* Every pixel's depth is the primitive depth */
#define RDP_UPSCALER_DEPTH_CONSTANT (1 << 3)
/* Blended over what is behind it, writing no depth (particles, glows):
 * the upscaler takes this frame's color there over its history */
#define RDP_UPSCALER_TRANSLUCENT    (1 << 4)
/* Drawn on every pixel an edge touches (coverage antialiasing, fill and
 * copy cycles); without it, on the pixels whose top left corner is
 * inside */
#define RDP_UPSCALER_ANY_COVERAGE   (1 << 5)
/* Textured: s and t, over w with perspective, address its texels */
#define RDP_UPSCALER_TEXTURED       (1 << 6)
#define RDP_UPSCALER_PERSPECTIVE    (1 << 7)
/* Shaded: its colour varies over it */
#define RDP_UPSCALER_SHADED         (1 << 8)

/* The framebuffer the VI showed */
typedef struct rdp_upscaler_framebuffer
{
   uint32_t origin;    /* byte address of the first pixel shown */
   uint32_t stride;    /* pixels per line */
   unsigned size_log2; /* bytes per pixel, log2 */
   unsigned width;     /* native pixels shown */
   unsigned height;
} rdp_upscaler_framebuffer_t;

/* In render pixels: where the vertex is, its depth (0 near) and how far
 * it is from where it was the frame before */
typedef struct rdp_upscaler_vertex
{
   float x, y, z;
   /* how far it moved, in render pixels, from where it is to where it
    * was, where the planes do not say */
   float motion_x, motion_y;
   /* Two attributes and w of its primitive as planes (cx, cy, c) over
    * the pass's pixels from their centre, then how far the planes of
    * the primitive it was differ: a pixel was where those give its
    * attributes over w. All zero when its primitive has none to go by. */
   float plane[18];
} rdp_upscaler_vertex_t;

/* A run of triangles under one depth state (RDP_UPSCALER_DEPTH_*) and
 * translucency (RDP_UPSCALER_TRANSLUCENT) */
typedef struct rdp_upscaler_draw
{
   unsigned first;
   unsigned count;
   unsigned flags;
} rdp_upscaler_draw_t;

/* The motion vector pass of a frame, valid until the next resolve */
typedef struct rdp_upscaler_geometry
{
   const rdp_upscaler_vertex_t *vertices;
   const rdp_upscaler_draw_t *draws;
   /* farther than this, in render pixels, nothing is the same thing */
   float farthest;
   unsigned num_vertices;
   unsigned num_draws;
   /* Render pixels the frame's triangles were moved by, x right, y down */
   float jitter_x;
   float jitter_y;
   /* Nothing of the frames before applies */
   bool reset;
} rdp_upscaler_geometry_t;

rdp_upscaler_t *rdp_upscaler_new(void);
void rdp_upscaler_free(rdp_upscaler_t *u);

/* At each scanout. Triangles are jittered from the next frame on while
 * @enable is set; @jitter_phase frames make the sequence. */
void rdp_upscaler_configure(rdp_upscaler_t *u, bool enable,
      unsigned jitter_phase, bool reset);
bool rdp_upscaler_is_active(const rdp_upscaler_t *u);

/* @size_log2: bytes per pixel, log2; -1 for 4 and 8 bit images */
void rdp_upscaler_set_color_image(rdp_upscaler_t *u, uint32_t addr,
      uint32_t stride, int size_log2);

/* Before the triangle is drawn: keeps it and jitters @tri in place.
 * @signature tells triangles drawn alike apart from others
 * (rdp_upscaler_hash); @prim_z is in the attributes' z units. */
void rdp_upscaler_triangle(rdp_upscaler_t *u, rdp_upscaler_triangle_t *tri,
      uint64_t signature, unsigned flags, int32_t prim_z, unsigned scale);
/* Corners in 10.2 fixed point, as the command has them */
void rdp_upscaler_rectangle(rdp_upscaler_t *u, uint32_t xh, uint32_t yh,
      uint32_t xl, uint32_t yl);

/* At a scanout of @fb rendered at @scale times the native size */
void rdp_upscaler_resolve(rdp_upscaler_t *u,
      const rdp_upscaler_framebuffer_t *fb, unsigned scale,
      rdp_upscaler_geometry_t *out);

/* What the frontend's upscaler asks of a frame, in render pixels: the
 * width it wants, and the sizes it takes */
typedef struct rdp_upscaler_range
{
   unsigned want_width;
   unsigned min_width, min_height;
   unsigned max_width, max_height;
} rdp_upscaler_range_t;

/* The factor (1, 2, 4, 8) to draw frames of @w x @h native pixels at for
 * @range: the one of those fitting whose width is nearest the wanted
 * one, the larger on a tie; 0 when none fits. @current, chosen for
 * frames of @current_w x @current_h, stands while it fits, unless frames
 * of another size have a larger factor nearer the wanted width: a game
 * entered from a wide menu is not left at the factor the menu needed */
unsigned rdp_upscaler_factor(const rdp_upscaler_range_t *range,
      unsigned w, unsigned h, unsigned current,
      unsigned current_w, unsigned current_h);

/* Folds @word into the signature @h; start from rdp_upscaler_hash_init() */
uint64_t rdp_upscaler_hash_init(void);
uint64_t rdp_upscaler_hash(uint64_t h, uint32_t word);

RETRO_END_DECLS

#endif
