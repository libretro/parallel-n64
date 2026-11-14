/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "rdram_coherency.h"
#include "command_buffer.h"

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
#include <stdio.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

/* the number of trailing zero bits of @v, not 0 */
static unsigned ctz32(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned)__builtin_ctz(v);
#elif defined(_MSC_VER)
	unsigned long index;
	_BitScanForward(&index, v);
	return (unsigned)index;
#else
	unsigned n = 0;
	while (!(v & 1))
	{
		v >>= 1;
		n++;
	}
	return n;
#endif
}

/* The next run of set bits of *@bits: its first bit and length. The run
 * is cleared from *@bits. */
static bool next_run(uint32_t *bits, unsigned *first, unsigned *count)
{
	uint32_t rest;
	if (!*bits)
		return false;
	*first = ctz32(*bits);
	rest = ~(*bits >> *first);
	*count = rest ? ctz32(rest) : 32 - *first;
	*bits &= *count + *first >= 32 ? ((1u << *first) - 1u) : ~(((1u << *count) - 1u) << *first);
	return true;
}

static size_t align8(size_t v)
{
	return (v + 7) & ~(size_t)7;
}

static granite_buffer_t *create_buffer(granite_device_t *device, size_t size, granite_buffer_domain_t domain,
		VkBufferUsageFlags usage)
{
	granite_buffer_info_t info;
	memset(&info, 0, sizeof(info));
	info.size = size;
	info.domain = domain;
	info.usage = usage;
	return granite_device_create_buffer(device, &info, NULL);
}

