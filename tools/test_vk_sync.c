/* parallel-rdp's fence wait protocol (vulkan/fence_wait.c), its fence
 * and semaphore recycle pools (vulkan/sync_pools.c) and its object
 * cookies (vulkan/cookie.c), without a GPU.
 *
 * Many threads wait on one fence at once: the object's own wait runs on
 * one thread at a time and once, every waiter returns only after it, and
 * a wait that fails hands the fence back to the next waiter. The pools
 * hand back what was recycled, last in first out, make new objects when
 * empty, report a failed create as VK_NULL_HANDLE, and destroy what they
 * hold when freed. Cookies stay unique when taken from several threads
 * at once, and the 64-bit counter behind them carries past 32 bits on
 * 32-bit hosts too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#include "fence_wait.h"
#include "sync_pools.h"
#include "cookie.h"

#define WAITERS 8
#define ROUNDS  200

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vk_sync: FAIL: %s\n", what); \
			failures++; \
		} \
	} while (0)

/* ---- the fence wait ---- */

typedef struct fake_fence
{
	retro_atomic_int_t state;
	retro_atomic_int_t inside;   /* threads in fake_wait now */
	retro_atomic_int_t waits;    /* fake_wait calls */
	retro_atomic_int_t overlap;  /* fake_wait entered while another ran */
	retro_atomic_int_t signalled;
	retro_atomic_int_t early;    /* waiters back before the signal */
	int fail_first;              /* the first wait fails */
} fake_fence_t;

static bool fake_wait(void *ctx)
{
	fake_fence_t *f = (fake_fence_t *)ctx;
	int n = retro_atomic_fetch_add_int(&f->waits, 1);
	volatile unsigned spin;
	unsigned i;

	if (retro_atomic_fetch_add_int(&f->inside, 1) != 0)
		retro_atomic_fetch_add_int(&f->overlap, 1);
	for (i = 0; i < 2000; i++)
		spin = i;
	(void)spin;
	retro_atomic_fetch_sub_int(&f->inside, 1);

	if (n == 0 && f->fail_first)
		return false;
	retro_atomic_store_release_int(&f->signalled, 1);
	return true;
}

typedef struct waiter_args
{
	fake_fence_t *fence;
	retro_eventcount_t *ec;
	retro_atomic_int_t *go;
	retro_atomic_int_t *ok;
} waiter_args_t;

static void waiter(void *data)
{
	waiter_args_t *a = (waiter_args_t *)data;
	while (!retro_atomic_load_acquire_int(a->go))
		;
	/* a failed claim hands the fence back: try again, as a later
	 * submission's wait would */
	while (!granite_fence_wait(&a->fence->state, a->ec, fake_wait, a->fence))
		;
	if (!retro_atomic_load_acquire_int(&a->fence->signalled))
		retro_atomic_fetch_add_int(&a->fence->early, 1);
	retro_atomic_fetch_add_int(a->ok, 1);
}

static void run_round(retro_eventcount_t *ec, int fail_first)
{
	fake_fence_t f;
	retro_atomic_int_t go, ok;
	waiter_args_t args;
	sthread_t *threads[WAITERS];
	unsigned i;

	memset(&f, 0, sizeof(f));
	retro_atomic_int_init(&f.state, GRANITE_FENCE_UNWAITED);
	retro_atomic_int_init(&f.inside, 0);
	retro_atomic_int_init(&f.waits, 0);
	retro_atomic_int_init(&f.overlap, 0);
	retro_atomic_int_init(&f.signalled, 0);
	retro_atomic_int_init(&f.early, 0);
	f.fail_first = fail_first;
	retro_atomic_int_init(&go, 0);
	retro_atomic_int_init(&ok, 0);
	args.fence = &f;
	args.ec = ec;
	args.go = &go;
	args.ok = &ok;

	for (i = 0; i < WAITERS; i++)
		threads[i] = sthread_create(waiter, &args);
	retro_atomic_store_release_int(&go, 1);
	for (i = 0; i < WAITERS; i++)
		if (threads[i])
			sthread_join(threads[i]);

	CHECK(retro_atomic_load_acquire_int(&ok) == WAITERS, "a waiter did not return");
	CHECK(retro_atomic_load_acquire_int(&f.early) == 0, "a waiter returned before the signal");
	CHECK(granite_fence_seen(&f.state), "the fence is not marked seen");
	if (ec)
	{
		CHECK(retro_atomic_load_acquire_int(&f.overlap) == 0, "two threads waited on the fence at once");
		CHECK(retro_atomic_load_acquire_int(&f.waits) == 1 + fail_first,
				"the fence was waited on more often than needed");
	}
}

static void test_fence_wait(void)
{
	retro_eventcount_t ec;
	unsigned r;
	bool have_ec = retro_eventcount_init(&ec);
	CHECK(have_ec, "no eventcount");
	for (r = 0; r < ROUNDS && !failures; r++)
	{
		run_round(have_ec ? &ec : NULL, (int)(r & 1));
		/* without the eventcount, every thread waits on the object */
		run_round(NULL, (int)(r & 1));
	}
	if (have_ec)
		retro_eventcount_free(&ec);
}

/* ---- the pools, through a stand-in device table ---- */

static unsigned live_fences, live_semaphores, next_handle;
static int fail_creates;

