/* test_temporal_upscaler -- parallel-RDP's temporal upscaler geometry.
 *
 * Drives rdp_upscaler (parallel-rdp/temporal_upscaler.c) the way the
 * command processor does: color images, triangles as triangle setup
 * decodes them, rectangles and scanouts, and checks what it does to the
 * triangles (the jitter) and the motion vector pass it builds from them:
 *
 * - only framebuffers the VI shows are jittered and kept, and an image
 *   laid out like one (a z-buffer written as a color image) is jittered
 *   with the frame;
 * - the jitter moves the edges and the attributes anchored on them;
 * - a triangle that moved gets the motion back to where it was, whichever
 *   order its vertices come in and when the command cut it short;
 * - depth comes from the z plane, or the primitive depth, and the depth
 *   state splits the draws;
 * - a frame shown twice stands still, a jump too far stands still, and
 *   turning the upscaler on or a reset start the history over.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "temporal_upscaler.h"

static int s_failures;

#define CHECK(cond, what) \
   do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define FB      0x100000u
#define FB2     0x180000u
#define OFFSCR  0x200000u
#define STRIDE  320u
#define SCALE   2u

static int near(double a, double b, double eps)
{
   return fabs(a - b) <= eps;
}

/* A triangle with corners (x0,y0) top, (x1,y1) mid, (x2,y2) bottom (y in
 * pixels, y0 <= y1 <= y2), as the RDP gets it: the major edge from top
 * to bottom, the mid edge top to mid, the low edge mid to bottom */
static void make_triangle(rdp_upscaler_triangle_t *t,
      double x0, double y0, double x1, double y1, double x2, double y2)
{
   memset(t, 0, sizeof(*t));
   t->yh    = (int32_t)(y0 * 4.0);
   t->ym    = (int32_t)(y1 * 4.0);
   t->yl    = (int32_t)(y2 * 4.0);
   t->dxhdy = (int32_t)floor((x2 - x0) / ((y2 - y0) * 4.0) * 32768.0 + 0.5);
   t->dxmdy = y1 > y0 ? (int32_t)floor((x1 - x0) / ((y1 - y0) * 4.0) * 32768.0 + 0.5) : 0;
   t->dxldy = y2 > y1 ? (int32_t)floor((x2 - x1) / ((y2 - y1) * 4.0) * 32768.0 + 0.5) : 0;
   /* x at the top, from y = yh & ~3 */
   t->xh    = (int32_t)floor(x0 * 32768.0 + 0.5) - (t->yh - (t->yh & ~3)) * t->dxhdy;
   t->xm    = (int32_t)floor(x0 * 32768.0 + 0.5) - (t->yh - (t->yh & ~3)) * t->dxmdy;
   t->xl    = (int32_t)floor(x1 * 32768.0 + 0.5);
}

static void scanout(rdp_upscaler_t *u, uint32_t origin, rdp_upscaler_geometry_t *g)
{
   rdp_upscaler_framebuffer_t fb;
   fb.origin    = origin;
   fb.stride    = STRIDE;
   fb.size_log2 = 1;
   fb.width     = 320;
   fb.height    = 240;
   rdp_upscaler_resolve(u, &fb, SCALE, g);
}

/* The motion at render pixel (x, y), from the triangle of @g covering it */
static int motion_at(const rdp_upscaler_geometry_t *g, double x, double y,
      double *mx, double *my, double *z)
{
   unsigned i;
   int found = 0;
   for (i = 0; i + 2 < g->num_vertices; i += 3)
   {
      const rdp_upscaler_vertex_t *a = &g->vertices[i];
      const rdp_upscaler_vertex_t *b = &g->vertices[i + 1];
      const rdp_upscaler_vertex_t *c = &g->vertices[i + 2];
      double det = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
      double l1, l2, l0;
      if (fabs(det) < 1e-9)
         continue;
      l1 = ((x - a->x) * (c->y - a->y) - (c->x - a->x) * (y - a->y)) / det;
      l2 = ((b->x - a->x) * (y - a->y) - (x - a->x) * (b->y - a->y)) / det;
      l0 = 1.0 - l1 - l2;
      if (l0 < -1e-6 || l1 < -1e-6 || l2 < -1e-6)
         continue;
      /* (later triangles are drawn over earlier ones) */
      *mx   = l0 * a->motion_x + l1 * b->motion_x + l2 * c->motion_x;
      *my   = l0 * a->motion_y + l1 * b->motion_y + l2 * c->motion_y;
      *z    = l0 * a->z + l1 * b->z + l2 * c->z;
      found = 1;
   }
   return found;
}

