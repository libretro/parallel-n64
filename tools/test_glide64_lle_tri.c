/* test_glide64_lle_tri -- Glide64 drawing RDP triangles with W at or
 * below zero.
 *
 * GoldenEye builds the frigate level's water and sky as RDP triangles on
 * the CPU. Near the horizon their W, the RDP's perspective divisor, falls
 * to zero and below inside the triangle; the RDP steps S/W and T/W across
 * the screen and never meets it. Glide64 gave the GPU each corner's own
 * values, its W with 1.0 added: with W at or below zero at a corner the
 * water came out a black wedge, and the 1.0 bent the texture where W is
 * small. The corners also came from an edge walk that dropped two bits of
 * every step.
 *
 * Now each corner is the RDP's planes at that point, and a strip where W
 * falls below GLIDE64_LLE_MIN_W is cut to where it does not, every value
 * along screen space as the RDP steps it. Held here: the planes, a strip
 * with W positive throughout passed as it is, and a cut strip covering
 * just the part of each triangle where W is at least the least, every
 * point there the planes' values at that point.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../glide2gl/src/Glide64/glide64_lle_tri.h"

static int failures;

static void check(int _cond, const char *_fmt, ...)
{
   va_list ap;
   if (_cond)
      return;
   ++failures;
   printf("FAIL: ");
   va_start(ap, _fmt);
   vprintf(_fmt, ap);
   va_end(ap);
   printf("\n");
}

static void barycentric(const glide64_lle_point *_a, const glide64_lle_point *_b,
      const glide64_lle_point *_c, double _x, double _y, double _l[3])
{
   const double d = (_b->y - _c->y) * (_a->x - _c->x) + (_c->x - _b->x) * (_a->y - _c->y);
   _l[0] = ((_b->y - _c->y) * (_x - _c->x) + (_c->x - _b->x) * (_y - _c->y)) / d;
   _l[1] = ((_c->y - _a->y) * (_x - _c->x) + (_a->x - _c->x) * (_y - _c->y)) / d;
   _l[2] = 1.0 - _l[0] - _l[1];
}

static double area(const glide64_lle_point *_a, const glide64_lle_point *_b, const glide64_lle_point *_c)
{
   return fabs((_b->x - _a->x) * (_c->y - _a->y) - (_c->x - _a->x) * (_b->y - _a->y)) * 0.5;
}

/* The strip of @_n corners @_c from plane @_pl, cut: of each triangle the
 * part where W is at least the least, every point the planes' values */
static void check_cut(const char *_name, const glide64_lle_plane *_pl, const glide64_lle_point *_c, unsigned _n)
{
   glide64_lle_point out[GLIDE64_LLE_MAX_POINTS(8)];
   double kept[8], want[8];
   unsigned written, i, j, k;
   int list = 0;

   written = glide64_lle_cut(_c, _n, 1, out, &list);
   check(list, "%s: not cut", _name);
   check(written % 3 == 0 && written <= GLIDE64_LLE_MAX_POINTS(_n), "%s: %u points", _name, written);

   for (k = 0; k + 2 < _n; ++k)
   {
      const glide64_lle_point *a = &_c[k], *b = &_c[k + 1], *c = &_c[k + 2];
      double minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
      unsigned in = 0, all = 0, sx, sy;
      if (b->x < minx) minx = b->x;
      if (c->x < minx) minx = c->x;
      if (b->x > maxx) maxx = b->x;
      if (c->x > maxx) maxx = c->x;
      if (b->y < miny) miny = b->y;
      if (c->y < miny) miny = c->y;
      if (b->y > maxy) maxy = b->y;
      if (c->y > maxy) maxy = c->y;
      for (sy = 0; sy < 200; ++sy)
         for (sx = 0; sx < 200; ++sx)
         {
            double l[3];
            const double x = minx + (maxx - minx) * (sx + 0.5) / 200.0;
            const double y = miny + (maxy - miny) * (sy + 0.5) / 200.0;
            barycentric(a, b, c, x, y, l);
            if (l[0] < 0.0 || l[1] < 0.0 || l[2] < 0.0)
               continue;
            ++all;
            in += l[0] * a->w + l[1] * b->w + l[2] * c->w >= GLIDE64_LLE_MIN_W;
         }
      want[k] = all ? area(a, b, c) * in / all : 0.0;
      kept[k] = 0.0;
   }

   for (i = 0; i + 2 < written; i += 3)
   {
      const double cx = (out[i].x + out[i + 1].x + out[i + 2].x) / 3.0;
      const double cy = (out[i].y + out[i + 1].y + out[i + 2].y) / 3.0;
      unsigned owner = _n;
      for (k = 0; k + 2 < _n && owner == _n; ++k)
      {
         double l[3];
         barycentric(&_c[k], &_c[k + 1], &_c[k + 2], cx, cy, l);
         if (l[0] >= -1e-6 && l[1] >= -1e-6 && l[2] >= -1e-6)
            owner = k;
      }
      check(owner != _n, "%s: triangle %u outside the strip", _name, i / 3);
      if (owner == _n)
         continue;
      kept[owner] += area(&out[i], &out[i + 1], &out[i + 2]);
      for (j = 0; j < 3; ++j)
      {
         glide64_lle_point p;
         const double *got = &out[i + j].r, *at = &p.r;
         unsigned v;
         glide64_lle_point_at(_pl, out[i + j].x, out[i + j].y, &p);
         check(out[i + j].w >= GLIDE64_LLE_MIN_W - 1.0, "%s: point %u where W is %g", _name, i + j, out[i + j].w);
         for (v = 0; v < 8; ++v)
            check(fabs(got[v] - at[v]) <= 1e-6 * (1.0 + fabs(at[v])),
                  "%s: point %u value %u is %g, the RDP has %g", _name, i + j, v, got[v], at[v]);
      }
   }
   for (k = 0; k + 2 < _n; ++k)
      check(fabs(kept[k] - want[k]) <= 0.02 * want[k] + 0.5,
            "%s: strip triangle %u keeps area %g, %g has W at least the least", _name, k, kept[k], want[k]);
}

