/* parallel-rdp's per-frame garbage lists (vulkan/frame_garbage.c),
 * through a stand-in device table: everything a frame context lets go of
 * is destroyed exactly once when it is collected, kind by kind in the
 * order the frame context always destroyed them (framebuffers before
 * the views they use, views before their images and buffers), recycled
 * semaphores go back to the pool instead, fences are reset in batches
 * and go back to theirs, and the lists are empty and reusable
 * afterwards. */
#include <stdio.h>
#include <string.h>

#include "frame_garbage.h"

#define PER_KIND 40 /* past the lists' first growth */
#define MAX_LOG  (GRANITE_GARBAGE_KINDS * PER_KIND * 2)

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_frame_garbage: FAIL: %s\n", what); \
			failures++; \
		} \
	} while (0)

/* every destroy, in order: the kind it came in as, and the handle */
static unsigned log_kind[MAX_LOG];
static uint64_t log_handle[MAX_LOG];
static unsigned log_count;
static unsigned live_semaphores;
static unsigned fences_reset, reset_calls;
static uint64_t reset_handles[MAX_LOG];

static void note(unsigned kind, uint64_t handle)
{
	if (log_count < MAX_LOG)
	{
		log_kind[log_count] = kind;
		log_handle[log_count] = handle;
	}
	log_count++;
}

#define FAKE_DESTROY(name, T, kind) \
	static void VKAPI_CALL name(VkDevice d, T h, const VkAllocationCallbacks *a) \
	{ \
		(void)d; (void)a; \
		note(kind, GRANITE_VK_TO_BITS(h)); \
	}

FAKE_DESTROY(fake_destroy_framebuffer, VkFramebuffer, GRANITE_GARBAGE_FRAMEBUFFER)
FAKE_DESTROY(fake_destroy_sampler, VkSampler, GRANITE_GARBAGE_SAMPLER)
FAKE_DESTROY(fake_destroy_pipeline, VkPipeline, GRANITE_GARBAGE_PIPELINE)
FAKE_DESTROY(fake_destroy_image_view, VkImageView, GRANITE_GARBAGE_IMAGE_VIEW)
FAKE_DESTROY(fake_destroy_buffer_view, VkBufferView, GRANITE_GARBAGE_BUFFER_VIEW)
FAKE_DESTROY(fake_destroy_image, VkImage, GRANITE_GARBAGE_IMAGE)
FAKE_DESTROY(fake_destroy_buffer, VkBuffer, GRANITE_GARBAGE_BUFFER)

static unsigned fences_destroyed;

static void VKAPI_CALL fake_destroy_fence(VkDevice d, VkFence h, const VkAllocationCallbacks *a)
{
	(void)d; (void)h; (void)a;
	fences_destroyed++;
}

static void VKAPI_CALL fake_destroy_semaphore(VkDevice d, VkSemaphore h, const VkAllocationCallbacks *a)
{
	(void)d; (void)a;
	note(GRANITE_GARBAGE_SEMAPHORE, GRANITE_VK_TO_BITS(h));
	live_semaphores--;
}

static unsigned first_of(unsigned kind)
{
	unsigned i;
	for (i = 0; i < log_count && i < MAX_LOG; i++)
		if (log_kind[i] == kind)
			return i;
	return MAX_LOG;
}

static unsigned last_of(unsigned kind)
{
	unsigned i, at = 0;
	for (i = 0; i < log_count && i < MAX_LOG; i++)
		if (log_kind[i] == kind)
			at = i;
	return at;
}

static VkResult VKAPI_CALL fake_reset_fences(VkDevice d, uint32_t n, const VkFence *f)
{
	uint32_t i;
	(void)d;
	reset_calls++;
	for (i = 0; i < n; i++)
	{
		if (fences_reset < MAX_LOG)
			reset_handles[fences_reset] = GRANITE_VK_TO_BITS(f[i]);
		fences_reset++;
	}
	return VK_SUCCESS;
}

/* distinct per kind and index, never 0 */
static uint64_t handle_of(unsigned kind, unsigned i)
{
	return ((uint64_t)(kind + 1) << 16) | (uint64_t)(i + 1);
}

static void fill(granite_frame_garbage_t *g)
{
	unsigned kind, i;
	/* interleaved, as releases arrive */
	for (i = 0; i < PER_KIND; i++)
		for (kind = 0; kind < GRANITE_GARBAGE_KINDS; kind++)
			granite_frame_garbage_push(g, (enum granite_garbage_kind)kind, handle_of(kind, i));
}

