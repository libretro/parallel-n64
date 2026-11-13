/* parallel-rdp's counted images and image views (vulkan/image.c), with a
 * stand-in owner.
 *
 * An image object holds the image, its memory and what it was made as,
 * with one reference and a cookie of its own; an image with views gets
 * an object for its own view, of all of it. With the image's last
 * reference the image, then its memory, go to the owner, then its view
 * is let go: its views go to the owner unless something else still
 * holds the view. A view made on its own hands back every view it
 * holds. Objects of the owner's thread say so.
 *
 * A level's size halves down to one texel. An image kept in the general
 * layout is in it for every use. The view to render a layer to is the
 * layer's own where there are layer views; the views to sample depth or
 * stencil alone are the per-aspect ones where there are any. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "image.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_image: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* ---- the stand-in owner ---- */
#define MAX_EVENTS 64
typedef struct event
{
	char what; /* 'd' destroy, 'm' free memory */
	unsigned kind;
	uint64_t handle;
	bool owner_thread;
} event_t;

static event_t events[MAX_EVENTS];
static unsigned num_events;
static int user_token;

static void fake_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	CHECK(user == &user_token, "the owner's pointer comes back");
	if (num_events < MAX_EVENTS)
	{
		events[num_events].what = 'd';
		events[num_events].kind = kind;
		events[num_events].handle = handle;
		events[num_events].owner_thread = owner_thread;
	}
	num_events++;
}

static void fake_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	CHECK(user == &user_token, "the owner's pointer comes back");
	if (num_events < MAX_EVENTS)
	{
		events[num_events].what = 'm';
		events[num_events].kind = 0;
		events[num_events].handle = GRANITE_VK_TO_BITS(alloc->base);
		events[num_events].owner_thread = owner_thread;
	}
	num_events++;
}

static granite_handle_owner_t owner;
static granite_image_pools_t pools;

#define IMG(n) GRANITE_VK_FROM_BITS(VkImage, 0x1000 + (n))
#define MEM(n) GRANITE_VK_FROM_BITS(VkDeviceMemory, 0x2000 + (n))
#define VIEW(n) GRANITE_VK_FROM_BITS(VkImageView, 0x3000 + (n))

static granite_image_info_t image_info(uint32_t w, uint32_t h, uint32_t levels, uint32_t layers)
{
	granite_image_info_t info;
	memset(&info, 0, sizeof(info));
	info.width = w;
	info.height = h;
	info.depth = 1;
	info.levels = levels;
	info.layers = layers;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.type = VK_IMAGE_TYPE_2D;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	return info;
}

static granite_allocation_t memory(unsigned n)
{
	granite_allocation_t alloc;
	memset(&alloc, 0, sizeof(alloc));
	if (n)
		alloc.base = MEM(n);
	return alloc;
}

