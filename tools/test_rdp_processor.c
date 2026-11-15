/* test_rdp_processor -- parallel-RDP's command processor
 * (parallel-rdp/rdp_processor.c) on a stand-in device, with its command
 * thread, or without one (PARALLEL_RDP_SINGLE_THREADED_COMMAND).
 *
 * - made: RDRAM of its own, or the host's kept in step where it cannot be
 *   imported (the processor's offset then 0, the host's pointer past
 *   it); hidden RDRAM cleared to full coverage, 8 or 16 bits an entry;
 *   TMEM cleared; supported;
 * - the game's commands: those of the processor's own opcodes never run
 *   (a call among them is not made), the RDP's reach the commands;
 * - the processor's: a call runs where the commands run and returns once
 *   it has; quirks reach the commands; timeline values are signalled in
 *   order and waited for; a flush submits what is queued;
 * - the scanout: nothing to show without a framebuffer, the VI's image
 *   with one (a reference for the caller), no upscaler frame without the
 *   upscaler; with RDRAM of its own the VI's range is uploaded first;
 * - freed: every buffer, view, image and pipeline went back.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp_processor.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_rdp_processor: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* (32 slots of stream buffers, and the rest, for each processor made) */
#define MAX_BUFFERS 4096
#include "vk_test_device.h"

#define RDRAM_SIZE (4u << 20)
#define HIDDEN_SIZE (RDRAM_SIZE / 2)

static uint8_t host_rdram[RDRAM_SIZE + 0x1000];

static void open_device(void)
{
	fill_context();
	context.gpu_props.limits.maxStorageBufferRange = 0xffffffffu;
	context.ext.storage_16bit_features.storageBuffer16BitAccess = VK_TRUE;
	context.ext.storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
	context.ext.subgroup_properties.subgroupSize = 32;
	granite_device_init(&dev);
	granite_device_set_context(&dev, &context);
}

static void close_device(void)
{
	unsigned i;
	granite_device_free(&dev);
	for (i = 0; i < num_memory; i++)
		free(memory_data[i]);
	num_memory = 0;
}

static bool all_bytes(rdp_processor_t *p, const granite_buffer_t *b, uint8_t value)
{
	const uint8_t *data = (const uint8_t *)granite_device_map_host_buffer(p->device, b, GRANITE_MEMORY_ACCESS_READ_BIT,
			0, b->info.size);
	VkDeviceSize i;
	for (i = 0; i < b->info.size; i++)
		if (data[i] != value)
			return false;
	return true;
}

static bool hidden_wide(rdp_processor_t *p)
{
	const uint16_t *data = (const uint16_t *)granite_device_map_host_buffer(p->device, p->hidden_rdram,
			GRANITE_MEMORY_ACCESS_READ_BIT, 0, p->hidden_rdram->info.size);
	VkDeviceSize i;
	for (i = 0; i < p->hidden_rdram->info.size / 2; i++)
		if (data[i] != 0x0003)
			return false;
	return true;
}

/* ---- the calls ---- */
static unsigned calls;
static void *called_with;

static void call(void *ctx)
{
	calls++;
	called_with = ctx;
}

/* A call as the processor's own command would carry it */
static void call_words(uint32_t words[5], void (*fn)(void *), void *ctx)
{
	uintptr_t f;
	const uint64_t c = (uint64_t)(uintptr_t)ctx;
	memcpy(&f, &fn, sizeof(f));
	words[0] = (uint32_t)RDP_PROCESSOR_CALL << 24;
	words[1] = (uint32_t)(uint64_t)f;
	words[2] = (uint32_t)((uint64_t)f >> 32);
	words[3] = (uint32_t)c;
	words[4] = (uint32_t)(c >> 32);
}

static void run2(rdp_processor_t *p, uint32_t w0, uint32_t w1)
{
	uint32_t words[2];
	words[0] = w0;
	words[1] = w1;
	rdp_processor_enqueue(p, 2, words);
}

static uint32_t pair(uint32_t hi, uint32_t lo)
{
	return (hi << 16) | lo;
}

static void ntsc_320x240(rdp_processor_t *p, uint32_t origin)
{
	unsigned i;
	for (i = 0; i < RDP_VI_REGISTER_COUNT; i++)
		rdp_processor_set_vi_register(p, (enum rdp_vi_register)i, 0);
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_CONTROL,
			VI_CONTROL_TYPE_RGBA5551_BIT | VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT);
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_ORIGIN, origin);
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_WIDTH, 320);
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_V_SYNC, VI_V_SYNC_NTSC);
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_H_START, pair(VI_H_OFFSET_NTSC, VI_H_OFFSET_NTSC + 640));
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_V_START, pair(VI_V_OFFSET_NTSC, VI_V_OFFSET_NTSC + 480));
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_X_SCALE, pair(0, 512));
	rdp_processor_set_vi_register(p, RDP_VI_REGISTER_Y_SCALE, pair(0, 1024));
}

