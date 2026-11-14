/* parallel-rdp's compute pipelines (parallel-rdp/rdp_pipelines.c), on a
 * stand-in device.
 *
 * Built ahead, at load, are exactly the pipelines every frame can need,
 * each once: TMEM updates, clearing the indirect buffer, span setup, tile
 * binning, the generic rasterizer (not for the ubershader) and depth
 * blending for each framebuffer format, with or without its depth at the
 * colour's address; upscaled, those again at the upscaled size, the
 * updates of the upscaled domain and clearing the super-sampled write
 * mask; with RDRAM the CPU does not share, the masked resolve and clearing
 * its write mask. Each with the specialization constants its draw sets.
 *
 * A draw that misses a pipeline queues its variant once, and only while
 * a job for it is free. Content close builds nothing still queued. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_pipelines.h"
#include "pipeline_map.h"
#include "rdram_coherency.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_pipelines: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

#include "vk_test_device.h"

static rdp_shader_bank_t bank;

/* The scanout's pipelines are video_interface.c's: none here */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx)
{
	(void)vi; (void)upscaling; (void)with_upscaler; (void)add; (void)ctx;
}

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

/* ---- what should be built ahead ---- */
typedef struct want
{
	granite_program_t *program;
	uint32_t mask;
	/* the constants checked: the rest are not the shader's */
	uint32_t checked;
	uint32_t spec[8];
	unsigned subgroup_control;
	unsigned found;
} want_t;

#define MAX_WANT 128
static want_t wants[MAX_WANT];
static unsigned num_wants;

static want_t *want(granite_program_t *program, uint32_t mask)
{
	want_t *w = &wants[num_wants++];
	memset(w, 0, sizeof(*w));
	w->program = program;
	w->mask = mask;
	w->checked = mask;
	return w;
}

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

/* The list rdp_renderer.cpp built ahead before it was C, worked out
 * again from its setters */
static void expect(const rdp_pipeline_setup_t *s)
{
	const rdp_renderer_caps_t *caps = s->caps;
	const unsigned up = caps->upscaling;
	unsigned u, fmt, same, stage, size;
	want_t *w;

	num_wants = 0;
	w = want(bank.tmem_update, 1);
	w->spec[0] = 64;
	w = want(bank.clear_indirect_buffer, 1);
	w->spec[0] = 64;
	for (u = 0; u < (up > 1 ? 2u : 1u); u++)
	{
		w = want(bank.span_setup, 3);
		w->spec[0] = (u ? up : 1) * 64;
		w->spec[1] = u ? log2_of(up) : 0;

		w = want(bank.tile_binning_combined, 0x7f);
		w->spec[0] = caps->subgroup_tile_binning ? s->subgroup_size : 32;
		w->spec[1] = 8;
		w->spec[2] = 8;
		w->spec[3] = 256;
		w->spec[4] = u ? caps->max_width : 1024;
		w->spec[5] = caps->max_num_tile_instances;
		w->spec[6] = u ? up : 1;
		w->subgroup_control = caps->subgroup_tile_binning;

		if (!caps->ubershader)
		{
			w = want(bank.rasterizer, 7);
			w->spec[0] = 8;
			w->spec[1] = 8;
			w->spec[2] = (u ? log2_of(up) : 0) << 26;
		}

		for (fmt = 0; fmt < 5; fmt++)
			for (same = 0; same < 2; same++)
			{
				w = want(caps->ubershader ? bank.ubershader : bank.depth_blend, 0xff);
				w->spec[0] = s->rdram_size;
				w->spec[1] = fmt;
				w->spec[2] = same;
				w->spec[3] = 8;
				w->spec[4] = 8;
				w->spec[5] = 256;
				w->spec[6] = u ? caps->max_width : 1024;
				w->spec[7] = (uint32_t)((u && caps->super_sample_readback) || (!s->host_coherent && !u)) |
						((u ? log2_of(up) : 0) << 1);
			}
	}
	if (up > 1)
	{
		granite_program_t *programs[3];
		programs[0] = bank.update_upscaled_domain_pre;
		programs[1] = bank.update_upscaled_domain_post;
		programs[2] = bank.update_upscaled_domain_resolve;
		for (stage = 0; stage < 3; stage++)
			for (size = 0; size <= 2; size++)
				for (same = 0; same < 2; same++)
				{
					w = want(programs[stage], 0x7f);
					w->spec[0] = s->rdram_size;
					w->spec[1] = size;
					w->spec[2] = same;
					w->spec[3] = 64;
					w->spec[4] = up * up;
					if (stage == 2)
					{
						w->spec[5] = caps->super_sample_readback_dither;
						w->spec[6] = !s->host_coherent;
					}
					else
						w->checked = 0x1f;
				}
		w = want(bank.clear_super_sampled_write_mask, 1);
		w->spec[0] = 64;
	}
	if (!s->host_coherent)
	{
		w = want(bank.masked_rdram_resolve, 3);
		w->spec[0] = w->spec[1] = RDP_RDRAM_PAGE_SIZE / 4;
		w = want(bank.clear_write_mask, 3);
		w->spec[0] = w->spec[1] = RDP_RDRAM_PAGE_SIZE / 4;
	}
}

