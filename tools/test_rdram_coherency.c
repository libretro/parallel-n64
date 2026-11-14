/* parallel-RDP's RDRAM kept in step with the GPU's
 * (parallel-rdp/rdram_coherency.c), for hosts whose RDRAM the GPU cannot
 * import, on a real device (vulkan/device.c) over a stand-in device table.
 *
 * - The pages a GPU read covers are marked: under the mask where a GPU
 *   write to them is pending (or not yet read back), whole otherwise, a
 *   word of pages at a time, wrapping round the ring, a read of the
 *   whole ring or more marking every page once.
 * - Set up: a staging copy of RDRAM, and a ring to read back into when
 *   the GPU's RDRAM is not mapped; comparing before an upload only where
 *   it is mapped, cached and coherent, and not when asked to upload whole.
 * - Read back, mapped: a copy for each run of pages written, from the
 *   GPU's RDRAM and its mask, with the pages' pending-write counters up.
 *   Not mapped: RDRAM and mask copied into the ring first, and the
 *   copies read from there, the ring starting over rather than wrapping
 *   a copy.
 * - Uploaded, mapped: pages whole straight into the GPU's RDRAM with
 *   their mask cleared (compared: a page the same left alone, its mask
 *   cleared), masked pages through the staging copy by
 *   masked_rdram_resolve, a work group a page. Not mapped: whole pages
 *   copied from the staging copy, their masks cleared by
 *   clear_write_mask. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdram_coherency.h"
#include "shader_bank.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdram_coherency: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

/* ---- marking pages, against a page at a time ---- */
static unsigned rng = 1;
static uint32_t rnd(uint32_t n)
{
	rng = rng * 1103515245u + 12345u;
	return (rng >> 8) % n;
}

#define MAX_RING 2048
static uint32_t pending_readback[MAX_RING / 32], want_masked[MAX_RING / 32], want_direct[MAX_RING / 32];
static uint32_t got_masked[MAX_RING / 32], got_direct[MAX_RING / 32];
static retro_atomic_int_t pending_writes[MAX_RING];

static bool bit(const uint32_t *v, uint32_t p)
{
	return (v[p / 32] >> (p & 31)) & 1;
}

static void reference(uint32_t num, uint32_t start, uint32_t count)
{
	uint32_t i;
	if (count > num)
		count = num;
	for (i = 0; i < count; i++)
	{
		const uint32_t page = (start + i) & (num - 1);
		if (bit(pending_readback, page) || retro_atomic_load_acquire_int(&pending_writes[page]))
			want_masked[page / 32] |= 1u << (page & 31);
		else
			want_direct[page / 32] |= 1u << (page & 31);
	}
}

static void test_mark_pages(void)
{
	static const uint32_t sizes[] = { 8, 16, 32, 64, 256, 2048 };
	unsigned s, iter, k, bad = 0;

	for (s = 0; s < 6; s++)
	{
		const uint32_t num = sizes[s];
		uint32_t page;

		for (iter = 0; iter < 400; iter++)
		{
			uint32_t start = rnd(4 * num), count;
			memset(pending_readback, 0, sizeof(pending_readback));
			memset(want_masked, 0, sizeof(want_masked));
			memset(want_direct, 0, sizeof(want_direct));
			memset(got_masked, 0, sizeof(got_masked));
			memset(got_direct, 0, sizeof(got_direct));
			for (page = 0; page < num; page++)
			{
				if (rnd(5) == 0)
					pending_readback[page / 32] |= 1u << (page & 31);
				retro_atomic_int_init(&pending_writes[page], rnd(7) == 0);
			}
			switch (iter % 4)
			{
			case 0: count = 1 + rnd(40); break;
			case 1: count = 1 + rnd(num); break;
			case 2: count = num; break;               /* the whole ring */
			default: count = num + rnd(3 * num); break; /* more than it */
			}
			/* a few reads in a row accumulate */
			for (k = 0; k < 3; k++)
			{
				reference(num, start, count);
				rdp_coherency_mark_pages(start, count, num, pending_readback, pending_writes, got_masked, got_direct);
				start = rnd(4 * num);
				count = 1 + rnd(num);
			}
			if (memcmp(want_masked, got_masked, sizeof(got_masked)) || memcmp(want_direct, got_direct, sizeof(got_direct)))
				bad++;
		}

		/* every page of a whole-ring read is marked, once, one way */
		memset(got_masked, 0, sizeof(got_masked));
		memset(got_direct, 0, sizeof(got_direct));
		rdp_coherency_mark_pages(5, num, num, pending_readback, pending_writes, got_masked, got_direct);
		for (page = 0; page < num; page++)
			if (bit(got_masked, page) == bit(got_direct, page))
				bad++;
	}
	CHECK(bad == 0, "a read's pages are marked masked where a write is pending, whole otherwise");
}

