/* test_rdp_renderer -- parallel-RDP's renderer (parallel-rdp/rdp_renderer.c)
 * on a stand-in device, with the timeline worker.
 *
 * - init: no factor, or the super-sampled read-back at 1x, is refused;
 *   otherwise the caps, pipelines and buffers are made, the stream
 *   buffers of every slot mapped;
 * - nothing queued: a flush submits nothing;
 * - a fill rectangle into a 16-bit framebuffer: one submission, on the
 *   async compute queue, its fence waited for by the timeline worker
 *   (the submission in flight counted, then done), the queue empty after
 *   and the primitive counted among the frame's;
 * - another color image, or depth image, flushes what was queued under
 *   the old one; the same one does not;
 * - a command a real RDP would crash on is reported, with the error,
 *   while a reporter is set: fill on 4 bits, fill with depth test, image
 *   read or depth write (not at the primitive depth), copy on 32 bits, a
 *   TLUT load of more than one line; none without one;
 * - the constants: the convert factors as 2K + 1 of 9-bit signed K, the
 *   primitive depth in z units;
 * - with an RDRAM of its own (not the host's): the flush uploads the
 *   framebuffer's pages and the GPU's writes go back to the host through
 *   the worker;
 * - freed: every fence, buffer, view and pipeline went back.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_renderer.h"
#include "rdp_command_decode.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_renderer: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* (32 slots of stream buffers, and the rest, for each renderer made) */
#define MAX_BUFFERS 4096
#include "vk_test_device.h"

/* video_interface.c's: none here */
void rdp_vi_queue_known_pipelines(rdp_vi_t *vi, unsigned upscaling, bool with_upscaler,
		rdp_known_pipeline_fn add, void *ctx)
{
	(void)vi; (void)upscaling; (void)with_upscaler; (void)add; (void)ctx;
}

#define RDRAM_SIZE (1u << 20)
#define FB 0x10000u
#define ZB 0x40000u

static int no_defines(void *user, const char *name, const char *define)
{
	(void)user; (void)name; (void)define;
	return 0;
}

static rdp_shader_bank_t bank;
static rdp_timeline_t timeline;
static granite_buffer_t *rdram, *hidden, *tmem;
static uint8_t host_rdram[RDRAM_SIZE];
static uint64_t timeline_value;

static granite_buffer_t *buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	granite_buffer_info_t info;
	info.domain = GRANITE_BUFFER_DOMAIN_DEVICE;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | usage;
	info.misc = 0;
	return granite_device_create_buffer(&dev, &info, NULL);
}

static void open_device(void)
{
	fill_context();
	context.gpu_props.limits.maxStorageBufferRange = 0xffffffffu;
	context.ext.storage_16bit_features.storageBuffer16BitAccess = VK_TRUE;
	context.ext.storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
	context.ext.subgroup_properties.subgroupSize = 32;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
	rdp_shader_bank_init(&bank, &dev, no_defines, NULL);
	CHECK(rdp_timeline_init(&timeline, &dev), "the timeline worker starts");
	rdram = buffer(2 * RDRAM_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	hidden = buffer(RDRAM_SIZE / 2, 0);
	tmem = buffer(0x1000, 0);
}

static void close_device(void)
{
	unsigned i;
	rdp_timeline_free(&timeline);
	granite_buffer_unref(rdram);
	granite_buffer_unref(hidden);
	granite_buffer_unref(tmem);
	granite_device_free(&dev);
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);
	num_memory = 0;
}

/* Every submission before has been seen done by the worker */
static void worker_caught_up(void)
{
	rdp_timeline_op_t op;
	memset(&op, 0, sizeof(op));
	op.timeline_value = ++timeline_value;
	rdp_timeline_enqueue(&timeline, &op);
	rdp_timeline_wait(&timeline, timeline_value);
}

/* The slot the last flush read holds the fence of its submission, and
 * nothing else does any more */
static bool only_the_ring_holds(const rdp_renderer_t *r)
{
	const granite_fence_t *fence = r->sync_ring.fences[(r->sync_ring.current + RDP_SYNC_RING_SIZE - 1)
			% RDP_SYNC_RING_SIZE];
	return fence && retro_atomic_load_acquire_int((retro_atomic_int_t *)&fence->refs) == 1;
}