static uint64_t sig(uint32_t a)
{
   return rdp_upscaler_hash(rdp_upscaler_hash_init(), a);
}

static void test_inactive_and_offscreen(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_triangle_t t, before;
   rdp_upscaler_geometry_t g;

   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   before = t;
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &before, sizeof(t)), "nothing changes while the upscaler is off");

   rdp_upscaler_configure(u, true, 8, false);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &before, sizeof(t)), "nothing before the VI showed the framebuffer");

   scanout(u, FB, &g);
   CHECK(g.reset, "the first frame resets the history");
   CHECK(g.num_vertices == 0, "nothing kept before the VI showed the framebuffer");

   rdp_upscaler_set_color_image(u, OFFSCR, 64, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &before, sizeof(t)), "a texture rendered to is not jittered");
   rdp_upscaler_set_color_image(u, FB, STRIDE, -1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &before, sizeof(t)), "an 8-bit image is not jittered");

   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(memcmp(&t, &before, sizeof(t)) != 0, "the shown framebuffer is jittered");
   scanout(u, FB, &g);
   CHECK(!g.reset, "the next frame follows");
   CHECK(g.num_vertices == 12, "the triangle is drawn as its outline's fan");
   rdp_upscaler_free(u);
}

/* An image laid out like the framebuffer (a z-buffer the game writes as
 * a color image to mask its shadows, as Pilotwings 64 does) is drawn in
 * the frame's screen space: its triangles move by the frame's jitter,
 * before the frame's first triangle and after, so that the frame's
 * triangles meet them through the depth test where they were drawn. */
static void test_screen_image(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_triangle_t t, before, frame, side;
   rdp_upscaler_geometry_t g;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);
   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   before = t;

   /* a side image drawn before the frame's first triangle */
   rdp_upscaler_set_color_image(u, OFFSCR, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(memcmp(&t, &before, sizeof(t)) != 0, "a side image laid out like the frame is jittered");
   frame = before;
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &frame, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &frame, sizeof(t)), "the side image moves by the jitter the frame then takes");

   /* and during the frame */
   side = before;
   rdp_upscaler_set_color_image(u, OFFSCR, STRIDE, 1);
   rdp_upscaler_triangle(u, &side, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&side, &frame, sizeof(side)), "the side image moves by the frame's jitter");
   scanout(u, FB, &g);
   CHECK(g.num_vertices == 12, "the side image's triangles are not kept");

   /* the next frame's jitter differs, and the side image follows it */
   t = before;
   rdp_upscaler_set_color_image(u, OFFSCR, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(memcmp(&t, &frame, sizeof(t)) != 0, "the side image moves on with the frames");
   frame = before;
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &frame, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &frame, sizeof(t)), "the side image moves by the next frame's jitter");

   /* a texture, laid out otherwise, is not */
   t = before;
   rdp_upscaler_set_color_image(u, OFFSCR, 64, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   CHECK(!memcmp(&t, &before, sizeof(t)), "a texture rendered to is not jittered");
   rdp_upscaler_free(u);
}

