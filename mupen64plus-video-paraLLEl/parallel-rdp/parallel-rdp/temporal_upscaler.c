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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "temporal_upscaler.h"

#define UPSCALER_RECORDS   4
#define UPSCALER_ORIGINS   4
/* How far into its framebuffer the VI may start showing it */
#define UPSCALER_ORIGIN_LINES 16
#define UPSCALER_MAX_PRIMS 65536
/* Further than this, in native pixels, nothing on screen is the same
 * thing a frame later */
#define UPSCALER_FARTHEST  96.0f
/* How far from its own ordinal a triangle's match is looked for: a
 * triangle culled or added earlier in the frame shifts the rest */
#define UPSCALER_ORDINAL_WINDOW 8

#define U64(hi, lo) (((uint64_t)(hi) << 32) | (uint64_t)(lo))

/* A triangle, in native pixels: its corners, which a match pairs up, and
 * the outline drawn, which the scissor or a clipped command may cut short
 * of the corners: the major edge on one side, the mid and low edges on
 * the other. A rectangle is outline 0 to 1. */
#define UPSCALER_OUTLINE 6
typedef struct upscaler_prim
{
   uint64_t signature;
   uint32_t ordinal;
   uint32_t flags;
   float cx[3], cy[3];
   float x[UPSCALER_OUTLINE], y[UPSCALER_OUTLINE], z[UPSCALER_OUTLINE];
   /* in render pixels: how far the outline is grown to cover what the RDP
    * does, every pixel an edge touches */
   float grow_x[UPSCALER_OUTLINE], grow_y[UPSCALER_OUTLINE];
} upscaler_prim_t;

/* Open addressing; a value of 0 is an empty slot */
typedef struct upscaler_table
{
   uint64_t *keys;
   uint32_t *values;
   uint32_t mask;
   uint32_t count;
} upscaler_table_t;

typedef struct upscaler_record
{
   upscaler_prim_t *prims;
   unsigned num_prims;
   unsigned cap_prims;
   /* signature -> triangles drawn under it so far */
   upscaler_table_t ordinals;
   uint64_t serial;
   uint32_t addr;
   uint32_t stride;
   unsigned size_log2;
   float jitter_x, jitter_y;
   bool used;
   bool shown;
} upscaler_record_t;

typedef struct upscaler_origin
{
   uint32_t addr;
   uint32_t stride;
   unsigned size_log2;
} upscaler_origin_t;

struct rdp_upscaler
{
   upscaler_record_t records[UPSCALER_RECORDS];
   upscaler_origin_t origins[UPSCALER_ORIGINS];
   upscaler_record_t *drawing;

   /* The frame shown before, and (signature, ordinal) -> its triangle */
   upscaler_prim_t *prev;
   unsigned num_prev;
   unsigned cap_prev;
   upscaler_table_t prev_table;

   rdp_upscaler_vertex_t *vertices;
   unsigned cap_vertices;
   rdp_upscaler_draw_t *draws;
   unsigned cap_draws;

   uint64_t serial;
   uint64_t shown_serial;
   uint32_t color_addr;
   uint32_t color_stride;
   int color_size_log2;
   unsigned num_origins;
   unsigned jitter_phase;
   unsigned jitter_index;
   bool active;
   bool reset_pending;
};

static bool upscaler_grow(void **ptr, unsigned *cap, unsigned need, size_t size)
{
   unsigned new_cap;
   void *p;
   if (need <= *cap)
      return true;
   new_cap = *cap ? *cap : 64;
   while (new_cap < need)
      new_cap *= 2;
   p = realloc(*ptr, (size_t)new_cap * size);
   if (!p)
      return false;
   *ptr = p;
   *cap = new_cap;
   return true;
}

static uint32_t upscaler_slot(uint64_t key, uint32_t mask)
{
   return (uint32_t)((key * U64(0x9e3779b9, 0x7f4a7c15)) >> 40) & mask;
}

static void upscaler_table_clear(upscaler_table_t *t)
{
   if (t->values)
      memset(t->values, 0, ((size_t)t->mask + 1) * sizeof(*t->values));
   t->count = 0;
}

static void upscaler_table_free(upscaler_table_t *t)
{
   free(t->keys);
   free(t->values);
   memset(t, 0, sizeof(*t));
}

