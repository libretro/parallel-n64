#pragma once

/* The boundary between GLideN64's C++ and the C89 Vulkan backend: this file
 * maps its calls and its parameters to vk_renderer.h and holds no state of
 * its own beyond what GLideN64 expects back. */
#ifdef __cplusplus
#include <Graphics/ContextImpl.h>

namespace vulkan {

	graphics::ContextImpl * createContextImpl();

}
#endif

#ifdef __cplusplus
extern "C"
#endif
void gliden64_vulkan_present(void);
