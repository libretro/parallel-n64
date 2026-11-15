#ifndef GLITCH64_VK_TEXTURES_H
#define GLITCH64_VK_TEXTURES_H

/* The textures Glitch64 on Vulkan holds, by the TMU address each was
 * downloaded to: kept in that address's order, so a texture is found, and
 * the textures in TMU memory written over are found, by a binary search
 * rather than a walk over all of them. C89. */

#include <stdint.h>
#include <string.h>
#include <retro_inline.h>

typedef struct vk_tex
{
   unsigned id;      /* the TMU address, plus one */
   unsigned handle;
   int fmt;
   uint16_t w, h;
} vk_tex;

/* the first of @t's @n at or past @id */
static INLINE unsigned vk_tex_lower(const vk_tex *t, unsigned n, unsigned id)
{
   unsigned lo = 0, hi = n;
   while (lo < hi)
   {
      const unsigned mid = (lo + hi) >> 1;
      if (t[mid].id < id)
         lo = mid + 1;
      else
         hi = mid;
   }
   return lo;
}

static INLINE vk_tex *vk_tex_find(vk_tex *t, unsigned n, unsigned id)
{
   const unsigned i = vk_tex_lower(t, n, id);
   return i < n && t[i].id == id ? &t[i] : NULL;
}

/* the textures at [@lo, @hi): t[*a] up to t[*b] */
static INLINE void vk_tex_range(const vk_tex *t, unsigned n, unsigned lo, unsigned hi,
      unsigned *a, unsigned *b)
{
   *a = vk_tex_lower(t, n, lo);
   *b = vk_tex_lower(t, n, hi);
}

/* t[a] up to t[b] gone */
static INLINE void vk_tex_close(vk_tex *t, unsigned *n, unsigned a, unsigned b)
{
   memmove(&t[a], &t[b], (*n - b) * sizeof(*t));
   *n -= b - a;
}

/* room at t[@at] for one more, which the caller fills: @t must have it */
static INLINE vk_tex *vk_tex_open(vk_tex *t, unsigned *n, unsigned at)
{
   memmove(&t[at + 1], &t[at], (*n - at) * sizeof(*t));
   (*n)++;
   return &t[at];
}

#endif