/* The value stored under @key, inserting 0 for it if it is new;
 * NULL when out of memory */
static uint32_t *upscaler_table_get(upscaler_table_t *t, uint64_t key)
{
   uint32_t i;
   if (!t->values || (t->count + 1) * 2 > t->mask + 1)
   {
      uint32_t old_size = t->values ? t->mask + 1 : 0;
      uint32_t new_size = old_size ? old_size * 2 : 256;
      uint64_t *keys    = (uint64_t*)malloc(new_size * sizeof(*keys));
      uint32_t *values  = (uint32_t*)calloc(new_size, sizeof(*values));
      if (!keys || !values)
      {
         free(keys);
         free(values);
         return NULL;
      }
      for (i = 0; i < old_size; i++)
      {
         uint32_t j;
         if (!t->values[i])
            continue;
         j = upscaler_slot(t->keys[i], new_size - 1);
         while (values[j])
            j = (j + 1) & (new_size - 1);
         keys[j]   = t->keys[i];
         values[j] = t->values[i];
      }
      free(t->keys);
      free(t->values);
      t->keys   = keys;
      t->values = values;
      t->mask   = new_size - 1;
   }
   i = upscaler_slot(key, t->mask);
   while (t->values[i])
   {
      if (t->keys[i] == key)
         return &t->values[i];
      i = (i + 1) & t->mask;
   }
   t->keys[i] = key;
   t->count++;
   return &t->values[i];
}

static uint32_t upscaler_table_find(const upscaler_table_t *t, uint64_t key)
{
   uint32_t i;
   if (!t->values)
      return 0;
   i = upscaler_slot(key, t->mask);
   while (t->values[i])
   {
      if (t->keys[i] == key)
         return t->values[i];
      i = (i + 1) & t->mask;
   }
   return 0;
}

static uint64_t upscaler_prim_key(uint64_t signature, uint32_t ordinal)
{
   return signature ^ ((uint64_t)ordinal * U64(0xff51afd7, 0xed558ccd));
}

uint64_t rdp_upscaler_hash_init(void)
{
   return U64(0xcbf29ce4, 0x84222325);
}

uint64_t rdp_upscaler_hash(uint64_t h, uint32_t word)
{
   unsigned i;
   for (i = 0; i < 4; i++)
   {
      h ^= (word >> (i * 8)) & 0xff;
      h *= U64(0x00000100, 0x000001b3);
   }
   return h;
}

/* Halton (2, 3), the sequence DLSS's own samples use */
static float upscaler_halton(unsigned index, unsigned base)
{
   float r = 0.0f;
   float f = 1.0f;
   for (index++; index > 0; index /= base)
   {
      f /= (float)base;
      r += f * (float)(index % base);
   }
   return r;
}

static int32_t upscaler_offset(int32_t value, double delta)
{
   int32_t d = (int32_t)floor(delta + 0.5);
   return (int32_t)((uint32_t)value + (uint32_t)d);
}

rdp_upscaler_t *rdp_upscaler_new(void)
{
   rdp_upscaler_t *u = (rdp_upscaler_t*)calloc(1, sizeof(*u));
   if (!u)
      return NULL;
   u->color_size_log2 = -1;
   u->jitter_phase    = 8;
   u->reset_pending   = true;
   return u;
}

void rdp_upscaler_free(rdp_upscaler_t *u)
{
   unsigned i;
   if (!u)
      return;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      free(u->records[i].prims);
      upscaler_table_free(&u->records[i].ordinals);
   }
   free(u->prev);
   upscaler_table_free(&u->prev_table);
   free(u->vertices);
   free(u->draws);
   free(u);
}

static void upscaler_forget(rdp_upscaler_t *u)
{
   unsigned i;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      u->records[i].used      = false;
      u->records[i].num_prims = 0;
   }
   u->num_prev      = 0;
   upscaler_table_clear(&u->prev_table);
   u->shown_serial  = 0;
   u->jitter_index  = 0;
   u->drawing       = NULL;
   u->reset_pending = true;
}

void rdp_upscaler_configure(rdp_upscaler_t *u, bool enable,
      unsigned jitter_phase, bool reset)
{
   if (enable != u->active || reset)
      upscaler_forget(u);
   u->active       = enable;
   u->jitter_phase = jitter_phase ? jitter_phase : 1;
}