static void test_jitter(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_triangle_t t, before;
   rdp_upscaler_geometry_t g;
   double dx, dy;
   unsigned i;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);

   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   for (i = 0; i < 8; i++)
   {
      t.attr[i]    = 0x10000 * (int32_t)(i + 1);
      t.attr_de[i] = 0x8000 * (int32_t)(i + 1);
   }
   before = t;
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   scanout(u, FB, &g);
   CHECK(g.jitter_x != 0.0f || g.jitter_y != 0.0f, "a frame is jittered");
   CHECK(g.jitter_x >= -0.5f && g.jitter_x <= 0.5f && g.jitter_y >= -0.5f && g.jitter_y <= 0.5f,
         "the jitter is within half a pixel");
   /* Halton (2, 3), the first point after the origin */
   CHECK(near(g.jitter_x, 0.0, 1e-6) && near(g.jitter_y, 1.0 / 3.0 - 0.5, 1e-6),
         "the jitter starts the Halton (2, 3) sequence");

   dx = g.jitter_x / SCALE;
   dy = g.jitter_y / SCALE;
   CHECK(near(t.xh - before.xh, dx * 32768.0 - 4.0 * dy * before.dxhdy, 1.0), "the major edge moves");
   CHECK(near(t.xm - before.xm, dx * 32768.0 - 4.0 * dy * before.dxmdy, 1.0), "the mid edge moves");
   CHECK(near(t.xl - before.xl, dx * 32768.0 - 4.0 * dy * before.dxldy, 1.0), "the low edge moves");
   CHECK(t.yh == before.yh && t.ym == before.ym && t.yl == before.yl, "the rows stay");
   for (i = 0; i < 8; i++)
      CHECK(near(t.attr[i] - before.attr[i], -dy * before.attr_de[i], 1.0),
            "the attributes follow the major edge");

   /* the vertices drawn are where the jittered triangle is */
   CHECK(near(g.vertices[0].x, (double)100 * SCALE + g.jitter_x, 1.0)
         && near(g.vertices[0].y, (double)50 * SCALE + g.jitter_y, 1.0),
         "the motion vector pass draws the triangle where it was drawn");
   rdp_upscaler_free(u);
}

/* Two frames of one triangle, the second moved; the motion where the
 * second has (px, py) */
static void moved(rdp_upscaler_t *u, const double *a, const double *b, double px, double py,
      double *mx, double *my, rdp_upscaler_geometry_t *g)
{
   rdp_upscaler_triangle_t t;
   double z;
   make_triangle(&t, a[0], a[1], a[2], a[3], a[4], a[5]);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(7), 0, 0, SCALE);
   scanout(u, FB, g);
   make_triangle(&t, b[0], b[1], b[2], b[3], b[4], b[5]);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(7), 0, 0, SCALE);
   scanout(u, FB, g);
   *mx = *my = 1e9;
   motion_at(g, px * SCALE + g->jitter_x, py * SCALE + g->jitter_y, mx, my, &z);
}

/* @rel about (100, 100), turned by (cos, sin), sorted by y */
static void turned(const double rel[3][2], double c, double sn, double *out)
{
   double v[3][2], t;
   unsigned i, j;
   for (i = 0; i < 3; i++)
   {
      v[i][0] = 100.0 + rel[i][0] * c - rel[i][1] * sn;
      v[i][1] = 100.0 + rel[i][0] * sn + rel[i][1] * c;
   }
   for (i = 0; i < 3; i++)
      for (j = i + 1; j < 3; j++)
         if (v[j][1] < v[i][1])
         {
            t = v[i][0]; v[i][0] = v[j][0]; v[j][0] = t;
            t = v[i][1]; v[i][1] = v[j][1]; v[j][1] = t;
         }
   for (i = 0; i < 3; i++)
   {
      out[i * 2]     = v[i][0];
      out[i * 2 + 1] = v[i][1];
   }
}

static void test_motion(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   double mx, my;
   static const double a[6] = { 100, 50, 60, 100, 150, 150 };
   static const double b[6] = { 110, 54, 70, 104, 160, 154 };
   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);

   moved(u, a, b, 110, 100, &mx, &my, &g);
   CHECK(near(mx, -10.0 * SCALE, 0.05) && near(my, -4.0 * SCALE, 0.05),
         "a triangle that moved points back to where it was");

   moved(u, b, b, 110, 100, &mx, &my, &g);
   CHECK(near(mx, 0.0, 0.05) && near(my, 0.0, 0.05), "a triangle that stayed stands still");

   /* turned 15 degrees about (100, 100), which reorders two of its
    * vertices by y */
   {
      static const double rel[3][2] = { { 0, -40 }, { -40, 0 }, { 40, 2 } };
      double r0[6], r1[6], c = cos(-0.2617993878), sn = sin(-0.2617993878);
      double px, py;
      turned(rel, 1.0, 0.0, r0);
      turned(rel, c, sn, r1);
      CHECK(r0[3] < r1[3] - 5.0 || r0[3] > r1[3] + 5.0, "(the turn reorders the vertices)");
      /* where (100, 85) of the turned frame was */
      px = 100.0 - 15.0 * sn;
      py = 100.0 - 15.0 * c;
      moved(u, r0, r1, 100, 85, &mx, &my, &g);
      /* (the corners' y are cut to quarter lines) */
      CHECK(near(mx, (px - 100.0) * SCALE, 0.4) && near(my, (py - 85.0) * SCALE, 0.4),
            "a turned triangle's vertices are paired up whatever their order");
   }

   /* a jump further than anything moves in a frame */
   {
      static const double far_[6] = { 300, 50, 260, 100, 350, 150 };
      static const double a2[6]   = { 10, 50, -30, 100, 60, 150 };
      moved(u, a2, far_, 300, 100, &mx, &my, &g);
      CHECK(near(mx, 0.0, 0.05) && near(my, 0.0, 0.05), "a jump too far stands still");
   }
   rdp_upscaler_free(u);
}

