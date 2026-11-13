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

#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "command_ops.h"
#include "texture_format.h"
#include "thread_id.h"
#include "logging.h"

/* ---- what the device's objects hand back when their last reference
 * goes (handle.h): to this frame's garbage on the owner thread, through
 * the release queues from any other ---- */

static void handle_destroy(void *user, unsigned kind, uint64_t handle, bool owner_thread)
{
	granite_device_t *d = (granite_device_t *)user;
	if (owner_thread)
		granite_frame_garbage_push(&granite_device_frame(d)->garbage, (enum granite_garbage_kind)kind, handle);
	else
		granite_release_queue_push(&d->releases, (enum granite_garbage_kind)kind, handle);
}

static void handle_free_memory(void *user, const granite_allocation_t *alloc, bool owner_thread)
{
	granite_device_t *d = (granite_device_t *)user;
	if (owner_thread)
		granite_memory_garbage_push(&granite_device_frame(d)->memory, alloc);
	else
		granite_memory_release_queue_push(&d->memory_releases, granite_release_queue_frame(&d->releases), alloc);
}

static void handle_recycle_fence(void *user, VkFence fence, bool seen, bool owner_thread)
{
	granite_device_t *d = (granite_device_t *)user;
	if (!owner_thread)
		/* Reset and recycled when this frame comes round again, whether
		 * or not someone already waited on it. */
		granite_release_queue_push(&d->releases, GRANITE_GARBAGE_RECYCLE_FENCE, GRANITE_VK_TO_BITS(fence));
	else if (seen)
	{
		d->table->vkResetFences(d->device, 1, &fence);
		granite_fence_pool_recycle(&d->fences, fence);
	}
	else
		granite_frame_garbage_push(&granite_device_frame(d)->garbage, GRANITE_GARBAGE_RECYCLE_FENCE,
				GRANITE_VK_TO_BITS(fence));
}

static void destroy_framebuffer(void *user, VkFramebuffer framebuffer)
{
	granite_device_t *d = (granite_device_t *)user;
	granite_frame_garbage_push(&granite_device_frame(d)->garbage, GRANITE_GARBAGE_FRAMEBUFFER,
			GRANITE_VK_TO_BITS(framebuffer));
}

static void destroy_pipeline(void *user, const granite_pipeline_t *pipeline)
{
	granite_device_t *d = (granite_device_t *)user;
	granite_release_queue_push(&d->releases, GRANITE_GARBAGE_PIPELINE, GRANITE_VK_TO_BITS(pipeline->pipeline));
}

/* ---- the buffers behind the vertex and uniform blocks (buffer_pool.h):
 * memory the host writes and the device reads in place ---- */

static void *block_create(void *user, VkDeviceSize size, VkBufferUsageFlags usage, uint8_t **mapped)
{
	granite_device_t *d = (granite_device_t *)user;
	granite_buffer_info_t info;
	granite_buffer_t *buffer;

	memset(&info, 0, sizeof(info));
	info.domain = GRANITE_BUFFER_DOMAIN_LINKED_DEVICE_HOST;
	info.size = size;
	info.usage = usage;

	buffer = granite_device_create_buffer(d, &info, NULL);
	if (!buffer)
		return NULL;
	buffer->internal_sync = true;
	*mapped = (uint8_t *)granite_device_map_host_buffer(d, buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, size);
	/* The block owns the reference from here on. */
	return buffer;
}

static void block_release(void *user, void *handle)
{
	(void)user;
	granite_buffer_unref((granite_buffer_t *)handle);
}

static uint8_t *block_map(void *user, void *handle)
{
	granite_device_t *d = (granite_device_t *)user;
	const granite_buffer_t *buffer = (const granite_buffer_t *)handle;
	return (uint8_t *)granite_device_map_host_buffer(d, buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer->info.size);
}

static void block_unmap(void *user, void *handle)
{
	granite_device_t *d = (granite_device_t *)user;
	const granite_buffer_t *buffer = (const granite_buffer_t *)handle;
	granite_device_unmap_host_buffer(d, buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer->info.size);
}

static const granite_buffer_pool_ops_t block_ops = { block_create, block_release, block_map, block_unmap };

static void request_block(void *user, bool vertex, granite_buffer_block_t *block, VkDeviceSize size)
{
	granite_device_t *d = (granite_device_t *)user;
	granite_frame_context_t *frame = granite_device_frame(d);
	if (vertex)
		granite_buffer_pool_exchange(&d->vbo, block, size, &frame->vbo_blocks);
	else
		granite_buffer_pool_exchange(&d->ubo, block, size, &frame->ubo_blocks);
}

static void record_pipeline(void *user, const granite_pipeline_compile_t *compile, uint32_t kind)
{
	granite_device_record_pipeline((granite_device_t *)user, compile, kind);
}

/* ---- setup and teardown ---- */