static bool matches(const want_t *w, const granite_pipeline_compile_t *c)
{
	unsigned i;
	if (c->program != w->program || c->potential_static_state.spec_constant_mask != w->mask ||
	    c->static_state.state.subgroup_control_size != w->subgroup_control)
		return false;
	for (i = 0; i < 8; i++)
		if ((w->checked & (1u << i)) && c->potential_static_state.spec_constants[i] != w->spec[i])
			return false;
	return true;
}

static bool built(const granite_pipeline_compile_t *c)
{
	return granite_pipeline_map_find(&c->program->pipelines, c->hash).pipeline != VK_NULL_HANDLE;
}

static void open_device(void)
{
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);
}

/* A device of its own: none of its pipelines built yet */
static void test_ahead(const char *what, rdp_pipeline_setup_t *s)
{
	rdp_pipeline_builder_t b;
	unsigned before = num_pipelines;
	int i;
	unsigned j;

	open_device();
	s->bank = &bank;
	expect(s);
	rdp_pipeline_builder_clear(&b);
	CHECK(rdp_pipeline_builder_init(&b, &dev, 4), what);
	rdp_pipeline_builder_ahead(&b, s, NULL, 0, NULL, false);
	rdp_pipeline_builder_wait(&b);
	CHECK(rdp_pipeline_builder_ready(&b), what);

	CHECK(b.ahead_urgent == (int)num_wants, what);
	for (i = 0; i < b.ahead_urgent; i++)
	{
		const rdp_pipeline_job_t *job = &b.ahead_jobs[i];
		unsigned hits = 0;
		for (j = 0; j < num_wants; j++)
			if (matches(&wants[j], &job->compile))
			{
				wants[j].found++;
				hits++;
			}
		CHECK(hits == 1, what);
		CHECK(job->kind == PIPELINE_RECORD_COMPUTE && job->urgent && job->ahead, what);
		CHECK(built(&job->compile), what);
	}
	for (j = 0; j < num_wants; j++)
		CHECK(wants[j].found == 1, what);
	CHECK(num_pipelines - before == num_wants, what);

	/* a second time: nothing */
	rdp_pipeline_builder_ahead(&b, s, NULL, 0, NULL, false);
	CHECK(b.ahead_urgent == (int)num_wants, what);
	rdp_pipeline_builder_free(&b);

	/* built already: nothing queued */
	rdp_pipeline_builder_init(&b, &dev, 4);
	rdp_pipeline_builder_ahead(&b, s, NULL, 0, NULL, false);
	CHECK(b.ahead_urgent == 0 && rdp_pipeline_builder_ready(&b), what);
	rdp_pipeline_builder_free(&b);
	granite_device_free(&dev);
}

/* ---- a draw's miss ---- */
static granite_pipeline_compile_t missed;

static bool missed_built(void *ctx)
{
	(void)ctx;
	return built(&missed);
}

static void specialised(granite_command_buffer_t *cb, uint32_t combiner)
{
	unsigned i;
	granite_cb_set_program(cb, bank.rasterizer);
	granite_cb_set_spec_constant_mask(cb, 0xff);
	for (i = 0; i < 8; i++)
		granite_cb_set_spec_constant(cb, i, combiner + i);
}

