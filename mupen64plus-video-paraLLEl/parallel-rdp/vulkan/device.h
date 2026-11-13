/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#ifndef GRANITE_DEVICE_H
#define GRANITE_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <rthreads/retro_eventcount.h>
#include "volk.h"
#include "context.h"
#include "device_caps.h"
#include "handle.h"
#include "buffer.h"
#include "buffer_create.h"
#include "buffer_pool.h"
#include "image.h"
#include "image_create.h"
#include "image_upload.h"
#include "fence.h"
#include "semaphore.h"
#include "sampler.h"
#include "sync_pools.h"
#include "memory_allocator.h"
#include "release_queue.h"
#include "memory_release.h"
#include "frame_context.h"
#include "submission.h"
#include "command_buffer.h"
#include "program_cache.h"
#include "render_target.h"
#include "framebuffer.h"
#include "pipeline_cache.h"

RETRO_BEGIN_DECLS

/* a uniform block's pieces are bound with at least this much */
#define GRANITE_DEVICE_MAX_UBO_SIZE (16 * 1024)

/* The renderer's side of a Vulkan device: its frame contexts, how work
 * gets to its queues, its buffers, images, shaders, pipelines and render
 * passes.
 *
 * No lock. The frame, submission, command pool, buffer pool and memory
 * allocator state has a single owner at any time: the thread that
 * processes RDP commands (parallel-rdp's command thread, or the
 * emulation thread with single-threaded processing). Setup happens on
 * the calling thread before the command thread starts, teardown after
 * it is joined; in between, everything that needs that state is routed
 * to the command thread. Other threads only release objects (lock-free
 * per-frame stacks), wait on fences (lock-free claim) and use the
 * lock-free pools and caches. */
typedef struct granite_device
{
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
	const struct VolkDeviceTable *table;
	granite_queue_info_t queue_info;
	unsigned num_thread_indices;

	VkPhysicalDeviceMemoryProperties mem_props;
	VkPhysicalDeviceProperties gpu_props;
	granite_device_features_t ext;
	granite_workarounds_t workarounds;

	/* the counted objects handed out: buffers and their views, images
	 * and theirs, fences, semaphores, command buffers */
	granite_buffer_pools_t buffer_objects;
	granite_image_pools_t image_objects;
	granite_lf_pool_t fence_objects;
	granite_lf_pool_t semaphore_objects;
	granite_lf_pool_t command_buffer_objects;
	/* what they hand back when their last reference goes */
	granite_handle_owner_t handle_owner;

	/* Released from any thread without a lock: Vulkan objects, and
	 * device memory. Each frame context drains its own when it begins,
	 * after waiting for that frame's GPU work. */
	granite_release_queue_t releases;
	granite_memory_release_queue_t memory_releases;

	/* where threads that find another already waiting on a fence park */
	retro_eventcount_t fence_wait_ec;
	bool fence_wait_ec_live;

	granite_device_allocator_t memory;
	granite_fence_pool_t fences;
	granite_semaphore_pool_t semaphores;
	/* vertex and uniform data written in a frame (buffer_pool.h) */
	granite_buffer_pool_t vbo;
	granite_buffer_pool_t ubo;

	/* command buffers handed out and not yet submitted */
	unsigned open_command_buffers;

	/* the frame contexts and what they share */
	granite_frame_env_t frame_env;
	granite_frame_context_t per_frame[GRANITE_RELEASE_FRAMES];
	unsigned num_frames;
	unsigned frame_index;

	/* how command buffers get to the queues */
	granite_submission_t submission;

	granite_sampler_t samplers[GRANITE_STOCK_SAMPLER_COUNT];

	/* shaders, programs, pipeline layouts, set allocators, render passes */
	granite_program_cache_t program_cache;
	bool program_cache_live;

	granite_framebuffer_allocator_t framebuffer_allocator;
	VkPipelineCache pipeline_cache;
	/* the pipelines built, for the next run to build ahead */
	granite_pipeline_record_table_t pipeline_records;

	/* what command buffers need of the device */
	granite_cmd_device_t cmd_device;
} granite_device_t;

/* ---- setup and teardown: the calling thread, alone ---- */
void granite_device_init(granite_device_t *d);
/* @context outlives the device. Leaves it with two frame contexts. */
void granite_device_set_context(granite_device_t *d, const granite_context_t *context);
/* Waits for the device to be idle and frees everything */
void granite_device_free(granite_device_t *d);
/* @count frame contexts from now on, at most GRANITE_RELEASE_FRAMES.
 * Waits for the device to be idle first. */
void granite_device_init_frame_contexts(granite_device_t *d, unsigned count);

