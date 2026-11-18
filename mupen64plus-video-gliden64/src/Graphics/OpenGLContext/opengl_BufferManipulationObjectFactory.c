#include "GLFunctions.h"
#include "opengl_Utils.h"
#include "opengl_BufferManipulationObjectFactory.h"

/* GL 4.5's direct state access, off: making a frame buffer and adding its
 * render targets without binding it */
/*#define ENABLE_GL_4_5*/

#ifdef ENABLE_GL_4_5
static int has_gl_4_5(const gli_gl_info *_info)
{
	return _info->majorVersion > 4 || (_info->majorVersion == 4 && _info->minorVersion >= 5);
}
#endif

/*---------------Frame buffers and render buffers-------------*/

u32 gli_gl_create_framebuffer(const gli_gl_buffer_objects *_objects)
{
	GLuint fbo;
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_objects->info)) {
		glCreateFramebuffers(1, &fbo);
		return fbo;
	}
#else
	(void)_objects;
#endif
	glGenFramebuffers(1, &fbo);
	return fbo;
}

u32 gli_gl_create_renderbuffer(void)
{
	GLuint renderbuffer;
	glGenRenderbuffers(1, &renderbuffer);
	return renderbuffer;
}

void gli_gl_init_renderbuffer(const gli_gl_buffer_objects *_objects, const gli_init_renderbuffer_params *_params)
{
	gli_gl_bind_renderbuffer(_objects->bindRenderbuffer, _params->target, _params->handle);
	glRenderbufferStorage((GLenum)_params->target, (GLenum)_params->format, _params->width, _params->height);
}

void gli_gl_add_render_target(const gli_gl_buffer_objects *_objects, const gli_fb_render_target *_params)
{
#ifdef ENABLE_GL_4_5
	if (has_gl_4_5(_objects->info)) {
		glNamedFramebufferTexture(_params->bufferHandle, (GLenum)_params->attachment, _params->textureHandle, 0);
		return;
	}
#endif
	gli_gl_bind_framebuffer(_objects->bindFramebuffer, _params->bufferTarget, _params->bufferHandle);
	if (_params->textureTarget == GLI_TT_RENDERBUFFER)
		glFramebufferRenderbuffer((GLenum)_params->bufferTarget, (GLenum)_params->attachment,
			(GLenum)_params->textureTarget, _params->textureHandle);
	else
		glFramebufferTexture2D((GLenum)_params->bufferTarget, (GLenum)_params->attachment,
			(GLenum)_params->textureTarget, _params->textureHandle, 0);
}

/*---------------Blits-------------*/

bool gli_gl_blit_framebuffers(const gli_gl_buffer_objects *_objects, const gli_blit_params *_params)
{
	/* Adreno's blits are a pixel off */
	const s32 coordFix = _objects->info->renderer == GLI_GL_ADRENO ? 1 : 0;
	if (_objects->info->isGLES2)
		return 0;

	gli_gl_bind_framebuffer(_objects->bindFramebuffer, GLI_BT_READ_FRAMEBUFFER, _params->readBuffer);
	gli_gl_bind_framebuffer(_objects->bindFramebuffer, GLI_BT_DRAW_FRAMEBUFFER, _params->drawBuffer);
	gli_gl_enable_cached(_objects->enableScissor, 0);
	glBlitFramebuffer(
		coordFix + _params->srcX0, _params->srcY0, _params->srcX1, _params->srcY1,
		coordFix + _params->dstX0, _params->dstY0, _params->dstX1, _params->dstY1,
		(GLbitfield)_params->mask, (GLenum)_params->filter);
	gli_gl_enable_cached(_objects->enableScissor, 1);
	return !gli_gl_has_error();
}

/*---------------Texture formats-------------*/

void gli_gl_fb_formats(const gli_gl_info *_info, gli_fb_formats *_formats)
{
	if (!_info->isGLESX)
		gli_fb_formats_gl(_formats, GLI_FB_GL, 0);
	else if (!_info->isGLES2)
		gli_fb_formats_gl(_formats, GLI_FB_GLES3, _info->renderer == GLI_GL_ADRENO530 ? GLI_FB_ADRENO530 : 0);
	else
		gli_fb_formats_gl(_formats, GLI_FB_GLES2,
			(gli_gl_has_extension(_info->majorVersion, "GL_OES_depth_texture") ? GLI_FB_OES_DEPTH : 0) |
			(gli_gl_has_extension(_info->majorVersion, "GL_OES_rgb8_rgba8") ? GLI_FB_OES_RGBA8 : 0));
}
