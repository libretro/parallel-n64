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
/* The size of the image it presented */
#ifdef __cplusplus
extern "C"
#endif
void gliden64_vulkan_presented_size(unsigned * _width, unsigned * _height);
#ifdef __cplusplus
extern "C"
#endif
void gliden64_vulkan_shown(unsigned _address, unsigned _width, unsigned _size,
	unsigned _height, float _scale, const int * _src, const int * _dst);