/* A triangle whose top is above the scissor: the command starts at the
 * cut, the mid edge's x there differs from the major edge's */
static void test_cut_short(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   rdp_upscaler_triangle_t t, full;
   double mx, my, z;
   int k;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);

   for (k = 0; k < 2; k++)
   {
      double sh = k * 6.0, sv = k * 4.0;
      /* corners (100, -40), (40, 60), (160, 140), cut at y = 0, then
       * moved by (6, 4) */
      make_triangle(&full, 100 + sh, -40 + sv, 40 + sh, 60 + sv, 160 + sh, 140 + sv);
      t       = full;
      t.yh    = 0;
      t.xh    = full.xh + (0 - (full.yh & ~3)) * full.dxhdy;
      t.xm    = full.xm + (0 - (full.yh & ~3)) * full.dxmdy;
      rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
      rdp_upscaler_triangle(u, &t, sig(9), 0, 0, SCALE);
      scanout(u, FB, &g);
   }
   CHECK(motion_at(&g, 106.0 * SCALE + g.jitter_x, 1.0 * SCALE + g.jitter_y, &mx, &my, &z),
         "the outline reaches the cut");
   CHECK(near(mx, -6.0 * SCALE, 0.05) && near(my, -4.0 * SCALE, 0.05),
         "a triangle cut short moves as the whole triangle does");
   rdp_upscaler_free(u);
}

static void test_depth_and_draws(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   rdp_upscaler_triangle_t t;
   double mx, my, z;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);

   rdp_upscaler_rectangle(u, 0, 0, 319 * 4, 239 * 4);
   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   /* z 0.25 at the top, half more per line down */
   t.attr[7]    = 0x20000000;
   t.attr_de[7] = 0x100000;
   rdp_upscaler_triangle(u, &t, sig(1), RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE, 0, SCALE);
   make_triangle(&t, 200, 50, 160, 100, 250, 150);
   rdp_upscaler_triangle(u, &t, sig(2), RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_CONSTANT,
         0x40000000, SCALE);
   rdp_upscaler_triangle(u, &t, sig(3), RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_CONSTANT,
         0x40000000, SCALE);
   scanout(u, FB, &g);

   CHECK(g.num_draws == 3, "the depth state splits the draws, alike ones run together");
   CHECK(g.num_draws == 3 && g.draws[0].flags == 0 && g.draws[0].count == 6,
         "a rectangle is two triangles without depth");
   {
      /* (with nothing in its planes: they are not left as the vertex
       * memory was) */
      static const float none[18];
      unsigned i;
      int clean = 1;
      for (i = 0; i < 6; i++)
         clean &= !memcmp(g.vertices[i].plane, none, sizeof(none));
      CHECK(clean, "a rectangle's planes say nothing");
   }
   CHECK(g.num_draws == 3 && g.draws[1].flags == (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE),
         "a z-buffered triangle tests and writes depth");
   CHECK(g.num_draws == 3 && g.draws[2].flags == RDP_UPSCALER_DEPTH_TEST && g.draws[2].count == 24,
         "a decal tests depth only");

   CHECK(motion_at(&g, 100.0 * SCALE + g.jitter_x + 0.5, 50.0 * SCALE + g.jitter_y + 1.0, &mx, &my, &z)
         && near(z, 0.25, 0.01), "the depth is the z plane's");
   CHECK(motion_at(&g, 100.0 * SCALE + g.jitter_x, 100.0 * SCALE + g.jitter_y, &mx, &my, &z)
         && near(z, 0.25 + 50.0 * 0x100000 / 2147483648.0, 0.01), "the depth follows the z plane down");
   CHECK(motion_at(&g, 200.0 * SCALE + g.jitter_x, 100.0 * SCALE + g.jitter_y, &mx, &my, &z)
         && near(z, 0.5, 0.001), "the primitive depth is every pixel's");
   rdp_upscaler_free(u);
}

