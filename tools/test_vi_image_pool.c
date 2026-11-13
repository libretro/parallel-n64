/* test_vi_image_pool -- reuse rules for parallel-RDP's VI intermediates
 * (parallel-rdp/vi_image_pool.c).
 *
 * The VI reuses its VRAM, AA and divot images across frames. Reuse is
 * safe only once the frame context that last used an image has come
 * round again, as the device waits for that context's work before it
 * begins it; handing an image out sooner races the GPU. This checks
 * when entries come back, that one is never handed out twice in a
 * frame, that a mismatch never reuses, and that the pool stays bounded.
 *
 * The images handed on (the scanout the frontend shows, the previous
 * frame kept to blend with) come from the pool too, so an image comes
 * back only once the pool holds its last reference: the frontend keeps
 * the scanout for as many frames as it has sync indices, however many
 * frame contexts the device has.
 *
 * The pool allocates nothing. Full of images in use it hands out a
 * spare entry that is kept for nobody. What it drops it lets go of,
 * image and layer views, once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "vi_image_pool.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ---- images and views with a reference count and nothing else ---- */
static unsigned images_disposed, views_disposed;

void granite_image_dispose(granite_image_t *image)
{
   image->image = VK_NULL_HANDLE;
   images_disposed++;
}

void granite_image_view_dispose(granite_image_view_t *view)
{
   (void)view;
   views_disposed++;
}

static granite_image_t image_storage[512];
static granite_image_view_t view_storage[64];
static unsigned images_made, views_made;

static granite_image_t *counted_image(void)
{
   granite_image_t *image = &image_storage[images_made++];
   memset(image, 0, sizeof(*image));
   granite_refs_init(&image->refs);
   image->image = (VkImage)(size_t)images_made;
   return image;
}

static granite_image_view_t *counted_view(void)
{
   granite_image_view_t *view = &view_storage[views_made++];
   memset(view, 0, sizeof(*view));
   granite_refs_init(&view->refs);
   return view;
}

static granite_image_info_t image(unsigned w, unsigned h, unsigned layers)
{
   granite_image_info_t info;
   memset(&info, 0, sizeof(info));
   info.width = w;
   info.height = h;
   info.depth = 1;
   info.levels = 1;
   info.layers = layers;
   info.format = VK_FORMAT_R8G8B8A8_UINT;
   info.type = VK_IMAGE_TYPE_2D;
   info.samples = VK_SAMPLE_COUNT_1_BIT;
   info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
   /* (not part of what is compared: the tag below) */
   info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
   return info;
}

static rdp_vi_image_pool_t pool, fresh;

/* One frame's request, after the frame's trim: reuses or adds, and says which. */
static unsigned get(const granite_image_info_t *info, uint64_t serial, uint64_t contexts, unsigned *adds)
{
   rdp_vi_image_entry_t *e;
   rdp_vi_image_pool_trim(&pool, serial, contexts);
   e = rdp_vi_image_pool_reuse(&pool, info, false, serial, contexts);
   if (!e)
   {
      e = rdp_vi_image_pool_add(&pool, info, serial, contexts);
      (*adds)++;
   }
   /* Tag entries so reuse can be told apart from adds. */
   if (!e->info.initial_layout)
      e->info.initial_layout = (VkImageLayout)*adds;
   return (unsigned)e->info.initial_layout;
}

static void reset(void)
{
   rdp_vi_image_pool_free(&pool);
   CHECK(pool.count == 0);
}