bool rdp_upscaler_is_active(const rdp_upscaler_t *u)
{
   return u->active;
}

void rdp_upscaler_set_color_image(rdp_upscaler_t *u, uint32_t addr,
      uint32_t stride, int size_log2)
{
   if (!u)
      return;
   u->color_addr      = addr;
   u->color_stride    = stride;
   u->color_size_log2 = size_log2;
   u->drawing         = NULL;
}

static bool upscaler_origin_in(const upscaler_origin_t *o, uint32_t addr,
      uint32_t stride, unsigned size_log2)
{
   return o->stride == stride && o->size_log2 == size_log2
      && o->addr >= addr
      && o->addr - addr < ((stride << size_log2) * UPSCALER_ORIGIN_LINES);
}

/* The record of the frame being drawn into the color image, when the
 * VI shows that framebuffer; a frame starts with its first triangle
 * after the framebuffer was last shown */
static upscaler_record_t *upscaler_record_for_draw(rdp_upscaler_t *u)
{
   unsigned i;
   bool shown = false;
   upscaler_record_t *rec = NULL;
   unsigned size_log2;

   if (!u || !u->active || u->color_size_log2 < 1)
      return NULL;
   if (u->drawing)
      return u->drawing;

   size_log2 = (unsigned)u->color_size_log2;
   for (i = 0; i < u->num_origins && !shown; i++)
      shown = upscaler_origin_in(&u->origins[i], u->color_addr,
            u->color_stride, size_log2);
   if (!shown)
      return NULL;

   for (i = 0; i < UPSCALER_RECORDS && !rec; i++)
   {
      upscaler_record_t *r = &u->records[i];
      if (r->used && r->addr == u->color_addr
            && r->stride == u->color_stride && r->size_log2 == size_log2)
         rec = r;
   }
   if (!rec)
   {
      /* an unused record, else the one shown longest ago */
      for (i = 0; i < UPSCALER_RECORDS; i++)
      {
         upscaler_record_t *r = &u->records[i];
         if (!r->used)
         {
            rec = r;
            break;
         }
         if (!rec || r->serial < rec->serial)
            rec = r;
      }
      rec->used = false;
   }

   if (!rec->used || rec->shown)
   {
      unsigned j      = u->jitter_index++ % u->jitter_phase;
      rec->num_prims  = 0;
      upscaler_table_clear(&rec->ordinals);
      rec->serial     = ++u->serial;
      rec->addr       = u->color_addr;
      rec->stride     = u->color_stride;
      rec->size_log2  = size_log2;
      rec->jitter_x   = upscaler_halton(j, 2) - 0.5f;
      rec->jitter_y   = upscaler_halton(j, 3) - 0.5f;
      rec->used       = true;
      rec->shown      = false;
   }

   u->drawing = rec;
   return rec;
}

static upscaler_prim_t *upscaler_add_prim(upscaler_record_t *rec,
      uint64_t signature, unsigned flags)
{
   upscaler_prim_t *p;
   uint32_t *count;
   if (rec->num_prims >= UPSCALER_MAX_PRIMS
         || !upscaler_grow((void**)&rec->prims, &rec->cap_prims,
            rec->num_prims + 1, sizeof(*rec->prims)))
      return NULL;
   p            = &rec->prims[rec->num_prims];
   p->signature = signature;
   p->flags     = flags;
   p->ordinal   = 0;
   if (!(flags & RDP_UPSCALER_RECT))
   {
      if (!(count = upscaler_table_get(&rec->ordinals, signature)))
         return NULL;
      p->ordinal = (*count)++;
   }
   rec->num_prims++;
   return p;
}

static float upscaler_depth(double z)
{
   z /= 2147483648.0;
   if (z < 0.0)
      return 0.0f;
   if (z > 1.0)
      return 1.0f;
   return (float)z;
}

/* Where the edges x = x0 + (y - y0) * dx0 and x = x1 + (y - y1) * dx1
 * cross (x 16.15, y in quarter lines, slopes per quarter line), in native
 * lines; @fallback when they hardly do */
static double upscaler_cross(double x0, double y0, double dx0,
      double x1, double y1, double dx1, double fallback)
{
   double y;
   if (dx0 == dx1)
      return fallback;
   y = (x1 - x0 + y0 * dx0 - y1 * dx1) / (dx0 - dx1) / 4.0;
   if (y < fallback - 4096.0 || y > fallback + 4096.0)
      return fallback;
   return y;
}

