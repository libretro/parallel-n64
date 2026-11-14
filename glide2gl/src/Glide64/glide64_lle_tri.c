#include "glide64_lle_tri.h"

void glide64_lle_point_at(const glide64_lle_plane *_pl, double _x, double _y,
      glide64_lle_point *_p)
{
   const double dy = _y - _pl->y0;
   const double dx = _x - (_pl->x0 + _pl->dxhdy * dy);
   double *o = &_p->r;
   unsigned i;
   _p->x = _x;
   _p->y = _y;
   for (i = 0; i < 8; ++i)
      o[i] = _pl->v[i] + _pl->dvdx[i] * dx + _pl->dvde[i] * dy;
}

static void lerp(const glide64_lle_point *_a, const glide64_lle_point *_b, double _t,
      glide64_lle_point *_o)
{
   const double *a = &_a->x, *b = &_b->x;
   double *o = &_o->x;
   unsigned i;
   for (i = 0; i < sizeof(glide64_lle_point) / sizeof(double); ++i)
      o[i] = a[i] + (b[i] - a[i]) * _t;
}

unsigned glide64_lle_cut(const glide64_lle_point *_c, unsigned _n, int _persp,
      glide64_lle_point *_out, int *_list)
{
   unsigned i, k, written = 0;
   int cut = 0;

   if (_persp)
      for (i = 0; i < _n; ++i)
         cut |= _c[i].w < GLIDE64_LLE_MIN_W;

   *_list = cut;
   if (!cut)
   {
      for (i = 0; i < _n; ++i)
         _out[i] = _c[i];
      return _n;
   }

   /* each triangle of the strip, cut where W falls below the least */
   for (k = 0; k + 2 < _n; ++k)
   {
      glide64_lle_point out[4];
      unsigned m = 0;
      for (i = 0; i < 3; ++i)
      {
         const glide64_lle_point *a = &_c[k + i], *b = &_c[k + (i + 1) % 3];
         const double da = a->w - GLIDE64_LLE_MIN_W, db = b->w - GLIDE64_LLE_MIN_W;
         if (da >= 0.0)
            out[m++] = *a;
         if ((da >= 0.0) != (db >= 0.0))
            lerp(a, b, da / (da - db), &out[m++]);
      }
      for (i = 1; i + 1 < m; ++i)
      {
         _out[written++] = out[0];
         _out[written++] = out[i];
         _out[written++] = out[i + 1];
      }
   }
   return written;
}
