#ifndef GLI_GL_BUFFER_OBJECTS_H
#define GLI_GL_BUFFER_OBJECTS_H

/* GL's frame buffers and render buffers: making them, their storage and
 * render targets, blits between them, and their texture formats. C89. */

#include <Graphics/gli_context.h>
#include "opengl_GLInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the context's binding caches and scissor test the calls go through
 * (opengl_CachedFunctions.cpp) */
typedef struct gli_gl_buffer_objects
{
	const gli_gl_info *info;
	void *bindFramebuffer;
	void *bindRenderbuffer;
	void *enableScissor;
} gli_gl_buffer_objects;

u32 gli_gl_create_framebuffer(const gli_gl_buffer_objects *_objects);
u32 gli_gl_create_renderbuffer(void);
void gli_gl_init_renderbuffer(const gli_gl_buffer_objects *_objects, const gli_init_renderbuffer_params *_params);
void gli_gl_add_render_target(const gli_gl_buffer_objects *_objects, const gli_fb_render_target *_params);
/* whether the blit went without a GL error; none on GLES 2 */
bool gli_gl_blit_framebuffers(const gli_gl_buffer_objects *_objects, const gli_blit_params *_params);
void gli_gl_fb_formats(const gli_gl_info *_info, gli_fb_formats *_formats);

/* glBindFramebuffer, glBindRenderbuffer and glEnable/glDisable through
 * the context's caches, which skip a call that changes nothing */
void gli_gl_bind_framebuffer(void *_cache, u32 _target, u32 _name);
void gli_gl_bind_renderbuffer(void *_cache, u32 _target, u32 _name);
void gli_gl_enable_cached(void *_cache, int _enable);

#ifdef __cplusplus
}
#endif

#endif