/* ---- frames ---- */
static INLINE granite_frame_context_t *granite_device_frame(granite_device_t *d)
{
	return &d->per_frame[d->frame_index];
}

/* Submits what the frame has and moves to the next frame context,
 * waiting for the work it did last time round */
void granite_device_next_frame_context(granite_device_t *d);
/* Submits what the frame has, waits for the device to finish all of it,
 * and gives everything not in use back */
void granite_device_wait_idle(granite_device_t *d);
/* Submits what the frame has */
void granite_device_flush_frame(granite_device_t *d);

/* ---- command buffers ---- */
/* One to record into on the calling thread, for queue @type, with one
 * reference; NULL when out of memory */
granite_command_buffer_t *granite_device_request_command_buffer(granite_device_t *d, unsigned type);
/* The same for thread index @thread */
granite_command_buffer_t *granite_device_request_command_buffer_for_thread(granite_device_t *d, unsigned thread,
		unsigned type);
/* @cb is done: see granite_submission_submit. Takes the caller's
 * reference to @cb. */
void granite_device_submit(granite_device_t *d, granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned semaphore_count, granite_semaphore_t **semaphores);
/* What the frame has for queue @type goes out now, if only for @fence */
void granite_device_submit_empty(granite_device_t *d, unsigned type, granite_fence_t **fence);
/* @cb filled buffers used as @usage, once: ordered before later work on
 * every queue that may use them. Takes the caller's reference to @cb. */
void granite_device_submit_initialization(granite_device_t *d, granite_command_buffer_t *cb,
		VkBufferUsageFlags usage);
/* Queue @type's next submission waits for @semaphore at @stages; the
 * caller keeps its reference */
void granite_device_add_wait_semaphore(granite_device_t *d, unsigned type, granite_semaphore_t *semaphore,
		VkPipelineStageFlags stages, bool flush);

/* ---- buffers and images: each with one reference, NULL on failure ---- */
/* @initial: info->size bytes to fill it with, or NULL */
granite_buffer_t *granite_device_create_buffer(granite_device_t *d, const granite_buffer_info_t *info,
		const void *initial);
/* A buffer that is @host_buffer's memory, which must stay where it is */
granite_buffer_t *granite_device_create_imported_host_buffer(granite_device_t *d, const granite_buffer_info_t *info,
		void *host_buffer);
/* A 2D image; @initial: its levels' data, or NULL */
granite_image_t *granite_device_create_image(granite_device_t *d, const granite_image_info_t *info,
		const granite_image_initial_data_t *initial);
granite_image_view_t *granite_device_create_image_view(granite_device_t *d, const granite_image_view_info_t *info);
granite_buffer_view_t *granite_device_create_buffer_view(granite_device_t *d, const granite_buffer_view_info_t *info);

static INLINE void *granite_device_map_host_buffer(granite_device_t *d, const granite_buffer_t *buffer,
		uint32_t access, VkDeviceSize offset, VkDeviceSize length)
{
	return granite_device_allocator_map(&d->memory, &buffer->alloc, access, offset, length);
}

static INLINE void granite_device_unmap_host_buffer(granite_device_t *d, const granite_buffer_t *buffer,
		uint32_t access, VkDeviceSize offset, VkDeviceSize length)
{
	granite_device_allocator_unmap(&d->memory, &buffer->alloc, access, offset, length);
}

/* ---- render passes and framebuffers: the owner thread ---- */
/* Out of memory ends the program: there is nothing to draw with */
const granite_cached_render_pass_t *granite_device_request_render_pass(granite_device_t *d,
		const granite_render_pass_desc_t *desc, bool compatible);
const granite_framebuffer_t *granite_device_request_framebuffer(granite_device_t *d,
		const granite_render_pass_info_t *info);

/* ---- the pipelines built ---- */
/* Records a pipeline that was built, from the thread that built it */
void granite_device_record_pipeline(granite_device_t *d, const granite_pipeline_compile_t *compile, uint32_t kind);
/* The compatible render pass a signature describes, made again from
 * 1x1 stand-ins when there is none yet; NULL when it cannot be */
const granite_cached_render_pass_t *granite_device_recreate_render_pass(granite_device_t *d,
		const pipeline_record_pass_t *signature);
/* The pipeline a record describes, made ready to build: its program
 * (made from the device's shaders when there is none yet) and render
 * pass, its state; false when what it was made from is not there, or
 * hashes to another pipeline. The owner thread. */
bool granite_device_resolve_pipeline_record(granite_device_t *d, const pipeline_record_t *record,
		granite_pipeline_compile_t *compile);

RETRO_END_DECLS

#endif