void rdp_upscaler_triangle(rdp_upscaler_t *u, rdp_upscaler_triangle_t *tri,
      uint64_t signature, unsigned flags, int32_t prim_z, unsigned scale)
{
   unsigned i;
   upscaler_prim_t *p;
   upscaler_record_t *rec = upscaler_record_for_draw(u);

   if (!rec)
      return;

   if ((p = upscaler_add_prim(rec, signature, flags & ~RDP_UPSCALER_RECT)))
   {
      /* the major (h) edge runs from yh to yl, the mid (m) one from yh
       * to ym, the low (l) one from ym to yl */
      double base = (double)(tri->yh & ~3);
      double yh   = (double)tri->yh / 4.0;
      double ym   = (double)tri->ym / 4.0;
      double yl   = (double)tri->yl / 4.0;
      double line = floor(yh);
      double xh   = (double)tri->xh,    xm   = (double)tri->xm,    xl   = (double)tri->xl;
      double dxh  = (double)tri->dxhdy, dxm  = (double)tri->dxmdy, dxl  = (double)tri->dxldy;
      double y[UPSCALER_OUTLINE], x[UPSCALER_OUTLINE], edge, cy;

#define UPSCALER_H(yy) ((xh + ((yy) * 4.0 - base) * dxh) / 32768.0)
#define UPSCALER_M(yy) ((xm + ((yy) * 4.0 - base) * dxm) / 32768.0)
#define UPSCALER_L(yy) ((xl + ((yy) * 4.0 - (double)tri->ym) * dxl) / 32768.0)
      y[0] = yh; x[0] = UPSCALER_H(yh);
      y[1] = yh; x[1] = UPSCALER_M(yh);
      y[2] = ym; x[2] = UPSCALER_M(ym);
      y[3] = ym; x[3] = UPSCALER_L(ym);
      y[4] = yl; x[4] = UPSCALER_L(yl);
      y[5] = yl; x[5] = UPSCALER_H(yl);

      /* the corners: where the edges meet, beyond the outline when the
       * triangle was cut short */
      cy       = ym > yh ? upscaler_cross(xh, base, dxh, xm, base, dxm, yh) : yh;
      p->cx[0] = (float)UPSCALER_H(cy);
      p->cy[0] = (float)cy;
      p->cx[1] = (float)x[3];
      p->cy[1] = (float)ym;
      cy       = yl > ym ? upscaler_cross(xh, base, dxh, xl, (double)tri->ym, dxl, yl) : yl;
      p->cx[2] = (float)UPSCALER_H(cy);
      p->cy[2] = (float)cy;

      /* The RDP fills a row's pixels from the leftmost to the rightmost
       * an edge reaches over its sub-scanlines, the pixels it only grazes
       * included, and a row any sub-scanline of which is inside: each
       * edge moves out by half a pixel plus half its slope, the top and
       * bottom out by half a row */
      {
         double mid   = (yh + yl) / 2.0;
         double other = mid < ym ? UPSCALER_M(mid) : UPSCALER_L(mid);
         double side  = UPSCALER_H(mid) <= other ? -1.0 : 1.0;
         double kh    = dxh * 4.0 / 32768.0;
         double km    = dxm * 4.0 / 32768.0;
         double kl    = dxl * 4.0 / 32768.0;
         double k[UPSCALER_OUTLINE], out[UPSCALER_OUTLINE], up[UPSCALER_OUTLINE];
         k[0] = kh; out[0] =  side; up[0] = -0.5;
         k[1] = km; out[1] = -side; up[1] = -0.5;
         k[2] = km; out[2] = -side; up[2] =  0.0;
         k[3] = kl; out[3] = -side; up[3] =  0.0;
         k[4] = kl; out[4] = -side; up[4] =  0.5;
         k[5] = kh; out[5] =  side; up[5] =  0.5;
         for (i = 0; i < UPSCALER_OUTLINE; i++)
         {
            p->grow_x[i] = (float)(out[i] * (0.5 + 0.5 * fabs(k[i])) + k[i] * up[i]);
            p->grow_y[i] = (float)up[i];
         }
      }

      for (i = 0; i < UPSCALER_OUTLINE; i++)
      {
         double z = (double)prim_z;
         edge     = UPSCALER_H(y[i]);
         p->x[i]  = (float)x[i];
         p->y[i]  = (float)y[i];
         if (!(flags & RDP_UPSCALER_DEPTH_CONSTANT))
            z = (double)tri->attr[7]
               + (double)tri->attr_de[7] * (y[i] - line)
               + (double)tri->dzdx * (x[i] - edge);
         p->z[i]  = upscaler_depth(z);
      }
#undef UPSCALER_H
#undef UPSCALER_M
#undef UPSCALER_L
   }

   if (rec->jitter_x != 0.0f || rec->jitter_y != 0.0f)
   {
      /* Moving the triangle down by dy moves each edge along its slope;
       * the attributes, anchored at the major edge, follow it */
      double dx = (double)rec->jitter_x / (double)scale;
      double dy = (double)rec->jitter_y / (double)scale;
      tri->xh = upscaler_offset(tri->xh, dx * 32768.0 - 4.0 * dy * (double)tri->dxhdy);
      tri->xm = upscaler_offset(tri->xm, dx * 32768.0 - 4.0 * dy * (double)tri->dxmdy);
      tri->xl = upscaler_offset(tri->xl, dx * 32768.0 - 4.0 * dy * (double)tri->dxldy);
      for (i = 0; i < 8; i++)
         tri->attr[i] = upscaler_offset(tri->attr[i], -dy * (double)tri->attr_de[i]);
   }
}

