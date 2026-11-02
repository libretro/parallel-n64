/* test_granite_util -- Granite's util containers without the standard library.
 *
 * ObjectPool, ThreadSafeObjectPool, SmallVector and IntrusiveHashMap now
 * keep their storage in malloc'd arrays and placement-construct through
 * UTIL_PLACEMENT_NEW, and ThreadSafeObjectPool maps a slot index to its
 * block in closed form. This checks them against what the standard
 * containers did: every object constructed and destroyed exactly once,
 * slots unique across block boundaries and reused after a free, element
 * order kept by insert and erase, and lookups surviving table growth.
 * The Vulkan layer's replacements for std::vector, unique_ptr arrays,
 * std::atomic<uint64_t> and std::string tags are checked the same way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object_pool.hpp"
#include "small_vector.hpp"
#include "intrusive.hpp"
#include "intrusive_hash_map.hpp"
#include "bitops.hpp"

#include <rthreads/rthreads.h>

static int failures;
static int live;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct Tracked
{
	int value;
	explicit Tracked(int v = 0) : value(v) { live++; }
	Tracked(const Tracked &o) : value(o.value) { live++; }
	Tracked(Tracked &&o) : value(o.value) { o.value = -1; live++; }
	Tracked &operator=(const Tracked &o) { value = o.value; return *this; }
	Tracked &operator=(Tracked &&o) { value = o.value; o.value = -1; return *this; }
	~Tracked() { live--; }
};

static int cmp_ptr(const void *a, const void *b)
{
	uintptr_t x = (uintptr_t)*(void *const *)a, y = (uintptr_t)*(void *const *)b;
	return x < y ? -1 : x > y;
}

/* Enough slots to fill blocks 0..5 and start block 6: 64 * (2^6 - 1) + 3 */
enum { POOL_N = 64 * 63 + 3 };

static void test_thread_safe_pool()
{
	static Tracked *ptrs[POOL_N], *again[POOL_N];
	Util::ThreadSafeObjectPool<Tracked> pool;
	int i;

	for (i = 0; i < POOL_N; i++)
	{
		ptrs[i] = pool.allocate(i);
		CHECK(ptrs[i] && ptrs[i]->value == i);
	}
	CHECK(live == POOL_N);

	/* Distinct slots, none overlapping */
	memcpy(again, ptrs, sizeof(ptrs));
	qsort(again, POOL_N, sizeof(*again), cmp_ptr);
	for (i = 1; i < POOL_N; i++)
		CHECK((char *)again[i] - (char *)again[i - 1] >= (ptrdiff_t)sizeof(Tracked));

	/* Values intact after every block was added */
	for (i = 0; i < POOL_N; i++)
		CHECK(ptrs[i]->value == i);

	for (i = 0; i < POOL_N; i++)
		pool.free(ptrs[i]);
	CHECK(live == 0);

	/* Freed slots come back before any new block is added */
	for (i = 0; i < POOL_N; i++)
		again[i] = pool.allocate(i);
	qsort(again, POOL_N, sizeof(*again), cmp_ptr);
	qsort(ptrs, POOL_N, sizeof(*ptrs), cmp_ptr);
	CHECK(memcmp(again, ptrs, sizeof(ptrs)) == 0);
	for (i = 0; i < POOL_N; i++)
		pool.free(again[i]);
	CHECK(live == 0);
}

static void test_object_pool()
{
	static Tracked *ptrs[POOL_N];
	Util::ObjectPool<Tracked> pool;
	int i;

	for (i = 0; i < POOL_N; i++)
		ptrs[i] = pool.allocate(i);
	for (i = 0; i < POOL_N; i++)
		CHECK(ptrs[i]->value == i);
	for (i = 0; i < POOL_N; i += 2)
		pool.free(ptrs[i]);
	CHECK(live == POOL_N / 2);
	for (i = 0; i < POOL_N; i += 2)
		ptrs[i] = pool.allocate(-i);
	for (i = 0; i < POOL_N; i++)
		CHECK(ptrs[i]->value == ((i & 1) ? i : -i));
	for (i = 0; i < POOL_N; i++)
		pool.free(ptrs[i]);
	CHECK(live == 0);
	pool.clear();
	ptrs[0] = pool.allocate(7);
	CHECK(ptrs[0] && ptrs[0]->value == 7);
	pool.free(ptrs[0]);
}

static bool vec_is(const Util::SmallVector<Tracked, 4> &v, const int *want, size_t n)
{
	size_t i;
	if (v.size() != n)
		return false;
	for (i = 0; i < n; i++)
		if (v[i].value != want[i])
			return false;
	return true;
}

