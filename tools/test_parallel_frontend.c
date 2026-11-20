/* test_parallel_frontend -- paraLLEl-RDP as the core's graphics plugin
 * (mupen64plus-video-paraLLEl/parallel.c), with stand-ins for the
 * command processor, the Vulkan device and the frontend.
 *
 * - the device made through the frontend: its queues handed back, the
 *   device the frontend's to destroy; with the frontend's own vkCreateDevice
 *   (negotiation v2);
 * - load: the processor made at the option's factor with the quirks,
 *   the pipelines ahead; a device the processor cannot run gives the
 *   "ERR" image;
 * - the command list: commands handed to the processor, a sync full
 *   raising the DP interrupt and, synchronous, waiting for the RDP;
 * - each frame: the VI's registers, the scanout's image handed to the
 *   frontend at the sync index and held until the index comes round,
 *   a blank image when there is none; the frontend's temporal upscaler
 *   turning on remakes the processor at its factor, its inputs handed
 *   over with the frame and held with it; turned off, let go;
 * - close: every image let go, the pipeline files saved, the device
 *   freed and the context destroyed.
 *
 * Built by `make check-core` (top-level Makefile). C89. Exit status 0 on
 * pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "Gfx #1.3.h"
#include "z64.h"
#include "parallel.h"
#include "parallel_internal.h"
#include "rdp_processor.h"
#include "rdp_pipeline_files.h"
#include "pipeline_cache.h"
#include "command_ops.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_parallel_frontend: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

/* ---- the core's ---- */
GFX_INFO gfx_info;
static unsigned regs[32];
static unsigned interrupts_checked;
static uint8_t rdram[1 << 20], dmem[0x1000], imem[0x1000];

static void check_interrupts(void)
{
	interrupts_checked++;
}

static void log_stub(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	(void)fmt;
}
retro_log_printf_t log_cb = log_stub;

static unsigned messages;
static bool environ_stub(unsigned cmd, void *data)
{
	if (cmd == RETRO_ENVIRONMENT_SET_MESSAGE)
		messages++;
	if (cmd == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY)
		*(const char **)data = "/nowhere";
	return true;
}
retro_environment_t environ_cb = environ_stub;

static unsigned returns;
int retro_return(bool just_flipping)
{
	(void)just_flipping;
	returns++;
	return 0;
}

static unsigned dlists;
void parallel_hle_process_dlist(void)
{
	dlists++;
}

/* ---- images ---- */
static int live_images;
static unsigned images_made;
static granite_image_view_t views[256];

static granite_image_t *new_image(unsigned width, unsigned height)
{
	granite_image_t *image = (granite_image_t *)calloc(1, sizeof(*image));
	retro_atomic_int_init(&image->refs, 1);
	image->info.width = width;
	image->info.height = height;
	image->info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image->image = (VkImage)(uintptr_t)(0x1000 + images_made);
	image->view = &views[images_made % 256];
	image->view->views.view = (VkImageView)(uintptr_t)(0x9000 + images_made);
	images_made++;
	live_images++;
	return image;
}

void granite_image_dispose(granite_image_t *image)
{
	live_images--;
	free(image);
}

/* ---- the device ---- */
static unsigned devices_inited, devices_freed, contexts_destroyed, frame_contexts, flushes, created_images;
static unsigned blank_submits, caches_inited;
static bool context_fails;
static granite_create_device_t factory_given;

bool granite_context_init_loader(PFN_vkGetInstanceProcAddr addr)
{
	return addr != NULL;
}

bool granite_context_init(granite_context_t *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
		const char **extensions, unsigned num_extensions, const VkPhysicalDeviceFeatures *required_features,
		granite_create_device_t create_device, void *user)
{
	(void)instance; (void)surface; (void)extensions; (void)num_extensions; (void)required_features;
	factory_given = create_device;
	if (create_device)
		context->device = create_device(user, gpu, NULL);
	if (context_fails)
		return false;
	context->gpu = gpu;
	if (!create_device)
		context->device = (VkDevice)(uintptr_t)0x77;
	context->queue_info.queues[GRANITE_QUEUE_GRAPHICS] = (VkQueue)(uintptr_t)0x55;
	context->queue_info.family_indices[GRANITE_QUEUE_GRAPHICS] = 3;
	context->owned_device = true;
	return true;
}

