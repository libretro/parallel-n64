/* test_incoherent_pages -- the RDRAM pages a parallel-RDP GPU read marks
 * for copying, on hosts whose RDRAM the GPU cannot import.
 *
 * Every page a read covers must be copied to the GPU before it reads:
 * masked where the GPU still has a readback pending or the CPU writes
 * pending, directly otherwise. The pages were marked one at a time from
 * start to end, wrapped at the ring; a read covering the whole ring
 * wrapped its end onto its start and marked nothing, so the GPU read
 * stale RDRAM. They are now marked a word at a time, at most the whole
 * ring. This holds the marks to a page-at-a-time reference over starts,
 * lengths (the whole ring and more included), ring sizes and pending
 * states, and checks that the old loop misses the whole ring.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "incoherent_pages.hpp"

static int failures;

static unsigned rng = 1;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

struct Pages
{
	uint32_t num;
	std::vector<uint32_t> pending_readback, masked, direct;
	std::vector<int> pending_writes;

	explicit Pages(uint32_t n)
		: num(n), pending_readback((n + 31) / 32), masked((n + 31) / 32), direct((n + 31) / 32), pending_writes(n)
	{
		for (uint32_t i = 0; i < n; i++)
		{
			if (rnd(5) == 0)
				pending_readback[i / 32] |= 1u << (i & 31);
			pending_writes[i] = rnd(7) == 0;
		}
	}
	bool bit(const std::vector<uint32_t> &v, uint32_t p) const { return (v[p / 32] >> (p & 31)) & 1; }
};

/* the marks, one page at a time, over [start, start + count) wrapped */
static void reference(Pages &p, uint32_t start, uint32_t count)
{
	if (count > p.num)
		count = p.num;
	for (uint32_t i = 0; i < count; i++)
	{
		uint32_t page = (start + i) & (p.num - 1);
		if (p.bit(p.pending_readback, page) || p.pending_writes[page])
			p.masked[page / 32] |= 1u << (page & 31);
		else
			p.direct[page / 32] |= 1u << (page & 31);
	}
}

/* the old loop: from start to end, both wrapped first */
static void old_loop(Pages &p, uint32_t start_page, uint32_t end_page)
{
	start_page &= p.num - 1;
	end_page &= p.num - 1;
	for (uint32_t page = start_page; page != end_page; page = (page + 1) & (p.num - 1))
	{
		if (p.bit(p.pending_readback, page) || p.pending_writes[page])
			p.masked[page / 32] |= 1u << (page & 31);
		else
			p.direct[page / 32] |= 1u << (page & 31);
	}
}

static void mark(Pages &p, uint32_t start, uint32_t count)
{
	const int *pw = p.pending_writes.data();
	RDP::mark_incoherent_pages_for_read(start, count, p.num, p.pending_readback.data(),
	                                    [pw](uint32_t page) { return pw[page] != 0; },
	                                    p.masked.data(), p.direct.data());
}

int main()
{
	static const uint32_t sizes[] = { 32, 64, 256, 2048 };
	unsigned runs = 0;

	for (uint32_t num : sizes)
	{
		for (unsigned iter = 0; iter < 400; iter++)
		{
			Pages a(num);
			Pages b = a;
			uint32_t start = rnd(4 * num);
			uint32_t count;
			switch (iter % 4)
			{
			case 0: count = 1 + rnd(40); break;
			case 1: count = 1 + rnd(num); break;
			case 2: count = num; break;               /* the whole ring */
			default: count = num + rnd(3 * num); break; /* more than it */
			}
			/* a few reads in a row accumulate */
			for (unsigned k = 0; k < 3; k++)
			{
				reference(a, start, count);
				mark(b, start, count);
				start = rnd(4 * num);
				count = 1 + rnd(num);
			}
			if (a.masked != b.masked || a.direct != b.direct)
			{
				if (failures++ < 10)
					fprintf(stderr, "ring %u, iteration %u: marks differ\n", num, iter);
			}
			++runs;
		}

		/* every page of a whole-ring read is marked, once, one way */
		{
			Pages p(num);
			mark(p, 5, num);
			for (uint32_t page = 0; page < num; page++)
				if (p.bit(p.masked, page) == p.bit(p.direct, page))
				{
					fprintf(stderr, "ring %u: page %u marked %s\n", num, page,
					        p.bit(p.masked, page) ? "both ways" : "not at all");
					failures++;
					break;
				}
		}

		/* the old loop marks nothing for it */
		{
			Pages p(num);
			old_loop(p, 5, 5 + num);
			bool any = false;
			for (uint32_t w = 0; w < p.masked.size(); w++)
				any |= p.masked[w] || p.direct[w];
			if (any)
			{
				fprintf(stderr, "ring %u: the old loop marks a whole-ring read\n", num);
				failures++;
			}
		}
	}

	if (failures)
	{
		fprintf(stderr, "test_incoherent_pages: %d failures\n", failures);
		return 1;
	}
	printf("test_incoherent_pages: %u runs ok\n", runs);
	return 0;
}
