/* The caches' entries for the C modules that are handed one part of the
 * context's cache (opengl_CachedFunctions.h): the readers and drawers a
 * buffer bind, the buffer objects the frame buffer, render buffer and
 * scissor ones, the textures the texture bind. C89. */
#include "opengl_CachedFunctions.h"
#include "opengl_ColorBufferReader.h"
#include "opengl_BufferManipulationObjectFactory.h"
#include "opengl_TextureManipulationObjectFactory.h"

void gli_gl_bind_buffer(void *_cache, u32 _target, u32 _name)
{
	gli_gl_cache_bind_buffer((gli_gl_cache2 *)_cache, _target, _name);
}

void gli_gl_bind_framebuffer(void *_cache, u32 _target, u32 _name)
{
	gli_gl_cache_bind_framebuffer((gli_gl_cache2 *)_cache, _target, _name);
}

void gli_gl_bind_renderbuffer(void *_cache, u32 _target, u32 _name)
{
	gli_gl_cache_bind_renderbuffer((gli_gl_cache2 *)_cache, _target, _name);
}

void gli_gl_enable_cached(void *_cache, int _enable)
{
	gli_gl_enable_set((gli_gl_enable *)_cache, _enable);
}

void gli_gl_bind_texture(void *_cache, u32 _unit, u32 _target, u32 _name)
{
	gli_gl_cache_bind_texture((gli_gl_cache2 *)_cache, _unit, _target, _name);
}

void gli_gl_bind_texture_reset(void *_cache)
{
	gli_gl_cache2_reset_name((gli_gl_cache2 *)_cache);
}