void granite_device_init(granite_device_t *d)
{
	memset(d, 0, sizeof(*d));
	d->num_thread_indices = 1;

	granite_buffer_pools_init(&d->buffer_objects);
	granite_image_pools_init(&d->image_objects);
	granite_fence_pool_slots_init(&d->fence_objects);
	granite_semaphore_pool_slots_init(&d->semaphore_objects);
	granite_cb_pool_slots_init(&d->command_buffer_objects);

	granite_release_queue_init(&d->releases);
	granite_memory_release_queue_init(&d->memory_releases);

	granite_framebuffer_allocator_init(&d->framebuffer_allocator, VK_NULL_HANDLE, NULL, destroy_framebuffer, d);
	granite_submission_init(&d->submission);
	d->fence_wait_ec_live = retro_eventcount_init(&d->fence_wait_ec);
	d->handle_owner.user = d;
	d->handle_owner.destroy = handle_destroy;
	d->handle_owner.free_memory = handle_free_memory;
	d->handle_owner.recycle_fence = handle_recycle_fence;
	granite_pipeline_record_table_init(&d->pipeline_records);
}

void granite_device_set_context(granite_device_t *d, const granite_context_t *context)
{
	const bool timeline = context->ext.timeline_semaphore_features.timelineSemaphore != VK_FALSE;
	VkDeviceSize ubo_alignment;
	unsigned i;

	d->table = &context->device_table;

	granite_register_thread_index(0);
	d->instance = context->instance;
	d->gpu = context->gpu;
	d->device = context->device;
	d->queue_info = context->queue_info;
	d->mem_props = context->mem_props;
	d->gpu_props = context->gpu_props;
	d->ext = context->ext;

	granite_workarounds_init(&d->workarounds, d->gpu_props.vendorID, d->ext.driver_properties.driverID, timeline,
			d->ext.supports_tooling_info && granite_device_tracing_tool_attached(d->gpu));

	/* What command buffers need of the device (command_buffer.h). */
	d->cmd_device.device = d->device;
	d->cmd_device.table = d->table;
	d->cmd_device.workarounds = &d->workarounds;
	d->cmd_device.features = &d->ext;
	d->cmd_device.programs = &d->program_cache;
	d->cmd_device.framebuffers = &d->framebuffer_allocator;
	d->cmd_device.stock_samplers = d->samplers;
	d->cmd_device.user = d;
	d->cmd_device.request_block = request_block;
	d->cmd_device.record_pipeline = record_pipeline;

	/* (the allocator is made before the device has its handles) */
	d->framebuffer_allocator.device = d->device;
	d->framebuffer_allocator.table = d->table;

	granite_program_cache_init(&d->program_cache, d->device, d->table, d->ext.supports_push_descriptor,
			d->ext.push_descriptor_properties.maxPushDescriptors,
			d->gpu_props.limits.maxBoundDescriptorSets, d->num_thread_indices);
	d->program_cache_live = true;

	granite_sampler_init_linear_clamp(&d->samplers[GRANITE_STOCK_SAMPLER_LINEAR_CLAMP], d->device, d->table);

	/* How command buffers get to the queues (submission.h). */
	d->submission.device = d->device;
	d->submission.table = d->table;
	d->submission.timeline = timeline;
	d->submission.split = d->workarounds.split_binary_timeline_semaphores;
	for (i = 0; i < GRANITE_QUEUE_COUNT; i++)
		d->submission.queues[i].queue = d->queue_info.queues[i];
	d->submission.fences = &d->fences;
	d->submission.semaphores = &d->semaphores;
	d->submission.fence_objects = &d->fence_objects;
	d->submission.semaphore_objects = &d->semaphore_objects;
	d->submission.owner = &d->handle_owner;
	d->submission.fence_wait_ec = d->fence_wait_ec_live ? &d->fence_wait_ec : NULL;
	granite_submission_init_timelines(&d->submission);

	/* What the frame contexts share (frame_context.h). */
	d->frame_env.device = d->device;
	d->frame_env.table = d->table;
	d->frame_env.timeline = timeline;
	d->frame_env.memory = &d->memory;
	d->frame_env.fences = &d->fences;
	d->frame_env.semaphores = &d->semaphores;
	d->frame_env.vbo = &d->vbo;
	d->frame_env.ubo = &d->ubo;
	d->frame_env.releases = &d->releases;
	d->frame_env.memory_releases = &d->memory_releases;

	granite_device_allocator_init(&d->memory, d->device, d->gpu, d->table, &d->mem_props,
			d->gpu_props.limits.nonCoherentAtomSize, d->ext.supports_memory_budget,
			d->ext.memory_priority_features.memoryPriority != VK_FALSE);
	granite_semaphore_pool_init(&d->semaphores, d->device, d->table);
	granite_fence_pool_init(&d->fences, d->device, d->table);
	ubo_alignment = d->gpu_props.limits.minUniformBufferOffsetAlignment;
	if (ubo_alignment < 16)
		ubo_alignment = 16;
	if (!granite_buffer_pool_init(&d->vbo, &block_ops, d, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, 256)
			|| !granite_buffer_pool_init(&d->ubo, &block_ops, d, 256 * 1024, ubo_alignment,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, GRANITE_DEVICE_MAX_UBO_SIZE, 64))
		LOGE("Failed to set up the buffer pools.\n");

	/* By default, regular double buffer between CPU and GPU. */
	granite_device_init_frame_contexts(d, 2);
}