static bool open_renderer(rdp_renderer_t *r, bool coherent)
{
	rdp_renderer_options_t options;
	rdp_renderer_init(r, &timeline);
	rdp_renderer_set_device(r, &dev);
	rdp_renderer_set_shader_bank(r, &bank);
	CHECK(rdp_renderer_set_rdram(r, rdram, coherent ? NULL : host_rdram, 0, RDRAM_SIZE, coherent), "RDRAM");
	rdp_renderer_set_hidden_rdram(r, hidden);
	rdp_renderer_set_tmem(r, tmem);
	memset(&options, 0, sizeof(options));
	options.upscaling_factor = 1;
	return rdp_renderer_init_renderer(r, &options);
}

/* ---- the commands, as a game sends them ---- */
static void color_image(rdp_renderer_t *r, uint32_t addr, uint32_t width, unsigned fmt, unsigned size)
{
	uint32_t words[2], a, w, f;
	unsigned s;
	words[0] = 0x3f000000u | (fmt << 21) | (size << 19) | (width - 1);
	words[1] = addr;
	rdp_decode_color_image(words, &a, &w, &f, &s);
	rdp_renderer_set_color_framebuffer(r, a, w, f);
}

/* the whole of 320x240, and other modes of @cycle with @depth_word's
 * depth bits (word 1) */
static void modes(rdp_renderer_t *r, unsigned cycle, uint32_t depth_word)
{
	uint32_t scissor[2], other[2];
	rdp_scissor_state_t sc;
	rdp_static_state_t st;
	rdp_depth_blend_state_t db;
	bool prim_depth;

	memset(&st, 0, sizeof(st));
	memset(&db, 0, sizeof(db));
	other[0] = 0x2f000000u | (cycle << 20);
	other[1] = depth_word;
	prim_depth = rdp_decode_other_modes(other, &st, &db);
	scissor[0] = 0x2d000000u;
	scissor[1] = (320u << 2 << 12) | (240u << 2);
	rdp_decode_scissor(scissor, &sc, &st.flags);
	rdp_renderer_set_scissor_state(r, &sc);
	rdp_renderer_set_static_state(r, &st);
	rdp_renderer_set_depth_blend_state(r, &db);
	rdp_renderer_set_enable_primitive_depth(r, prim_depth);
}

static void fill_rectangle(rdp_renderer_t *r, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
	uint32_t words[2];
	rdp_triangle_setup_t setup;
	words[0] = 0x36000000u | (x1 << 2 << 12) | (y1 << 2);
	words[1] = (x0 << 2 << 12) | (y0 << 2);
	rdp_decode_fill_rectangle(words, RDP_DECODE_FILL_CYCLE, &setup);
	rdp_renderer_draw(r, &setup, NULL);
}

/* ---- the reports ---- */
static unsigned reports, last_error;

static void reported(void *user, enum rdp_validation_error err, const char *msg)
{
	(void)msg;
	CHECK(user == &reports, "the reporter's own pointer");
	reports++;
	last_error = (unsigned)err;
}

static void test_init(void)
{
	rdp_renderer_t r;
	rdp_renderer_options_t options;
	unsigned buffers_before = num_buffers;
	unsigned i;

	rdp_renderer_init(&r, &timeline);
	rdp_renderer_set_device(&r, &dev);
	rdp_renderer_set_shader_bank(&r, &bank);
	rdp_renderer_set_rdram(&r, rdram, NULL, 0, RDRAM_SIZE, true);
	rdp_renderer_set_hidden_rdram(&r, hidden);
	rdp_renderer_set_tmem(&r, tmem);
	memset(&options, 0, sizeof(options));
	CHECK(!rdp_renderer_init_renderer(&r, &options), "no upscaling factor is refused");
	options.upscaling_factor = 1;
	options.super_sampled_readback = true;
	CHECK(!rdp_renderer_init_renderer(&r, &options), "and the super-sampled read-back at 1x");
	CHECK(num_buffers == buffers_before, "before anything is made");
	options.super_sampled_readback = false;
	CHECK(rdp_renderer_init_renderer(&r, &options), "1x is made");
	CHECK(rdp_renderer_scaling_factor(&r) == 1 && !rdp_renderer_upscaled_rdram(&r), "at 1x, no upscaled RDRAM");
	CHECK(r.queue.triangles && r.queue.attributes && r.queue.derived && r.queue.scissors && r.queue.indices
			&& r.queue.span_offsets && r.queue.span_jobs, "the first slot's streams are mapped for the queue");
	for (i = 0; i < RDP_SYNC_RING_SIZE; i++)
		CHECK(r.buffer_instances[i].cpu.buffers[RDP_STREAM_SPAN_INFO_JOBS]
				&& r.buffer_instances[i].cpu.buffers[RDP_STREAM_SPAN_INFO_JOBS]->info.size
				== sizeof(rdp_span_job_t) * RDP_QUEUE_MAX_SPAN_SETUPS, "every slot's streams, sized for a flush");
	CHECK(r.buffers.span_setups && r.buffers.span_setups->info.size
			== (VkDeviceSize)RDP_QUEUE_MAX_SPAN_SETUPS * RDP_RENDERER_SPAN_SETUP_SIZE, "the span setups");
	CHECK(r.buffers.tmem_instances && r.buffers.tmem_instances->info.size == RDP_QUEUE_MAX_UPLOADS * 0x1000,
			"a TMEM for each load");
	rdp_renderer_free(&r);
}

