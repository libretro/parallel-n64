#include <stdlib.h>
#include <Graphics/Context.h>
#include <Graphics/OpenGLContext/GLFunctions.h>
#include <Graphics/VulkanContext/vk_device.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include "DisplayWindow.h"
#include "gli_framebuffer.h"

#ifdef __LIBRETRO__
#include "glsm/glsm.h"
#endif

using namespace opengl;

/* the drawer's data, made and dropped with the context */
struct WindowGpu
{
	static void init() { dwnd().getDrawer()._initData(); }
	static void destroy() { dwnd().getDrawer()._destroyData(); }
};

DisplayWindow & DisplayWindow::get()
{
	static DisplayWindow video;
	return video;
}

void DisplayWindow::updateScale()
{
	gli_window_update_scale();
}

void gli_window_gpu_start(void)
{
	graphics::ObjectHandle::defaultFramebuffer = graphics::ObjectHandle::null;
	gfxContext.init();
	WindowGpu::init();
}

void gli_window_gpu_stop(void)
{
	WindowGpu::destroy();
	gfxContext.destroy();
}

void gli_window_destroy_gfx_context(void)
{
	WindowGpu::destroy();
	gfxContext.destroy();
}

void gli_window_reinit_gfx_context(void)
{
	gfxContext.init();
	WindowGpu::init();
}

void gli_drawer_init_data(void)
{
	WindowGpu::init();
}

void gli_drawer_destroy_data(void)
{
	WindowGpu::destroy();
}

bool gli_drawer_is_drawing(void)
{
	return dwnd().getDrawer().getDrawingState() != DrawingState::Non;
}

void gli_gl_reset_error(void)
{
	if (!gliden64_vulkan_enabled())
		opengl::Utils::isGLError();
}

void gli_window_read_screen2(void * _dest, int * _width, int * _height, int _front)
{
	if (_width == nullptr || _height == nullptr)
		return;

	*_width = g_window.screenWidth;
	*_height = g_window.screenHeight;

	if (_dest == nullptr)
		return;

	u8 *pBufferData = (u8*)malloc((*_width)*(*_height) * 4);
	if (pBufferData == nullptr)
		return;
	u8 *pDest = (u8*)_dest;

#if !defined(OS_ANDROID) && !defined(OS_IOS)
	GLint oldMode;
	glGetIntegerv(GL_READ_BUFFER, &oldMode);
	if (_front != 0)
		glReadBuffer(GL_FRONT);
	else
		glReadBuffer(GL_BACK);
	glReadPixels(0, g_window.heightOffset, g_window.screenWidth, g_window.screenHeight, GL_RGBA, GL_UNSIGNED_BYTE, pBufferData);
	glReadBuffer(oldMode);
#else
	glReadPixels(0, g_window.heightOffset, g_window.screenWidth, g_window.screenHeight, GL_RGBA, GL_UNSIGNED_BYTE, pBufferData);
#endif

	//Convert RGBA to RGB
	for (s32 y = 0; y < *_height; ++y) {
		u8 *ptr = pBufferData + ((*_width) * 4 * y);
		for (s32 x = 0; x < *_width; ++x) {
			pDest[x * 3] = ptr[0]; // red
			pDest[x * 3 + 1] = ptr[1]; // green
			pDest[x * 3 + 2] = ptr[2]; // blue
			ptr += 4;
		}
		pDest += (*_width) * 3;
	}

	free(pBufferData);
}

s32 gli_gfx_max_texture_size(void)
{
	return gfxContext.getMaxTextureSize();
}

void gli_gfx_touch_error(void)
{
	gfxContext.isError();
}

void gli_gfx_clear_color(void)
{
	gfxContext.clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);
}

void gli_gfx_set_no_near_clip(bool _noNearClip)
{
	gfxContext.setClampMode(_noNearClip ? graphics::ClampMode::NoNearPlaneClipping : graphics::ClampMode::ClippingEnabled);
}