/* A triangle blended over what is behind it without writing depth runs
 * apart, marked translucent, alike ones together */
static void test_translucent(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   rdp_upscaler_triangle_t t;
   const unsigned zbuf  = RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE;
   const unsigned glow  = RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_TRANSLUCENT;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);

   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   rdp_upscaler_triangle(u, &t, sig(1), zbuf, 0, SCALE);
   make_triangle(&t, 200, 50, 160, 100, 250, 150);
   rdp_upscaler_triangle(u, &t, sig(2), glow, 0, SCALE);
   make_triangle(&t, 210, 60, 170, 110, 260, 160);
   rdp_upscaler_triangle(u, &t, sig(2), glow, 0, SCALE);
   make_triangle(&t, 100, 150, 60, 200, 150, 230);
   rdp_upscaler_triangle(u, &t, sig(3), zbuf, 0, SCALE);
   scanout(u, FB, &g);

   CHECK(g.num_draws == 3, "the translucent triangles run apart, together");
   CHECK(g.num_draws == 3 && g.draws[0].flags == zbuf && g.draws[2].flags == zbuf,
         "the z-buffered triangles are not translucent");
   CHECK(g.num_draws == 3 && g.draws[1].flags == glow
         && g.draws[1].count == g.draws[0].count * 2,
         "the translucent ones are marked so, both in the one run");
   rdp_upscaler_free(u);
}

/* The pass covers the pixels the RDP fills: with coverage antialiasing
 * every pixel an edge touches, without only those whose top left corner
 * is inside (sampled at their centres, half a pixel on) */
static void test_sample_rule(void)
{
   unsigned pass;
   for (pass = 0; pass < 2; pass++)
   {
      rdp_upscaler_t *u = rdp_upscaler_new();
      rdp_upscaler_geometry_t g;
      rdp_upscaler_triangle_t t;
      double mx, my, z, e;
      unsigned flags = RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE
         | (pass ? RDP_UPSCALER_ANY_COVERAGE : 0);

      rdp_upscaler_configure(u, true, 8, false);
      scanout(u, FB, &g);
      rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
      /* its major edge upright at x = 100, on the left */
      make_triangle(&t, 100, 50, 180, 100, 100, 150);
      rdp_upscaler_triangle(u, &t, sig(1), flags, 0, SCALE);
      scanout(u, FB, &g);
      e = 100.0 * SCALE + g.jitter_x;

      if (pass)
         CHECK(motion_at(&g, e - 0.4, 100.0 * SCALE + g.jitter_y, &mx, &my, &z),
               "with coverage antialiasing, the pixel the edge grazes is covered");
      else
      {
         CHECK(!motion_at(&g, e + 0.4, 100.0 * SCALE + g.jitter_y + 0.5, &mx, &my, &z),
               "without, a pixel whose corner is left of the edge is not covered");
         CHECK(motion_at(&g, e + 0.6, 100.0 * SCALE + g.jitter_y + 0.5, &mx, &my, &z),
               "without, a pixel whose corner is right of the edge is covered");
      }
      rdp_upscaler_free(u);
   }
}

/* What the motion pass's fragment shader makes of a vertex's planes at
 * render pixel (x, y): where the pixel was, from it; 0 when the planes
 * say nothing */
static int plane_motion_at(const rdp_upscaler_vertex_t *v, double x, double y,
      double *mx, double *my)
{
   const float *a0 = v->plane, *a1 = v->plane + 3, *aw = v->plane + 6;
   const float *d0 = v->plane + 9, *d1 = v->plane + 12, *dw = v->plane + 15;
   double px = x - 160.0 * SCALE, py = y - 120.0 * SCALE;
   double w, u, vv, l0x, l0y, l1x, l1y, r0, r1, det;
   if (!aw[0] && !aw[1] && !aw[2])
      return 0;
   w   = aw[0] * px + aw[1] * py + aw[2];
   u   = (a0[0] * px + a0[1] * py + a0[2]) / w;
   vv  = (a1[0] * px + a1[1] * py + a1[2]) / w;
   l0x = a0[0] + d0[0] - u * (aw[0] + dw[0]);
   l0y = a0[1] + d0[1] - u * (aw[1] + dw[1]);
   l1x = a1[0] + d1[0] - vv * (aw[0] + dw[0]);
   l1y = a1[1] + d1[1] - vv * (aw[1] + dw[1]);
   r0  = (d0[0] - u * dw[0]) * px + (d0[1] - u * dw[1]) * py + (d0[2] - u * dw[2]);
   r1  = (d1[0] - vv * dw[0]) * px + (d1[1] - vv * dw[1]) * py + (d1[2] - vv * dw[2]);
   det = l0x * l1y - l0y * l1x;
   *mx = (r1 * l0y - r0 * l1y) / det;
   *my = (r0 * l1x - r1 * l0x) / det;
   return 1;
}