void rdp_upscaler_rectangle(rdp_upscaler_t *u, uint32_t xh, uint32_t yh,
      uint32_t xl, uint32_t yl)
{
   upscaler_prim_t *p;
   upscaler_record_t *rec = upscaler_record_for_draw(u);
   if (!rec || !(p = upscaler_add_prim(rec, 0, RDP_UPSCALER_RECT)))
      return;
   memset(p->x, 0, sizeof(p->x));
   memset(p->y, 0, sizeof(p->y));
   memset(p->z, 0, sizeof(p->z));
   memset(p->grow_x, 0, sizeof(p->grow_x));
   memset(p->grow_y, 0, sizeof(p->grow_y));
   p->x[0] = (float)xh / 4.0f;
   p->y[0] = (float)yh / 4.0f;
   p->x[1] = (float)xl / 4.0f;
   p->y[1] = (float)yl / 4.0f;
}

static void upscaler_remember_origin(rdp_upscaler_t *u,
      const rdp_upscaler_framebuffer_t *fb)
{
   unsigned i;
   upscaler_origin_t o;
   o.addr      = fb->origin;
   o.stride    = fb->stride;
   o.size_log2 = fb->size_log2;
   for (i = 0; i < u->num_origins; i++)
      if (     u->origins[i].addr      == o.addr
            && u->origins[i].stride    == o.stride
            && u->origins[i].size_log2 == o.size_log2)
         break;
   if (i == u->num_origins && u->num_origins < UPSCALER_ORIGINS)
      u->num_origins++;
   if (i == UPSCALER_ORIGINS)
      i = UPSCALER_ORIGINS - 1;
   /* most recent first */
   memmove(&u->origins[1], &u->origins[0], i * sizeof(o));
   u->origins[0] = o;
}

/* The vertex orders a triangle can come back in: the RDP sorts its
 * vertices by y, which a turn of the triangle reorders */