int main(void)
{
   granite_image_info_t a, b, d;
   rdp_vi_image_entry_t *e, *back;
   uint64_t contexts, serial, s;
   unsigned adds, i, before, before_views;

   for (contexts = 1; contexts <= 3; contexts++)
   {
      reset();
      adds = 0;
      a = image(326, 242, 1);
      b = image(323, 239, 1);

      /* Steady state: one image per stage per frame context, no more. */
      for (serial = 0; serial < 50; serial++)
      {
         unsigned ta = get(&a, serial, contexts, &adds);
         unsigned tb = get(&b, serial, contexts, &adds);
         CHECK(ta != tb);
      }
      CHECK(adds == 2 * contexts);
      CHECK(pool.count == 2 * contexts);

      /* Never back before its frame context comes round again. */
      rdp_vi_image_pool_free(&fresh);
      e = rdp_vi_image_pool_add(&fresh, &a, 10, contexts);
      e->info.initial_layout = (VkImageLayout)1;
      for (s = 10; s < 10 + contexts; s++)
         CHECK(rdp_vi_image_pool_reuse(&fresh, &a, false, s, contexts) == NULL);
      back = rdp_vi_image_pool_reuse(&fresh, &a, false, 10 + contexts, contexts);
      CHECK(back && back->info.initial_layout == (VkImageLayout)1);
      /* and busy again for the frame that took it */
      CHECK(rdp_vi_image_pool_reuse(&fresh, &a, false, 10 + contexts, contexts) == NULL);
   }

   /* A second request in the same frame never gets the same image. */
   reset();
   a = image(64, 64, 1);
   rdp_vi_image_pool_add(&pool, &a, 0, 1);
   CHECK(rdp_vi_image_pool_reuse(&pool, &a, false, 1, 1) != NULL);
   CHECK(rdp_vi_image_pool_reuse(&pool, &a, false, 1, 1) == NULL);

   /* Any difference in what is asked for means a new image. */
   reset();
   rdp_vi_image_pool_add(&pool, &a, 0, 1);
   d = a; d.width++;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.height++;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.depth = 2;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.levels = 2;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.layers = 2;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.misc |= GRANITE_IMAGE_MISC_FORCE_ARRAY_BIT;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.format = VK_FORMAT_R8G8B8A8_UNORM;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.type = VK_IMAGE_TYPE_3D;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.samples = VK_SAMPLE_COUNT_4_BIT;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   d = a; d.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) == NULL);
   /* (the layout it starts in is not part of it) */
   d = a; d.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
   /* asking for layer views reuses only an entry that has them */
   CHECK(rdp_vi_image_pool_reuse(&pool, &a, true, 5, 1) == NULL);
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 5, 1) != NULL);
   pool.entries[0].layer_views[0] = counted_view();
   CHECK(rdp_vi_image_pool_reuse(&pool, &a, true, 6, 1) == &pool.entries[0]);

   /* A size change lets the old sizes go after a while, with no new
    * image added since; what goes is let go of, views and all. */
   reset();
   adds = 0;
   b = image(100, 100, 1);
   d = image(200, 150, 1);
   for (serial = 0; serial < 10; serial++)
   {
      get(&b, serial, 2, &adds);
      for (i = 0; i < pool.count; i++)
         if (!pool.entries[i].image)
         {
            pool.entries[i].image = counted_image();
            pool.entries[i].layer_views[0] = counted_view();
            pool.entries[i].layer_views[1] = counted_view();
         }
   }
   before = images_disposed;
   before_views = views_disposed;
   for (; serial < 40; serial++)
      get(&d, serial, 2, &adds);
   CHECK(pool.count == 2);
   CHECK(images_disposed == before + 2 && views_disposed == before_views + 4);
   /* (four turns of the frame contexts, not sooner) */
   reset();
   rdp_vi_image_pool_add(&pool, &b, 0, 2);
   rdp_vi_image_pool_trim(&pool, 2 + 4 * 2 - 1, 2);
   CHECK(pool.count == 1);
   rdp_vi_image_pool_trim(&pool, 2 + 4 * 2, 2);
   CHECK(pool.count == 0);
   /* (all that are due go in one trim) */
   rdp_vi_image_pool_add(&pool, &b, 0, 2);
   rdp_vi_image_pool_add(&pool, &d, 0, 2);
   rdp_vi_image_pool_add(&pool, &a, 0, 2);
   rdp_vi_image_pool_trim(&pool, 100, 2);
   CHECK(pool.count == 0);

   /* Bounded even when every frame asks for a new size. */
   reset();
   adds = 0;
   for (serial = 0; serial < 200; serial++)
   {
      d = image(64 + (unsigned)serial, 64, 1);
      get(&d, serial, 1, &adds);
   }
   CHECK(pool.count <= RDP_VI_IMAGE_POOL_ENTRIES);

   /* When full, the free one released longest ago makes room. */
   reset();
   for (i = 0; i < RDP_VI_IMAGE_POOL_ENTRIES; i++)
   {
      d = image(10 + i, 10, 1);
      /* (entry 5 was released first, entry 9 is still in use) */
      rdp_vi_image_pool_add(&pool, &d, i == 5 ? 0 : i == 9 ? 1000 : 10 + i, 1);
   }
   /* (the last one has an image: it moves into the room made) */
   pool.entries[RDP_VI_IMAGE_POOL_ENTRIES - 1].image = counted_image();
   d = image(999, 10, 1);
   e = rdp_vi_image_pool_add(&pool, &d, 500, 1);
   CHECK(pool.count == RDP_VI_IMAGE_POOL_ENTRIES);
   CHECK(e == &pool.entries[RDP_VI_IMAGE_POOL_ENTRIES - 1] && !e->image && pool.entries[5].image);
   d = image(10 + 5, 10, 1);
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 2000, 1) == NULL);
   d = image(10 + 9, 10, 1);
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 2000, 1) != NULL);
   d = image(10 + 6, 10, 1);
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 2000, 1) != NULL);

   /* Handed on: back only when the pool holds the last reference,
    * however long after its frame context came round that is. */
   reset();
   a = image(640, 480, 1);
   e = rdp_vi_image_pool_add(&pool, &a, 0, 2);
   e->image = counted_image();
   {
      granite_image_t *shown = e->image;
      granite_image_ref(shown);
      for (serial = 2; serial < 8; serial++)
         CHECK(rdp_vi_image_pool_reuse(&pool, &a, false, serial, 2) == NULL);
      /* nor evicted for a new size while it is out */
      for (i = 0; i < RDP_VI_IMAGE_POOL_ENTRIES; i++)
      {
         d = image(10 + i, 10, 1);
         rdp_vi_image_pool_add(&pool, &d, 5, 1);
      }
      CHECK(pool.count == RDP_VI_IMAGE_POOL_ENTRIES + 1);
      CHECK(rdp_vi_image_pool_reuse(&pool, &a, false, 8, 2) == NULL);
      before = images_disposed;
      granite_image_unref(shown);
      CHECK(images_disposed == before);
      back = rdp_vi_image_pool_reuse(&pool, &a, false, 8, 2);
      CHECK(back && back->image == shown);
   }

   /* Full of busy entries: grows rather than evicting one in use, up
    * to what it has room for; past that, a spare nobody keeps. */
   reset();
   for (i = 0; i < RDP_VI_IMAGE_POOL_CAPACITY; i++)
   {
      d = image(10 + i, 10, 1);
      e = rdp_vi_image_pool_add(&pool, &d, 0, 1);
      CHECK(e == &pool.entries[i]);
   }
   CHECK(pool.count == RDP_VI_IMAGE_POOL_CAPACITY);
   d = image(5000, 10, 1);
   e = rdp_vi_image_pool_add(&pool, &d, 0, 1);
   CHECK(e == &pool.spare && pool.count == RDP_VI_IMAGE_POOL_CAPACITY && e->info.width == 5000 && !e->image);
   e->image = counted_image();
   e->layer_views[0] = counted_view();
   CHECK(rdp_vi_image_pool_reuse(&pool, &d, false, 100, 1) == NULL);
   before = images_disposed;
   before_views = views_disposed;
   d = image(5001, 10, 1);
   e = rdp_vi_image_pool_add(&pool, &d, 0, 1);
   CHECK(e == &pool.spare && e->info.width == 5001 && !e->image && !e->layer_views[0]
         && images_disposed == before + 1 && views_disposed == before_views + 1);
   e->image = counted_image();
   /* once some are free again it is back to the entries */
   d = image(5002, 10, 1);
   e = rdp_vi_image_pool_add(&pool, &d, 50, 1);
   CHECK(e != &pool.spare && pool.count == RDP_VI_IMAGE_POOL_CAPACITY);

   /* Freed: everything it held is let go of, once. */
   pool.entries[3].image = counted_image();
   before = images_disposed;
   rdp_vi_image_pool_free(&pool);
   CHECK(pool.count == 0 && images_disposed == before + 2 && !pool.spare.image);
   rdp_vi_image_pool_free(&pool);
   CHECK(images_disposed == before + 2);
   rdp_vi_image_pool_free(&fresh);

   if (failures)
   {
      fprintf(stderr, "test_vi_image_pool: %d failures\n", failures);
      return 1;
   }
   printf("test_vi_image_pool: ok\n");
   return 0;
}