/* The vertex of @g drawn over render pixel (x, y) */
static const rdp_upscaler_vertex_t *vertex_at(const rdp_upscaler_geometry_t *g,
      double x, double y)
{
   unsigned i;
   const rdp_upscaler_vertex_t *found = NULL;
   for (i = 0; i + 2 < g->num_vertices; i += 3)
   {
      const rdp_upscaler_vertex_t *a = &g->vertices[i];
      const rdp_upscaler_vertex_t *b = &g->vertices[i + 1];
      const rdp_upscaler_vertex_t *c = &g->vertices[i + 2];
      double det = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
      double l1, l2;
      if (fabs(det) < 1e-9)
         continue;
      l1 = ((x - a->x) * (c->y - a->y) - (c->x - a->x) * (y - a->y)) / det;
      l2 = ((b->x - a->x) * (y - a->y) - (x - a->x) * (b->y - a->y)) / det;
      if (l1 < -1e-6 || l2 < -1e-6 || 1.0 - l1 - l2 < -1e-6)
         continue;
      found = a;
   }
   return found;
}

/* The attributes s, t, w (and r, g with @shade) as planes over the
 * screen, anchored where the triangle's major edge starts: s = s0 + sx x
 * + sy y, the texels of a surface the screen shows moved by (dx, dy) */
static void set_planes(rdp_upscaler_triangle_t *t, double dx, double dy, int shade)
{
   static const double c[5][3] = {
      { 2000.0, 50.0, 20.0 }, { 3000.0, -10.0, 60.0 }, { 8000.0, 1.0, 2.0 },
      { 1500.0, 30.0, 5.0 }, { 2500.0, -5.0, 40.0 } };
   /* where the attributes are anchored: x at the top of the major edge */
   double line = floor(t->yh / 4.0);
   double x0   = (t->xh + (line * 4.0 - (t->yh & ~3)) * t->dxhdy) / 32768.0;
   double kh   = t->dxhdy * 4.0 / 32768.0;
   unsigned i;
   for (i = 0; i < 5; i++)
   {
      unsigned a = i < 3 ? 4 + i : i - 3;
      double s0  = c[i][0] + c[i][1] * (x0 - dx) + c[i][2] * (line - dy);
      if (!shade && i >= 3)
         break;
      t->attr[a]    = (int32_t)floor(s0 * 65536.0 + 0.5);
      t->attr_dx[a] = (int32_t)floor(c[i][1] * 65536.0 + 0.5);
      /* (per line down the major edge, which slants by kh a line) */
      t->attr_de[a] = (int32_t)floor((c[i][2] + c[i][1] * kh) * 65536.0 + 0.5);
   }
}

/* A triangle cut off far beyond the screen: its corner there jumps
 * about as the RSP clips it, while what is on screen stands still or
 * moves as its texture (or its shade) says, pixel by pixel */
