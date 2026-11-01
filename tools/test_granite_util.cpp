/* test_granite_util -- Granite's util containers without the standard library.
 *
 * ObjectPool, ThreadSafeObjectPool, SmallVector and IntrusiveHashMap now
 * keep their storage in malloc'd arrays and placement-construct through
 * UTIL_PLACEMENT_NEW, and ThreadSafeObjectPool maps a slot index to its
 * block in closed form. This checks them against what the standard
 * containers did: every object constructed and destroyed exactly once,
 * slots unique across block boundaries and reused after a free, element
 * order kept by insert and erase, and lookups surviving table growth.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object_pool.hpp"
#include "small_vector.hpp"
#include "intrusive.hpp"
#include "intrusive_hash_map.hpp"

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

int main()
{
	test_thread_safe_pool();
	test_object_pool();
	test_small_vector();
	test_intrusive_hash_map();
	test_intrusive_ptr();
	if (failures)
	{
		fprintf(stderr, "test_granite_util: %d failures\n", failures);
		return 1;
	}
	printf("test_granite_util: ok\n");
	return 0;
}
