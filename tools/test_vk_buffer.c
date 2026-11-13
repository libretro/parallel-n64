/* parallel-rdp's counted buffers and buffer views (vulkan/buffer.c), with
 * a stand-in owner.
 *
 * A buffer object starts with one reference and a cookie no other
 * object has. References are added and dropped from any thread; with
 * the last one the buffer, then its memory, go to the owner, marked as
 * coming from the owner's thread or not, and the object's slot is free
 * for the next. A view hands its VkBufferView back the same way and
 * does not keep its buffer alive. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>
#include <rthreads/rthreads.h>

#include "buffer.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_buffer: FAIL (line %d): %s\n", __LINE__, what); \
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
	VkDeviceMemory memory;
	uint32_t offset;
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
		events[num_events].memory = alloc->base;
		events[num_events].offset = alloc->offset;
		events[num_events].owner_thread = owner_thread;
	}
	num_events++;
}

static granite_handle_owner_t owner;
static granite_buffer_pools_t pools;

static granite_buffer_t *make_buffer(unsigned n, VkDeviceSize size)
{
	granite_allocation_t alloc;
	granite_buffer_info_t info;

	memset(&alloc, 0, sizeof(alloc));
	alloc.base = GRANITE_VK_FROM_BITS(VkDeviceMemory, 0x2000 + n);
	alloc.offset = 64 * n;
	alloc.size = (uint32_t)size;
	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_HOST;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	info.misc = GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT;
	return granite_buffer_new(&pools, &owner, GRANITE_VK_FROM_BITS(VkBuffer, 0x1000 + n), &alloc, &info);
}

static void test_buffer(void)
{
	granite_buffer_t *a, *b, *c;
	uint64_t cookie_a;

	num_events = 0;
	a = make_buffer(1, 4096);
	b = make_buffer(2, 8192);
	CHECK(a && b && a != b, "two buffer objects");
	CHECK(a->buffer == GRANITE_VK_FROM_BITS(VkBuffer, 0x1001) && a->alloc.base == GRANITE_VK_FROM_BITS(VkDeviceMemory, 0x2001) &&
			a->alloc.offset == 64 && a->info.size == 4096 && a->info.domain == GRANITE_BUFFER_DOMAIN_HOST &&
			a->info.usage == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT && a->info.misc == GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT,
			"hold the buffer, its memory and what it was made as");
	CHECK(a->cookie != 0 && b->cookie != 0 && a->cookie != b->cookie, "each with a cookie of its own");
	CHECK(!a->internal_sync && granite_refs_only(&a->refs), "one reference, not the owner thread's");
	cookie_a = a->cookie;

	granite_buffer_ref(a);
	CHECK(!granite_refs_only(&a->refs), "a second reference");
	granite_buffer_unref(a);
	CHECK(num_events == 0 && granite_refs_only(&a->refs), "dropping it leaves the buffer alone");

	granite_buffer_unref(a);
	CHECK(num_events == 2, "the last reference hands back two things");
	CHECK(events[0].what == 'd' && events[0].kind == GRANITE_GARBAGE_BUFFER && events[0].handle == 0x1001 &&
			!events[0].owner_thread, "the buffer first, from any thread");
	CHECK(events[1].what == 'm' && events[1].memory == GRANITE_VK_FROM_BITS(VkDeviceMemory, 0x2001) &&
			events[1].offset == 64 && !events[1].owner_thread, "then its memory");

	/* the slot is free again */
	c = make_buffer(3, 16);
	CHECK(c == a, "its slot serves the next buffer");
	CHECK(c->cookie != cookie_a && c->cookie != b->cookie && granite_refs_only(&c->refs) && !c->internal_sync,
			"which is a new object: new cookie, one reference");

	/* on the owner's thread */
	num_events = 0;
	b->internal_sync = true;
	granite_buffer_unref(b);
	CHECK(num_events == 2 && events[0].owner_thread && events[1].owner_thread && events[0].handle == 0x1002,
			"an object of the owner's thread says so when it goes");
	granite_buffer_unref(c);
	CHECK(num_events == 4 && events[2].handle == 0x1003 && !events[2].owner_thread, "the other does not");
}

static void test_view(void)
{
	granite_buffer_t *buffer = make_buffer(7, 256);
	granite_buffer_view_info_t info;
	granite_buffer_view_t *view, *null_view;

	memset(&info, 0, sizeof(info));
	info.buffer = buffer;
	info.format = VK_FORMAT_R16_UINT;
	info.offset = 32;
	info.range = 128;
	view = granite_buffer_view_new(&pools, &owner, GRANITE_VK_FROM_BITS(VkBufferView, 0x3001), &info);
	CHECK(view && view->view == GRANITE_VK_FROM_BITS(VkBufferView, 0x3001) && view->info.buffer == buffer &&
			view->info.format == VK_FORMAT_R16_UINT && view->info.offset == 32 && view->info.range == 128,
			"a view object holds the view and what it is of");
	CHECK(view->cookie != 0 && view->cookie != buffer->cookie && granite_refs_only(&view->refs),
			"with its own cookie and one reference");
	CHECK(granite_refs_only(&buffer->refs), "it does not hold a reference to its buffer");

	num_events = 0;
	granite_buffer_view_ref(view);
	granite_buffer_view_unref(view);
	CHECK(num_events == 0, "a view with a reference left stays");
	view->internal_sync = true;
	granite_buffer_view_unref(view);
	CHECK(num_events == 1 && events[0].what == 'd' && events[0].kind == GRANITE_GARBAGE_BUFFER_VIEW &&
			events[0].handle == 0x3001 && events[0].owner_thread, "its last reference hands the view back");

	/* a view object without a view has nothing to hand back */
	null_view = granite_buffer_view_new(&pools, &owner, VK_NULL_HANDLE, &info);
	CHECK(null_view == view, "the slot is reused");
	num_events = 0;
	granite_buffer_view_unref(null_view);
	CHECK(num_events == 0, "no view, nothing to destroy");

	granite_buffer_unref(buffer);
	CHECK(num_events == 2 && events[0].kind == GRANITE_GARBAGE_BUFFER, "the buffer goes on its own");
}

/* ---- references from several threads ---- */
#define THREADS 4
#define ROUNDS 20000
static granite_buffer_t *shared;

static void ref_thread(void *arg)
{
	unsigned i;
	(void)arg;
	for (i = 0; i < ROUNDS; i++)
	{
		granite_buffer_ref(shared);
		granite_buffer_unref(shared);
	}
	/* the reference this thread was given */
	granite_buffer_unref(shared);
}

static void test_threads(void)
{
	sthread_t *threads[THREADS];
	unsigned i;

	num_events = 0;
	shared = make_buffer(9, 64);
	for (i = 0; i < THREADS; i++)
		granite_buffer_ref(shared);
	for (i = 0; i < THREADS; i++)
		threads[i] = sthread_create(ref_thread, NULL);
	for (i = 0; i < THREADS; i++)
		sthread_join(threads[i]);
	CHECK(num_events == 0 && granite_refs_only(&shared->refs), "after the threads are done one reference is left");
	granite_buffer_unref(shared);
	CHECK(num_events == 2 && events[0].handle == 0x1009, "and with it the buffer goes, once");
}

int main(void)
{
	owner.user = &user_token;
	owner.destroy = fake_destroy;
	owner.free_memory = fake_free_memory;
	granite_buffer_pools_init(&pools);

	test_buffer();
	test_view();
	test_threads();

	granite_buffer_pools_free(&pools);

	if (failures)
	{
		fprintf(stderr, "test_vk_buffer: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vk_buffer: OK\n");
	return 0;
}