static void test_planes(void)
{
   static const double a[6] = { 100, 20, -500, 300, 300, 200 };
   static const double b[6] = { 100, 20, -470, 300, 300, 200 };
   static const double on[6] = { 100, 20, 40, 200, 300, 200 };
   static const double on2[6] = { 104, 20, 44, 200, 304, 200 };
   const unsigned tex = RDP_UPSCALER_TEXTURED | RDP_UPSCALER_PERSPECTIVE;
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   rdp_upscaler_triangle_t t;
   const rdp_upscaler_vertex_t *v;
   double mx, my, px, py;
   int k;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);

   for (k = 0; k < 6; k++)
   {
      /* 0: the texture stood still; 1: it moved by (3, -2); 2: the
       * shade did, untextured; 3: the texture tells nothing (t does
       * not vary, and without perspective neither does t / w), the
       * corners do; 4: the texture moved under a triangle drawn again
       * exactly where it was; 5: all three corners on screen, the
       * triangle moved by (4, 0) and its texture by (3, -2): the
       * corners tell */
      unsigned flags = k == 2 ? RDP_UPSCALER_SHADED : k == 3 ? RDP_UPSCALER_TEXTURED : tex;
      double dx = k == 1 || k == 2 || k >= 4 ? 3.0 : 0.0, dy = dx ? -2.0 : 0.0;
      const double *first = k == 5 ? on : a;
      const double *second = k == 4 ? a : k == 5 ? on2 : b;
      make_triangle(&t, first[0], first[1], first[2], first[3], first[4], first[5]);
      set_planes(&t, 0.0, 0.0, k == 2);
      if (k == 3)
         t.attr_dx[5] = t.attr_de[5] = 0;
      rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
      rdp_upscaler_triangle(u, &t, sig(11 + k), flags, 0, SCALE);
      scanout(u, FB, &g);
      make_triangle(&t, second[0], second[1], second[2], second[3], second[4], second[5]);
      set_planes(&t, dx, dy, k == 2);
      if (k == 3)
         t.attr_dx[5] = t.attr_de[5] = 0;
      rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
      rdp_upscaler_triangle(u, &t, sig(11 + k), flags, 0, SCALE);
      scanout(u, FB, &g);

      px = 120.0 * SCALE + g.jitter_x;
      py = 150.0 * SCALE + g.jitter_y;
      v  = vertex_at(&g, px, py);
      CHECK(v != NULL, "the triangle covers the pixel");
      if (!v)
         continue;
      switch (k)
      {
         case 0:
            CHECK(!plane_motion_at(v, px, py, &mx, &my) && v->motion_x == 0.0f && v->motion_y == 0.0f,
                  "a clipped triangle whose texture stood still stands still, however its far corner jumped");
            break;
         case 1:
         case 2:
            CHECK(plane_motion_at(v, px, py, &mx, &my)
                  && near(mx, -dx * SCALE, 0.05) && near(my, -dy * SCALE, 0.05),
                  k == 1 ? "the pixel points back to where its texel was"
                         : "the pixel points back to where its shade was");
            CHECK(plane_motion_at(v, px + 50.0, py - 30.0, &mx, &my)
                  && near(mx, -dx * SCALE, 0.05) && near(my, -dy * SCALE, 0.05),
                  "so does every other pixel of the triangle");
            break;
         case 3:
            CHECK(!plane_motion_at(v, px, py, &mx, &my) && (v->motion_x != 0.0f || v->motion_y != 0.0f),
                  "with nothing in the attributes to go by, the corners tell");
            break;
         case 4:
            CHECK(!plane_motion_at(v, px, py, &mx, &my) && v->motion_x == 0.0f && v->motion_y == 0.0f,
                  "drawn again where it was, it stands still whatever its attributes did");
            break;
         case 5:
            CHECK(!plane_motion_at(v, px, py, &mx, &my)
                  && near(v->motion_x, -4.0 * SCALE, 0.05) && near(v->motion_y, 0.0, 0.05),
                  "with its corners on screen, they tell, whatever its texture did");
            break;
      }
   }
   rdp_upscaler_free(u);
}