static void check_round(granite_frame_garbage_t *g, granite_semaphore_pool_t *pool,
		granite_fence_pool_t *fpool)
{
	unsigned i, kind, expect = 0;
	unsigned recycle = GRANITE_GARBAGE_RECYCLE_SEMAPHORE;
	unsigned fence = GRANITE_GARBAGE_RECYCLE_FENCE;
	size_t pooled_before = pool->count;
	size_t fences_before = fpool->count;

	log_count = 0;
	fences_reset = 0;
	reset_calls = 0;
	live_semaphores = 2 * PER_KIND;
	granite_frame_garbage_collect(g, VK_NULL_HANDLE, pool->table, pool, fpool);

	CHECK(log_count == (GRANITE_GARBAGE_KINDS - 2) * PER_KIND, "wrong number of destroys");
	CHECK(fences_reset == PER_KIND, "fences not all reset");
	CHECK(reset_calls == (PER_KIND + 31) / 32, "fences not reset in batches");
	CHECK(fpool->count == fences_before + PER_KIND, "reset fences did not reach the pool");
	for (i = 0; i < PER_KIND && i < fences_reset; i++)
	{
		CHECK(reset_handles[i] == handle_of(fence, i), "the wrong fences were reset");
		CHECK(fpool->items[fences_before + i] == GRANITE_VK_FROM_BITS(VkFence, handle_of(fence, i)),
				"the fence pool holds the wrong fences");
	}
	CHECK(g->lists[fence].count == 0, "the fence list is not empty after collecting");
	for (i = 0; i < log_count && i < MAX_LOG; i++)
	{
		/* each kind in one run */
		if (log_kind[i] < expect)
		{
			CHECK(0, "a kind's destroys are not together");
			break;
		}
		expect = log_kind[i];
		CHECK(log_kind[i] != recycle, "a recycled semaphore was destroyed");
		CHECK(log_kind[i] != fence, "a recycled fence was destroyed");
	}
	/* what uses an object goes before it */
	CHECK(last_of(GRANITE_GARBAGE_FRAMEBUFFER) < first_of(GRANITE_GARBAGE_IMAGE_VIEW),
			"an image view was destroyed before a framebuffer");
	CHECK(last_of(GRANITE_GARBAGE_IMAGE_VIEW) < first_of(GRANITE_GARBAGE_IMAGE),
			"an image was destroyed before an image view");
	CHECK(last_of(GRANITE_GARBAGE_BUFFER_VIEW) < first_of(GRANITE_GARBAGE_BUFFER),
			"a buffer was destroyed before a buffer view");
	for (kind = 0; kind < GRANITE_GARBAGE_KINDS; kind++)
	{
		unsigned seen = 0;
		if (kind == recycle || kind == fence)
			continue;
		for (i = 0; i < log_count && i < MAX_LOG; i++)
			if (log_kind[i] == kind && log_handle[i] == handle_of(kind, i % PER_KIND))
				seen++;
		CHECK(seen == PER_KIND, "a handle was not destroyed, or not in the order released");
		CHECK(g->lists[kind].count == 0, "a list is not empty after collecting");
	}
	CHECK(pool->count == pooled_before + PER_KIND, "recycled semaphores did not reach the pool");
	for (i = 0; i < PER_KIND; i++)
		CHECK(pool->items[pooled_before + i] == GRANITE_VK_FROM_BITS(VkSemaphore, handle_of(recycle, i)),
				"the pool holds the wrong semaphores");
}

int main(void)
{
	static struct VolkDeviceTable table;
	static granite_frame_garbage_t g;
	granite_semaphore_pool_t pool;
	granite_fence_pool_t fpool;
	VkImage image = GRANITE_VK_FROM_BITS(VkImage, handle_of(GRANITE_GARBAGE_IMAGE, 7));

	CHECK(GRANITE_VK_TO_BITS(image) == handle_of(GRANITE_GARBAGE_IMAGE, 7), "handle bits do not round-trip");

	table.vkDestroyFramebuffer = fake_destroy_framebuffer;
	table.vkDestroySampler = fake_destroy_sampler;
	table.vkDestroyPipeline = fake_destroy_pipeline;
	table.vkDestroyImageView = fake_destroy_image_view;
	table.vkDestroyBufferView = fake_destroy_buffer_view;
	table.vkDestroyImage = fake_destroy_image;
	table.vkDestroyBuffer = fake_destroy_buffer;
	table.vkDestroySemaphore = fake_destroy_semaphore;
	table.vkResetFences = fake_reset_fences;
	table.vkDestroyFence = fake_destroy_fence;
	granite_semaphore_pool_init(&pool, VK_NULL_HANDLE, &table);
	granite_fence_pool_init(&fpool, VK_NULL_HANDLE, &table);

	fill(&g);
	CHECK(granite_frame_garbage_has(&g, GRANITE_GARBAGE_BUFFER, handle_of(GRANITE_GARBAGE_BUFFER, 5)),
			"a pushed handle is not found");
	CHECK(!granite_frame_garbage_has(&g, GRANITE_GARBAGE_IMAGE, handle_of(GRANITE_GARBAGE_BUFFER, 5)),
			"a handle is found under the wrong kind");
	check_round(&g, &pool, &fpool);

	/* the emptied lists take the next frame's releases */
	fill(&g);
	check_round(&g, &pool, &fpool);

	granite_frame_garbage_free(&g);
	live_semaphores = 2 * PER_KIND;
	granite_semaphore_pool_free(&pool);
	CHECK(live_semaphores == 0, "the pool did not destroy the recycled semaphores");
	granite_fence_pool_free(&fpool);
	CHECK(fences_destroyed == 2 * PER_KIND, "the pool did not destroy the recycled fences");

	if (failures)
		return 1;
	printf("test_frame_garbage: ok\n");
	return 0;
}
