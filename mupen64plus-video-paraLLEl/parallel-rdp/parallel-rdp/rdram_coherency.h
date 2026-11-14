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

#ifndef RDP_RDRAM_COHERENCY_H
#define RDP_RDRAM_COHERENCY_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_atomic.h>
#include "device.h"
#include "shader_bank.h"

RETRO_BEGIN_DECLS

/* RDRAM is kept in step with the GPU's copy page by page */
#define RDP_RDRAM_PAGE_SIZE 1024

/* A readback for the timeline worker to make once the GPU is done: what
 * the GPU wrote, @size bytes from @src_offset of the readback's buffer,
 * copied to RDRAM at @dst_offset under the mask at @mask_offset; then the
 * @counters pending-write counters from @counter_base are dropped. */
typedef struct rdp_coherency_copy
{
	size_t src_offset;
	size_t mask_offset;
	size_t dst_offset;
	size_t size;
	retro_atomic_int_t *counter_base;
	unsigned counters;
} rdp_coherency_copy_t;

/* The GPU's RDRAM, for a host whose RDRAM the GPU cannot import
 * (without VK_EXT_external_memory_host): a buffer of RDRAM and its write
 * mask, which the GPU draws into, brought up to date from the host's
 * before the GPU reads it and read back after it writes. All zero is
 * one not set up. */
typedef struct rdp_rdram_coherency
{
	granite_device_t *device;
	/* the GPU's: RDRAM, then the write mask, @size each (not ours) */
	granite_buffer_t *rdram;
	size_t size;
	/* the host's */
	uint8_t *host_rdram;
	/* what is uploaded: the host's pages, to copy from */
	granite_buffer_t *staging_rdram;
	/* where the GPU's writes are read back to, when the GPU's RDRAM is
	 * not mapped: a ring of pages */
	granite_buffer_t *staging_readback;
	unsigned staging_readback_pages;
	unsigned staging_readback_index;
	/* RDRAM is mapped, cached and coherent: an upload reads the GPU's
	 * copy first and leaves alone what is the same */
	bool compare_before_upload;

	unsigned num_pages;
	/* words of 32 pages */
	unsigned packed_pages;

	/* One block, allocated with the rest: */
	void *arena;
	/* per page: readbacks of its not yet copied to the host */
	retro_atomic_int_t *pending_writes_for_page;
	/* bitmaps of packed_pages words: the pages to upload whole, under
	 * the write mask, and the GPU writes to */
	uint32_t *page_to_direct_copy;
	uint32_t *page_to_masked_copy;
	uint32_t *page_to_pending_readback;
	/* room for a readback a page */
	rdp_coherency_copy_t *copies;
	/* the copies, masked pages and pages to clear the mask of one
	 * update records: room for every page */
	VkBufferCopy *buffer_copies;
	uint32_t *masked_pages;
	uint32_t *clear_pages;
} rdp_rdram_coherency_t;

/* @rdram (@size of RDRAM, then @size of write mask) for @host_rdram.
 * @full_upload: every page uploaded whole, without comparing. False when
 * out of memory. */
bool rdp_coherency_init(rdp_rdram_coherency_t *c, granite_device_t *device, granite_buffer_t *rdram,
		uint8_t *host_rdram, size_t size, bool full_upload);
void rdp_coherency_free(rdp_rdram_coherency_t *c);

/* The GPU is going to read @size bytes from @addr: their pages are
 * uploaded first, under the mask where a GPU write to them is pending */
void rdp_coherency_mark_read(rdp_rdram_coherency_t *c, uint32_t addr, uint32_t size);

/* The GPU is going to write @size bytes from @addr: their pages are read
 * back after */
void rdp_coherency_lock_write(rdp_rdram_coherency_t *c, uint32_t addr, uint32_t size);

/* The pages @start_page on, @count of them (at most the ring), wrapping
 * at @num_pages (a power of two), into @masked_copy where
 * @pending_readback or @pending_writes has the page, into @direct_copy
 * otherwise */
void rdp_coherency_mark_pages(uint32_t start_page, uint32_t count, uint32_t num_pages,
		const uint32_t *pending_readback, retro_atomic_int_t *pending_writes, uint32_t *masked_copy,
		uint32_t *direct_copy);

/* Records, into @cb, the readback of the pages the GPU writes to: the
 * timeline worker copies c->copies, as many as this returns, from *@src
 * once @cb is done. Each page's pending-write counter goes up by one
 * until then. */
unsigned rdp_coherency_read_back(rdp_rdram_coherency_t *c, granite_command_buffer_t *cb,
		const granite_buffer_t **src);

/* Records, into @cb, the upload of the pages marked to the GPU */
void rdp_coherency_upload(rdp_rdram_coherency_t *c, granite_command_buffer_t *cb, const rdp_shader_bank_t *bank);

RETRO_END_DECLS

#endif
