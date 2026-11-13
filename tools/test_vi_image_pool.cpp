/* test_vi_image_pool -- reuse rules for parallel-RDP's VI intermediates.
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
 * back only once the pool holds its last handle: the frontend keeps the
 * scanout for as many frames as it has sync indices, however many frame
 * contexts the device has.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vi_image_pool.hpp"

/* Entries here hold no images: handles stay null, so nothing is ever
 * deleted. The deleters only need to link. */
extern "C" void granite_image_dispose(granite_image_t *) { abort(); }
extern "C" void granite_image_view_dispose(granite_image_view_t *) { abort(); }

/* An image with a reference count and nothing else: no device; the
 * test keeps it from being destroyed. */
static Vulkan::ImageHandle counted_image()
{
	static unsigned char storage[64][sizeof(Vulkan::Image)] __attribute__((aligned(16)));
	static unsigned used;
	unsigned char *p = storage[used++];
	uint32_t one = 1;
	memcpy(p, &one, sizeof(one)); /* the count: the base's first member */
	return Vulkan::ImageHandle(reinterpret_cast<Vulkan::Image *>(p));
}

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static Vulkan::ImageCreateInfo image(unsigned w, unsigned h, unsigned layers = 1)
{
	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::render_target(w, h, VK_FORMAT_R8G8B8A8_UINT);
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.layers = layers;
	/* (not part of what is compared: the tag below) */
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	return info;
}

/* One frame's request, after the frame's trim: reuses or adds, and says which. */
static unsigned get(RDP::VIImagePool &pool, const Vulkan::ImageCreateInfo &info, uint64_t serial,
                    uint64_t contexts, unsigned &adds)
{
	pool.trim(serial, contexts);
	RDP::VIImagePool::Entry *e = pool.reuse(info, false, serial, contexts);
	if (!e)
	{
		e = pool.add(info, serial, contexts);
		adds++;
	}
	/* Tag entries so reuse can be told apart from adds. */
	if (!e->info.initial_layout)
		e->info.initial_layout = VkImageLayout(adds);
	return unsigned(e->info.initial_layout);
}

int main()
{
	for (uint64_t contexts = 1; contexts <= 3; contexts++)
	{
		RDP::VIImagePool pool;
		unsigned adds = 0;
		Vulkan::ImageCreateInfo a = image(326, 242), b = image(323, 239);

		/* Steady state: one image per stage per frame context, no more. */
		for (uint64_t serial = 0; serial < 50; serial++)
		{
			unsigned ta = get(pool, a, serial, contexts, adds);
			unsigned tb = get(pool, b, serial, contexts, adds);
			CHECK(ta != tb);
		}
		CHECK(adds == 2 * contexts);
		CHECK(pool.size() == 2 * contexts);

		/* Never back before its frame context comes round again. */
		RDP::VIImagePool fresh;
		RDP::VIImagePool::Entry *first = fresh.add(a, 10, contexts);
		first->info.initial_layout = VkImageLayout(1);
		for (uint64_t s = 10; s < 10 + contexts; s++)
			CHECK(fresh.reuse(a, false, s, contexts) == nullptr);
		RDP::VIImagePool::Entry *back = fresh.reuse(a, false, 10 + contexts, contexts);
		CHECK(back && back->info.initial_layout == VkImageLayout(1));
		/* and busy again for the frame that took it */
		CHECK(fresh.reuse(a, false, 10 + contexts, contexts) == nullptr);
	}

	/* A second request in the same frame never gets the same image. */
	{
		RDP::VIImagePool pool;
		Vulkan::ImageCreateInfo a = image(64, 64);
		pool.add(a, 0, 1);
		CHECK(pool.reuse(a, false, 1, 1) != nullptr);
		CHECK(pool.reuse(a, false, 1, 1) == nullptr);
	}

	/* Any difference in what is asked for means a new image. */
	{
		RDP::VIImagePool pool;
		Vulkan::ImageCreateInfo a = image(64, 64);
		pool.add(a, 0, 1);
		Vulkan::ImageCreateInfo d = a;
		d.width++;
		CHECK(pool.reuse(d, false, 5, 1) == nullptr);
		d = a;
		d.layers = 2;
		CHECK(pool.reuse(d, false, 5, 1) == nullptr);
		d = a;
		d.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		CHECK(pool.reuse(d, false, 5, 1) == nullptr);
		d = a;
		d.misc |= Vulkan::IMAGE_MISC_FORCE_ARRAY_BIT;
		CHECK(pool.reuse(d, false, 5, 1) == nullptr);
		d = a;
		d.format = VK_FORMAT_R8G8B8A8_UNORM;
		CHECK(pool.reuse(d, false, 5, 1) == nullptr);
		/* asking for layer views reuses only an entry that has them */
		CHECK(pool.reuse(a, true, 5, 1) == nullptr);
		CHECK(pool.reuse(a, false, 5, 1) != nullptr);
	}

	/* A size change lets the old sizes go after a while, with no new
	 * image added since. */
	{
		RDP::VIImagePool pool;
		unsigned adds = 0;
		uint64_t serial;
		for (serial = 0; serial < 10; serial++)
			get(pool, image(100, 100), serial, 2, adds);
		for (; serial < 40; serial++)
			get(pool, image(200, 150), serial, 2, adds);
		CHECK(pool.size() == 2);
	}

	/* Bounded even when every frame asks for a new size. */
	{
		RDP::VIImagePool pool;
		unsigned adds = 0;
		for (uint64_t serial = 0; serial < 200; serial++)
			get(pool, image(64 + unsigned(serial), 64), serial, 1, adds);
		CHECK(pool.size() <= RDP::VIImagePool::MAX_ENTRIES);
	}

	/* Handed on: back only when the pool holds the last handle, however
	 * long after its frame context came round that is. */
	{
		RDP::VIImagePool pool;
		Vulkan::ImageCreateInfo a = image(640, 480);
		RDP::VIImagePool::Entry *e = pool.add(a, 0, 2);
		e->image = counted_image();
		Vulkan::ImageHandle shown = e->image;
		for (uint64_t serial = 2; serial < 8; serial++)
			CHECK(pool.reuse(a, false, serial, 2) == nullptr);
		/* nor evicted for a new size while it is out */
		for (unsigned i = 0; i < RDP::VIImagePool::MAX_ENTRIES; i++)
			pool.add(image(10 + i, 10), 0, 1);
		CHECK(pool.reuse(a, false, 8, 2) == nullptr);
		shown.reset();
		RDP::VIImagePool::Entry *back = pool.reuse(a, false, 8, 2);
		CHECK(back && back->image);
		/* the pool's last handle goes with it: keep it from the deleter */
		if (back && back->image)
			back->image->add_reference();
	}

	/* Full of busy entries: grows rather than evicting one in use. */
	{
		RDP::VIImagePool pool;
		for (unsigned i = 0; i < RDP::VIImagePool::MAX_ENTRIES + 4; i++)
			pool.add(image(10 + i, 10), 0, 1);
		CHECK(pool.size() == RDP::VIImagePool::MAX_ENTRIES + 4);
	}

	if (failures)
	{
		fprintf(stderr, "test_vi_image_pool: %d failures\n", failures);
		return 1;
	}
	printf("test_vi_image_pool: ok\n");
	return 0;
}