bool rdp_coherency_init(rdp_rdram_coherency_t *c, granite_device_t *device, granite_buffer_t *rdram,
		uint8_t *host_rdram, size_t size, bool full_upload)
{
	const VkMemoryPropertyFlags cached_coherent = VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	size_t counters, bitmaps, copies, buffer_copies, pages;
	uint8_t *arena;
	unsigned i;

	rdp_coherency_free(c);
	c->device = device;
	c->rdram = rdram;
	c->size = size;
	c->host_rdram = host_rdram;
	c->num_pages = (unsigned)((size + RDP_RDRAM_PAGE_SIZE - 1) / RDP_RDRAM_PAGE_SIZE);
	c->packed_pages = (c->num_pages + 31) / 32;

	/* The host's pages are copied here to be uploaded: RDRAM may be
	 * uploaded in a masked way (if the GPU has pending writes), or by a
	 * direct copy (if no pending writes are outstanding). */
	c->staging_rdram = create_buffer(device, size, GRANITE_BUFFER_DOMAIN_HOST,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	if (!c->staging_rdram)
		goto fail;

	if (!rdram->alloc.host_base)
	{
		/* If we cannot map RDRAM, we need a staging readback buffer: a
		 * ring of a copy for each sync state. */
		const size_t readback_size = size * 32;
		c->staging_readback = create_buffer(device, readback_size,
				GRANITE_BUFFER_DOMAIN_CACHED_COHERENT_HOST_PREFER_CACHED, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		if (!c->staging_readback)
			goto fail;
		c->staging_readback_pages = (unsigned)((readback_size + RDP_RDRAM_PAGE_SIZE - 1) / RDP_RDRAM_PAGE_SIZE);
	}

	c->compare_before_upload = !full_upload && rdram->alloc.host_base &&
		(device->mem_props.memoryTypes[rdram->alloc.memory_type].propertyFlags & cached_coherent) == cached_coherent;

	counters = align8(sizeof(retro_atomic_int_t) * c->num_pages);
	bitmaps = align8(sizeof(uint32_t) * c->packed_pages);
	copies = align8(sizeof(rdp_coherency_copy_t) * c->num_pages);
	/* (two a run of pages when read back through the staging ring) */
	buffer_copies = align8(sizeof(VkBufferCopy) * 2 * c->num_pages);
	pages = align8(sizeof(uint32_t) * c->num_pages);
	arena = (uint8_t *)calloc(1, counters + 3 * bitmaps + copies + buffer_copies + 2 * pages);
	if (!arena)
		goto fail;
	c->arena = arena;
	c->pending_writes_for_page = (retro_atomic_int_t *)arena;
	arena += counters;
	c->page_to_direct_copy = (uint32_t *)arena;
	arena += bitmaps;
	c->page_to_masked_copy = (uint32_t *)arena;
	arena += bitmaps;
	c->page_to_pending_readback = (uint32_t *)arena;
	arena += bitmaps;
	c->copies = (rdp_coherency_copy_t *)arena;
	arena += copies;
	c->buffer_copies = (VkBufferCopy *)arena;
	arena += buffer_copies;
	c->masked_pages = (uint32_t *)arena;
	arena += pages;
	c->clear_pages = (uint32_t *)arena;
	for (i = 0; i < c->num_pages; i++)
		retro_atomic_int_init(&c->pending_writes_for_page[i], 0);
	return true;

fail:
	rdp_coherency_free(c);
	return false;
}

void rdp_coherency_free(rdp_rdram_coherency_t *c)
{
	if (c->staging_rdram)
		granite_buffer_unref(c->staging_rdram);
	if (c->staging_readback)
		granite_buffer_unref(c->staging_readback);
	free(c->arena);
	memset(c, 0, sizeof(*c));
}

/* ---- marking pages ---- */
void rdp_coherency_mark_pages(uint32_t start_page, uint32_t count, uint32_t num_pages,
		const uint32_t *pending_readback, retro_atomic_int_t *pending_writes, uint32_t *masked_copy,
		uint32_t *direct_copy)
{
	uint32_t page = start_page & (num_pages - 1);

	if (count > num_pages)
		count = num_pages;

	/* A word of pages at a time: only the pages without a pending readback
	 * need their write counter looked at. */
	while (count)
	{
		const uint32_t word = page / 32;
		const uint32_t bit = page & 31;
		uint32_t n = 32 - bit, mask, masked, bits, first, run;

		if (n > count)
			n = count;
		if (n > num_pages - page)
			n = num_pages - page;
		mask = (n == 32 ? ~0u : ((1u << n) - 1u)) << bit;

		masked = pending_readback[word] & mask;
		bits = mask & ~masked;
		while (next_run(&bits, &first, &run))
		{
			unsigned b;
			/* A page whose write-backs are all done is read straight from
			 * host memory: the acquire load pairs with the timeline
			 * worker's decrement after its copy, so the copy is seen. */
			for (b = first; b < first + run; b++)
				if (retro_atomic_load_acquire_int(&pending_writes[32 * word + b]) != 0)
					masked |= 1u << b;
		}

		masked_copy[word] |= masked;
		direct_copy[word] |= mask & ~masked;

		count -= n;
		page = (page + n) & (num_pages - 1);
	}
}

void rdp_coherency_mark_read(rdp_rdram_coherency_t *c, uint32_t addr, uint32_t size)
{
	uint32_t start_page, end_page;
	if (!size)
		return;
	start_page = addr / RDP_RDRAM_PAGE_SIZE;
	end_page = (addr + size - 1) / RDP_RDRAM_PAGE_SIZE + 1;
	rdp_coherency_mark_pages(start_page, end_page - start_page, c->num_pages, c->page_to_pending_readback,
			c->pending_writes_for_page, c->page_to_masked_copy, c->page_to_direct_copy);
}

void rdp_coherency_lock_write(rdp_rdram_coherency_t *c, uint32_t addr, uint32_t size)
{
	uint32_t page, end_page;
	if (!size)
		return;
	end_page = (addr + size - 1) / RDP_RDRAM_PAGE_SIZE + 1;
	for (page = addr / RDP_RDRAM_PAGE_SIZE; page < end_page; page++)
	{
		const uint32_t wrapped = page & (c->num_pages - 1);
		c->page_to_pending_readback[wrapped / 32] |= 1u << (wrapped & 31);
	}
}

/* ---- GPU to host ---- */
/* The next @count pages of the staging readback ring, from its start
 * when they would wrap round */
static size_t staging_readback_pages(rdp_rdram_coherency_t *c, unsigned count)
{
	const unsigned first = c->staging_readback_index;
	c->staging_readback_index = (first + count) & (c->staging_readback_pages - 1);
	/* Unclean wraparound check. */
	if (c->staging_readback_index != 0 && c->staging_readback_index < first)
	{
		c->staging_readback_index = count;
		return 0;
	}
	return (size_t)first * RDP_RDRAM_PAGE_SIZE;
}

unsigned rdp_coherency_read_back(rdp_rdram_coherency_t *c, granite_command_buffer_t *cb, const granite_buffer_t **src)
{
	unsigned num_copies = 0, num_buffer_copies = 0, word;

	*src = c->staging_readback ? c->staging_readback : c->rdram;

	for (word = 0; word < c->packed_pages; word++)
	{
		uint32_t bits = c->page_to_pending_readback[word];
		unsigned first, count, i;

		while (next_run(&bits, &first, &count))
		{
			const unsigned index = 32 * word + first;
			const size_t offset = (size_t)index * RDP_RDRAM_PAGE_SIZE;
			const size_t size = (size_t)count * RDP_RDRAM_PAGE_SIZE;
			rdp_coherency_copy_t *copy = &c->copies[num_copies++];

			for (i = 0; i < count; i++)
				retro_atomic_fetch_add_int(&c->pending_writes_for_page[index + i], 1);

			copy->counter_base = &c->pending_writes_for_page[index];
			copy->counters = count;
			copy->dst_offset = offset;
			copy->size = size;

			if (!c->staging_readback)
			{
				/* RDRAM is mapped (iGPU): read from it */
				copy->src_offset = offset;
				copy->mask_offset = offset + c->size;
			}
			else
			{
				/* Discrete GPU: RDRAM and its mask copied to the ring */
				VkBufferCopy *data = &c->buffer_copies[num_buffer_copies++];
				VkBufferCopy *mask = &c->buffer_copies[num_buffer_copies++];
				data->srcOffset = offset;
				data->dstOffset = staging_readback_pages(c, count);
				data->size = size;
				mask->srcOffset = offset + c->size;
				mask->dstOffset = staging_readback_pages(c, count);
				mask->size = size;
				copy->src_offset = data->dstOffset;
				copy->mask_offset = mask->dstOffset;
			}
		}

		c->page_to_pending_readback[word] = 0;
	}

	if (num_buffer_copies)
	{
		VkMemoryBarrier barrier;
		granite_cb_copy_buffer(cb, c->staging_readback, c->rdram, c->buffer_copies, num_buffer_copies);
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		granite_cb_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1, &barrier, 0, NULL, 0, NULL);
	}
	return num_copies;
}

/* ---- host to GPU ---- */
/* The direct upload of @count pages from @index, of only what differs: a
 * page the same as the GPU's copy is not written, and its write mask is
 * cleared only when it holds a bit. RDRAM ends up as a full upload leaves
 * it, whatever wrote the guest's pages; no page here has a GPU write
 * pending. */
static void upload_pages_compared(rdp_rdram_coherency_t *c, unsigned index, unsigned count)
{
	static const uint8_t zero_page[RDP_RDRAM_PAGE_SIZE] = { 0 };
	const uint32_t access = GRANITE_MEMORY_ACCESS_READ_BIT | GRANITE_MEMORY_ACCESS_WRITE_BIT;
	const size_t offset = (size_t)RDP_RDRAM_PAGE_SIZE * index;
	const size_t size = (size_t)RDP_RDRAM_PAGE_SIZE * count;
	const uint8_t *host = c->host_rdram + offset;
	uint8_t *data, *mask;
	size_t i;
#ifdef PARALLEL_RDP_VERIFY_UPLOADS
	/* The guest's pages as this upload reads them, which another thread
	 * may be writing meanwhile: what a full upload of them would leave. */
	static uint8_t seen[32 * RDP_RDRAM_PAGE_SIZE];
	memcpy(seen, host, size);
	host = seen;
#endif

	data = (uint8_t *)granite_device_map_host_buffer(c->device, c->rdram, access, offset, size);
	mask = (uint8_t *)granite_device_map_host_buffer(c->device, c->rdram, access, offset + c->size, size);

	for (i = 0; i < size; i += RDP_RDRAM_PAGE_SIZE)
	{
		if (memcmp(data + i, host + i, RDP_RDRAM_PAGE_SIZE))
		{
			memcpy(data + i, host + i, RDP_RDRAM_PAGE_SIZE);
			memset(mask + i, 0, RDP_RDRAM_PAGE_SIZE);
		}
		else if (memcmp(mask + i, zero_page, RDP_RDRAM_PAGE_SIZE))
			memset(mask + i, 0, RDP_RDRAM_PAGE_SIZE);
	}

#ifdef PARALLEL_RDP_VERIFY_UPLOADS
	for (i = 0; i < size; i += RDP_RDRAM_PAGE_SIZE)
		if (memcmp(data + i, host + i, RDP_RDRAM_PAGE_SIZE) || memcmp(mask + i, zero_page, RDP_RDRAM_PAGE_SIZE))
		{
			fprintf(stderr, "parallel-rdp: RDRAM page %u is not as a full upload leaves it\n",
					(unsigned)(index + i / RDP_RDRAM_PAGE_SIZE));
			abort();
		}
#endif

	granite_device_unmap_host_buffer(c->device, c->rdram, access, offset + c->size, size);
	granite_device_unmap_host_buffer(c->device, c->rdram, access, offset, size);
}

/* @count pages from @index of the host's, straight into the GPU's mapped
 * RDRAM, its write mask cleared */
static void upload_pages(rdp_rdram_coherency_t *c, unsigned index, unsigned count)
{
	const size_t offset = (size_t)RDP_RDRAM_PAGE_SIZE * index;
	const size_t size = (size_t)RDP_RDRAM_PAGE_SIZE * count;
	void *mapped = granite_device_map_host_buffer(c->device, c->rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, offset, size);

	memcpy(mapped, c->host_rdram + offset, size);
	granite_device_unmap_host_buffer(c->device, c->rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, offset, size);

	mapped = granite_device_map_host_buffer(c->device, c->rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, offset + c->size, size);
	memset(mapped, 0, size);
	granite_device_unmap_host_buffer(c->device, c->rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, offset + c->size, size);
}

/* Runs @program over @pages, a work group a page, the page numbers read
 * from a uniform buffer of up to 4096 */
static void dispatch_pages(granite_command_buffer_t *cb, const uint32_t *pages, unsigned count)
{
	unsigned i;
	for (i = 0; i < count; i += 4096)
	{
		const unsigned n = count - i < 4096 ? count - i : 4096;
		void *data = granite_cb_allocate_constant_data(cb, 1, 0, n * sizeof(uint32_t));
		if (!data)
			return;
		memcpy(data, pages + i, n * sizeof(uint32_t));
		granite_cb_dispatch(cb, n, 1, 1);
	}
}

static void set_page_program(granite_command_buffer_t *cb, granite_program_t *program)
{
	granite_cb_set_program(cb, program);
	granite_cb_set_spec_constant_mask(cb, 3);
	granite_cb_set_spec_constant(cb, 0, RDP_RDRAM_PAGE_SIZE / 4);
	granite_cb_set_spec_constant(cb, 1, RDP_RDRAM_PAGE_SIZE / 4);
}

static void global_barrier(granite_command_buffer_t *cb, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
		VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	VkMemoryBarrier barrier;
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	granite_cb_barrier(cb, src_stages, dst_stages, 1, &barrier, 0, NULL, 0, NULL);
}

void rdp_coherency_upload(rdp_rdram_coherency_t *c, granite_command_buffer_t *cb, const rdp_shader_bank_t *bank)
{
	unsigned num_masked = 0, num_clear = 0, num_copies = 0, word;
	uint8_t *staging;

	/* Now, ensure that the GPU sees a coherent view of the CPU memory
	 * writes up until now. Writes made by the GPU which are not known to
	 * be resolved on the timeline waiter thread will always "win" over
	 * writes made by CPU, since CPU is not allowed to meaningfully
	 * overwrite data which the GPU is going to touch. */
	retro_atomic_thread_fence_acquire();

	if (c->rdram->alloc.host_base)
	{
		/* If we're able to map RDRAM directly, we can just memcpy straight
		 * into RDRAM if we have an unmasked copy. Important for iGPU. */
		for (word = 0; word < c->packed_pages; word++)
		{
			uint32_t bits = c->page_to_direct_copy[word];
			unsigned first, count;
			while (next_run(&bits, &first, &count))
			{
				if (c->compare_before_upload)
					upload_pages_compared(c, 32 * word + first, count);
				else
					upload_pages(c, 32 * word + first, count);
			}
			c->page_to_direct_copy[word] = 0;
		}

		staging = (uint8_t *)granite_device_map_host_buffer(c->device, c->staging_rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT,
				0, c->staging_rdram->info.size);
		for (word = 0; word < c->packed_pages; word++)
		{
			uint32_t bits = c->page_to_masked_copy[word];
			unsigned first, count, i;
			while (next_run(&bits, &first, &count))
				for (i = 32 * word + first; i < 32 * word + first + count; i++)
				{
					c->masked_pages[num_masked++] = i;
					memcpy(staging + (size_t)RDP_RDRAM_PAGE_SIZE * i, c->host_rdram + (size_t)RDP_RDRAM_PAGE_SIZE * i,
							RDP_RDRAM_PAGE_SIZE);
				}
			c->page_to_masked_copy[word] = 0;
		}
		granite_device_unmap_host_buffer(c->device, c->staging_rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
				c->staging_rdram->info.size);
	}
	else
	{
		staging = (uint8_t *)granite_device_map_host_buffer(c->device, c->staging_rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT,
				0, c->staging_rdram->info.size);
		for (word = 0; word < c->packed_pages; word++)
		{
			const uint32_t masked = c->page_to_masked_copy[word];
			uint32_t bits = masked | c->page_to_direct_copy[word];
			unsigned first, count, i;
			while (next_run(&bits, &first, &count))
				for (i = first; i < first + count; i++)
				{
					const unsigned index = 32 * word + i;
					if (masked & (1u << i))
						c->masked_pages[num_masked++] = index;
					else
					{
						/* If we cannot map the device memory, copy */
						VkBufferCopy *copy = &c->buffer_copies[num_copies++];
						copy->srcOffset = copy->dstOffset = (VkDeviceSize)index * RDP_RDRAM_PAGE_SIZE;
						copy->size = RDP_RDRAM_PAGE_SIZE;
						c->clear_pages[num_clear++] = index;
					}
					memcpy(staging + (size_t)RDP_RDRAM_PAGE_SIZE * index,
							c->host_rdram + (size_t)RDP_RDRAM_PAGE_SIZE * index, RDP_RDRAM_PAGE_SIZE);
				}
			c->page_to_masked_copy[word] = 0;
			c->page_to_direct_copy[word] = 0;
		}
		granite_device_unmap_host_buffer(c->device, c->staging_rdram, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
				c->staging_rdram->info.size);
	}

	if (num_masked)
	{
		set_page_program(cb, bank->masked_rdram_resolve);
		granite_cb_set_storage_buffer(cb, 0, 0, c->rdram, 0, c->size);
		granite_cb_set_storage_buffer(cb, 0, 1, c->staging_rdram, 0, c->staging_rdram->info.size);
		granite_cb_set_storage_buffer(cb, 0, 2, c->rdram, c->size, c->size);
		dispatch_pages(cb, c->masked_pages, num_masked);
	}

	/* Could use FillBuffer here, but would need to use TRANSFER stage, and
	 * introduce more barriers than needed. */
	if (num_clear)
	{
		set_page_program(cb, bank->clear_write_mask);
		granite_cb_set_storage_buffer(cb, 0, 0, c->rdram, c->size, c->size);
		dispatch_pages(cb, c->clear_pages, num_clear);
	}

	if (num_masked || num_clear)
		global_barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	/* We're latency sensitive, so don't use DMA queue. */
	if (num_copies)
	{
		global_barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT);
		granite_cb_copy_buffer(cb, c->rdram, c->staging_rdram, c->buffer_copies, num_copies);
		global_barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}
}
