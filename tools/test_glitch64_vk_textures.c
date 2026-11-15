/* test_glitch64_vk_textures -- Glitch64 on Vulkan's textures, by the TMU
 * address each was downloaded to.
 *
 * They used to be found by a walk over all of them, and the ones in TMU
 * memory written over by a second walk, taken out by moving the last into
 * each hole. Now they are kept in their addresses' order and found by a
 * binary search. This runs both over the same downloads and binds --
 * addresses written over again and again, ranges taking one texture, many
 * or none, the table filling up and being emptied -- and holds the new one
 * to the old: the same textures held, the same found or not for each
 * address, the same ones given up when written over, and the order kept.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../glide2gl/src/Glitch64/glitch64_vk_textures.h"

#define CAP 64

/* the old table */
static vk_tex old_t[CAP];
static unsigned old_n;
/* the new */
static vk_tex new_t[CAP];
static unsigned new_n;

/* the handles each gave up, in the order they did */
static unsigned old_gone[CAP * 2], new_gone[CAP * 2];
static unsigned n_old_gone, n_new_gone;

static int failures;

static vk_tex *old_find(unsigned id)
{
   unsigned i;
   for (i = 0; i < old_n; i++)
      if (old_t[i].id == id)
         return &old_t[i];
   return NULL;
}

static void old_remove(unsigned lo, unsigned hi)
{
   unsigned i = 0;
   while (i < old_n)
   {
      vk_tex *t = &old_t[i];
      if (t->id >= lo && t->id < hi)
      {
         old_gone[n_old_gone++] = t->handle;
         *t = old_t[--old_n];
      }
      else
         i++;
   }
}

static void old_download(unsigned id, unsigned size, unsigned handle)
{
   vk_tex *t;
   old_remove(id, id + size);
   if (old_n >= CAP)
      old_remove(0, ~0u);
   t = &old_t[old_n++];
   t->id = id;
   t->handle = handle;
   t->fmt = (int)(handle & 7);
   t->w = (uint16_t)size;
   t->h = 1;
}

/* as glitch64_vk.c's tex_remove and vk_grTexSource have it */
static unsigned new_remove(unsigned lo, unsigned hi)
{
   unsigned a, b, i;
   vk_tex_range(new_t, new_n, lo, hi, &a, &b);
   for (i = a; i < b; i++)
      new_gone[n_new_gone++] = new_t[i].handle;
   vk_tex_close(new_t, &new_n, a, b);
   return a;
}

static void new_download(unsigned id, unsigned size, unsigned handle)
{
   unsigned at = new_remove(id, id + size);
   vk_tex *t;
   if (new_n >= CAP)
      at = new_remove(0, ~0u);
   t = vk_tex_open(new_t, &new_n, at);
   t->id = id;
   t->handle = handle;
   t->fmt = (int)(handle & 7);
   t->w = (uint16_t)size;
   t->h = 1;
}

static int cmp_unsigned(const void *a, const void *b)
{
   const unsigned x = *(const unsigned*)a, y = *(const unsigned*)b;
   return x < y ? -1 : x > y;
}

static void fail(unsigned step, const char *what)
{
   if (failures++ < 10)
      fprintf(stderr, "step %u: %s\n", step, what);
}

static void check(unsigned step)
{
   unsigned i;
   if (old_n != new_n)
   {
      fail(step, "a different number of textures held");
      return;
   }
   for (i = 0; i + 1 < new_n; i++)
      if (new_t[i].id >= new_t[i + 1].id)
         fail(step, "the textures out of their addresses' order");
   for (i = 0; i < old_n; i++)
   {
      const vk_tex *n = vk_tex_find(new_t, new_n, old_t[i].id);
      if (!n || n->handle != old_t[i].handle || n->fmt != old_t[i].fmt || n->w != old_t[i].w)
         fail(step, "a texture held not found as it was");
   }
   /* what was given up: the same handles, whatever the order */
   if (n_old_gone != n_new_gone)
      fail(step, "a different number of textures given up");
   else
   {
      qsort(old_gone, n_old_gone, sizeof(unsigned), cmp_unsigned);
      qsort(new_gone, n_new_gone, sizeof(unsigned), cmp_unsigned);
      for (i = 0; i < n_old_gone; i++)
         if (old_gone[i] != new_gone[i])
            fail(step, "a different texture given up");
   }
   n_old_gone = n_new_gone = 0;
}

int main(void)
{
   /* TMU memory: small, so addresses come round again; large, so the
    * table fills before they do */
   static const unsigned spans[] = { 256, 4096, 1u << 20 };
   unsigned si, step = 0, finds = 0, emptied = 0;
   for (si = 0; si < sizeof(spans) / sizeof(spans[0]); si++)
   {
      unsigned seed = si * 7919u + 1u, k, handle = 1;
      old_n = new_n = 0;
      for (k = 0; k < 20000; k++, step++)
      {
         unsigned id, size, r;
         seed = seed * 1103515245u + 12345u;
         r = seed >> 8;
         /* small sizes mostly, so the table fills, and now and then up
          * to a quarter of the span; addresses anywhere, or at or next to
          * one held */
         id = 1 + (r % spans[si]);
         if ((r & 3) == 0 && old_n)
            id = old_t[(r >> 4) % old_n].id + ((r >> 12) & 1);
         seed = seed * 1103515245u + 12345u;
         size = (seed >> 20) % 16 == 0 ? 1 + (seed >> 9) % (spans[si] / 4)
               : 1 + (seed >> 9) % (spans[si] / (CAP * 4) + 1);
         if ((seed >> 3) % 5 == 0)
         {
            /* a bind: found by both or by neither */
            const vk_tex *o = old_find(id), *n = vk_tex_find(new_t, new_n, id);
            if (!o != !n || (o && o->handle != n->handle))
               fail(step, "a bind found a different texture");
            finds++;
            continue;
         }
         if (old_n == CAP)
            emptied++;
         old_download(id, size, handle);
         new_download(id, size, handle);
         handle++;
         check(step);
      }
   }
   /* an empty table, and the lowest and highest addresses */
   old_n = new_n = 0;
   if (vk_tex_find(new_t, new_n, 1) || vk_tex_find(new_t, new_n, ~0u))
      fail(step, "found in an empty table");
   new_download(1, 1, 1);
   new_download(~0u - 1, 1, 2);
   if (!vk_tex_find(new_t, new_n, 1) || !vk_tex_find(new_t, new_n, ~0u - 1) || vk_tex_find(new_t, new_n, 2))
      fail(step, "the lowest or highest address");
   new_remove(0, ~0u);
   if (new_n)
      fail(step, "not emptied");

   if (failures)
   {
      fprintf(stderr, "test_glitch64_vk_textures: %d failures\n", failures);
      return 1;
   }
   if (!emptied || !finds)
   {
      fprintf(stderr, "test_glitch64_vk_textures: the table never filled, or nothing was bound\n");
      return 1;
   }
   printf("test_glitch64_vk_textures: %u steps, %u binds, the table full %u times, ok\n", step, finds, emptied);
   return 0;
}