static void test_image(void)
{
	granite_image_info_t info = image_info(320, 240, 3, 2);
	granite_allocation_t alloc = memory(1);
	granite_image_views_t views;
	granite_image_t *image, *plain, *again;
	granite_image_view_t *view;
	uint64_t cookie;

	memset(&views, 0, sizeof(views));
	views.view = VIEW(1);
	views.layer_views[0] = VIEW(2);
	views.layer_views[1] = VIEW(3);
	views.num_layer_views = 2;

	num_events = 0;
	image = granite_image_new(&pools, &owner, IMG(1), &alloc, &info, &views, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
	CHECK(image && image->image == IMG(1) && image->alloc.base == MEM(1) && image->info.width == 320 &&
			image->info.height == 240 && image->info.levels == 3 && image->info.layers == 2 &&
			image->info.initial_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			"an image object holds the image, its memory and what it was made as");
	CHECK(image->cookie != 0 && granite_refs_only(&image->refs) && !image->internal_sync && !image->general_layout &&
			image->stage_flags == 0 && image->access_flags == 0, "one reference, a cookie, nothing else set");
	view = image->view;
	CHECK(view && view->views.view == VIEW(1) && view->views.num_layer_views == 2 &&
			view->views.layer_views[1] == VIEW(3), "it has an object for its own view, with the views given");
	CHECK(view->info.image == image && view->info.format == VK_FORMAT_R8G8B8A8_UNORM && view->info.base_level == 0 &&
			view->info.levels == 3 && view->info.base_layer == 0 && view->info.layers == 2 &&
			view->info.view_type == VK_IMAGE_VIEW_TYPE_2D_ARRAY, "which is of all of the image");
	CHECK(view->cookie != 0 && view->cookie != image->cookie && granite_refs_only(&view->refs),
			"with a cookie of its own and one reference, the image's");
	cookie = image->cookie;

	/* sizes */
	CHECK(granite_image_width(image, 0) == 320 && granite_image_height(image, 0) == 240 &&
			granite_image_width(image, 2) == 80 && granite_image_height(image, 2) == 60, "a level is half the one above");
	CHECK(granite_image_width(image, 8) == 1 && granite_image_height(image, 8) == 1 &&
			granite_image_width(image, 9) == 1 && granite_image_height(image, 7) == 1, "never under one texel");

	/* layout */
	CHECK(granite_image_layout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			"an image is in the best layout for each use");
	image->general_layout = true;
	CHECK(granite_image_layout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_GENERAL,
			"unless it is kept in the general one");

	/* references */
	granite_image_ref(image);
	granite_image_unref(image);
	CHECK(num_events == 0 && granite_refs_only(&image->refs), "a reference added and dropped leaves it alone");

	granite_image_unref(image);
	CHECK(num_events == 5, "the last reference hands back the image, its memory and three views");
	CHECK(events[0].what == 'd' && events[0].kind == GRANITE_GARBAGE_IMAGE && events[0].handle == 0x1001 &&
			!events[0].owner_thread, "the image first");
	CHECK(events[1].what == 'm' && events[1].handle == 0x2001 && !events[1].owner_thread, "then its memory");
	CHECK(events[2].kind == GRANITE_GARBAGE_IMAGE_VIEW && events[2].handle == 0x3001 &&
			events[3].kind == GRANITE_GARBAGE_IMAGE_VIEW && events[3].handle == 0x3002 &&
			events[4].kind == GRANITE_GARBAGE_IMAGE_VIEW && events[4].handle == 0x3003 && !events[2].owner_thread,
			"then its view and the layer views");

	/* an image without views or memory of the allocator's */
	num_events = 0;
	alloc = memory(0);
	plain = granite_image_new(&pools, &owner, IMG(2), &alloc, &info, NULL, VK_IMAGE_VIEW_TYPE_MAX_ENUM);
	CHECK(plain == image && plain->view == NULL && plain->cookie != cookie && !plain->general_layout,
			"the slot serves the next image, which is a new object");
	plain->internal_sync = true;
	granite_image_unref(plain);
	CHECK(num_events == 1 && events[0].kind == GRANITE_GARBAGE_IMAGE && events[0].handle == 0x1002 &&
			events[0].owner_thread, "an image with no view and no memory hands back the image alone, marked");

	/* views given but without a view: no view object */
	memset(&views, 0, sizeof(views));
	alloc = memory(3);
	again = granite_image_new(&pools, &owner, IMG(3), &alloc, &info, &views, VK_IMAGE_VIEW_TYPE_2D);
	CHECK(again && again->view == NULL, "no view given, no view object");

	/* the view outlives the image when something else holds it */
	num_events = 0;
	granite_image_unref(again);
	views.view = VIEW(9);
	alloc = memory(4);
	image = granite_image_new(&pools, &owner, IMG(4), &alloc, &info, &views, VK_IMAGE_VIEW_TYPE_2D);
	view = image->view;
	granite_image_view_ref(view);
	num_events = 0;
	image->internal_sync = true;
	granite_image_unref(image);
	CHECK(num_events == 2 && events[0].handle == 0x1004 && events[1].handle == 0x2004 && events[1].owner_thread,
			"an image whose view is held elsewhere hands back image and memory");
	CHECK(granite_refs_only(&view->refs), "the view keeps its other reference");
	granite_image_view_unref(view);
	CHECK(num_events == 3 && events[2].kind == GRANITE_GARBAGE_IMAGE_VIEW && events[2].handle == 0x3009 &&
			!events[2].owner_thread, "and goes with that one, as marked itself");
}

static void test_view(void)
{
	granite_image_info_t info = image_info(64, 64, 1, 1);
	granite_allocation_t alloc = memory(5);
	granite_image_t *image = granite_image_new(&pools, &owner, IMG(5), &alloc, &info, NULL, VK_IMAGE_VIEW_TYPE_MAX_ENUM);
	granite_image_view_info_t view_info;
	granite_image_views_t views;
	granite_image_view_t *view;

	memset(&view_info, 0, sizeof(view_info));
	view_info.image = image;
	view_info.format = VK_FORMAT_D24_UNORM_S8_UINT;
	view_info.base_level = 1;
	view_info.levels = 1;
	view_info.base_layer = 1;
	view_info.layers = 1;
	view_info.view_type = VK_IMAGE_VIEW_TYPE_2D;

	/* a plain view */
	memset(&views, 0, sizeof(views));
	views.view = VIEW(20);
	view = granite_image_view_new(&pools, &owner, &views, &view_info);
	CHECK(view && view->info.image == image && view->info.base_level == 1 && view->info.base_layer == 1 &&
			view->info.format == VK_FORMAT_D24_UNORM_S8_UINT && view->info.view_type == VK_IMAGE_VIEW_TYPE_2D,
			"a view object holds what it is a view of");
	CHECK(granite_refs_only(&image->refs), "without a reference to the image");
	CHECK(granite_image_view_render_target(view, 0) == VIEW(20) && granite_image_view_render_target(view, 3) == VIEW(20),
			"without layer views every layer is rendered through the view");
	CHECK(granite_image_view_depth(view) == VIEW(20) && granite_image_view_stencil(view) == VIEW(20),
			"without aspect views depth and stencil are sampled through it");
	num_events = 0;
	granite_image_view_unref(view);
	CHECK(num_events == 1 && events[0].kind == GRANITE_GARBAGE_IMAGE_VIEW && events[0].handle == 0x3014,
			"it hands back its one view");

	/* every kind of view */
	views.view = VIEW(30);
	views.depth = VIEW(31);
	views.stencil = VIEW(32);
	views.layer_views[0] = VIEW(33);
	views.layer_views[1] = VIEW(34);
	views.num_layer_views = 2;
	{
		granite_image_view_t *gone = view;
		view = granite_image_view_new(&pools, &owner, &views, &view_info);
		CHECK(view == gone, "its slot serves the next view");
	}
	CHECK(granite_image_view_render_target(view, 0) == VIEW(33) && granite_image_view_render_target(view, 1) == VIEW(34),
			"with layer views a layer is rendered through its own");
	CHECK(granite_image_view_render_target(view, 2) == VIEW(30), "a layer without one through the view");
	CHECK(granite_image_view_depth(view) == VIEW(31) && granite_image_view_stencil(view) == VIEW(32),
			"with aspect views depth and stencil are sampled through theirs");
	num_events = 0;
	view->internal_sync = true;
	granite_image_view_unref(view);
	CHECK(num_events == 5 && events[0].handle == 0x301e && events[1].handle == 0x301f && events[2].handle == 0x3020 &&
			events[3].handle == 0x3021 && events[4].handle == 0x3022, "it hands back all five");
	CHECK(events[0].owner_thread && events[4].owner_thread && events[2].kind == GRANITE_GARBAGE_IMAGE_VIEW,
			"as views, from the owner's thread");

	granite_image_unref(image);
}

int main(void)
{
	owner.user = &user_token;
	owner.destroy = fake_destroy;
	owner.free_memory = fake_free_memory;
	granite_image_pools_init(&pools);

	test_image();
	test_view();

	granite_image_pools_free(&pools);

	if (failures)
	{
		fprintf(stderr, "test_vk_image: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_image: OK\n");
	return 0;
}