static void test_processor(void *rdram_ptr, size_t offset)
{
	static rdp_processor_t p;
	rdp_vi_scanout_options_t options;
	rdp_upscaler_targets_t frame;
	uint32_t words[5];
	granite_image_t *image;
	uint64_t v1, v2;
	unsigned submits;
	int x;

	rdp_processor_init(&p, &dev, rdram_ptr, offset, RDRAM_SIZE, HIDDEN_SIZE, 0);
	CHECK(rdp_processor_supported(&p) && rdp_processor_scaling_factor(&p) == 1, "made, at 1x");
	CHECK(p.single_threaded == (getenv("PARALLEL_RDP_SINGLE_THREADED_COMMAND") != NULL) && (p.ring != NULL) == !p.single_threaded,
			"with a command thread unless asked not to");
	if (!rdram_ptr)
		CHECK(p.is_host_coherent && p.rdram && p.rdram->info.size == RDRAM_SIZE && p.rdram_offset == offset,
				"RDRAM of its own");
	else
		CHECK(!p.is_host_coherent && p.rdram && p.rdram->info.size == 2 * RDRAM_SIZE && p.rdram_offset == 0
				&& p.host_rdram == (uint8_t *)rdram_ptr + offset,
				"the host's RDRAM, not importable, kept in step: the GPU's twice the size, for the write mask");
	CHECK(p.hidden_rdram && p.hidden_rdram->info.size == HIDDEN_SIZE * (p.no_storage_8bit ? 2 : 1),
			"hidden RDRAM, 8 bits an entry, 16 without 8-bit storage");
	CHECK(p.no_storage_8bit ? hidden_wide(&p) : all_bytes(&p, p.hidden_rdram, 0x03),
			"cleared to full coverage, 0x0003 an entry when 16 bits");
	CHECK(p.tmem && p.tmem->info.size == 0x1000 && all_bytes(&p, p.tmem, 0), "TMEM, cleared");

	/* the game's commands: none of the processor's own */
	calls = 0;
	call_words(words, call, &x);
	rdp_processor_enqueue(&p, 5, words);
	for (x = 1; x <= 7; x++)
		run2(&p, (uint32_t)x << 24, 0);
	rdp_processor_idle(&p);
	CHECK(calls == 0, "a call among the game's commands is never made, nor the rest of the processor's own");
	rdp_processor_run_on_command_thread(&p, call, &x);
	CHECK(calls == 1 && called_with == &x, "the processor's own call runs where the commands run, before it returns");

	/* quirks */
	rdp_processor_set_quirks(&p, RDP_DECODE_NATIVE_TEXTURE_LOD);
	rdp_processor_run_on_command_thread(&p, call, NULL);
	CHECK(p.commands.quirks == RDP_DECODE_NATIVE_TEXTURE_LOD, "quirks reach the commands");

	/* the RDP's commands, timeline values, flushes */
	run2(&p, 0x3f000000u | (2u << 19) | 319, 0x100000u);
	run2(&p, 0x2d000000u, (320u << 2 << 12) | (240u << 2));
	run2(&p, 0x2f000000u | (3u << 20), 0);
	run2(&p, 0x36000000u | (100u << 2 << 12) | (100u << 2), 0);
	rdp_processor_run_on_command_thread(&p, call, NULL);
	CHECK(p.renderer.fb.addr == 0x100000 && p.renderer.queue.num_primitives == 1, "the RDP's commands are run");
	submits = submits_made;
	v1 = rdp_processor_signal_timeline(&p);
	v2 = rdp_processor_signal_timeline(&p);
	rdp_processor_wait_for_timeline(&p, v2);
	CHECK(v2 == v1 + 1 && p.renderer.queue.num_primitives == 0 && submits_made > submits,
			"a timeline value flushes and submits what is queued, and is waited for");

	/* the scanout */
	rdp_vi_scanout_options_init(&options);
	image = rdp_processor_scanout(&p, &options, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(!image, "nothing to show without a framebuffer");
	ntsc_320x240(&p, 0x100002);
	if (rdram_ptr)
		memset(host_rdram + offset + 0x100000, 0x5a, 320 * 240 * 2);
	image = rdp_processor_scanout(&p, &options, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image != NULL && !p.scanout_result, "with one, the VI's image, the caller's");
	if (image)
		granite_image_unref(image);
	if (rdram_ptr)
	{
		const uint8_t *gpu = (const uint8_t *)granite_device_map_host_buffer(&dev, p.rdram,
				GRANITE_MEMORY_ACCESS_READ_BIT, 0, p.rdram->info.size);
		CHECK(gpu[0x100000] == 0x5a && gpu[0x100000 + 320 * 240 * 2 - 1] == 0x5a,
				"the VI's range of the host's RDRAM went to the GPU first");
	}
	rdp_processor_upscaler_frame(&p, &frame);
	CHECK(!frame.motion_vectors && !frame.depth, "no upscaler frame without the upscaler");

	/* with it: the frame's motion vectors and depth, the caller's too */
	options.upscaler.enable = true;
	options.upscaler.jitter_phase = 8;
	image = rdp_processor_scanout(&p, &options, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	CHECK(image != NULL, "the upscaler's scanout");
	if (image)
		granite_image_unref(image);
	rdp_processor_upscaler_frame(&p, &frame);
	CHECK(frame.motion_vectors && frame.depth && p.upscaler_frame.motion_vectors == frame.motion_vectors,
			"its motion vectors and depth");
	if (frame.motion_vectors)
		granite_image_unref(frame.motion_vectors);
	if (frame.depth)
		granite_image_unref(frame.depth);
	if (frame.translucent)
		granite_image_unref(frame.translucent);
	CHECK(p.upscaler_frame.motion_vectors && p.upscaler_frame.depth
			&& retro_atomic_load_acquire_int(&p.upscaler_frame.motion_vectors->refs) >= 1
			&& retro_atomic_load_acquire_int(&p.upscaler_frame.depth->refs) >= 1,
			"the caller's references were its own");
	rdp_processor_upscaler_frame(&p, &frame);
	CHECK(frame.motion_vectors && frame.depth, "kept until the next scanout");
	if (frame.motion_vectors)
		granite_image_unref(frame.motion_vectors);
	if (frame.depth)
		granite_image_unref(frame.depth);
	if (frame.translucent)
		granite_image_unref(frame.translucent);
	/* a new frame's targets in place of the last's */
	{
		unsigned destroyed = images_destroyed;
		granite_image_t *before = p.upscaler_frame.motion_vectors;
		ntsc_320x240(&p, 0x100002);
		image = rdp_processor_scanout(&p, &options, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (image)
			granite_image_unref(image);
		granite_device_next_frame_context(&dev);
		granite_device_next_frame_context(&dev);
		granite_device_next_frame_context(&dev);
		CHECK(p.upscaler_frame.motion_vectors && p.upscaler_frame.motion_vectors != before
				&& images_destroyed > destroyed, "a new frame's targets, the last's let go");
	}
	options.upscaler.enable = false;
	image = rdp_processor_scanout(&p, &options, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (image)
		granite_image_unref(image);
	rdp_processor_upscaler_frame(&p, &frame);
	CHECK(!frame.motion_vectors && !frame.depth && !p.upscaler_frame.motion_vectors,
			"a scanout without the upscaler drops them");

	rdp_processor_begin_frame_context(&p);
	rdp_processor_free(&p);
	CHECK(!p.rdram && !p.ring && !p.timeline.worker, "freed");
}

int main(void)
{
	setenv("PARALLEL_RDP_NO_KNOWN_PIPELINES", "1", 1);
	unsetenv("PARALLEL_RDP_SINGLE_THREADED_COMMAND");
	unsetenv("PARALLEL_RDP_NO_STORAGE_8BIT");
	open_device();
	/* (new memory is not zero: what is cleared is) */
	memory_fill_byte = 0xcd;
	test_processor(NULL, 0);
	test_processor(host_rdram, 0x1000);
	setenv("PARALLEL_RDP_SINGLE_THREADED_COMMAND", "1", 1);
	setenv("PARALLEL_RDP_NO_STORAGE_8BIT", "1", 1);
	test_processor(NULL, 0);
	close_device();
	CHECK(images_destroyed == num_images, "every image went back");
	CHECK(unmade == made + num_pipelines, "and every buffer, view and pipeline");

	if (failures)
	{
		fprintf(stderr, "test_rdp_processor: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_rdp_processor: ok\n");
	return 0;
}
