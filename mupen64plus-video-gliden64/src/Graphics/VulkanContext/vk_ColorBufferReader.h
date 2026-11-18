#ifndef GLI_VK_COLOR_BUFFER_READER_H
#define GLI_VK_COLOR_BUFFER_READER_H

/* Vulkan's color buffer reader (Graphics/ColorBufferReader.h). C89. */

#include <Graphics/ColorBufferReader.h>
#include "vk_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A reader with a ring of readbacks of @_renderer's, as many as
 * copyToRDRAM asks for, at least one, at most eight */
gli_cbr *gli_vk_cbr_new(vkr_renderer *_renderer, struct gli_tex *_texture);

#ifdef __cplusplus
}
#endif

#endif