static void test_flush(void)
{
	rdp_renderer_t r;
	unsigned submits;

	CHECK(open_renderer(&r, true), "made");

	submits = submits_made;
	rdp_renderer_flush_and_signal(&r);
	CHECK(submits_made == submits && !r.cmd, "with nothing queued a flush submits nothing");

	color_image(&r, FB, 320, 0, 2);
	rdp_renderer_set_depth_framebuffer(&r, ZB);
	modes(&r, 3, 0);
	fill_rectangle(&r, 0, 0, 100, 100);
	CHECK(r.queue.num_primitives == 1 && r.submits.primitives == 1 && submits_made == submits,
			"a fill rectangle is queued");
	/* (the worker held in its wait for the fence) */
	retro_atomic_store_release_int(&hold_semaphore_waits, 1);
	rdp_renderer_flush_and_signal(&r);
	CHECK(submits_made == submits + 1 && !r.cmd, "the flush is one submission");
	CHECK(r.queue.num_primitives == 0 && r.base_primitive_index == 1, "the queue is empty after, the primitive counted");
	CHECK(retro_atomic_load_acquire_int(&r.active_submissions) == 1, "the submission is in flight");
	retro_atomic_store_release_int(&hold_semaphore_waits, 0);
	worker_caught_up();
	CHECK(retro_atomic_load_acquire_int(&r.active_submissions) == 0,
			"its fence was waited for and the submission counted done");
	CHECK(only_the_ring_holds(&r), "and only the slot it read holds its fence");

	/* another color image flushes what was queued under the old one */
	submits = submits_made;
	fill_rectangle(&r, 0, 0, 10, 10);
	color_image(&r, FB, 320, 0, 2);
	CHECK(r.queue.num_primitives == 1, "the same color image does not flush");
	color_image(&r, FB + 0x20000, 320, 0, 2);
	CHECK(r.queue.num_primitives == 0 && r.base_primitive_index == 2, "another color image flushes");
	fill_rectangle(&r, 0, 0, 10, 10);
	color_image(&r, FB + 0x20000, 160, 0, 2);
	CHECK(r.queue.num_primitives == 0, "and another width");
	fill_rectangle(&r, 0, 0, 10, 10);
	color_image(&r, FB + 0x20000, 160, 0, 3);
	CHECK(r.queue.num_primitives == 0, "and another format");
	fill_rectangle(&r, 0, 0, 10, 10);
	rdp_renderer_set_depth_framebuffer(&r, ZB);
	CHECK(r.queue.num_primitives == 1, "the same depth image does not");
	rdp_renderer_set_depth_framebuffer(&r, ZB + 0x20000);
	CHECK(r.queue.num_primitives == 0, "another depth image does");
	rdp_renderer_flush_and_signal(&r);
	worker_caught_up();
	CHECK(retro_atomic_load_acquire_int(&r.active_submissions) == 0 && !r.cmd, "all of it done");

	rdp_renderer_free(&r);
}

