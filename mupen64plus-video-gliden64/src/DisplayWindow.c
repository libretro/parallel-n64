#include <assert.h>
#include "Config.h"
#include "RSP.h"
#include "VI.h"
#include "gDP.h"
#include "mupenplus/GLideN64_mupenplus.h"
#include "gli_framebuffer.h"
#include "DisplayWindow.h"

extern u32 screen_width;
extern u32 screen_height;
/* libretro.c: hands the frame to the frontend */
int retro_return(bool just_flipping);

gli_window g_window;

static void _setBufferSize(void)
{
	g_window.adjustScreen = false;
	switch (config.frameBufferEmulation.aspect) {
	case aStretch:
		g_window.width = g_window.screenWidth;
		g_window.height = g_window.screenHeight;
		break;
	case a43:
		if (g_window.screenWidth * 3 / 4 > g_window.screenHeight) {
			g_window.height = g_window.screenHeight;
			g_window.width = g_window.screenHeight * 4 / 3;
		} else if (g_window.screenHeight * 4 / 3 > g_window.screenWidth) {
			g_window.width = g_window.screenWidth;
			g_window.height = g_window.screenWidth * 3 / 4;
		} else {
			g_window.width = g_window.screenWidth;
			g_window.height = g_window.screenHeight;
		}
		break;
	case a169:
		if (g_window.screenWidth * 9 / 16 > g_window.screenHeight) {
			g_window.height = g_window.screenHeight;
			g_window.width = g_window.screenHeight * 16 / 9;
		} else if (g_window.screenHeight * 16 / 9 > g_window.screenWidth) {
			g_window.width = g_window.screenWidth;
			g_window.height = g_window.screenWidth * 9 / 16;
		} else {
			g_window.width = g_window.screenWidth;
			g_window.height = g_window.screenHeight;
		}
		break;
	case aAdjust:
		g_window.width = g_window.screenWidth;
		g_window.height = g_window.screenHeight;
		if (g_window.screenWidth * 3 / 4 > g_window.screenHeight) {
			const f32 width43 = g_window.screenHeight * 4.0f / 3.0f;
			g_window.adjustScale = width43 / g_window.screenWidth;
			g_window.adjustScreen = true;
		}
		break;
	default:
		assert(false && "Unknown aspect ratio");
		g_window.width = g_window.screenWidth;
		g_window.height = g_window.screenHeight;
	}
}

static void _updateScale(void)
{
	if (VI.width == 0 || VI.height == 0)
		return;
	g_window.scaleX = g_window.width / (float)VI.width;
	g_window.scaleY = g_window.height / (float)VI.height;
}

void gli_window_start(void)
{
	g_window.fullscreen = false;
	/* Both eyes side by side: GLideN64 draws for one eye's half */
	g_window.screenWidth = config.stereo.mode == stereoBothEyes ? screen_width / 2 : screen_width;
	g_window.screenHeight = screen_height;
	_setBufferSize();

	gli_window_gpu_start();
	g_window.buffersSwapCount = 0;
}

void gli_window_stop(void)
{
	gli_window_gpu_stop();
}

void gli_window_swap_buffers(void)
{
	if (renderCallback)
		(*renderCallback)(1);
	retro_return(true);

	if (!RSP.LLE) {
		if ((config.generalEmulation.hacks & hack_doNotResetOtherModeL) == 0)
			gDP.otherMode.l = 0;
		if ((config.generalEmulation.hacks & hack_doNotResetOtherModeH) == 0)
			gDP.otherMode.h = 0x0CFF;
	}
	++g_window.buffersSwapCount;
}

bool gli_window_change(void)
{
	if (!g_window.toggleFullscreen)
		return false;
	gli_drawer_destroy_data();
	_updateScale();
	gli_drawer_init_data();
	g_window.toggleFullscreen = false;
	return true;
}

void gli_window_close(void)
{
	if (!g_window.toggleFullscreen || !g_window.fullscreen)
		return;
	if (gli_drawer_is_drawing())
		gli_drawer_destroy_data();
	g_window.toggleFullscreen = false;
}

void gli_window_set_toggle_fullscreen(void)
{
	g_window.toggleFullscreen = true;
}

void gli_window_set_size(u32 _width, u32 _height)
{
	if (g_window.width != _width || g_window.height != _height) {
		g_window.resizeWidth = _width;
		g_window.resizeHeight = _height;
		g_window.resizeWindow = true;
	}
}

/* The same size again when what is drawn at it changed */
void gli_window_set_size_now(u32 _width, u32 _height)
{
	g_window.resizeWidth = _width;
	g_window.resizeHeight = _height;
	g_window.resizeWindow = true;
}

bool gli_window_resize(void)
{
	if (!g_window.resizeWindow)
		return false;
	gli_drawer_destroy_data();
	g_window.fullscreen = true;
	g_window.width = g_window.screenWidth = g_window.resizeWidth;
	g_window.height = g_window.screenHeight = g_window.resizeHeight;
	gli_gl_reset_error();
	_updateScale();
	gli_drawer_init_data();
	g_window.resizeWindow = false;
	return true;
}

void gli_window_update_scale(void)
{
	_updateScale();
}

u32 gli_window_swap_count(void)
{
	return g_window.buffersSwapCount;
}

bool gli_window_is_adjust_screen(void)
{
	return g_window.adjustScreen;
}

f32 gli_window_adjust_scale(void)
{
	return g_window.adjustScale;
}
