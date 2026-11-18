/* The graphics context's life and what the core asks of it
 * (DisplayWindow.h, gli_framebuffer.h). C89. */
#include "DisplayWindow.h"
#include "gli_framebuffer.h"
#include "Graphics/gli_context.h"
#include "Graphics/gli_params.h"
#include "Graphics/OpenGLContext/opengl_Utils.h"
#include "Graphics/VulkanContext/vk_device.h"

void gli_window_gpu_start(void)
{
	gli_context_init();
	gli_drawer_init_data();
}

void gli_window_gpu_stop(void)
{
	gli_drawer_destroy_data();
	gli_context_destroy();
}

void gli_window_destroy_gfx_context(void)
{
	gli_drawer_destroy_data();
	gli_context_destroy();
}

void gli_window_reinit_gfx_context(void)
{
	gli_context_init();
	gli_drawer_init_data();
}

void gli_gl_reset_error(void)
{
	if (!gliden64_vulkan_enabled())
		gli_gl_has_error();
}

s32 gli_gfx_max_texture_size(void)
{
	return gli_ctx.ops->getMaxTextureSize(gli_ctx.impl);
}

void gli_gfx_touch_error(void)
{
	gli_ctx.ops->isError(gli_ctx.impl);
}

void gli_gfx_clear_color(void)
{
	gli_ctx.ops->clearColorBuffer(gli_ctx.impl, 0.0f, 0.0f, 0.0f, 0.0f);
}

/* 0 clipping, 1 no near plane clipping, 2 none: the context's own */
u32 gli_gfx_clamp_mode(void)
{
	return gli_ctx.ops->getClampMode(gli_ctx.impl);
}

bool gli_gfx_polygon_offset_fill(void)
{
	return gli_ctx.ops->isEnabled(gli_ctx.impl, GLI_EN_POLYGON_OFFSET_FILL) != 0;
}

void gli_gfx_set_no_near_clip(bool _noNearClip)
{
	gli_ctx.ops->setClampMode(gli_ctx.impl, _noNearClip ? 1 : 0);
}