static void test_validation(void)
{
	rdp_renderer_t r;
	rdp_load_tile_info_t load;

	CHECK(open_renderer(&r, true), "made");
	color_image(&r, FB, 320, 4, 0);
	modes(&r, 3, 0);
	reports = 0;
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 0, "nothing reported without a reporter");

	rdp_renderer_set_validation(&r, reported, &reports);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 1 && last_error == RDP_VALIDATION_FILL_4BPP, "fill on a 4-bit color image");
	color_image(&r, FB, 320, 0, 2);
	reports = 0;
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 0, "fill on 16 bits is fine");

	modes(&r, 3, 1u << 4);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 1 && last_error == RDP_VALIDATION_FILL_DEPTH_TEST, "fill with depth test");
	reports = 0;
	modes(&r, 3, 1u << 6);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 1 && last_error == RDP_VALIDATION_FILL_IMAGE_READ_ENABLE, "fill with image read");
	reports = 0;
	modes(&r, 3, 1u << 5);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 1 && last_error == RDP_VALIDATION_FILL_DEPTH_WRITE, "fill with depth write");
	reports = 0;
	modes(&r, 3, (1u << 5) | (1u << 2));
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 0, "unless at the primitive depth");

	color_image(&r, FB, 320, 0, 3);
	modes(&r, 2, 0);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 1 && last_error == RDP_VALIDATION_COPY_32BPP, "copy on 32 bits");
	reports = 0;
	color_image(&r, FB, 320, 0, 2);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 0, "copy on 16 bits is fine");

	memset(&load, 0, sizeof(load));
	load.tex_addr = 0x80000;
	load.tex_width = 16;
	load.mode = RDP_UPLOAD_TLUT;
	load.fmt = 0;
	load.size = RDP_TEXTURE_SIZE_16;
	load.shi = 15 << 2;
	load.tlo = 0;
	load.thi = 1 << 2;
	rdp_renderer_load_tile(&r, 0, &load);
	CHECK(reports >= 1 && last_error == RDP_VALIDATION_INVALID_MULTILINE_LOAD_TLUT, "a TLUT load of two lines");
	reports = 0;
	load.thi = 3;
	rdp_renderer_load_tile(&r, 0, &load);
	CHECK(reports == 0, "one line is fine");

	rdp_renderer_set_validation(&r, NULL, NULL);
	color_image(&r, FB, 320, 4, 0);
	modes(&r, 3, 0);
	fill_rectangle(&r, 0, 0, 10, 10);
	CHECK(reports == 0, "nothing once the reporter is gone");

	rdp_renderer_flush_and_signal(&r);
	worker_caught_up();
	rdp_renderer_free(&r);
}

static void test_constants(void)
{
	rdp_renderer_t r;
	rdp_renderer_init(&r, &timeline);
	rdp_renderer_set_convert(&r, 0x1ff, 0x100, 0x0ff, 0, 0x1ab, 0x0cd);
	CHECK(r.constants.convert[0] == (uint16_t)-1, "K = -1: 2K + 1 = -1");
	CHECK(r.constants.convert[1] == (uint16_t)-511, "K = -256: -511");
	CHECK(r.constants.convert[2] == 511, "K = 255: 511");
	CHECK(r.constants.convert[3] == 1, "K = 0: 1");
	CHECK(r.constants.convert[4] == 0x1ab && r.constants.convert[5] == 0x0cd, "K4 and K5 as they are");
	rdp_renderer_set_convert(&r, 0xfe01, 0, 0, 0, 0, 0);
	CHECK(r.constants.convert[0] == 3, "the bits past 9 are not K's");
	rdp_renderer_set_primitive_depth(&r, 0x8001, 0x1234);
	CHECK(r.constants.prim_depth == 0x10000 && r.constants.prim_dz == 0x1234, "the primitive depth in z units");
	rdp_renderer_set_color_key(&r, 2, 0xabc, 0x12, 0x34);
	CHECK(r.constants.key_width[2] == 0xabc && r.constants.key_center[2] == 0x12 && r.constants.key_scale[2] == 0x34,
			"the color key");
	rdp_renderer_set_primitive_color(&r, 3, 0x80, 0x11223344);
	CHECK(r.constants.min_level == 3 && r.constants.prim_lod_frac == 0x80 && r.constants.primitive_color == 0x11223344,
			"the primitive color");
	CHECK(r.tiles[5].meta.size == RDP_TEXTURE_SIZE_16, "a tile not set is 16 bits");
	{
		rdp_tile_meta_t meta;
		memset(&meta, 0, sizeof(meta));
		meta.size = RDP_TEXTURE_SIZE_32;
		meta.offset = 0x100;
		rdp_renderer_set_tile(&r, 13, &meta);
		CHECK(r.tiles[5].meta.size == RDP_TEXTURE_SIZE_32 && r.tiles[5].meta.offset == 0x100, "set tile, 3 bits");
	}
	rdp_renderer_set_tile_size(&r, 13, 1, 2, 3, 4);
	CHECK(r.tiles[5].size.slo == 1 && r.tiles[5].size.thi == 4, "the tile index is 3 bits");
	rdp_renderer_free(&r);
}

