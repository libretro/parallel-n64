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
#include "rdp_pipelines.h"
#include "pipeline_map.h"
#include "rdram_coherency.h"
#include "rdp_state.h"
#include "thread_name.h"

#define WORKGROUP_SIZE 64u

/* log2 of a power of two */
static uint32_t log2_of(uint32_t v)
{
	uint32_t n = 0;
	while (v > 1)
	{
		v >>= 1;
		n++;
	}
	return n;
}

void rdp_pipeline_span_setup(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale)
{
	granite_cb_set_program(cb, setup->bank->span_setup);
	granite_cb_set_spec_constant_mask(cb, 3);
	granite_cb_set_spec_constant(cb, 0, (upscale ? setup->caps->upscaling : 1) * WORKGROUP_SIZE);
	granite_cb_set_spec_constant(cb, 1, upscale ? log2_of(setup->caps->upscaling) : 0);
}

static void set_subgroup_size_control(granite_command_buffer_t *cb, bool on, uint32_t max_log2)
{
	granite_pipeline_state_t *state = &cb->pipeline_state.static_state;
	if (on)
	{
		if (!state->state.subgroup_control_size || !state->state.subgroup_full_group ||
		    state->state.subgroup_minimum_size_log2 != 5 || state->state.subgroup_maximum_size_log2 != max_log2)
		{
			state->state.subgroup_control_size = 1;
			state->state.subgroup_full_group = 1;
			state->state.subgroup_minimum_size_log2 = 5;
			state->state.subgroup_maximum_size_log2 = max_log2;
			granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
		}
	}
	else if (state->state.subgroup_control_size)
	{
		state->state.subgroup_control_size = 0;
		granite_cb_set_dirty(cb, GRANITE_COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}
}

uint32_t rdp_pipeline_tile_binning(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale)
{
	const rdp_renderer_caps_t *caps = setup->caps;
	uint32_t subgroup_size = setup->subgroup_size;

	granite_cb_set_spec_constant_mask(cb, 0x7f);
	granite_cb_set_spec_constant(cb, 1, RDP_CAPS_TILE_WIDTH);
	granite_cb_set_spec_constant(cb, 2, RDP_CAPS_TILE_HEIGHT);
	granite_cb_set_spec_constant(cb, 3, RDP_CAPS_MAX_PRIMITIVES);
	granite_cb_set_spec_constant(cb, 4, upscale ? caps->max_width : RDP_CAPS_MAX_WIDTH);
	granite_cb_set_spec_constant(cb, 5, caps->max_num_tile_instances);
	granite_cb_set_spec_constant(cb, 6, upscale ? caps->upscaling : 1);

	granite_cb_set_program(cb, setup->bank->tile_binning_combined);
	/* (subgroup tile binning only where rdp_caps_tile_binning_size_control
	 * says the size can be asked for) */
	if (caps->subgroup_tile_binning)
		set_subgroup_size_control(cb, true, log2_of(subgroup_size));
	else
		subgroup_size = 32;

	granite_cb_set_spec_constant(cb, 0, subgroup_size);
	return subgroup_size;
}

void rdp_pipeline_end_tile_binning(granite_command_buffer_t *cb)
{
	set_subgroup_size_control(cb, false, 0);
}

void rdp_pipeline_rasterizer(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, bool upscale)
{
	granite_cb_set_program(cb, setup->bank->rasterizer);
	granite_cb_set_spec_constant_mask(cb, 7);
	granite_cb_set_spec_constant(cb, 0, RDP_CAPS_TILE_WIDTH);
	granite_cb_set_spec_constant(cb, 1, RDP_CAPS_TILE_HEIGHT);
	granite_cb_set_spec_constant(cb, 2, (upscale ? log2_of(setup->caps->upscaling) : 0)
			<< RASTERIZATION_UPSCALING_LOG2_BIT_OFFSET);
}

void rdp_pipeline_depth_blend(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb, uint32_t fmt,
		bool same_addr, bool upscaled, bool force_write_mask)
{
	const rdp_renderer_caps_t *caps = setup->caps;

	granite_cb_set_program(cb, caps->ubershader ? setup->bank->ubershader : setup->bank->depth_blend);
	granite_cb_set_spec_constant_mask(cb, 0xff);
	granite_cb_set_spec_constant(cb, 0, setup->rdram_size);
	granite_cb_set_spec_constant(cb, 1, fmt);
	granite_cb_set_spec_constant(cb, 2, same_addr);
	granite_cb_set_spec_constant(cb, 3, RDP_CAPS_TILE_WIDTH);
	granite_cb_set_spec_constant(cb, 4, RDP_CAPS_TILE_HEIGHT);
	granite_cb_set_spec_constant(cb, 5, RDP_CAPS_MAX_PRIMITIVES);
	granite_cb_set_spec_constant(cb, 6, upscaled ? caps->max_width : RDP_CAPS_MAX_WIDTH);
	granite_cb_set_spec_constant(cb, 7, (uint32_t)(force_write_mask || (!setup->host_coherent && !upscaled)) |
			((upscaled ? log2_of(caps->upscaling) : 0) << 1));
}

void rdp_pipeline_upscaled_domain(const rdp_pipeline_setup_t *setup, granite_command_buffer_t *cb,
		enum rdp_resolve_stage stage, unsigned pixel_size_log2, bool same_addr)
{
	const rdp_renderer_caps_t *caps = setup->caps;

	if (stage == RDP_RESOLVE_PRE)
		granite_cb_set_program(cb, setup->bank->update_upscaled_domain_pre);
	else if (stage == RDP_RESOLVE_POST)
		granite_cb_set_program(cb, setup->bank->update_upscaled_domain_post);
	else
		granite_cb_set_program(cb, setup->bank->update_upscaled_domain_resolve);

	granite_cb_set_spec_constant_mask(cb, 0x7f);
	granite_cb_set_spec_constant(cb, 0, setup->rdram_size);
	granite_cb_set_spec_constant(cb, 1, pixel_size_log2);
	granite_cb_set_spec_constant(cb, 2, same_addr);
	granite_cb_set_spec_constant(cb, 3, WORKGROUP_SIZE);
	granite_cb_set_spec_constant(cb, 4, caps->upscaling * caps->upscaling);
	if (stage == RDP_RESOLVE_SSAA)
	{
		granite_cb_set_spec_constant(cb, 5, caps->super_sample_readback_dither);
		granite_cb_set_spec_constant(cb, 6, !setup->host_coherent);
	}
}

/* ---- the builder ---- */

static bool built(const granite_pipeline_compile_t *compile)
{
	return granite_pipeline_map_find(&compile->program->pipelines, compile->hash).pipeline != VK_NULL_HANDLE;
}

static void worker_start(void *user)
{
	(void)user;
	granite_set_thread_name("RDP Pipelines");
}

static void worker_work(void *user, mpsc_stack_node_t *item)
{
	rdp_pipeline_builder_t *b = (rdp_pipeline_builder_t *)user;
	rdp_pipeline_job_t *job = (rdp_pipeline_job_t *)item;

	/* Not one built meanwhile (ahead, or by a draw), and none past
	 * teardown: content close does not wait for the queue. */
	if (!retro_atomic_load_acquire_int(&b->stop) && !built(&job->compile))
	{
		if (job->kind == PIPELINE_RECORD_GRAPHICS)
			granite_pipeline_compile_build_graphics(&b->device->cmd_device, &job->compile,
					GRANITE_COMPILE_ASYNC_THREAD);
		else
			granite_pipeline_compile_build_compute(&b->device->cmd_device, &job->compile,
					GRANITE_COMPILE_ASYNC_THREAD);
	}
	if (job->urgent)
		retro_atomic_fetch_add_int(&b->ahead_urgent_done, 1);
	if (!job->ahead)
		rdp_node_pool_release(&b->pool, &job->node);
}

void rdp_pipeline_builder_clear(rdp_pipeline_builder_t *b)
{
	memset(b, 0, sizeof(*b));
	retro_atomic_int_init(&b->ahead_urgent_done, 0);
	retro_atomic_int_init(&b->stop, 0);
	rdp_node_pool_init(&b->pool);
}

bool rdp_pipeline_builder_init(rdp_pipeline_builder_t *b, granite_device_t *device, unsigned max_misses)
{
	unsigned i;

	rdp_pipeline_builder_free(b);
	b->device = device;
	if (!(b->worker = rdp_worker_new(worker_start, worker_work, b)))
		return false;

	/* All a miss needs, so that it allocates nothing mid-game. Without
	 * them a miss queues nothing. */
	rdp_hash_set_init(&b->pending, PIPELINE_RECORD_LIST_MAX);
	if ((b->jobs = (rdp_pipeline_job_t *)calloc(max_misses, sizeof(*b->jobs))))
		for (i = 0; i < max_misses; i++)
			rdp_node_pool_release(&b->pool, &b->jobs[i].node);
	return true;
}

void rdp_pipeline_builder_free(rdp_pipeline_builder_t *b)
{
	/* What is skipped was asked for and never used; the next session's
	 * list or draws ask again. */
	retro_atomic_store_release_int(&b->stop, 1);
	rdp_worker_free(b->worker);
	free(b->ahead_jobs);
	free(b->jobs);
	rdp_hash_set_free(&b->pending);
	rdp_pipeline_builder_clear(b);
}

void rdp_pipeline_builder_miss(rdp_pipeline_builder_t *b, const granite_command_buffer_t *cb)
{
	/* (no job free: the next miss of this variant asks again) */
	rdp_pipeline_job_t *job = (rdp_pipeline_job_t *)rdp_node_pool_acquire(&b->pool);

	if (!job)
		return;
	granite_cb_extract_pipeline_state(cb, &job->compile);
	if (!rdp_hash_set_insert(&b->pending, job->compile.hash))
	{
		rdp_node_pool_release(&b->pool, &job->node);
		return;
	}
	job->kind = PIPELINE_RECORD_COMPUTE;
	job->ahead = 0;
	job->urgent = 0;
	rdp_worker_push(b->worker, &job->node);
}

/* rdp_pipeline_builder_ahead's list */
typedef struct known_list
{
	rdp_pipeline_builder_t *b;
	rdp_pipeline_job_t *jobs;
	size_t n, limit;
} known_list_t;

/* The pipeline @cb is set up for, unless it is built or listed already */
static void add_known(void *ctx, granite_command_buffer_t *cb, uint32_t kind)
{
	known_list_t *list = (known_list_t *)ctx;
	rdp_pipeline_job_t *job;
	size_t i;

	if (list->n >= list->limit)
		return;
	job = &list->jobs[list->n];
	granite_cb_extract_pipeline_state(cb, &job->compile);
	if (built(&job->compile))
		return;
	for (i = 0; i < list->n; i++)
		if (list->jobs[i].kind == kind && list->jobs[i].compile.hash == job->compile.hash)
			return;
	job->kind = kind;
	job->ahead = 1;
	job->urgent = 1;
	list->b->ahead_urgent++;
	list->n++;
}

static void set_plain(granite_command_buffer_t *cb, granite_program_t *program, unsigned constants, uint32_t value)
{
	unsigned i;
	granite_cb_set_program(cb, program);
	granite_cb_set_spec_constant_mask(cb, (1u << constants) - 1);
	for (i = 0; i < constants; i++)
		granite_cb_set_spec_constant(cb, i, value);
}

/* The compute pipelines every frame can need, whose settings all follow
 * from the renderer's own options: no draw has to build one, even on a
 * first run. The specialised rasterizer variants are not here: the
 * generic one covers them. Each uses the setter its draw uses, so the
 * two cannot differ. */
static void add_known_pipelines(known_list_t *list, const rdp_pipeline_setup_t *setup,
		rdp_vi_t *vi, bool with_upscaler)
{
	const rdp_renderer_caps_t *caps = setup->caps;
	const rdp_shader_bank_t *bank = setup->bank;
	const bool can_upscale = caps->upscaling > 1;
	granite_command_buffer_t *cb = granite_device_request_command_buffer(list->b->device, GRANITE_QUEUE_COMPUTE);
	unsigned upscaled, fmt, same_addr, stage, size_log2;

	if (cb)
	{
		set_plain(cb, bank->tmem_update, 1, WORKGROUP_SIZE);
		add_known(list, cb, PIPELINE_RECORD_COMPUTE);
		set_plain(cb, bank->clear_indirect_buffer, 1, WORKGROUP_SIZE);
		add_known(list, cb, PIPELINE_RECORD_COMPUTE);

		for (upscaled = 0; upscaled < (can_upscale ? 2u : 1u); upscaled++)
		{
			rdp_pipeline_span_setup(setup, cb, upscaled != 0);
			add_known(list, cb, PIPELINE_RECORD_COMPUTE);

			rdp_pipeline_tile_binning(setup, cb, upscaled != 0);
			add_known(list, cb, PIPELINE_RECORD_COMPUTE);
			rdp_pipeline_end_tile_binning(cb);

			if (!caps->ubershader)
			{
				rdp_pipeline_rasterizer(setup, cb, upscaled != 0);
				add_known(list, cb, PIPELINE_RECORD_COMPUTE);
			}

			for (fmt = 0; fmt < RDP_PIPELINE_FB_FORMATS; fmt++)
				for (same_addr = 0; same_addr < 2; same_addr++)
				{
					rdp_pipeline_depth_blend(setup, cb, fmt, same_addr != 0, upscaled != 0,
							upscaled != 0 && caps->super_sample_readback);
					add_known(list, cb, PIPELINE_RECORD_COMPUTE);
				}
		}

		if (can_upscale)
		{
			for (stage = RDP_RESOLVE_PRE; stage <= RDP_RESOLVE_SSAA; stage++)
				for (size_log2 = 0; size_log2 <= 2; size_log2++)
					for (same_addr = 0; same_addr < 2; same_addr++)
					{
						rdp_pipeline_upscaled_domain(setup, cb, (enum rdp_resolve_stage)stage, size_log2,
								same_addr != 0);
						add_known(list, cb, PIPELINE_RECORD_COMPUTE);
					}
			set_plain(cb, bank->clear_super_sampled_write_mask, 1, WORKGROUP_SIZE);
			add_known(list, cb, PIPELINE_RECORD_COMPUTE);
		}

		if (!setup->host_coherent)
		{
			set_plain(cb, bank->masked_rdram_resolve, 2, RDP_RDRAM_PAGE_SIZE / 4);
			add_known(list, cb, PIPELINE_RECORD_COMPUTE);
			set_plain(cb, bank->clear_write_mask, 2, RDP_RDRAM_PAGE_SIZE / 4);
			add_known(list, cb, PIPELINE_RECORD_COMPUTE);
		}

		granite_device_submit(list->b->device, cb, NULL, 0, NULL);
	}

	if (vi)
		rdp_vi_queue_known_pipelines(vi, caps->upscaling, with_upscaler, add_known, list);
}

void rdp_pipeline_builder_ahead(rdp_pipeline_builder_t *b, const rdp_pipeline_setup_t *setup,
		const pipeline_record_t *records, size_t count, rdp_vi_t *vi, bool with_upscaler)
{
	known_list_t list;
	size_t i;
	unsigned pass;

	if (!b->worker || b->ahead_jobs)
		return;
	if (!(b->ahead_jobs = (rdp_pipeline_job_t *)calloc(count + RDP_PIPELINE_MAX_KNOWN, sizeof(*b->ahead_jobs))))
		return;

	list.b = b;
	list.jobs = b->ahead_jobs;
	list.n = 0;
	for (i = 0; i < count; i++)
	{
		rdp_pipeline_job_t *job = &list.jobs[list.n];
		/* (what a record names is gone, or is not the same any more) */
		if (!granite_device_resolve_pipeline_record(b->device, &records[i], &job->compile) || built(&job->compile))
			continue;
		job->kind = records[i].kind;
		job->ahead = 1;
		job->urgent = 1;
		/* The rasterizer's specialised variants: a draw that misses one
		 * runs on the generic one meanwhile, and asks the thread for it,
		 * which finds it built by then (the thread takes its work in
		 * order). */
		if (job->kind == PIPELINE_RECORD_COMPUTE && !setup->caps->force_sync &&
		    job->compile.program == setup->bank->rasterizer &&
		    job->compile.potential_static_state.spec_constant_mask == 0xff)
			job->urgent = 0;
		b->ahead_urgent += job->urgent;
		list.n++;
	}
	list.limit = list.n + RDP_PIPELINE_MAX_KNOWN;
	/* tools/ci: frames do not depend on it, and a software driver takes
	 * most of a minute over these */
	if (!getenv("PARALLEL_RDP_NO_KNOWN_PIPELINES"))
		add_known_pipelines(&list, setup, vi, with_upscaler);
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < list.n; i++)
			if (list.jobs[i].urgent == (pass == 0))
				rdp_worker_push(b->worker, &list.jobs[i].node);
}

bool rdp_pipeline_builder_ready(rdp_pipeline_builder_t *b)
{
	return !b->worker || retro_atomic_load_acquire_int(&b->ahead_urgent_done) >= b->ahead_urgent;
}

static bool urgent_built(void *ctx)
{
	return rdp_pipeline_builder_ready((rdp_pipeline_builder_t *)ctx);
}

void rdp_pipeline_builder_wait(rdp_pipeline_builder_t *b)
{
	if (b->worker)
		rdp_worker_wait(b->worker, urgent_built, b);
}