static void free_frame_contexts(granite_device_t *d)
{
	unsigned i;
	for (i = 0; i < d->num_frames; i++)
		granite_frame_context_free(&d->per_frame[i]);
	d->num_frames = 0;
}

void granite_device_init_frame_contexts(granite_device_t *d, unsigned count)
{
	VkSemaphore timelines[GRANITE_QUEUE_COUNT];
	unsigned i;

	granite_device_wait_idle(d);

	/* Clear out caches which might contain stale data from now on. */
	granite_framebuffer_allocator_clear(&d->framebuffer_allocator);
	free_frame_contexts(d);

	if (count > GRANITE_RELEASE_FRAMES)
		count = GRANITE_RELEASE_FRAMES;
	for (i = 0; i < GRANITE_QUEUE_COUNT; i++)
		timelines[i] = d->submission.queues[i].timeline_semaphore;
	for (i = 0; i < count; i++)
	{
		if (!granite_frame_context_init(&d->per_frame[i], &d->frame_env, i, d->queue_info.family_indices,
					timelines, d->num_thread_indices))
			break;
		d->num_frames++;
	}
	/* Nothing can be recorded without one. */
	if (!d->num_frames)
		abort();
	if (d->frame_index >= d->num_frames)
		d->frame_index = 0;
	granite_release_queue_set_frame(&d->releases, d->frame_index);
}

void granite_device_free(granite_device_t *d)
{
	unsigned i;

	granite_device_wait_idle(d);

	if (d->pipeline_cache != VK_NULL_HANDLE)
		d->table->vkDestroyPipelineCache(d->device, d->pipeline_cache, NULL);
	d->pipeline_cache = VK_NULL_HANDLE;

	for (i = 0; i < GRANITE_STOCK_SAMPLER_COUNT; i++)
		granite_sampler_free(&d->samplers[i], d->device, d->table);

	granite_pipeline_record_table_free(&d->pipeline_records);

	granite_framebuffer_allocator_free(&d->framebuffer_allocator);
	if (d->program_cache_live)
		granite_program_cache_free(&d->program_cache, destroy_pipeline, d);
	d->program_cache_live = false;
	granite_submission_free(&d->submission);

	granite_submission_free_timelines(&d->submission);
	/* Make sure we don't accidentally try to wait for these after we
	 * destroy the semaphores. */
	for (i = 0; i < d->num_frames; i++)
		granite_frame_sync_forget_timelines(&d->per_frame[i].sync);

	if (d->fence_wait_ec_live)
		retro_eventcount_free(&d->fence_wait_ec);
	d->fence_wait_ec_live = false;

	/* The frames take what the caches above released, and give it all
	 * back to the pools and the allocator, which go next. */
	free_frame_contexts(d);

	granite_buffer_pool_free(&d->vbo);
	granite_buffer_pool_free(&d->ubo);
	granite_semaphore_pool_free(&d->semaphores);
	granite_fence_pool_free(&d->fences);
	granite_device_allocator_free(&d->memory);

	granite_release_queue_free(&d->releases);
	granite_memory_release_queue_free(&d->memory_releases);

	granite_buffer_pools_free(&d->buffer_objects);
	granite_image_pools_free(&d->image_objects);
	granite_lf_pool_clear(&d->fence_objects);
	granite_lf_pool_clear(&d->semaphore_objects);
	granite_lf_pool_clear(&d->command_buffer_objects);
}

/* ---- frames ---- */

void granite_device_wait_idle(granite_device_t *d)
{
	unsigned i;

	if (d->num_frames)
		granite_submission_end_frame(&d->submission, granite_device_frame(d));

	granite_submission_wait_idle(&d->submission);

	/* Free memory for buffer pools. */
	granite_buffer_pool_reset(&d->vbo);
	granite_buffer_pool_reset(&d->ubo);
	for (i = 0; i < d->num_frames; i++)
	{
		granite_buffer_block_list_release(&d->per_frame[i].vbo_blocks, &d->vbo);
		granite_buffer_block_list_release(&d->per_frame[i].ubo_blocks, &d->ubo);
	}

	granite_framebuffer_allocator_clear(&d->framebuffer_allocator);

	if (d->program_cache_live)
		granite_program_cache_clear_sets(&d->program_cache);

	for (i = 0; i < d->num_frames; i++)
	{
		/* We have done WaitIdle, no need to wait for extra fences, it's
		 * also not safe. */
		granite_frame_sync_forget_fences(&d->per_frame[i].sync);
		granite_frame_context_begin(&d->per_frame[i]);
		granite_frame_context_trim(&d->per_frame[i]);
	}

	granite_device_allocator_collect(&d->memory);
}