static VkResult VKAPI_CALL fake_create_fence(VkDevice d, const VkFenceCreateInfo *info,
		const VkAllocationCallbacks *a, VkFence *fence)
{
	(void)d; (void)info; (void)a;
	if (fail_creates)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	live_fences++;
	*fence = (VkFence)(uintptr_t)(++next_handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_fence(VkDevice d, VkFence fence, const VkAllocationCallbacks *a)
{
	(void)d; (void)fence; (void)a;
	live_fences--;
}

static VkResult VKAPI_CALL fake_create_semaphore(VkDevice d, const VkSemaphoreCreateInfo *info,
		const VkAllocationCallbacks *a, VkSemaphore *semaphore)
{
	(void)d; (void)info; (void)a;
	if (fail_creates)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	live_semaphores++;
	*semaphore = (VkSemaphore)(uintptr_t)(++next_handle);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_destroy_semaphore(VkDevice d, VkSemaphore semaphore, const VkAllocationCallbacks *a)
{
	(void)d; (void)semaphore; (void)a;
	live_semaphores--;
}

static void test_pools(void)
{
	static struct VolkDeviceTable table;
	granite_fence_pool_t fences;
	granite_semaphore_pool_t semaphores;
	VkFence f[40];
	VkSemaphore s[40];
	unsigned i;

	table.vkCreateFence = fake_create_fence;
	table.vkDestroyFence = fake_destroy_fence;
	table.vkCreateSemaphore = fake_create_semaphore;
	table.vkDestroySemaphore = fake_destroy_semaphore;
	granite_fence_pool_init(&fences, VK_NULL_HANDLE, &table);
	granite_semaphore_pool_init(&semaphores, VK_NULL_HANDLE, &table);

	/* past the first growth of the kept list */
	for (i = 0; i < 40; i++)
	{
		f[i] = granite_fence_pool_request(&fences);
		s[i] = granite_semaphore_pool_request(&semaphores);
	}
	CHECK(live_fences == 40 && live_semaphores == 40, "requests did not create");
	for (i = 0; i < 40; i++)
	{
		granite_fence_pool_recycle(&fences, f[i]);
		granite_semaphore_pool_recycle(&semaphores, s[i]);
	}
	granite_semaphore_pool_recycle(&semaphores, VK_NULL_HANDLE);
	granite_fence_pool_recycle(&fences, VK_NULL_HANDLE);

	/* reused, last in first out, before anything is created */
	for (i = 40; i-- > 0;)
	{
		CHECK(granite_fence_pool_request(&fences) == f[i], "fence not reused in order");
		CHECK(granite_semaphore_pool_request(&semaphores) == s[i], "semaphore not reused in order");
	}
	CHECK(live_fences == 40 && live_semaphores == 40, "a reuse created");

	fail_creates = 1;
	CHECK(granite_fence_pool_request(&fences) == VK_NULL_HANDLE, "failed fence create not reported");
	CHECK(granite_semaphore_pool_request(&semaphores) == VK_NULL_HANDLE, "failed semaphore create not reported");
	fail_creates = 0;

	for (i = 0; i < 40; i++)
	{
		granite_fence_pool_recycle(&fences, f[i]);
		granite_semaphore_pool_recycle(&semaphores, s[i]);
	}
	granite_fence_pool_free(&fences);
	granite_semaphore_pool_free(&semaphores);
	CHECK(live_fences == 0 && live_semaphores == 0, "free left objects alive");
}

/* ---- cookies ---- */

#define COOKIE_THREADS     4
#define COOKIES_PER_THREAD 20000

static uint64_t cookies[COOKIE_THREADS][COOKIES_PER_THREAD];

static void cookie_thread(void *arg)
{
	uint64_t *out = (uint64_t *)arg;
	unsigned i;
	for (i = 0; i < COOKIES_PER_THREAD; i++)
		out[i] = granite_cookie_next();
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

static void test_cookies(void)
{
	granite_counter64_t c;
	sthread_t *threads[COOKIE_THREADS];
	const uint64_t *all;
	unsigned i;

	granite_counter64_init(&c, 0xfffffff0u);
	CHECK(granite_counter64_add(&c, 16) == ((uint64_t)1 << 32), "the counter did not carry past 32 bits");
	CHECK(granite_counter64_add(&c, 16) == ((uint64_t)1 << 32) + 16, "the counter did not count on past 32 bits");

	for (i = 0; i < COOKIE_THREADS; i++)
		threads[i] = sthread_create(cookie_thread, cookies[i]);
	for (i = 0; i < COOKIE_THREADS; i++)
		if (threads[i])
			sthread_join(threads[i]);
		else
			cookie_thread(cookies[i]);
	all = &cookies[0][0];
	qsort(cookies, COOKIE_THREADS * COOKIES_PER_THREAD, sizeof(uint64_t), cmp_u64);
	for (i = 0; i < COOKIE_THREADS * COOKIES_PER_THREAD; i++)
		if (all[i] != (uint64_t)(i + 1) * 16)
		{
			CHECK(0, "cookies not unique, or not in steps of 16");
			break;
		}
}

int main(void)
{
	test_pools();
	test_cookies();
	test_fence_wait();
	if (failures)
		return 1;
	printf("test_vk_sync: ok\n");
	return 0;
}
