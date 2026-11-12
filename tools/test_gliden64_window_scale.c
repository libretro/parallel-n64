/* test_gliden64_window_scale -- the scale GLideN64 draws a window at.
 *
 * The scale is the window's size over the N64's. The core is built with
 * -Ofast, which takes that quotient of floats as a product with the
 * reciprocal: 640 over 320 came out 1.9999999, a 320 pixel frame buffer
 * drawn at it is 639 pixels wide, and those were stretched over the 640
 * of the window, every frame, at every whole factor. A window a whole
 * number of times the N64's size must be drawn at exactly that; one that
 * is not, at the quotient as before.
 *
 * Built with -Ofast, as the core is.
 */
#include <stdio.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/Config.h"
#include "../mupen64plus-video-gliden64/src/RSP.h"
#include "../mupen64plus-video-gliden64/src/VI.h"
#include "../mupen64plus-video-gliden64/src/gDP.h"
#include "../mupen64plus-video-gliden64/src/DisplayWindow.h"

/* what DisplayWindow.c asks of the rest of GLideN64 */
Config config;
RSPInfo RSP;
VIInfo VI;
gDPInfo gDP;
u32 screen_width = 640;
u32 screen_height = 480;
int retro_return(bool just_flipping) { (void)just_flipping; return 0; }
void gli_window_gpu_start(void) {}
void gli_window_gpu_stop(void) {}
void gli_drawer_init_data(void) {}
void gli_drawer_destroy_data(void) {}
bool gli_drawer_is_drawing(void) { return false; }
void gli_gl_reset_error(void) {}

void gli_window_set_size(u32 _width, u32 _height);
bool gli_window_resize(void);

static int failures;

/* (the sizes come in at run time, as they do in the core) */
static void check(volatile u32 window_w, volatile u32 window_h, volatile u32 native_w, volatile u32 native_h)
{
	u32 buffer_w, buffer_h;
	VI.width = native_w;
	VI.height = native_h;
	/* (another size first: the same size again is no resize) */
	gli_window_set_size(1, 1);
	gli_window_resize();
	gli_window_set_size(window_w, window_h);
	gli_window_resize();
	/* as FrameBuffer.cpp sizes a buffer's texture */
	buffer_w = (u32)(native_w * g_window.scaleX);
	buffer_h = (u32)(native_h * g_window.scaleY);
	if (window_w % native_w == 0 && (g_window.scaleX != (f32)(window_w / native_w) || buffer_w != window_w)) {
		fprintf(stderr, "%ux%u over %ux%u: drawn %.9f wide, a buffer of %u\n", window_w, window_h,
			native_w, native_h, g_window.scaleX, buffer_w);
		failures++;
	}
	if (window_h % native_h == 0 && (g_window.scaleY != (f32)(window_h / native_h) || buffer_h != window_h)) {
		fprintf(stderr, "%ux%u over %ux%u: drawn %.9f high, a buffer of %u\n", window_w, window_h,
			native_w, native_h, g_window.scaleY, buffer_h);
		failures++;
	}
	if (window_w % native_w != 0) {
		const f32 want = window_w / (f32)native_w;
		const f32 off = g_window.scaleX > want ? g_window.scaleX - want : want - g_window.scaleX;
		if (off > want * 1e-6f) {
			fprintf(stderr, "%ux%u over %ux%u: drawn %.9f wide, not %.9f\n", window_w, window_h,
				native_w, native_h, g_window.scaleX, want);
			failures++;
		}
	}
}

int main(void)
{
	u32 factor;
	memset(&config, 0, sizeof(config));
	for (factor = 1; factor <= 16; ++factor) {
		check(320 * factor, 240 * factor, 320, 240);
		check(640 * factor, 480 * factor, 640, 480);
		check(320 * factor, 237 * factor, 320, 237);
		check(640 * factor, 240 * factor, 320, 240);
	}
	/* not whole: the quotient */
	check(1000, 700, 320, 240);
	check(1920, 1080, 320, 240);
	if (failures) {
		fprintf(stderr, "test_gliden64_window_scale: %d failed\n", failures);
		return 1;
	}
	printf("test_gliden64_window_scale: ok\n");
	return 0;
}