static void test_incoherent(void)
{
	rdp_renderer_t r;
	const uint32_t page = FB / RDP_RDRAM_PAGE_SIZE;
	uint8_t *gpu;
	unsigned submits, i;

	CHECK(open_renderer(&r, false), "made with an RDRAM of its own");
	CHECK(!r.is_host_coherent && r.incoherent.host_rdram == host_rdram, "kept in step with the host's");
	gpu = (uint8_t *)granite_device_map_host_buffer(&dev, rdram, GRANITE_MEMORY_ACCESS_READ_BIT, 0, 2 * RDRAM_SIZE);

	for (i = 0; i < 320 * 100 * 2; i++)
		host_rdram[FB + i] = (uint8_t)(i * 7 + 1);
	color_image(&r, FB, 320, 0, 2);
	rdp_renderer_set_depth_framebuffer(&r, ZB);
	modes(&r, 3, 0);
	fill_rectangle(&r, 0, 0, 100, 99);

	/* (the worker held in its wait for the fence) */
	retro_atomic_store_release_int(&hold_semaphore_waits, 1);
	submits = submits_made;
	rdp_renderer_flush_and_signal(&r);
	CHECK(submits_made == submits + 1 && !r.cmd, "the flush is one submission");
	CHECK(!memcmp(gpu + FB, host_rdram + FB, 320 * 100 * 2), "the framebuffer's pages went to the GPU first");
	CHECK(retro_atomic_load_acquire_int(&r.incoherent.pending_writes_for_page[page]) == 1
			&& retro_atomic_load_acquire_int(&r.incoherent.pending_writes_for_page[ZB / RDP_RDRAM_PAGE_SIZE]) == 1,
			"the GPU's writes to the color and depth images are to be copied back");
	CHECK(retro_atomic_load_acquire_int(&r.active_submissions) == 1, "the submission is in flight");
	retro_atomic_store_release_int(&hold_semaphore_waits, 0);
	worker_caught_up();
	CHECK(retro_atomic_load_acquire_int(&r.incoherent.pending_writes_for_page[page]) == 0
			&& retro_atomic_load_acquire_int(&r.active_submissions) == 0,
			"once its fence is signalled the worker copied them back and saw it done");
	CHECK(only_the_ring_holds(&r), "and only the slot it read holds its fence");

	/* the VI's range, uploaded on its own */
	for (i = 0; i < 4096; i++)
		host_rdram[FB + 0x30000 + i] = (uint8_t)(i * 3 + 5);
	rdp_renderer_resolve_coherency_external(&r, FB + 0x30000, 4096);
	CHECK(!r.cmd && !memcmp(gpu + FB + 0x30000, host_rdram + FB + 0x30000, 4096),
			"the scanout's range is uploaded and submitted on its own");
	granite_device_unmap_host_buffer(&dev, rdram, GRANITE_MEMORY_ACCESS_READ_BIT, 0, 2 * RDRAM_SIZE);
	worker_caught_up();
	rdp_renderer_free(&r);
}

int main(void)
{
	unsetenv("PARALLEL_RDP_UBERSHADER");
	setenv("PARALLEL_RDP_NO_KNOWN_PIPELINES", "1", 1);
	open_device();
	test_init();
	test_flush();
	test_validation();
	test_constants();
	test_incoherent();

	/* the same through the ubershader */
	setenv("PARALLEL_RDP_UBERSHADER", "1", 1);
	test_flush();
	test_incoherent();
	close_device();
	CHECK(unmade == made + num_pipelines, "every fence, buffer, view and pipeline went back");
	if (unmade != made + num_pipelines)
		fprintf(stderr, "made %u, unmade %u\n", made, unmade);

	if (failures)
	{
		fprintf(stderr, "test_rdp_renderer: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_renderer: ok\n");
	return 0;
}
