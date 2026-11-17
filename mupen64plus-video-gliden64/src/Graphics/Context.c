/* GLideN64's graphics context: the backend picked, started and stopped
 * (gli_context.h). C89. */
#include "gli_context.h"
#include "VulkanContext/vk_device.h"

gli_context gli_ctx;

void gli_context_init(void)
{
	bool * const features[GLI_FEATURE_COUNT] = {
		&gli_ctx.multisampling,
		&gli_ctx.blitFramebuffer,
		&gli_ctx.weakBlitFramebuffer,
		&gli_ctx.depthFramebufferTextures,
		&gli_ctx.shaderProgramBinary,
		&gli_ctx.imageTextures,
		&gli_ctx.integerTextures,
		&gli_ctx.clipControl,
		&gli_ctx.framebufferFetch,
		&gli_ctx.textureBarrier,
		&gli_ctx.dualSourceBlending
	};
	u32 i;

	/* a backend left running is freed, as replacing it did */
	if (gli_ctx.impl != NULL)
		gli_ctx.ops->free_impl(gli_ctx.impl);

	if (gliden64_vulkan_enabled()) {
		gli_ctx.ops = gli_vk_context_ops();
		gli_ctx.impl = gli_vk_context_new();
	} else {
		gli_ctx.ops = gli_gl_context_ops();
		gli_ctx.impl = gli_gl_context_new();
	}
	gli_ctx.ops->init(gli_ctx.impl);

	/* the formats of the backend before are freed with its own function */
	if (gli_ctx.formats != NULL)
		gli_ctx.free_formats(gli_ctx.formats);
	gli_ctx.formats = gli_ctx.ops->getFramebufferTextureFormats(gli_ctx.impl);
	gli_ctx.free_formats = gli_ctx.ops->free_formats;

	for (i = 0; i < GLI_FEATURE_COUNT; i++)
		*features[i] = gli_ctx.ops->isSupported(gli_ctx.impl, i);
}

void gli_context_destroy(void)
{
	gli_ctx.ops->destroy(gli_ctx.impl);
	gli_ctx.ops->free_impl(gli_ctx.impl);
	gli_ctx.impl = NULL;
}