/* ---- the GPU's RDRAM ---- */
#define PAGES 64
#define SIZE (PAGES * RDP_RDRAM_PAGE_SIZE)

static rdp_shader_bank_t bank;
static granite_buffer_t *rdram;
static rdp_rdram_coherency_t c;
static uint8_t host[SIZE];

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

static granite_buffer_t *gpu_rdram(void)
{
	granite_buffer_info_t info;
	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST;
	info.size = 2 * SIZE;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	return granite_device_create_buffer(&dev, &info, NULL);
}

/* the GPU's RDRAM (page @page of RDRAM, or of its mask past @mask) */
static uint8_t *gpu_page(unsigned page, bool mask)
{
	return rdram->alloc.host_base + (mask ? SIZE : 0) + (size_t)page * RDP_RDRAM_PAGE_SIZE;
}

static uint8_t *staging_page(unsigned page)
{
	return c.staging_rdram->alloc.host_base + (size_t)page * RDP_RDRAM_PAGE_SIZE;
}

static void fill_host(void)
{
	unsigned i;
	for (i = 0; i < SIZE; i++)
		host[i] = (uint8_t)(i * 7 + (i >> 10));
}

/* how often @n words of @pages are in memory the host wrote */
static unsigned in_memory(const uint32_t *pages, unsigned n)
{
	unsigned i, found = 0;
	size_t at;
	for (i = 0; i < num_memory; i++)
		for (at = 0; at + n * 4 <= memory_sizes[i]; at += 4)
			if (!memcmp(memory_data[i] + at, pages, n * 4))
				found++;
	return found;
}

static granite_command_buffer_t *begin(void)
{
	num_events = 0;
	num_buffer_copies = 0;
	return granite_device_request_command_buffer(&dev, GRANITE_QUEUE_GRAPHICS);
}

static void test_init(void)
{
	uint8_t *saved;

	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, false), "set up");
	CHECK(c.num_pages == PAGES && c.packed_pages == 2 && c.size == SIZE && c.host_rdram == host && c.rdram == rdram
			&& c.device == &dev, "a page a KiB, 32 a word");
	CHECK(c.staging_rdram && c.staging_rdram->info.size == SIZE && c.staging_rdram->alloc.host_base
			&& (c.staging_rdram->info.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT), "a staging copy of RDRAM, mapped");
	CHECK(!c.staging_readback && !c.staging_readback_pages, "no readback ring when the GPU's RDRAM is mapped");
	CHECK(c.compare_before_upload, "mapped, cached and coherent: compared before an upload");
	CHECK(!c.page_to_direct_copy[0] && !c.page_to_masked_copy[1] && !c.page_to_pending_readback[1]
			&& !retro_atomic_load_acquire_int(&c.pending_writes_for_page[PAGES - 1]), "nothing marked");

	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, true) && !c.compare_before_upload,
			"asked to upload whole, not compared");
	dev.mem_props.memoryTypes[rdram->alloc.memory_type].propertyFlags &= ~(VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, false) && !c.compare_before_upload, "nor when not cached");
	dev.mem_props.memoryTypes[rdram->alloc.memory_type].propertyFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	saved = rdram->alloc.host_base;
	rdram->alloc.host_base = NULL;
	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, false), "set up for RDRAM not mapped");
	CHECK(c.staging_readback && c.staging_readback->info.size == 32 * (VkDeviceSize)SIZE
			&& c.staging_readback_pages == 32 * PAGES && c.staging_readback->alloc.host_base && !c.compare_before_upload,
			"a readback ring of 32 copies of RDRAM, mapped; nothing to compare against");
	rdram->alloc.host_base = saved;
	rdp_coherency_free(&c);
	CHECK(!c.arena && !c.staging_rdram && !c.num_pages, "freed");
}

