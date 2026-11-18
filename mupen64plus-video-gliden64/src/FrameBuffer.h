#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

/* A frame buffer in the list (FrameBuffer.c): its state is gli_fb's, its
 * GPU side FrameBufferTextures.c's, its RDRAM side FrameBufferRdram.c's.
 * The list is reached through gli_framebuffer.h. C89. */

#include "Types.h"
#include "gli_vi_update.h"
#include "gli_fb_rdram.h"
#include "gli_fb_list.h"
#include "gli_framebuffer.h"
#include "Textures.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrameBuffer
{
	gli_fb fb;
} FrameBuffer;

#ifdef __cplusplus
}
#endif

#endif