void granite_device_next_frame_context(granite_device_t *d)
{
	/* Flush the frame here as we might have pending staging command
	 * buffers from init stage. */
	granite_submission_end_frame(&d->submission, granite_device_frame(d));

	granite_framebuffer_allocator_begin_frame(&d->framebuffer_allocator);
	granite_program_cache_begin_frame(&d->program_cache);

	d->frame_index++;
	if (d->frame_index >= d->num_frames)
		d->frame_index = 0;

	granite_frame_context_begin(granite_device_frame(d));
	granite_release_queue_set_frame(&d->releases, d->frame_index);
}

void granite_device_flush_frame(granite_device_t *d)
{
	granite_submission_flush(&d->submission, granite_device_frame(d));
}

/* ---- command buffers ---- */

granite_command_buffer_t *granite_device_request_command_buffer_for_thread(granite_device_t *d, unsigned thread,
		unsigned type)
{
	VkCommandBufferBeginInfo info;
	granite_command_buffer_t *cb;
	VkCommandBuffer cmd = granite_frame_context_request(granite_device_frame(d), type, thread);

	/* Out of memory: nothing to record into. */
	if (cmd == VK_NULL_HANDLE)
		return NULL;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	d->table->vkBeginCommandBuffer(cmd, &info);
	/* Without an object the VkCommandBuffer goes back with its pool's
	 * frame. */
	cb = granite_cb_new(&d->command_buffer_objects, &d->cmd_device, cmd, d->pipeline_cache, type);
	if (!cb)
		return NULL;
	cb->state.thread_index = thread;
	d->open_command_buffers++;
	return cb;
}

granite_command_buffer_t *granite_device_request_command_buffer(granite_device_t *d, unsigned type)
{
	return granite_device_request_command_buffer_for_thread(d, granite_thread_index(), type);
}

void granite_device_submit(granite_device_t *d, granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned semaphore_count, granite_semaphore_t **semaphores)
{
	granite_submission_submit(&d->submission, granite_device_frame(d), cb, fence, semaphore_count, semaphores);
	granite_cb_unref(cb);
	d->open_command_buffers--;
}

void granite_device_submit_empty(granite_device_t *d, unsigned type, granite_fence_t **fence)
{
	granite_submission_submit_empty(&d->submission, granite_device_frame(d), type, fence);
}

/* @flush: the queues that wait for it first send what they have */
static void submit_staging(granite_device_t *d, granite_command_buffer_t *cb, VkBufferUsageFlags usage, bool flush)
{
	granite_submission_submit_staging(&d->submission, granite_device_frame(d), cb, usage, flush);
	granite_cb_unref(cb);
	d->open_command_buffers--;
}

void granite_device_submit_initialization(granite_device_t *d, granite_command_buffer_t *cb,
		VkBufferUsageFlags usage)
{
	submit_staging(d, cb, usage, true);
}

void granite_device_add_wait_semaphore(granite_device_t *d, unsigned type, granite_semaphore_t *semaphore,
		VkPipelineStageFlags stages, bool flush)
{
	granite_submission_add_wait(&d->submission, granite_device_frame(d), type, semaphore, stages, flush);
}

/* ---- buffers ---- */

static void buffer_env(granite_device_t *d, granite_buffer_env_t *env)
{
	env->device = d->device;
	env->table = d->table;
	env->allocator = &d->memory;
	env->memory = &d->mem_props;
	env->queue_families = d->queue_info.family_indices;
	env->num_queue_families = GRANITE_QUEUE_COUNT;
	env->force_host_cached = d->workarounds.force_host_cached;
}

/* The counted object for a buffer and its memory; without room for one,
 * neither is kept. */
static granite_buffer_t *wrap_buffer(granite_device_t *d, VkBuffer buffer, granite_allocation_t *allocation,
		const granite_buffer_info_t *info)
{
	granite_buffer_t *object = granite_buffer_new(&d->buffer_objects, &d->handle_owner, buffer, allocation, info);
	if (!object)
	{
		d->table->vkDestroyBuffer(d->device, buffer, NULL);
		granite_allocation_free(allocation, &d->memory);
	}
	return object;
}