static unsigned owned_destroyed;
void granite_context_destroy(granite_context_t *context)
{
	if (context->owned_device && context->device)
		owned_destroyed++;
	contexts_destroyed++;
}

void granite_device_init(granite_device_t *d) { memset(d, 0, sizeof(*d)); devices_inited++; }
void granite_device_free(granite_device_t *d) { (void)d; devices_freed++; }
void granite_device_set_context(granite_device_t *d, const granite_context_t *c) { (void)d; (void)c; }
void granite_device_init_frame_contexts(granite_device_t *d, unsigned count) { (void)d; frame_contexts = count; }
void granite_device_next_frame_context(granite_device_t *d) { (void)d; }
void granite_device_flush_frame(granite_device_t *d) { (void)d; flushes++; }

granite_image_t *granite_device_create_image(granite_device_t *d, const granite_image_info_t *info,
		const granite_image_initial_data_t *initial)
{
	(void)d; (void)initial;
	created_images++;
	return new_image(info->width, info->height);
}

static char cb_storage[16];
granite_command_buffer_t *granite_device_request_command_buffer(granite_device_t *d, unsigned type)
{
	(void)d; (void)type;
	return (granite_command_buffer_t *)(void *)cb_storage;
}

void granite_device_submit(granite_device_t *d, granite_command_buffer_t *cb, granite_fence_t **fence,
		unsigned count, granite_semaphore_t **semaphores)
{
	(void)d; (void)cb; (void)fence; (void)count; (void)semaphores;
	blank_submits++;
}