static const unsigned char upscaler_perms[6][3] = {
   { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
};

/* The triangle of the frame before that @p is: of those drawn under its
 * signature with an ordinal near its own, the nearest, its vertices in
 * @perm order; NULL when none is near enough */
static const upscaler_prim_t *upscaler_match(const rdp_upscaler_t *u,
      const upscaler_prim_t *p, unsigned *perm)
{
   int k;
   const upscaler_prim_t *best = NULL;
   float best_cost             = 0.0f;
   const float far2            = UPSCALER_FARTHEST * UPSCALER_FARTHEST;

   for (k = -UPSCALER_ORDINAL_WINDOW; k <= UPSCALER_ORDINAL_WINDOW; k++)
   {
      unsigned i;
      uint32_t index;
      const upscaler_prim_t *q;
      long ordinal = (long)p->ordinal + k;
      if (ordinal < 0)
         continue;
      index = upscaler_table_find(&u->prev_table,
            upscaler_prim_key(p->signature, (uint32_t)ordinal));
      if (!index)
         continue;
      q = &u->prev[index - 1];
      if (q->signature != p->signature || q->ordinal != (uint32_t)ordinal)
         continue;
      for (i = 0; i < 6; i++)
      {
         unsigned j;
         float cost = 0.0f;
         for (j = 0; j < 3; j++)
         {
            float dx = q->cx[upscaler_perms[i][j]] - p->cx[j];
            float dy = q->cy[upscaler_perms[i][j]] - p->cy[j];
            float d2 = dx * dx + dy * dy;
            if (d2 > far2)
               break;
            cost += d2;
         }
         /* (the same ordinal wins a tie) */
         if (j == 3 && (!best || cost < best_cost))
         {
            best      = q;
            best_cost = cost;
            perm[0]   = upscaler_perms[i][0];
            perm[1]   = upscaler_perms[i][1];
            perm[2]   = upscaler_perms[i][2];
         }
      }
   }
   return best;
}

/* The map taking @p's corners to where they were, @q's in @perm order:
 * a turn, scale and shift of the plane; a shift alone for a sliver */
static bool upscaler_affine(const upscaler_prim_t *p, const upscaler_prim_t *q,
      const unsigned *perm, float *m)
{
   double x0  = p->cx[0], y0 = p->cy[0];
   double ax  = p->cx[1] - x0, ay = p->cy[1] - y0;
   double bx  = p->cx[2] - x0, by = p->cy[2] - y0;
   double det = ax * by - bx * ay;
   double qx0 = q->cx[perm[0]], qy0 = q->cy[perm[0]];
   double qax = q->cx[perm[1]] - qx0, qay = q->cy[perm[1]] - qy0;
   double qbx = q->cx[perm[2]] - qx0, qby = q->cy[perm[2]] - qy0;

   if (det > -1e-3 && det < 1e-3)
   {
      double tx = (qx0 + q->cx[perm[1]] + q->cx[perm[2]]
            - p->cx[0] - p->cx[1] - p->cx[2]) / 3.0;
      double ty = (qy0 + q->cy[perm[1]] + q->cy[perm[2]]
            - p->cy[0] - p->cy[1] - p->cy[2]) / 3.0;
      m[0] = 1.0f; m[1] = 0.0f; m[2] = (float)tx;
      m[3] = 0.0f; m[4] = 1.0f; m[5] = (float)ty;
      return true;
   }

   /* [qa qb] = M [a b], then the offset from the first corner */
   m[0] = (float)((qax * by - qbx * ay) / det);
   m[1] = (float)((qbx * ax - qax * bx) / det);
   m[3] = (float)((qay * by - qby * ay) / det);
   m[4] = (float)((qby * ax - qay * bx) / det);
   m[2] = (float)(qx0 - m[0] * x0 - m[1] * y0);
   m[5] = (float)(qy0 - m[3] * x0 - m[4] * y0);
   return true;
}

static void upscaler_build(rdp_upscaler_t *u, const upscaler_record_t *rec,
      const rdp_upscaler_framebuffer_t *fb, unsigned scale, bool still,
      rdp_upscaler_geometry_t *out)
{
   unsigned i, j;
   unsigned num_vertices = 0;
   unsigned num_draws    = 0;
   uint32_t offset       = (fb->origin - rec->addr) >> rec->size_log2;
   float ox              = (float)(offset % rec->stride);
   float oy              = (float)(offset / rec->stride);
   float s               = (float)scale;

   if (     !upscaler_grow((void**)&u->vertices, &u->cap_vertices,
               rec->num_prims * 12, sizeof(*u->vertices))
         || !upscaler_grow((void**)&u->draws, &u->cap_draws,
               rec->num_prims, sizeof(*u->draws)))
      return;

   for (i = 0; i < rec->num_prims; i++)
   {
      const upscaler_prim_t *p = &rec->prims[i];
      rdp_upscaler_vertex_t *v = &u->vertices[num_vertices];
      unsigned count           = 3;
      unsigned flags           = p->flags
         & (RDP_UPSCALER_DEPTH_TEST | RDP_UPSCALER_DEPTH_WRITE | RDP_UPSCALER_TRANSLUCENT);

      if (p->flags & RDP_UPSCALER_RECT)
      {
         static const unsigned char corners[6][2] = {
            { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
         for (j = 0; j < 6; j++)
         {
            v[j].x        = (p->x[corners[j][0]] - ox) * s + out->jitter_x;
            v[j].y        = (p->y[corners[j][1]] - oy) * s + out->jitter_y;
            v[j].z        = 0.0f;
            v[j].motion_x = 0.0f;
            v[j].motion_y = 0.0f;
         }
         count = 6;
         flags = 0;
      }
      else
      {
         /* the outline as a fan from its first point */
         static const unsigned char fan[12] = {
            0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5 };
         float affine[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
         unsigned perm[3];
         const upscaler_prim_t *q = still ? NULL : upscaler_match(u, p, perm);
         bool moved = q && upscaler_affine(p, q, perm, affine);
         for (j = 0; j < 12; j++)
         {
            unsigned k    = fan[j];
            float x       = p->x[k] + p->grow_x[k] / s;
            float y       = p->y[k] + p->grow_y[k] / s;
            v[j].x        = (x - ox) * s + out->jitter_x;
            v[j].y        = (y - oy) * s + out->jitter_y;
            v[j].z        = p->z[k];
            v[j].motion_x = 0.0f;
            v[j].motion_y = 0.0f;
            if (moved)
            {
               v[j].motion_x = (affine[0] * x + affine[1] * y + affine[2] - x) * s;
               v[j].motion_y = (affine[3] * x + affine[4] * y + affine[5] - y) * s;
            }
         }
         count = 12;
      }

      if (num_draws && u->draws[num_draws - 1].flags == flags)
         u->draws[num_draws - 1].count += count;
      else
      {
         u->draws[num_draws].first = num_vertices;
         u->draws[num_draws].count = count;
         u->draws[num_draws].flags = flags;
         num_draws++;
      }
      num_vertices += count;
   }

   out->vertices     = u->vertices;
   out->draws        = u->draws;
   out->num_vertices = num_vertices;
   out->num_draws    = num_draws;
}

static void upscaler_keep(rdp_upscaler_t *u, const upscaler_record_t *rec)
{
   unsigned i;
   u->num_prev = 0;
   upscaler_table_clear(&u->prev_table);
   if (!upscaler_grow((void**)&u->prev, &u->cap_prev,
            rec->num_prims, sizeof(*u->prev)))
      return;
   if (rec->num_prims)
      memcpy(u->prev, rec->prims, rec->num_prims * sizeof(*u->prev));
   for (i = 0; i < rec->num_prims; i++)
   {
      uint32_t *slot;
      if (u->prev[i].flags & RDP_UPSCALER_RECT)
         continue;
      if (!(slot = upscaler_table_get(&u->prev_table,
               upscaler_prim_key(u->prev[i].signature, u->prev[i].ordinal))))
         break;
      *slot = i + 1;
   }
   u->num_prev = rec->num_prims;
}

void rdp_upscaler_resolve(rdp_upscaler_t *u,
      const rdp_upscaler_framebuffer_t *fb, unsigned scale,
      rdp_upscaler_geometry_t *out)
{
   unsigned i;
   upscaler_record_t *rec = NULL;
   upscaler_origin_t o;

   memset(out, 0, sizeof(*out));
   upscaler_remember_origin(u, fb);

   o.addr      = fb->origin;
   o.stride    = fb->stride;
   o.size_log2 = fb->size_log2;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      upscaler_record_t *r = &u->records[i];
      if (     r->used
            && upscaler_origin_in(&o, r->addr, r->stride, r->size_log2)
            && (!rec || r->serial > rec->serial))
         rec = r;
   }

   if (rec)
   {
      /* shown again, unchanged: it stands still */
      bool still    = rec->serial == u->shown_serial;
      out->jitter_x = rec->jitter_x;
      out->jitter_y = rec->jitter_y;
      upscaler_build(u, rec, fb, scale, still, out);
      if (!still)
      {
         upscaler_keep(u, rec);
         u->shown_serial = rec->serial;
      }
      rec->shown = true;
   }

   u->drawing       = NULL;
   out->reset       = u->reset_pending;
   u->reset_pending = false;
}