static void test_small_vector()
{
	{
		Util::SmallVector<Tracked, 4> v;
		int i;
		for (i = 0; i < 10; i++)
			v.emplace_back(i);
		CHECK(live == 10);
		{
			static const int want[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
			CHECK(vec_is(v, want, 10));
		}

		/* Erase from the middle shifts the tail down */
		v.erase(v.begin() + 2, v.begin() + 5);
		{
			static const int want[] = { 0, 1, 5, 6, 7, 8, 9 };
			CHECK(vec_is(v, want, 7));
		}
		CHECK(live == 7);

		/* Single insert in the middle, then a range insert past the capacity */
		v.insert(v.begin() + 1, Tracked(42));
		{
			static const int want[] = { 0, 42, 1, 5, 6, 7, 8, 9 };
			CHECK(vec_is(v, want, 8));
		}
		{
			Tracked extra[20];
			for (i = 0; i < 20; i++)
				extra[i].value = 100 + i;
			v.insert(v.begin() + 3, extra, extra + 20);
			CHECK(v.size() == 28);
			CHECK(v[2].value == 1 && v[3].value == 100 && v[22].value == 119 && v[23].value == 5 && v[27].value == 9);
		}
		CHECK(live == 28);

		v.resize(3);
		CHECK(live == 3 && v.size() == 3);
		v.resize(6);
		CHECK(live == 6 && v[5].value == 0);
		v.pop_back();
		CHECK(live == 5);

		/* Moves steal heap storage and keep the source usable */
		Util::SmallVector<Tracked, 4> w(Util::move(v));
		CHECK(w.size() == 5 && v.size() == 0);
		v.push_back(Tracked(1));
		CHECK(v.size() == 1 && v[0].value == 1);
		CHECK(live == 6);

		/* Copy into inline storage */
		Util::SmallVector<Tracked, 4> c;
		c = v;
		CHECK(c.size() == 1 && c[0].value == 1 && live == 7);
		c.clear();
		CHECK(live == 6);
	}
	CHECK(live == 0);
}

struct Node : Util::IntrusiveHashMapEnabled<Node>
{
	explicit Node(int v) : value(v) { live++; }
	~Node() { live--; }
	int value;
};

static void test_intrusive_hash_map()
{
	{
		Util::IntrusiveHashMap<Node> map;
		Util::Hash h;
		for (h = 0; h < 5000; h++)
			map.emplace_yield(h * 0x9e3779b97f4a7c15ull, int(h));
		CHECK(live == 5000);
		for (h = 0; h < 5000; h++)
		{
			Node *n = map.find(h * 0x9e3779b97f4a7c15ull);
			CHECK(n && n->value == int(h));
		}
		CHECK(!map.find(12345));

		/* yield keeps the old value, replace frees it */
		CHECK(map.emplace_yield(0, -1)->value == 0);
		CHECK(map.emplace_replace(0, -2)->value == -2);
		CHECK(live == 5000);

		for (h = 0; h < 5000; h += 2)
			map.erase(h * 0x9e3779b97f4a7c15ull);
		CHECK(live == 2500);
		for (h = 1; h < 5000; h += 2)
			CHECK(map.find(h * 0x9e3779b97f4a7c15ull) != nullptr);
		CHECK(!map.find(2 * 0x9e3779b97f4a7c15ull));
	}
	CHECK(live == 0);
}

struct Shared : Util::ThreadSafeIntrusivePtrEnabled<Shared>
{
	Shared() { live++; }
	~Shared() { live--; }
};

static void test_intrusive_ptr()
{
	{
		Util::IntrusivePtr<Shared> a = Util::make_handle<Shared>();
		Util::IntrusivePtr<Shared> b = a;
		Util::IntrusivePtr<Shared> c(Util::move(b));
		CHECK(!b && a.get() == c.get() && live == 1);
		a.reset();
		CHECK(live == 1);
	}
	CHECK(live == 0);
}

static void test_heap_vector()
{
	{
		Util::Vector<Tracked> v;
		Util::Vector<Tracked> empty_moved(Util::move(v));
		CHECK(empty_moved.empty() && v.empty());

		int i;
		for (i = 0; i < 100; i++)
			v.push_back(Tracked(i));
		CHECK(v.size() == 100 && live == 100);
		for (i = 0; i < 100; i++)
			CHECK(v[i].value == i);

		/* pop_back destroys exactly the last element */
		v.pop_back();
		CHECK(v.size() == 99 && live == 99 && v.back().value == 98);

		/* resize with a fill value, both directions */
		v.resize(120, Tracked(-7));
		CHECK(v.size() == 120 && live == 120 && v[98].value == 98 && v[99].value == -7 && v[119].value == -7);
		v.resize(10, Tracked(5));
		CHECK(v.size() == 10 && live == 10 && v[9].value == 9);

		Util::Vector<Tracked> w;
		w = Util::move(v);
		CHECK(w.size() == 10 && v.empty() && live == 10);
		Util::swap(v, w);
		CHECK(v.size() == 10 && w.empty() && live == 10);
	}
	CHECK(live == 0);
}

static void test_owned_ptr_array()
{
	{
		Util::OwnedPtrArray<Tracked, 4> a;
		a.push_back(Util::create<Tracked>(1));
		a.push_back(Util::create<Tracked>(2));
		CHECK(a.size() == 2 && live == 2 && a[1]->value == 2);
		int sum = 0;
		for (auto *t : a)
			sum += t->value;
		CHECK(sum == 3);
		a.clear();
		CHECK(a.empty() && live == 0);
		a.push_back(Util::create<Tracked>(3));
	}
	CHECK(live == 0);
}

static void test_scalars()
{
	Util::AtomicU64 c;
	c.init(0xfffffff0ull);
	/* crosses 32 bits, as the 64-bit cookie counter must on 32-bit hosts */
	CHECK(c.add_fetch_relaxed(16) == 0x100000000ull);
	CHECK(c.add_fetch_relaxed(16) == 0x100000010ull);

	char buf[8];
	Util::copy_string(buf, sizeof(buf), "abc");
	CHECK(strcmp(buf, "abc") == 0);
	Util::copy_string(buf, sizeof(buf), "truncated tag");
	CHECK(strcmp(buf, "truncat") == 0);
	Util::copy_string(buf, sizeof(buf), nullptr);
	CHECK(buf[0] == '\0');

	CHECK(Util::min_of(3u, 7u) == 3u && Util::max_of(3u, 7u) == 7u);
	CHECK(Util::max_of<unsigned>(size_t(9), 1u) == 9u);
}

/* Cookies from several threads at once stay unique, as Device hands
 * them out from any thread. */
enum { COOKIE_THREADS = 4, COOKIES_PER_THREAD = 20000 };
static Util::AtomicU64 shared_cookie;
static uint64_t cookies[COOKIE_THREADS][COOKIES_PER_THREAD];

static void cookie_thread(void *arg)
{
	uint64_t *out = (uint64_t *)arg;
	int i;
	for (i = 0; i < COOKIES_PER_THREAD; i++)
		out[i] = shared_cookie.add_fetch_relaxed(16);
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

static void test_concurrent_cookies()
{
	sthread_t *threads[COOKIE_THREADS];
	int i;
	shared_cookie.init(0);
	for (i = 0; i < COOKIE_THREADS; i++)
		threads[i] = sthread_create(cookie_thread, cookies[i]);
	for (i = 0; i < COOKIE_THREADS; i++)
		if (threads[i])
			sthread_join(threads[i]);
		else
			cookie_thread(cookies[i]);
	qsort(cookies, COOKIE_THREADS * COOKIES_PER_THREAD, sizeof(uint64_t), cmp_u64);
	const uint64_t *all = &cookies[0][0];
	for (i = 0; i < COOKIE_THREADS * COOKIES_PER_THREAD; i++)
		CHECK(all[i] == uint64_t(i + 1) * 16);
}

/* next_bit / next_bit_range against a bit-by-bit walk */
static void check_bits(uint32_t mask)
{
	uint32_t expect[32], got[32];
	unsigned ne = 0, ng = 0, i;
	for (i = 0; i < 32; i++)
		if (mask & (1u << i))
			expect[ne++] = i;
	for (uint32_t bits = mask, bit; Util::next_bit(bits, bit);)
		got[ng++] = bit;
	CHECK(ne == ng && memcmp(expect, got, ne * sizeof(uint32_t)) == 0);

	/* runs: each must be maximal, in order, and cover the mask exactly */
	uint32_t covered = 0, last_end = 0;
	bool first = true;
	for (uint32_t bits = mask, base = 0, off, count; Util::next_bit_range(bits, base, off, count);)
	{
		uint64_t run = ((count == 32 ? 0xffffffffull : ((1ull << count) - 1)) << off);
		CHECK(count >= 1 && off + count <= 32);
		CHECK(first || off > last_end);
		CHECK((covered & uint32_t(run)) == 0);
		CHECK((mask & uint32_t(run)) == uint32_t(run));
		CHECK(off + count == 32 || !(mask & (1u << (off + count))));
		covered |= uint32_t(run);
		last_end = off + count;
		first = false;
	}
	CHECK(covered == mask);
}

static void test_bit_iteration()
{
	static const uint32_t fixed[] = { 0, 1, 0x80000000u, ~0u, 0x7fffffffu, 0xfffffffeu, 0x0f0f0f0fu, 0xaaaaaaaau, 0x80000001u };
	uint32_t x = 0x12345678u;
	unsigned i;
	for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
		check_bits(fixed[i]);
	for (i = 0; i < 200000; i++)
	{
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		check_bits(x);
		check_bits(x & (x >> 1));
	}
}

int main()
{
	test_thread_safe_pool();
	test_object_pool();
	test_small_vector();
	test_intrusive_hash_map();
	test_intrusive_ptr();
	test_heap_vector();
	test_owned_ptr_array();
	test_scalars();
	test_concurrent_cookies();
	test_bit_iteration();
	if (failures)
	{
		fprintf(stderr, "test_granite_util: %d failures\n", failures);
		return 1;
	}
	printf("test_granite_util: ok\n");
	return 0;
}