void granite_cb_image_barrier(granite_command_buffer_t *cb, const granite_image_t *image, VkImageLayout old_layout,
		VkImageLayout new_layout, VkPipelineStageFlags src_stages, VkAccessFlags src_access,
		VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
{
	(void)cb; (void)image; (void)old_layout; (void)new_layout; (void)src_stages; (void)src_access;
	(void)dst_stages; (void)dst_access;
}

void granite_cb_clear_image(granite_command_buffer_t *cb, const granite_image_t *image, const VkClearValue *value,
		VkImageAspectFlags aspect)
{
	(void)cb; (void)image; (void)value; (void)aspect;
}

VkImageAspectFlags granite_format_aspect_mask(VkFormat format)
{
	(void)format;
	return VK_IMAGE_ASPECT_COLOR_BIT;
}

bool granite_pipeline_cache_init(VkDevice device, const struct granite_vk_device_table *table, const uint8_t *uuid,
		const uint8_t *data, size_t size, VkPipelineCache *cache)
{
	(void)device; (void)table; (void)uuid; (void)data; (void)size; (void)cache;
	caches_inited++;
	return true;
}

size_t granite_pipeline_cache_blob_size(VkDevice device, const struct granite_vk_device_table *table,
		VkPipelineCache cache)
{
	(void)device; (void)table; (void)cache;
	return 0;
}

bool granite_pipeline_cache_blob(VkDevice device, const struct granite_vk_device_table *table, VkPipelineCache cache,
		const uint8_t *uuid, uint8_t *data, size_t size)
{
	(void)device; (void)table; (void)cache; (void)uuid; (void)data; (void)size;
	return false;
}

size_t granite_pipeline_record_table_get(granite_pipeline_record_table_t *table, const pipeline_record_t **records)
{
	(void)table;
	*records = NULL;
	return 0;
}

static unsigned files_loaded, files_saved;
void rdp_pipeline_files_load(rdp_pipeline_files_t *f, const char *dir)
{
	(void)dir;
	memset(f, 0, sizeof(*f));
	files_loaded++;
}

void rdp_pipeline_files_save(rdp_pipeline_files_t *f, const pipeline_record_t *built, size_t num_built,
		const void *cache, size_t cache_size)
{
	(void)f; (void)built; (void)num_built; (void)cache; (void)cache_size;
	files_saved++;
}

void rdp_vi_scanout_options_init(rdp_vi_scanout_options_t *o)
{
	memset(o, 0, sizeof(*o));
}

/* ---- the command processor ---- */
static unsigned processors_made, processors_freed, processor_factor_given, idles, aheads, enqueued, timeline_waits;
static uint32_t last_quirks, vi_regs[RDP_VI_REGISTER_COUNT];
static bool processor_unsupported, scanout_none, without_mask;
static uint64_t timeline;
static rdp_vi_scanout_options_t last_options;
static rdp_upscaler_targets_t held_targets;

void rdp_processor_init(rdp_processor_t *p, granite_device_t *dev, void *rdram_ptr, size_t rdram_offset,
		size_t rdram_size, size_t hidden_size, uint32_t flags)
{
	(void)dev; (void)rdram_ptr; (void)rdram_offset; (void)rdram_size; (void)hidden_size;
	memset(p, 0, sizeof(*p));
	p->flags = flags;
	processor_factor_given = flags & RDP_PROCESSOR_UPSCALING_8X ? 8 : flags & RDP_PROCESSOR_UPSCALING_4X ? 4
			: flags & RDP_PROCESSOR_UPSCALING_2X ? 2 : 1;
	processors_made++;
}

static void drop_held(void)
{
	if (held_targets.motion_vectors)
		granite_image_unref(held_targets.motion_vectors);
	if (held_targets.depth)
		granite_image_unref(held_targets.depth);
	if (held_targets.translucent)
		granite_image_unref(held_targets.translucent);
	memset(&held_targets, 0, sizeof(held_targets));
}

void rdp_processor_free(rdp_processor_t *p) { (void)p; drop_held(); processors_freed++; }
bool rdp_processor_supported(const rdp_processor_t *p) { (void)p; return !processor_unsupported; }
unsigned rdp_processor_scaling_factor(const rdp_processor_t *p) { (void)p; return processor_factor_given; }
bool rdp_processor_super_sampled_readback(const rdp_processor_t *p) { (void)p; return false; }
void rdp_processor_set_quirks(rdp_processor_t *p, uint32_t q) { (void)p; last_quirks = q; }
void rdp_processor_idle(rdp_processor_t *p) { (void)p; idles++; }
void rdp_processor_begin_frame_context(rdp_processor_t *p) { (void)p; }
void rdp_processor_wait_pipelines_ahead_for_test(rdp_processor_t *p) { (void)p; }
void rdp_processor_build_pipelines_ahead(rdp_processor_t *p, const pipeline_record_t *r, size_t n, bool up)
{
	(void)p; (void)r; (void)n; (void)up;
	aheads++;
}
void rdp_processor_enqueue(rdp_processor_t *p, unsigned n, const uint32_t *w) { (void)p; (void)n; (void)w; enqueued++; }
uint64_t rdp_processor_signal_timeline(rdp_processor_t *p) { (void)p; return ++timeline; }
void rdp_processor_wait_for_timeline(rdp_processor_t *p, uint64_t v) { (void)p; (void)v; timeline_waits++; }
void rdp_processor_run_on_command_thread(rdp_processor_t *p, void (*fn)(void *), void *ctx) { (void)p; fn(ctx); }
void rdp_processor_set_vi_register(rdp_processor_t *p, enum rdp_vi_register reg, uint32_t value)
{
	(void)p;
	vi_regs[reg] = value;
}

granite_image_t *rdp_processor_scanout(rdp_processor_t *p, const rdp_vi_scanout_options_t *o, VkImageLayout layout)
{
	(void)p; (void)layout;
	last_options = *o;
	if (o->upscaler.enable)
	{
		/* the frame's targets, the processor's references */
		drop_held();
		held_targets.motion_vectors = new_image(640, 480);
		held_targets.depth = new_image(640, 480);
		held_targets.translucent = without_mask ? NULL : new_image(640, 480);
		held_targets.jitter_x = 0.25f;
	}
	return scanout_none ? NULL : new_image(320 * processor_factor_given, 240 * processor_factor_given);
}

void rdp_processor_upscaler_frame(rdp_processor_t *p, rdp_upscaler_targets_t *frame)
{
	(void)p;
	*frame = held_targets;
	if (frame->motion_vectors)
		granite_image_ref(frame->motion_vectors);
	if (frame->depth)
		granite_image_ref(frame->depth);
	if (frame->translucent)
		granite_image_ref(frame->translucent);
}

/* ---- the frontend ---- */
static unsigned sync_mask = 3, sync_index, images_set;
static struct retro_vulkan_image last_set;

static uint32_t get_sync_index_mask(void *h) { (void)h; return sync_mask; }
static uint32_t get_sync_index(void *h) { (void)h; return sync_index; }
static void wait_sync_index(void *h) { (void)h; }
static void lock_queue(void *h) { (void)h; }
static void set_image(void *h, const struct retro_vulkan_image *image, uint32_t n, const VkSemaphore *s, uint32_t q)
{
	(void)h; (void)n; (void)s; (void)q;
	last_set = *image;
	images_set++;
}

static struct retro_hw_render_interface_vulkan vulkan;

static bool upscaler_wants;
static unsigned inputs_set;
static struct retro_hw_upscaler_inputs last_inputs;

static bool get_request(void *h, struct retro_hw_upscaler_request *r)
{
	(void)h;
	if (!upscaler_wants)
		return false;
	memset(r, 0, sizeof(*r));
	r->output_width = 1280;
	r->output_height = 960;
	r->render_width = 640;
	r->render_height = 480;
	r->min_render_width = 320;
	r->min_render_height = 240;
	r->max_render_width = 1280;
	r->max_render_height = 960;
	return true;
}

static void set_inputs(void *h, const struct retro_hw_upscaler_inputs *inputs)
{
	(void)h;
	last_inputs = *inputs;
	inputs_set++;
}

static struct retro_hw_render_upscaler_interface upscaler;

static VkDevice frontend_device(VkPhysicalDevice gpu, void *opaque, const VkDeviceCreateInfo *info)
{
	(void)gpu; (void)info;
	*(unsigned *)opaque += 1;
	return (VkDevice)(uintptr_t)0x88;
}

static PFN_vkVoidFunction VKAPI_CALL get_proc(VkInstance i, const char *name)
{
	(void)i; (void)name;
	return NULL;
}

static void setup(void)
{
	unsigned i;
	unsigned **fields[] = {
		&gfx_info.MI_INTR_REG, &gfx_info.DPC_START_REG, &gfx_info.DPC_END_REG, &gfx_info.DPC_CURRENT_REG,
		&gfx_info.DPC_STATUS_REG, &gfx_info.VI_STATUS_REG, &gfx_info.VI_ORIGIN_REG, &gfx_info.VI_WIDTH_REG,
		&gfx_info.VI_INTR_REG, &gfx_info.VI_V_CURRENT_LINE_REG, &gfx_info.VI_TIMING_REG, &gfx_info.VI_V_SYNC_REG,
		&gfx_info.VI_H_SYNC_REG, &gfx_info.VI_LEAP_REG, &gfx_info.VI_H_START_REG, &gfx_info.VI_V_START_REG,
		&gfx_info.VI_V_BURST_REG, &gfx_info.VI_X_SCALE_REG, &gfx_info.VI_Y_SCALE_REG
	};
	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
		*fields[i] = &regs[i];
	gfx_info.RDRAM = rdram;
	gfx_info.DMEM = dmem;
	gfx_info.IMEM = imem;
	gfx_info.CheckInterrupts = check_interrupts;

	vulkan.handle = &vulkan;
	vulkan.get_sync_index_mask = get_sync_index_mask;
	vulkan.get_sync_index = get_sync_index;
	vulkan.wait_sync_index = wait_sync_index;
	vulkan.lock_queue = lock_queue;
	vulkan.unlock_queue = lock_queue;
	vulkan.set_image = set_image;
	upscaler.handle = &upscaler;
	upscaler.get_request = get_request;
	upscaler.set_inputs = set_inputs;
}

static void frame(void)
{
	parallel_begin_frame();
	parallelUpdateScreen();
	sync_index = (sync_index + 1) % 2;
}

int main(void)
{
	struct retro_vulkan_context fc;
	unsigned wrapped = 0, i, before;
	VkPhysicalDevice gpu = (VkPhysicalDevice)(uintptr_t)0x66;

	setup();

	/* the device */
	memset(&fc, 0, sizeof(fc));
	CHECK(!parallel_init(&vulkan), "no load without a device");
	context_fails = true;
	CHECK(!parallel_create_device(&fc, VK_NULL_HANDLE, gpu, VK_NULL_HANDLE, get_proc, NULL, 0, NULL, 0, NULL),
			"a device that cannot be made");
	context_fails = false;
	CHECK(parallel_create_device2(&fc, VK_NULL_HANDLE, gpu, VK_NULL_HANDLE, get_proc, frontend_device, &wrapped)
			&& wrapped == 1 && factory_given && fc.device == (VkDevice)(uintptr_t)0x88,
			"negotiation v2: through the frontend's vkCreateDevice");
	CHECK(parallel_create_device(&fc, VK_NULL_HANDLE, gpu, VK_NULL_HANDLE, get_proc, NULL, 0, NULL, 0, NULL)
			&& !factory_given && fc.gpu == gpu && fc.device == (VkDevice)(uintptr_t)0x77
			&& fc.queue == (VkQueue)(uintptr_t)0x55 && fc.queue_family_index == 3
			&& fc.presentation_queue == fc.queue && fc.presentation_queue_family_index == 3,
			"the queues handed back");
	CHECK(!strcmp(parallel_get_application_info()->pApplicationName, "paraLLEl-RDP")
			&& parallel_get_application_info()->apiVersion == VK_API_VERSION_1_1, "the application, Vulkan 1.1");

	/* load */
	parallel_set_upscaling(2);
	parallel_set_native_tex_rect(false);
	parallel_set_native_texture_lod(true);
	parallel_offer_upscaler_interface(&upscaler);
	CHECK(parallel_init(&vulkan), "loaded");
	CHECK(devices_inited == 1 && frame_contexts == 2 && caches_inited == 1 && files_loaded == 1,
			"the device, a frame context per sync index, the pipeline files");
	CHECK(processors_made == 1 && processor_factor_given == 2 && aheads == 1, "the processor at the option's factor");
	CHECK(last_quirks == RDP_DECODE_NATIVE_TEXTURE_LOD, "with the quirks");

	/* the command list */
	{
		uint32_t w[4];
		w[0] = 0x37000000u; w[1] = 0x11;
		w[2] = 0x29000000u; w[3] = 0;
		memcpy(rdram + 0x1000, w, sizeof(w));
		*gfx_info.DPC_CURRENT_REG = 0x1000;
		*gfx_info.DPC_END_REG = 0x1010;
		*gfx_info.MI_INTR_REG = 0;
		parallelProcessRDPList();
		CHECK(enqueued == 2 && *gfx_info.DPC_CURRENT_REG == 0x1010, "the commands handed to the processor");
		CHECK((*gfx_info.MI_INTR_REG & DP_INTERRUPT) && interrupts_checked == 1 && timeline_waits == 1,
				"a sync full raises the DP interrupt, waited for");
		parallel_set_synchronous_rdp(false);
		*gfx_info.DPC_CURRENT_REG = 0x1008;
		parallelProcessRDPList();
		CHECK(timeline_waits == 1 && interrupts_checked == 2, "not waited for unless synchronous");
	}
	parallelProcessDList();
	CHECK(dlists == 1, "a display list to the HLE emitter");

	/* frames */
	*gfx_info.VI_ORIGIN_REG = 0x100000;
	*gfx_info.VI_V_BURST_REG = 0x1234;
	frame();
	CHECK(images_set == 1 && last_set.image_view != VK_NULL_HANDLE && parallel_frame_width() == 640
			&& parallel_frame_height() == 480 && returns == 1, "the scanout's image handed to the frontend");
	CHECK(vi_regs[RDP_VI_REGISTER_ORIGIN] == 0x100000 && vi_regs[RDP_VI_REGISTER_TIMING] == 0x1234
			&& vi_regs[RDP_VI_REGISTER_V_BURST] == 0x1234, "with the VI's registers");
	CHECK(last_options.persist_frame_on_invalid_input && last_options.blend_previous_frame && !last_options.upscaler.enable,
			"and the options");
	frame();
	CHECK(live_images == 2, "an image held per sync index");
	frame();
	frame();
	CHECK(live_images == 2, "let go when the index comes round");

	scanout_none = true;
	before = created_images;
	frame();
	CHECK(created_images == before + 1 && blank_submits == 1 && images_set == 5 && parallel_frame_width() == 1,
			"a blank image when there is none");
	scanout_none = false;

	/* the upscaler */
	upscaler_wants = true;
	before = processors_made;
	parallel_set_upscaler_interface(&upscaler);
	CHECK(processors_made == before && processor_factor_given == 2 && parallel_upscaler_active(),
			"turned on at the processor's own factor: nothing made anew");
	parallel_set_upscaling(1);
	frame();
	CHECK(last_options.upscaler.enable && last_options.upscaler.jitter_phase >= 8 && inputs_set == 1
			&& last_inputs.jitter_x == 0.25f && last_inputs.motion_vectors.image && last_inputs.depth.view
			&& last_inputs.transparency_mask.width == 640, "the frame's inputs handed over");
	frame();
	CHECK(inputs_set == 2 && live_images == 2 + 6, "held per sync index with the frame (the last shared with the processor)");
	if (live_images != 2 + 6)
		fprintf(stderr, "live %d\n", live_images);

	/* a frame without its translucency mask: no inputs, nothing kept */
	without_mask = true;
	before = inputs_set;
	frame();
	frame();
	CHECK(inputs_set == before && live_images == 2 + 2, "a frame without all its inputs hands none over, keeps none");
	without_mask = false;

	upscaler_wants = false;
	for (i = 0; i < 3; i++)
		frame();
	CHECK(!parallel_upscaler_active() && processor_factor_given == 1 && idles >= 1 && live_images == 2 + 0,
			"turned off: made anew at the option's factor, the last idled first, the inputs let go");

	/* close */
	parallel_deinit();
	CHECK(live_images == 0, "every image let go");
	CHECK(files_saved == 1 && devices_freed == 1 && contexts_destroyed >= 1, "the files saved, the device freed");
	CHECK(owned_destroyed == 0, "the device left to the frontend, whose it is");

	/* a device the processor cannot run: "ERR" */
	processor_unsupported = true;
	CHECK(parallel_create_device(&fc, VK_NULL_HANDLE, gpu, VK_NULL_HANDLE, get_proc, NULL, 0, NULL, 0, NULL),
			"the device");
	CHECK(!parallel_init(&vulkan), "a device the processor cannot run does not load");
	parallel_deinit();
	CHECK(live_images == 0, "nothing held");

	if (failures)
	{
		fprintf(stderr, "test_parallel_frontend: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_parallel_frontend: ok\n");
	return 0;
}