static void test_miss(void)
{
	rdp_pipeline_builder_t b;
	granite_command_buffer_t *cb = granite_device_request_command_buffer(&dev, GRANITE_QUEUE_COMPUTE);

	rdp_pipeline_builder_clear(&b);
	CHECK(rdp_pipeline_builder_init(&b, &dev, 2), "a builder");
	specialised(cb, 1000);
	granite_cb_extract_pipeline_state(cb, &missed);
	CHECK(!built(&missed), "not built yet");
	rdp_pipeline_builder_miss(&b, cb);
	rdp_pipeline_builder_miss(&b, cb);
	CHECK(b.pending.count == 1, "queued once");
	rdp_worker_wait(b.worker, missed_built, NULL);
	CHECK(built(&missed), "built on the thread");
	rdp_pipeline_builder_miss(&b, cb);
	CHECK(b.pending.count == 1, "not again");
	specialised(cb, 2000);
	rdp_pipeline_builder_miss(&b, cb);
	CHECK(b.pending.count == 2, "another variant");
	granite_cb_extract_pipeline_state(cb, &missed);
	rdp_worker_wait(b.worker, missed_built, NULL);
	CHECK(rdp_pipeline_builder_ready(&b), "no urgent ones");
	rdp_pipeline_builder_free(&b);

	/* no job free: nothing queued, nothing remembered */
	rdp_pipeline_builder_init(&b, &dev, 0);
	specialised(cb, 3000);
	rdp_pipeline_builder_miss(&b, cb);
	CHECK(b.pending.count == 0, "no job");
	rdp_pipeline_builder_free(&b);

	/* closing builds none of what is queued */
	{
		unsigned before;
		rdp_pipeline_builder_init(&b, &dev, 8);
		retro_atomic_store_release_int(&b.stop, 1);
		before = num_pipelines;
		specialised(cb, 4000);
		rdp_pipeline_builder_miss(&b, cb);
		rdp_pipeline_builder_free(&b);
		CHECK(num_pipelines == before, "none past close");
	}

	granite_device_submit(&dev, cb, NULL, 0, NULL);
}

int main(void)
{
	rdp_renderer_caps_t caps;
	rdp_pipeline_setup_t setup;

	unsetenv("PARALLEL_RDP_NO_KNOWN_PIPELINES");
	fill_context();
	context.ext.subgroup_size_control_features.subgroupSizeControl = VK_TRUE;
	context.ext.subgroup_size_control_features.computeFullSubgroups = VK_TRUE;
	context.ext.subgroup_size_control_properties.minSubgroupSize = 32;
	context.ext.subgroup_size_control_properties.maxSubgroupSize = 32;
	setup.caps = &caps;
	setup.rdram_size = 8 << 20;
	setup.subgroup_size = 32;

	/* native, RDRAM not shared with the CPU */
	rdp_caps_init(&caps);
	rdp_caps_init_limits(&caps, 1, false, 0xffffffffu);
	setup.host_coherent = false;
	test_ahead("1x", &setup);

	/* 4x, super-sampled read-back and dither, subgroup tile binning */
	rdp_caps_init(&caps);
	rdp_caps_init_limits(&caps, 4, false, 0xffffffffu);
	caps.upscaling = 4;
	caps.super_sample_readback = true;
	caps.super_sample_readback_dither = true;
	caps.subgroup_tile_binning = true;
	setup.host_coherent = true;
	setup.rdram_size = 4 << 20;
	test_ahead("4x", &setup);

	/* the ubershader */
	rdp_caps_init(&caps);
	rdp_caps_init_limits(&caps, 2, false, 0xffffffffu);
	caps.upscaling = 2;
	caps.ubershader = true;
	setup.host_coherent = false;
	test_ahead("2x ubershader", &setup);

	open_device();
	test_miss();
	granite_device_free(&dev);
	CHECK(unmade == made + num_pipelines, "everything made went back");

	if (failures)
	{
		fprintf(stderr, "test_rdp_pipelines: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_pipelines: all checks passed\n");
	return 0;
}