int main(void)
{
   glide64_lle_plane pl;
   glide64_lle_point water[4], ahead[4], p, q, out[GLIDE64_LLE_MAX_POINTS(8)];
   unsigned n, i;
   int list;

   memset(&pl, 0, sizeof(pl));
   pl.x0 = 10.0; pl.y0 = 120.0; pl.dxhdy = 0.0;
   pl.v[0] = 0x800000; pl.dvdx[0] = -0x4000; pl.dvde[0] = 0x6000;
   pl.v[4] = 4293918720.0; pl.dvdx[4] = 0x2000; pl.dvde[4] = -0x1000000;
   pl.v[5] = 0x400000; pl.dvdx[5] = 0x10000; pl.dvde[5] = -0x5000;
   pl.v[6] = 0x200000; pl.dvdx[6] = 0x800; pl.dvde[6] = 0x9000;
   pl.v[7] = 0x30000; pl.dvdx[7] = -0x60; pl.dvde[7] = -0x600;

   /* the planes: each value at the major edge, stepped along it and x */
   pl.dxhdy = 0.5;
   glide64_lle_point_at(&pl, 15.0, 130.0, &p);
   check(p.s == (double)0x400000 - 10.0 * 0x5000 && p.w == (double)0x30000 - 10.0 * 0x600,
         "plane: the major edge's values wrong");
   glide64_lle_point_at(&pl, 25.0, 130.0, &q);
   check(q.s == p.s + 10.0 * 0x10000 && q.w == p.w - 10.0 * 0x60, "plane: the step along x wrong");
   pl.dxhdy = 0.0;

   /* GoldenEye's water: W falls through zero towards the horizon */
   glide64_lle_point_at(&pl, 10.0, 120.0, &water[0]);
   glide64_lle_point_at(&pl, 300.0, 120.0, &water[1]);
   glide64_lle_point_at(&pl, 10.0, 239.0, &water[2]);
   glide64_lle_point_at(&pl, 300.0, 239.0, &water[3]);
   check(water[0].w > GLIDE64_LLE_MIN_W && water[3].w < 0.0, "water: W does not fall through zero");

   /* W positive throughout: the strip as it is */
   memcpy(ahead, water, sizeof(ahead));
   for (i = 0; i < 4; ++i)
      ahead[i].w = 0x8000 + i * 0x1000;
   n = glide64_lle_cut(ahead, 4, 1, out, &list);
   check(n == 4 && !list && memcmp(out, ahead, sizeof(ahead)) == 0, "ahead: changed (%u points)", n);

   check_cut("water", &pl, water, 4);
   check_cut("water, first triangle", &pl, &water[1], 3);

   /* without perspective W is not used: nothing cut */
   n = glide64_lle_cut(water, 4, 0, out, &list);
   check(n == 4 && !list, "no perspective: cut (%u points)", n);

   /* wholly behind: nothing drawn */
   memcpy(ahead, water, sizeof(ahead));
   for (i = 0; i < 4; ++i)
      ahead[i].w = -0x10000;
   n = glide64_lle_cut(ahead, 4, 1, out, &list);
   check(n == 0 && list, "behind: %u points", n);

   if (failures)
   {
      printf("%d failure(s)\n", failures);
      return 1;
   }
   printf("test_glide64_lle_tri: ok\n");
   return 0;
}