static void test_mapped(void)
{
	granite_command_buffer_t *cb;
	const granite_buffer_t *src;
	unsigned n, i;
	uint32_t want[3];

	fill_host();
	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, true), "set up, mapped");

	/* ---- uploads ---- */
	memset(gpu_page(0, false), 0xee, SIZE);
	memset(gpu_page(0, true), 0xff, SIZE);
	rdp_coherency_mark_read(&c, 3 * RDP_RDRAM_PAGE_SIZE + 10, 2 * RDP_RDRAM_PAGE_SIZE);
	CHECK(c.page_to_direct_copy[0] == (7u << 3) && !c.page_to_masked_copy[0], "a read of bytes over three pages marks them");
	rdp_coherency_mark_read(&c, 40 * RDP_RDRAM_PAGE_SIZE, 0);
	rdp_coherency_mark_read(&c, 0, 0);
	CHECK(!c.page_to_direct_copy[1] && c.page_to_direct_copy[0] == (7u << 3), "an empty read marks none");
	rdp_coherency_mark_read(&c, (PAGES + 33) * RDP_RDRAM_PAGE_SIZE, 1);
	CHECK(c.page_to_direct_copy[1] == 2u, "past the end wraps round");
	rdp_coherency_lock_write(&c, 7 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE + 1);
	CHECK(c.page_to_pending_readback[0] == (3u << 7), "a write locks its pages");
	rdp_coherency_lock_write(&c, (PAGES + 62) * RDP_RDRAM_PAGE_SIZE, 3 * RDP_RDRAM_PAGE_SIZE);
	CHECK(c.page_to_pending_readback[1] == (3u << 30) && c.page_to_pending_readback[0] == (3u << 7 | 1u),
			"wrapping round");
	rdp_coherency_mark_read(&c, 7 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE);
	CHECK(c.page_to_masked_copy[0] == (3u << 7), "a read of pages being written is masked");

	cb = begin();
	rdp_coherency_upload(&c, cb, &bank);
	CHECK(!memcmp(gpu_page(3, false), host + 3 * RDP_RDRAM_PAGE_SIZE, 3 * RDP_RDRAM_PAGE_SIZE)
			&& !memcmp(gpu_page(33, false), host + 33 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE),
			"the pages marked whole are copied into the GPU's RDRAM");
	CHECK(gpu_page(3, true)[0] == 0 && gpu_page(5, true)[RDP_RDRAM_PAGE_SIZE - 1] == 0 && gpu_page(33, true)[5] == 0
			&& gpu_page(2, true)[RDP_RDRAM_PAGE_SIZE - 1] == 0xff && gpu_page(6, true)[0] == 0xff,
			"and their masks cleared, no others");
	CHECK(gpu_page(2, false)[0] == 0xee && gpu_page(6, false)[0] == 0xee && gpu_page(7, false)[0] == 0xee,
			"other pages, and the masked ones, are left alone");
	CHECK(!memcmp(staging_page(7), host + 7 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE),
			"the masked pages are copied to the staging copy");
	n = count_events(EV_DISPATCH);
	i = (unsigned)(events[find_event(EV_DISPATCH, 0)].c / 16) - 1;
	CHECK(n == 1 && events[find_event(EV_DISPATCH, 0)].a == 2 && i < MAX_PIPELINES && pipelines[i].compute
			&& pipelines[i].spec_count == 2 && pipelines[i].spec[0] == 256 && pipelines[i].spec[1] == 256,
			"and resolved under the mask, a work group a page of 256 words");
	want[0] = 7;
	want[1] = 8;
	CHECK(in_memory(want, 2) >= 1, "the pages listed for it");
	CHECK(!c.page_to_direct_copy[0] && !c.page_to_direct_copy[1] && !c.page_to_masked_copy[0] && num_buffer_copies == 0,
			"all done, without buffer copies");
	granite_device_submit(&dev, cb, NULL, 0, NULL);

	/* ---- readback ---- */
	cb = begin();
	n = rdp_coherency_read_back(&c, cb, &src);
	CHECK(n == 3 && src == rdram && num_buffer_copies == 0, "mapped, the pages written are read back from RDRAM itself");
	CHECK(c.copies[0].dst_offset == 0 && c.copies[0].size == RDP_RDRAM_PAGE_SIZE && c.copies[0].src_offset == 0
			&& c.copies[0].mask_offset == SIZE && c.copies[0].counters == 1
			&& c.copies[0].counter_base == &c.pending_writes_for_page[0], "page 0");
	CHECK(c.copies[1].dst_offset == 7 * RDP_RDRAM_PAGE_SIZE && c.copies[1].size == 2 * RDP_RDRAM_PAGE_SIZE
			&& c.copies[1].mask_offset == SIZE + 7 * RDP_RDRAM_PAGE_SIZE && c.copies[1].counters == 2,
			"pages 7 and 8, as one");
	CHECK(c.copies[2].dst_offset == 62 * RDP_RDRAM_PAGE_SIZE && c.copies[2].counters == 2, "pages 62 and 63");
	CHECK(retro_atomic_load_acquire_int(&c.pending_writes_for_page[7]) == 1
			&& retro_atomic_load_acquire_int(&c.pending_writes_for_page[63]) == 1
			&& retro_atomic_load_acquire_int(&c.pending_writes_for_page[9]) == 0, "their counters up by one");
	CHECK(!c.page_to_pending_readback[0] && !c.page_to_pending_readback[1], "the pages unlocked");
	granite_device_submit(&dev, cb, NULL, 0, NULL);

	/* a whole word of pages is one copy */
	rdp_coherency_lock_write(&c, 32 * RDP_RDRAM_PAGE_SIZE, 32 * RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	n = rdp_coherency_read_back(&c, cb, &src);
	CHECK(n == 1 && c.copies[0].counters == 32 && c.copies[0].size == 32 * RDP_RDRAM_PAGE_SIZE,
			"32 pages written are one readback");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	for (i = 32; i < 64; i++)
		retro_atomic_fetch_sub_int(&c.pending_writes_for_page[i], 1);

	/* until it is copied, a read of the page is masked */
	rdp_coherency_mark_read(&c, 7 * RDP_RDRAM_PAGE_SIZE, 1);
	CHECK(c.page_to_masked_copy[0] == (1u << 7) && !c.page_to_direct_copy[0], "a page still being read back is masked");
	retro_atomic_fetch_sub_int(&c.pending_writes_for_page[7], 1);
	rdp_coherency_mark_read(&c, 7 * RDP_RDRAM_PAGE_SIZE, 1);
	CHECK(c.page_to_direct_copy[0] == (1u << 7), "once copied, whole");
	c.page_to_masked_copy[0] = 0;

	/* ---- compared ---- */
	c.compare_before_upload = true;
	memcpy(gpu_page(10, false), host + 10 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE);
	gpu_page(11, false)[100] ^= 1;
	memset(gpu_page(10, true), 0, RDP_RDRAM_PAGE_SIZE);
	gpu_page(10, true)[9] = 0xff;
	memset(gpu_page(11, true), 0xff, RDP_RDRAM_PAGE_SIZE);
	rdp_coherency_mark_read(&c, 10 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	rdp_coherency_upload(&c, cb, &bank);
	CHECK(!memcmp(gpu_page(10, false), host + 10 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE)
			&& gpu_page(10, true)[9] == 0 && gpu_page(11, true)[0] == 0 && count_events(EV_DISPATCH) == 0,
			"compared, a page ends up as a whole upload leaves it");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	rdp_coherency_free(&c);
}

static void test_not_mapped(void)
{
	granite_command_buffer_t *cb;
	const granite_buffer_t *src;
	uint8_t *saved = rdram->alloc.host_base;
	unsigned n, i;
	uint32_t want[3];

	fill_host();
	rdram->alloc.host_base = NULL;
	CHECK(rdp_coherency_init(&c, &dev, rdram, host, SIZE, false), "set up, not mapped");

	/* ---- uploads ---- */
	rdp_coherency_lock_write(&c, 20 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE);
	rdp_coherency_mark_read(&c, 19 * RDP_RDRAM_PAGE_SIZE, 3 * RDP_RDRAM_PAGE_SIZE);
	rdp_coherency_mark_read(&c, 40 * RDP_RDRAM_PAGE_SIZE, 1);
	cb = begin();
	rdp_coherency_upload(&c, cb, &bank);
	CHECK(!memcmp(staging_page(19), host + 19 * RDP_RDRAM_PAGE_SIZE, 3 * RDP_RDRAM_PAGE_SIZE)
			&& !memcmp(staging_page(40), host + 40 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE),
			"every page marked is copied to the staging copy");
	CHECK(num_buffer_copies == 3 && buffer_copies[0].src == c.staging_rdram->buffer && buffer_copies[0].dst == rdram->buffer
			&& buffer_copies[0].region.srcOffset == 19 * RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[0].region.dstOffset == 19 * RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[0].region.size == RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[1].region.dstOffset == 21 * RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[2].region.dstOffset == 40 * RDP_RDRAM_PAGE_SIZE,
			"the whole ones copied from it, a page each");
	n = count_events(EV_DISPATCH);
	CHECK(n == 2 && events[find_event(EV_DISPATCH, 0)].a == 1 && events[find_event(EV_DISPATCH, 1)].a == 3,
			"the masked page resolved, the masks of the whole ones cleared");
	i = (unsigned)(events[find_event(EV_DISPATCH, 1)].c / 16) - 1;
	n = (unsigned)(events[find_event(EV_DISPATCH, 0)].c / 16) - 1;
	CHECK(i < MAX_PIPELINES && n < MAX_PIPELINES && pipelines[i].spec[0] == 256
			&& pipelines[i].fragment != pipelines[n].fragment, "a work group a page, by another shader");
	want[0] = 19;
	want[1] = 21;
	want[2] = 40;
	CHECK(in_memory(want, 3) >= 1, "the pages whose masks are cleared listed for it");
	CHECK(!c.page_to_direct_copy[0] && !c.page_to_direct_copy[1] && !c.page_to_masked_copy[0], "all done");
	granite_device_submit(&dev, cb, NULL, 0, NULL);

	/* ---- readback through the ring ---- */
	rdp_coherency_lock_write(&c, 3 * RDP_RDRAM_PAGE_SIZE, 2 * RDP_RDRAM_PAGE_SIZE);
	rdp_coherency_lock_write(&c, 40 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	n = rdp_coherency_read_back(&c, cb, &src);
	CHECK(n == 3 && src == c.staging_readback, "not mapped, read back through the ring");
	CHECK(num_buffer_copies == 6 && buffer_copies[0].src == rdram->buffer && buffer_copies[0].dst == c.staging_readback->buffer,
			"RDRAM and its mask copied into it first, for each run");
	CHECK(buffer_copies[0].region.srcOffset == 3 * RDP_RDRAM_PAGE_SIZE && buffer_copies[0].region.dstOffset == 0
			&& buffer_copies[0].region.size == 2 * RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[1].region.srcOffset == SIZE + 3 * RDP_RDRAM_PAGE_SIZE
			&& buffer_copies[1].region.dstOffset == 2 * RDP_RDRAM_PAGE_SIZE,
			"pages 3 and 4, then their mask, one after the other in the ring");
	CHECK(c.copies[0].src_offset == 0 && c.copies[0].mask_offset == 2 * RDP_RDRAM_PAGE_SIZE
			&& c.copies[0].dst_offset == 3 * RDP_RDRAM_PAGE_SIZE && c.copies[0].counters == 2,
			"read from the ring into RDRAM");
	CHECK(c.copies[1].dst_offset == 20 * RDP_RDRAM_PAGE_SIZE && c.copies[1].src_offset == 4 * RDP_RDRAM_PAGE_SIZE
			&& c.copies[2].src_offset == 6 * RDP_RDRAM_PAGE_SIZE && c.staging_readback_index == 8,
			"and on round the ring");
	granite_device_submit(&dev, cb, NULL, 0, NULL);

	/* near the end of the ring, a run starts over at its beginning */
	c.staging_readback_index = c.staging_readback_pages - 1;
	rdp_coherency_lock_write(&c, 30 * RDP_RDRAM_PAGE_SIZE, 3 * RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	n = rdp_coherency_read_back(&c, cb, &src);
	CHECK(n == 2 && c.copies[0].counters == 2 && c.copies[0].src_offset == 0 && c.copies[0].mask_offset == 2 * RDP_RDRAM_PAGE_SIZE,
			"a run that would wrap round the ring starts over");
	CHECK(c.copies[1].counters == 1 && c.copies[1].dst_offset == 32 * RDP_RDRAM_PAGE_SIZE
			&& c.copies[1].src_offset == 4 * RDP_RDRAM_PAGE_SIZE, "(pages 30 and 31, then page 32: another word)");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	c.staging_readback_index = c.staging_readback_pages - 2;
	rdp_coherency_lock_write(&c, 9 * RDP_RDRAM_PAGE_SIZE, RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	n = rdp_coherency_read_back(&c, cb, &src);
	CHECK(n == 1 && c.copies[0].src_offset == (size_t)(c.staging_readback_pages - 2) * RDP_RDRAM_PAGE_SIZE
			&& c.copies[0].mask_offset == (size_t)(c.staging_readback_pages - 1) * RDP_RDRAM_PAGE_SIZE
			&& c.staging_readback_index == 0, "one that ends at the end of the ring fits");
	granite_device_submit(&dev, cb, NULL, 0, NULL);

	rdp_coherency_free(&c);
	rdram->alloc.host_base = saved;
}

/* a whole RDRAM's worth of masked pages */
static void test_many_pages(void)
{
	enum { BIG = 8192 };
	granite_buffer_info_t info;
	granite_buffer_t *big;
	granite_command_buffer_t *cb;
	uint8_t *big_host = (uint8_t *)calloc(BIG, RDP_RDRAM_PAGE_SIZE);
	uint32_t want[2];

	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST;
	info.size = 2 * (VkDeviceSize)BIG * RDP_RDRAM_PAGE_SIZE;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	big = granite_device_create_buffer(&dev, &info, NULL);
	CHECK(big && big_host && rdp_coherency_init(&c, &dev, big, big_host, (size_t)BIG * RDP_RDRAM_PAGE_SIZE, false),
			"set up for 8 MiB");
	rdp_coherency_lock_write(&c, 0, 5000 * RDP_RDRAM_PAGE_SIZE);
	rdp_coherency_mark_read(&c, 0, 5000 * RDP_RDRAM_PAGE_SIZE);
	cb = begin();
	rdp_coherency_upload(&c, cb, &bank);
	CHECK(count_events(EV_DISPATCH) == 2 && events[find_event(EV_DISPATCH, 0)].a == 4096
			&& events[find_event(EV_DISPATCH, 1)].a == 5000 - 4096, "pages are resolved 4096 at a time");
	want[0] = 4096;
	want[1] = 4097;
	CHECK(in_memory(want, 2) >= 1, "the second batch from page 4096");
	granite_device_submit(&dev, cb, NULL, 0, NULL);
	rdp_coherency_free(&c);
	granite_buffer_unref(big);
	free(big_host);
}

int main(void)
{
	unsigned i;

	test_mark_pages();

	fill_context();
	context.mem_props.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);
	rdram = gpu_rdram();
	CHECK(rdram && rdram->alloc.host_base, "(a mapped buffer for the GPU's RDRAM)");

	test_init();
	test_mapped();
	test_not_mapped();
	test_many_pages();

	granite_buffer_unref(rdram);
	granite_device_free(&dev);
	CHECK(unmade == made + num_pipelines, "everything made went back");
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);

	if (failures)
	{
		fprintf(stderr, "test_rdram_coherency: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdram_coherency: ok\n");
	return 0;
}
