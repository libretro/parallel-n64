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
 * the difference. A triangle without a match, or one that went further
 * than anything moves in a frame, stands still, as do rectangles.
 *
 * Runs on the thread that processes RDP commands. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

typedef struct rdp_upscaler rdp_upscaler_t;

/* A triangle as triangle setup decodes it: x 16.15 (32768 a pixel),
 * x slopes per quarter line, y in quarter lines; attributes at the
 * major edge and their change per line down it, z among them. */
typedef struct rdp_upscaler_triangle
{
   int32_t xh, xm, xl;
   int32_t dxhdy, dxmdy, dxldy;
   int32_t yh, ym, yl;
   int32_t attr[8];    /* r g b a s t w z */
   int32_t attr_de[8];
   int32_t dzdx;
} rdp_upscaler_triangle_t;

#define RDP_UPSCALER_DEPTH_TEST     (1 << 0)
#define RDP_UPSCALER_DEPTH_WRITE    (1 << 1)
#define RDP_UPSCALER_RECT           (1 << 2)
/* Every pixel's depth is the primitive depth */
#define RDP_UPSCALER_DEPTH_CONSTANT (1 << 3)

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
   float motion_x, motion_y;
} rdp_upscaler_vertex_t;

/* A run of triangles under one depth state (RDP_UPSCALER_DEPTH_*) */
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

/* Folds @word into the signature @h; start from rdp_upscaler_hash_init() */
uint64_t rdp_upscaler_hash_init(void);
uint64_t rdp_upscaler_hash(uint64_t h, uint32_t word);

RETRO_END_DECLS

#endif