static void test_history(void)
{
   rdp_upscaler_t *u = rdp_upscaler_new();
   rdp_upscaler_geometry_t g;
   rdp_upscaler_triangle_t t;
   double mx, my, z;

   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB, &g);

   /* double buffered: drawn into FB2 while FB is shown, and back */
   make_triangle(&t, 100, 50, 60, 100, 150, 150);
   rdp_upscaler_set_color_image(u, FB2, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   scanout(u, FB, &g);
   CHECK(g.num_vertices == 0, "a framebuffer the VI never showed is not jittered");
   scanout(u, FB2, &g);
   make_triangle(&t, 104, 50, 64, 100, 154, 150);
   rdp_upscaler_set_color_image(u, FB, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   make_triangle(&t, 108, 50, 68, 100, 158, 150);
   rdp_upscaler_set_color_image(u, FB2, STRIDE, 1);
   rdp_upscaler_triangle(u, &t, sig(1), 0, 0, SCALE);
   scanout(u, FB, &g);
   CHECK(motion_at(&g, 104.0 * SCALE + g.jitter_x, 100.0 * SCALE + g.jitter_y, &mx, &my, &z)
         && near(mx, 0.0, 0.05), "the first frame shown has nothing to come from");
   scanout(u, FB2, &g);
   CHECK(motion_at(&g, 108.0 * SCALE + g.jitter_x, 100.0 * SCALE + g.jitter_y, &mx, &my, &z)
         && near(mx, -4.0 * SCALE, 0.05), "double buffered frames move from the frame shown before");
   scanout(u, FB2, &g);
   CHECK(motion_at(&g, 108.0 * SCALE + g.jitter_x, 100.0 * SCALE + g.jitter_y, &mx, &my, &z)
         && near(mx, 0.0, 0.05), "a frame shown again stands still");

   rdp_upscaler_configure(u, true, 8, true);
   scanout(u, FB2, &g);
   CHECK(g.reset && g.num_vertices == 0, "a reset forgets the frames before");
   rdp_upscaler_configure(u, false, 8, false);
   rdp_upscaler_configure(u, true, 8, false);
   scanout(u, FB2, &g);
   CHECK(g.reset, "turning the upscaler on starts over");
   rdp_upscaler_free(u);
}

/* The factor for a frame: nearest the wanted width within the range, kept
 * while it fits, unless a frame of another size has a larger one nearer
 * (a menu drawn wide must not leave the game at the factor the menu
 * needed); a smaller one, though nearer, is not taken over a factor
 * that still fits, and nor is a processor remade for it */
static void test_factor(void)
{
   rdp_upscaler_range_t r;
   r.want_width = 2880;
   r.min_width  = 960;
   r.min_height = 720;
   r.max_width  = 2880;
   r.max_height = 2160;
   CHECK(rdp_upscaler_factor(&r, 320, 240, 0, 0, 0) == 8, "320x240: 8x, nearest 2880 wide");
   CHECK(rdp_upscaler_factor(&r, 440, 325, 0, 0, 0) == 4, "440x325: 8x is too wide, 4x");
   CHECK(rdp_upscaler_factor(&r, 440, 325, 4, 440, 325) == 4, "the same frames keep their factor");
   CHECK(rdp_upscaler_factor(&r, 320, 237, 4, 440, 325) == 8, "frames of another size take the larger factor nearer");
   CHECK(rdp_upscaler_factor(&r, 320, 237, 8, 320, 237) == 8, "and keep it");
   CHECK(rdp_upscaler_factor(&r, 440, 325, 8, 320, 237) == 4, "the wide menu again: 8x no longer fits, 4x");
   CHECK(rdp_upscaler_factor(&r, 100, 75, 0, 0, 0) == 0, "nothing fits: 0");
   CHECK(rdp_upscaler_factor(&r, 100, 75, 8, 320, 240) == 0, "a kept factor that no longer fits is not kept");
   r.want_width = 1600;
   CHECK(rdp_upscaler_factor(&r, 320, 240, 0, 0, 0) == 4, "the nearer of 1280 and 2560 to 1600");
   r.want_width = 1920;
   CHECK(rdp_upscaler_factor(&r, 320, 240, 0, 0, 0) == 8, "the larger on a tie");
   r.want_width = 640;
   r.min_width  = 160;
   r.min_height = 120;
   r.max_width  = 1280;
   r.max_height = 960;
   CHECK(rdp_upscaler_factor(&r, 320, 240, 0, 0, 0) == 2, "320x240 for 640 wide: 2x is 640 exactly");
   CHECK(rdp_upscaler_factor(&r, 640, 474, 2, 320, 240) == 2,
         "frames twice the size: 1x would be nearer, 2x still fits and stands");
   CHECK(rdp_upscaler_factor(&r, 640, 474, 2, 640, 474) == 2, "and stands");
}

int main(void)
{
   test_factor();
   test_inactive_and_offscreen();
   test_screen_image();
   test_jitter();
   test_motion();
   test_cut_short();
   test_depth_and_draws();
   test_sample_rule();
   test_planes();
   test_translucent();
   test_history();
   if (s_failures)
   {
      printf("%d failure(s)\n", s_failures);
      return 1;
   }
   printf("temporal upscaler: ok\n");
   return 0;
}