granite_buffer_t *granite_device_create_buffer(granite_device_t *d, const granite_buffer_info_t *info,
		const void *initial)
{
	const bool zero_initialize = (info->misc & GRANITE_BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
	granite_buffer_info_t object_info;
	granite_allocation_t allocation;
	granite_buffer_env_t env;
	granite_buffer_t *handle;
	uint32_t memory_type;
	VkBuffer buffer;

	if (initial && zero_initialize)
	{
		LOGE("Cannot initialize buffer with data and clear.\n");
		return NULL;
	}

	memset(&allocation, 0, sizeof(allocation));
	allocation.mode = GRANITE_ALLOCATION_MODE_COUNT;
	buffer_env(d, &env);
	if (!granite_buffer_create(&env, info->domain, info->size, info->usage, &buffer, &allocation, &memory_type))
		return NULL;

	object_info = *info;
	object_info.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	handle = wrap_buffer(d, buffer, &allocation, &object_info);
	if (!handle || (!initial && !zero_initialize))
		return handle;

	if (info->domain == GRANITE_BUFFER_DOMAIN_DEVICE
			&& !(d->mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		/* The host cannot write it: filled by the GPU. */
		granite_command_buffer_t *cb;

		if (initial)
		{
			granite_buffer_info_t staging_info = *info;
			granite_buffer_t *staging;
			VkBufferCopy region;

			staging_info.domain = GRANITE_BUFFER_DOMAIN_HOST;
			staging = granite_device_create_buffer(d, &staging_info, initial);
			cb = staging ? granite_device_request_command_buffer(d, GRANITE_QUEUE_TRANSFER) : NULL;
			if (!cb)
			{
				if (staging)
					granite_buffer_unref(staging);
				granite_buffer_unref(handle);
				return NULL;
			}
			region.srcOffset = 0;
			region.dstOffset = 0;
			region.size = info->size;
			granite_cb_copy_buffer(cb, handle, staging, &region, 1);
			/* (kept by its frame until the copy is done) */
			granite_buffer_unref(staging);
		}
		else
		{
			cb = granite_device_request_command_buffer(d, GRANITE_QUEUE_COMPUTE);
			if (!cb)
			{
				granite_buffer_unref(handle);
				return NULL;
			}
			granite_cb_fill_buffer(cb, handle, 0, 0, VK_WHOLE_SIZE);
		}

		submit_staging(d, cb, object_info.usage, true);
	}
	else
	{
		void *ptr = granite_device_allocator_map(&d->memory, &handle->alloc, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
				handle->alloc.size);
		if (!ptr)
		{
			granite_buffer_unref(handle);
			return NULL;
		}

		if (initial)
			memcpy(ptr, initial, (size_t)info->size);
		else
			memset(ptr, 0, (size_t)info->size);
		granite_device_allocator_unmap(&d->memory, &handle->alloc, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0,
				handle->alloc.size);
	}
	return handle;
}

granite_buffer_t *granite_device_create_imported_host_buffer(granite_device_t *d, const granite_buffer_info_t *info,
		void *host_buffer)
{
	granite_allocation_t allocation;
	granite_buffer_env_t env;
	VkBuffer buffer;

	if (!d->ext.supports_external_memory_host)
		return NULL;

	memset(&allocation, 0, sizeof(allocation));
	allocation.mode = GRANITE_ALLOCATION_MODE_COUNT;
	buffer_env(d, &env);
	if (!granite_buffer_create_host_import(&env, info->domain, info->size, info->usage,
				d->ext.host_memory_properties.minImportedHostPointerAlignment, host_buffer, &buffer, &allocation))
		return NULL;

	return wrap_buffer(d, buffer, &allocation, info);
}

granite_buffer_view_t *granite_device_create_buffer_view(granite_device_t *d, const granite_buffer_view_info_t *info)
{
	VkBufferViewCreateInfo create;
	granite_buffer_view_t *object;
	VkBufferView view;

	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
	create.buffer = info->buffer->buffer;
	create.format = info->format;
	create.offset = info->offset;
	create.range = info->range;
	if (d->table->vkCreateBufferView(d->device, &create, NULL, &view) != VK_SUCCESS)
		return NULL;

	object = granite_buffer_view_new(&d->buffer_objects, &d->handle_owner, view, info);
	if (!object)
		d->table->vkDestroyBufferView(d->device, view, NULL);
	return object;
}

/* ---- images ---- */

granite_image_view_t *granite_device_create_image_view(granite_device_t *d, const granite_image_view_info_t *info)
{
	const granite_image_info_t *image_info = &info->image->info;
	granite_image_view_info_t object_info;
	granite_image_view_desc_t desc;
	granite_image_view_t *object;
	granite_image_views_t views;

	memset(&desc, 0, sizeof(desc));
	desc.image = info->image->image;
	desc.format = info->format != VK_FORMAT_UNDEFINED ? info->format : image_info->format;
	desc.type = info->view_type;
	desc.base_level = info->base_level;
	desc.base_layer = info->base_layer;
	desc.levels = info->levels == VK_REMAINING_MIP_LEVELS ? image_info->levels - info->base_level : info->levels;
	desc.layers = info->layers == VK_REMAINING_ARRAY_LAYERS ? image_info->layers - info->base_layer : info->layers;
	desc.usage = image_info->usage;

	if (!granite_image_views_create(d->device, d->table, &desc, &views))
		return NULL;

	object_info = *info;
	object_info.format = desc.format;
	object = granite_image_view_new(&d->image_objects, &d->handle_owner, &views, &object_info);
	if (!object)
		granite_image_views_destroy(d->device, d->table, &views);
	return object;
}

/* The data an image starts with, in a buffer the GPU copies from */
typedef struct image_staging
{
	granite_buffer_t *buffer;
	VkBufferImageCopy blits[GRANITE_TEXTURE_MAX_LEVELS];
	unsigned num_blits;
} image_staging_t;

static bool create_image_staging(granite_device_t *d, const granite_image_info_t *info,
		const granite_image_initial_data_t *initial, image_staging_t *staging)
{
	granite_texture_layout_t layout;
	granite_buffer_info_t buffer_info;

	granite_texture_layout_set(&layout, info->type, info->format, info->width, info->height, info->depth,
			info->layers, info->levels);

	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.domain = GRANITE_BUFFER_DOMAIN_HOST;
	buffer_info.size = layout.required_size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	staging->buffer = granite_device_create_buffer(d, &buffer_info, NULL);
	if (!staging->buffer)
		return false;

	layout.buffer = (uint8_t *)granite_device_map_host_buffer(d, staging->buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT,
			0, buffer_info.size);
	if (!layout.buffer)
	{
		granite_buffer_unref(staging->buffer);
		staging->buffer = NULL;
		return false;
	}
	granite_image_upload_fill(&layout, initial);
	granite_device_unmap_host_buffer(d, staging->buffer, GRANITE_MEMORY_ACCESS_WRITE_BIT, 0, buffer_info.size);

	staging->num_blits = layout.mip_levels;
	granite_texture_layout_copies(&layout, granite_format_aspect_mask(info->format), staging->blits);
	return true;
}

/* Puts the new image in the layout asked for, with its data if it has
 * any, on the queues that will use it. False when out of memory. */
static bool upload_image(granite_device_t *d, granite_image_t *image, const granite_image_info_t *info,
		const granite_image_desc_t *desc, const image_staging_t *staging)
{
	granite_image_upload_queues_t queues;
	granite_image_upload_desc_t upload;
	granite_image_upload_plan_t plan;
	granite_command_buffer_t *graphics_cb;
	granite_semaphore_t *semaphore = NULL;

	queues.graphics = d->queue_info.queues[GRANITE_QUEUE_GRAPHICS];
	queues.compute = d->queue_info.queues[GRANITE_QUEUE_COMPUTE];
	queues.transfer = d->queue_info.queues[GRANITE_QUEUE_TRANSFER];
	queues.graphics_family = d->queue_info.family_indices[GRANITE_QUEUE_GRAPHICS];
	queues.compute_family = d->queue_info.family_indices[GRANITE_QUEUE_COMPUTE];
	queues.transfer_family = d->queue_info.family_indices[GRANITE_QUEUE_TRANSFER];

	memset(&upload, 0, sizeof(upload));
	upload.image = image->image;
	upload.format = info->format;
	upload.levels = info->levels;
	upload.layers = info->layers;
	upload.initial_layout = info->initial_layout;
	upload.stages = image->stage_flags;
	upload.access = image->access_flags;
	upload.concurrent = desc->on_graphics || desc->on_compute;
	upload.on_compute = desc->on_compute;
	upload.has_data = staging != NULL;

	granite_image_upload_plan(&queues, &upload, &plan);
	if (!plan.transition)
		return true;

	graphics_cb = granite_device_request_command_buffer(d, GRANITE_QUEUE_GRAPHICS);
	if (!graphics_cb)
		return false;

	/* Copy initial data to texture. */
	if (staging)
	{
		/* Don't split the upload into multiple command buffers unless we
		 * have to. */
		granite_command_buffer_t *transfer_cb = graphics_cb;

		if (plan.split_transfer)
		{
			transfer_cb = granite_device_request_command_buffer(d, GRANITE_QUEUE_TRANSFER);
			if (!transfer_cb)
			{
				granite_device_submit(d, graphics_cb, NULL, 0, NULL);
				return false;
			}
		}

		granite_cb_image_barrier(transfer_cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		granite_cb_copy_buffer_to_image(transfer_cb, image, staging->buffer, staging->num_blits, staging->blits);

		if (plan.split_transfer)
		{
			if (plan.hand_over)
			{
				granite_cb_barrier(transfer_cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						0, NULL, 0, NULL, 1, &plan.release);
				granite_cb_barrier(graphics_cb, plan.wait_stages, plan.wait_stages,
						0, NULL, 0, NULL, 1, &plan.acquire);
			}

			granite_device_submit(d, transfer_cb, NULL, 1, &semaphore);
			granite_device_add_wait_semaphore(d, GRANITE_QUEUE_GRAPHICS, semaphore, plan.wait_stages, true);
			if (semaphore)
				granite_semaphore_unref(semaphore);
			semaphore = NULL;
		}
	}

	if (plan.final_barrier)
		granite_cb_image_barrier(graphics_cb, image, plan.old_layout, info->initial_layout,
				plan.src_stage, plan.src_access, image->stage_flags, plan.dst_access);

	/* For concurrent queue, make sure that compute can see the final
	 * image as well. */
	if (plan.share_compute)
	{
		granite_device_submit(d, graphics_cb, NULL, 1, &semaphore);
		granite_device_add_wait_semaphore(d, GRANITE_QUEUE_COMPUTE, semaphore, plan.compute_stages, true);
		if (semaphore)
			granite_semaphore_unref(semaphore);
	}
	else
	{
		granite_device_submit(d, graphics_cb, NULL, 0, NULL);
		if (plan.flush_graphics)
			granite_submission_flush_queue(&d->submission, granite_device_frame(d), GRANITE_QUEUE_GRAPHICS);
	}
	return true;
}

granite_image_t *granite_device_create_image(granite_device_t *d, const granite_image_info_t *info,
		const granite_image_initial_data_t *initial)
{
	granite_image_info_t object_info;
	granite_allocation_t allocation;
	granite_image_views_t views;
	image_staging_t staging;
	granite_image_desc_t desc;
	granite_image_env_t env;
	granite_image_t *handle;
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	VkImageUsageFlags usage = info->usage;
	VkImageCreateFlags flags;
	VkImage image;
	bool has_view;
	bool views_ok = true;

	if (info->type != VK_IMAGE_TYPE_2D || info->depth != 1)
		return NULL;

	if (!(info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)
			&& !granite_image_format_is_supported(d->gpu, info->format, granite_image_usage_features(usage),
				VK_IMAGE_TILING_OPTIMAL, d->ext.supports_format_feature_flags2))
	{
		LOGE("Format %u is not supported for usage flags!\n", (unsigned)info->format);
		return NULL;
	}

	memset(&staging, 0, sizeof(staging));
	if (initial && !create_image_staging(d, info, initial, &staging))
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.format = info->format;
	desc.width = info->width;
	desc.height = info->height;
	desc.levels = info->levels;
	desc.layers = info->layers;
	desc.samples = info->samples;
	desc.usage = info->usage;
	desc.flags = info->flags;
	desc.force_array = (info->misc & GRANITE_IMAGE_MISC_FORCE_ARRAY_BIT) != 0;
	desc.mutable_srgb = (info->misc & GRANITE_IMAGE_MISC_MUTABLE_SRGB_BIT) != 0;
	desc.on_graphics = (info->misc & GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT) != 0;
	desc.on_compute = (info->misc & GRANITE_IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT) != 0;

	memset(&env, 0, sizeof(env));
	env.device = d->device;
	env.table = d->table;
	env.allocator = &d->memory;
	env.memory = &d->mem_props;
	env.graphics_family = d->queue_info.family_indices[GRANITE_QUEUE_GRAPHICS];
	env.compute_family = d->queue_info.family_indices[GRANITE_QUEUE_COMPUTE];
	env.transfer_family = d->queue_info.family_indices[GRANITE_QUEUE_TRANSFER];
	env.format_list = d->ext.supports_image_format_list;

	memset(&allocation, 0, sizeof(allocation));
	allocation.mode = GRANITE_ALLOCATION_MODE_COUNT;
	handle = NULL;
	if (granite_image_create(&env, &desc, initial != NULL, &image, &allocation, &usage, &flags))
	{
		object_info = *info;
		object_info.usage = usage;
		object_info.flags = flags;

		memset(&views, 0, sizeof(views));
		has_view = granite_image_usage_has_view(usage);
		if (has_view)
		{
			granite_image_view_desc_t view_desc;
			memset(&view_desc, 0, sizeof(view_desc));
			view_desc.image = image;
			view_desc.format = info->format;
			view_desc.type = view_type = granite_image_view_type(&desc);
			view_desc.levels = info->levels;
			view_desc.layers = info->layers;
			view_desc.usage = usage;
			views_ok = granite_image_views_create(d->device, d->table, &view_desc, &views);
		}

		if (views_ok)
		{
			handle = granite_image_new(&d->image_objects, &d->handle_owner, image, &allocation, &object_info,
					has_view ? &views : NULL, view_type);
			if (!handle)
				granite_image_views_destroy(d->device, d->table, &views);
		}
		if (!handle)
		{
			d->table->vkDestroyImage(d->device, image, NULL);
			granite_allocation_free(&allocation, &d->memory);
		}
	}

	if (handle)
	{
		/* Set possible dstStage and dstAccess. */
		handle->stage_flags = granite_image_usage_stages(usage);
		handle->access_flags = granite_image_usage_access(usage);

		if (!upload_image(d, handle, info, &desc, initial ? &staging : NULL))
		{
			granite_image_unref(handle);
			handle = NULL;
		}
	}

	/* (kept by its frame until the copy is done) */
	if (staging.buffer)
		granite_buffer_unref(staging.buffer);
	return handle;
}

/* ---- render passes and framebuffers ---- */

const granite_cached_render_pass_t *granite_device_request_render_pass(granite_device_t *d,
		const granite_render_pass_desc_t *desc, bool compatible)
{
	/* (out of memory leaves nothing to draw with) */
	const granite_cached_render_pass_t *pass = granite_program_cache_render_pass(&d->program_cache, desc, compatible);
	if (!pass)
		abort();
	return pass;
}

const granite_framebuffer_t *granite_device_request_framebuffer(granite_device_t *d,
		const granite_render_pass_info_t *info)
{
	/* (out of memory leaves nothing to draw to) */
	const granite_framebuffer_t *framebuffer = granite_render_target_framebuffer(&d->framebuffer_allocator,
			&d->program_cache, info);
	if (!framebuffer)
		abort();
	return framebuffer;
}

/* ---- the pipelines built ---- */

void granite_device_record_pipeline(granite_device_t *d, const granite_pipeline_compile_t *compile, uint32_t kind)
{
	const bool graphics = kind == PIPELINE_RECORD_GRAPHICS;
	const granite_cached_render_pass_t *pass = compile->compatible_render_pass;
	const granite_program_t *program = compile->program;
	const granite_shader_t *shader;
	granite_pipeline_record_ids_t ids;
	pipeline_record_t *record;

	/* A pass that cannot be made again is not one to build ahead for. */
	if (graphics && !pass->impl.signature.recordable)
		return;

	memset(&ids, 0, sizeof(ids));
	ids.program_hash = program->node.hash;
	shader = program->shaders[graphics ? GRANITE_SHADER_STAGE_VERTEX : GRANITE_SHADER_STAGE_COMPUTE];
	ids.shader_hashes[0] = shader ? shader->node.hash : 0;
	if (graphics)
	{
		shader = program->shaders[GRANITE_SHADER_STAGE_FRAGMENT];
		ids.shader_hashes[1] = shader ? shader->node.hash : 0;
		ids.render_pass_hash = pass->node.hash;
		ids.pass = &pass->impl.signature;
	}

	record = granite_pipeline_record_table_claim(&d->pipeline_records);
	if (!record)
		return;
	granite_pipeline_record_fill(record, kind, compile, &ids);
	granite_pipeline_record_table_commit(&d->pipeline_records);
}

const granite_cached_render_pass_t *granite_device_recreate_render_pass(granite_device_t *d,
		const pipeline_record_pass_t *signature)
{
	granite_render_pass_desc_t desc;
	if (!granite_render_pass_desc_from_signature(&desc, signature))
		return NULL;
	return granite_device_request_render_pass(d, &desc, true);
}

bool granite_device_resolve_pipeline_record(granite_device_t *d, const pipeline_record_t *record,
		granite_pipeline_compile_t *compile)
{
	const bool graphics = record->kind == PIPELINE_RECORD_GRAPHICS;
	granite_program_t *program = granite_program_cache_find_program(&d->program_cache, record->program_hash);
	const granite_cached_render_pass_t *pass = NULL;

	/* A program is made on first use; one this device has not made yet
	 * is made from the shaders it was made from, when those are here. */
	if (!program && !graphics)
		program = granite_program_cache_compute(&d->program_cache,
				granite_program_cache_find_shader(&d->program_cache, record->shader_hashes[0]));
	else if (!program)
		program = granite_program_cache_graphics(&d->program_cache,
				granite_program_cache_find_shader(&d->program_cache, record->shader_hashes[0]),
				granite_program_cache_find_shader(&d->program_cache, record->shader_hashes[1]));
	if (!program || program->node.hash != record->program_hash || !program->layout
			|| (program->shaders[GRANITE_SHADER_STAGE_COMPUTE] != NULL) == graphics)
		return false;
	if (graphics)
	{
		pass = granite_program_cache_find_render_pass(&d->program_cache, record->render_pass_hash);
		if (!pass)
			pass = granite_device_recreate_render_pass(d, &record->pass);
		if (!pass || pass->node.hash != record->render_pass_hash || record->subpass_index != 0)
			return false;
	}

	granite_pipeline_record_apply(record, compile, d->pipeline_cache);
	compile->program = program;
	compile->compatible_render_pass = pass;
	if (graphics)
	{
		uint32_t active_vbos;
		granite_pipeline_compile_update_hash_graphics(compile, &active_vbos);
	}
	else
		granite_pipeline_compile_update_hash_compute(compile);
	return compile->hash == record->pipeline_hash;
}
