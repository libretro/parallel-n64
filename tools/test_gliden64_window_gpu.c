/* test_gliden64_window_gpu -- the graphics context's life and what the
 * core asks of it (DisplayWindowGpu.c).
 *
 * Starting the window's GPU side, or making the context again, makes the
 * context and then the drawer's data; stopping it, or dropping the
 * context, drops the drawer's data and then the context. The clamp mode
 * is the context's own (0 clipping, 1 no near plane clipping, 2 none),
 * and no near clip sets 1 or 0. The polygon offset query asks the
 * context whether GL_POLYGON_OFFSET_FILL is on; the colour clear clears
 * to transparent black; resetting the GL error reads it, but not on
 * Vulkan. The context and drawer are stubs here.
 *
 *   ./test_gliden64_window_gpu
 */
#include <stdio.h>
#include <string.h>

#include "DisplayWindow.h"
#include "gli_framebuffer.h"
#include "Graphics/gli_context.h"
#include "Graphics/gli_params.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

gli_context gli_ctx;
static gli_context_ops ops;
static char order[16];
static u32 clamp, enabledAsked, clears, errors, glErrors;
static int vulkan, maxSize;
static f32 clear[4];

static void step(char _c)
{
	const size_t n = strlen(order);
	if (n + 1 < sizeof(order)) {
		order[n] = _c;
		order[n + 1] = 0;
	}
}

void gli_context_init(void) { step('C'); }
void gli_context_destroy(void) { step('c'); }
void gli_drawer_init_data(void) { step('D'); }
void gli_drawer_destroy_data(void) { step('d'); }
int gliden64_vulkan_enabled(void) { return vulkan; }
int gli_gl_has_error(void) { glErrors++; return 0; }

static void set_clamp(void *_i, u32 _m) { (void)_i; clamp = _m; }
static u32 get_clamp(void *_i) { (void)_i; return clamp; }
static u32 is_enabled(void *_i, u32 _p) { (void)_i; enabledAsked = _p; return _p == GLI_EN_POLYGON_OFFSET_FILL ? 2 : 0; }
static void clear_color(void *_i, f32 _r, f32 _g, f32 _b, f32 _a)
{
	(void)_i;
	clears++;
	clear[0] = _r; clear[1] = _g; clear[2] = _b; clear[3] = _a;
}
static s32 max_texture(void *_i) { (void)_i; return maxSize; }
static bool is_error(void *_i) { (void)_i; errors++; return 0; }

int main(void)
{
	ops.setClampMode = set_clamp;
	ops.getClampMode = get_clamp;
	ops.isEnabled = is_enabled;
	ops.clearColorBuffer = clear_color;
	ops.getMaxTextureSize = max_texture;
	ops.isError = is_error;
	gli_ctx.ops = &ops;

	gli_window_gpu_start();
	check(strcmp(order, "CD") == 0, "starting: the context, then the drawer's data");
	order[0] = 0;
	gli_window_gpu_stop();
	check(strcmp(order, "dc") == 0, "stopping: the drawer's data, then the context");
	order[0] = 0;
	gli_window_destroy_gfx_context();
	gli_window_reinit_gfx_context();
	check(strcmp(order, "dcCD") == 0, "dropping and making the context again, in the same orders");

	for (clamp = 0; clamp < 3; ++clamp)
		check(gli_gfx_clamp_mode() == clamp, "the clamp mode the context's own");
	gli_gfx_set_no_near_clip(1);
	check(clamp == 1, "no near clip: no near plane clipping");
	gli_gfx_set_no_near_clip(0);
	check(clamp == 0, "else clipping");

	check(gli_gfx_polygon_offset_fill() == 1 && enabledAsked == GLI_EN_POLYGON_OFFSET_FILL,
		"polygon offset: whether the context has GL_POLYGON_OFFSET_FILL on");
	gli_gfx_clear_color();
	check(clears == 1 && clear[0] == 0.0f && clear[1] == 0.0f && clear[2] == 0.0f && clear[3] == 0.0f,
		"the colour cleared to transparent black");
	maxSize = 8192;
	check(gli_gfx_max_texture_size() == 8192, "the context's largest texture");
	gli_gfx_touch_error();
	check(errors == 1, "the context's error read");

	gli_gl_reset_error();
	check(glErrors == 1, "GL's error read and so reset");
	vulkan = 1;
	gli_gl_reset_error();
	check(glErrors == 1, "but not on Vulkan");

	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("window gpu: ok\n");
	return 0;
}
