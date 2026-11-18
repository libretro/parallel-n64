/* The GL backend's graphics context (gli_context.h): what GLideN64 asks
 * of it, through the table gli_gl_context_ops gives, on the GL state
 * caches, textures, frame buffers, drawers and GLSL programs it holds.
 * C89. */
#include <stdlib.h>
#include <Config.h>
#include <Graphics/gli_context.h>
#include <gli_combiner.h>
#include "GLFunctions.h"
#include "opengl_GLInfo.h"
#include "opengl_Utils.h"
#include "opengl_CachedFunctions.h"
#include "opengl_TextureManipulationObjectFactory.h"
#include "opengl_BufferManipulationObjectFactory.h"
#include "opengl_ColorBufferReader.h"
#include "opengl_Drawer.h"
#include "GLSL/glsl_Programs.h"

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
/* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT is part of GL_EXT_texture_filter_
 * anisotropic. The per-texture parameter constant
 * GL_TEXTURE_MAX_ANISOTROPY_EXT (0x84FE) gets a similar fallback
 * #define in opengl_TextureManipulationObjectFactory.c; we need
 * the maximum-supported-anisotropy query constant (0x84FF) here.
 */
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* the parameters' numbers (gli_params.h) are GL's: a negative array where
 * one is not */
#define GLI_GL_SAME(_gli, _gl) typedef char gli_gl_same_##_gli[(_gli) == (_gl) ? 1 : -1]
GLI_GL_SAME(GLI_CF_RED_GREEN_BLUE, GL_RGB);
GLI_GL_SAME(GLI_CF_RGBA, GL_RGBA);
GLI_GL_SAME(GLI_CF_RG, GL_RG);
GLI_GL_SAME(GLI_CF_RED, GL_RED);
GLI_GL_SAME(GLI_CF_DEPTH, GL_DEPTH_COMPONENT);
GLI_GL_SAME(GLI_ICF_RGB8, GL_RGB8);
GLI_GL_SAME(GLI_ICF_RGBA8, GL_RGBA8);
GLI_GL_SAME(GLI_ICF_RGBA4, GL_RGBA4);
GLI_GL_SAME(GLI_ICF_RGB5_A1, GL_RGB5_A1);
GLI_GL_SAME(GLI_ICF_RG, GL_RG8);
GLI_GL_SAME(GLI_ICF_R16F, GL_R16F);
GLI_GL_SAME(GLI_ICF_DEPTH, GL_DEPTH_COMPONENT24);
GLI_GL_SAME(GLI_ICF_RG32F, GL_RG32F);
GLI_GL_SAME(GLI_CF_RED_INTEGER, GL_RED_INTEGER);
GLI_GL_SAME(GLI_ICF_RGB, GL_RGB);
GLI_GL_SAME(GLI_ICF_RGBA, GL_RGBA);
GLI_GL_SAME(GLI_ICF_R8, GL_R8);
GLI_GL_SAME(GLI_ICF_R16UI, GL_R16UI);
GLI_GL_SAME(GLI_ICF_R32F, GL_R32F);
GLI_GL_SAME(GLI_ICF_RGBA32F, GL_RGBA32F);
GLI_GL_SAME(GLI_ICF_DEPTH_COMPONENT, GL_DEPTH_COMPONENT);
GLI_GL_SAME(GLI_ICF_DEPTH16, GL_DEPTH_COMPONENT16);
GLI_GL_SAME(GLI_DT_UNSIGNED_BYTE, GL_UNSIGNED_BYTE);
GLI_GL_SAME(GLI_DT_UNSIGNED_SHORT, GL_UNSIGNED_SHORT);
GLI_GL_SAME(GLI_DT_UNSIGNED_INT, GL_UNSIGNED_INT);
GLI_GL_SAME(GLI_DT_FLOAT, GL_FLOAT);
GLI_GL_SAME(GLI_DT_HALF_FLOAT, GL_HALF_FLOAT);
GLI_GL_SAME(GLI_DT_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT_5_6_5);
GLI_GL_SAME(GLI_DT_UNSIGNED_SHORT_5_5_5_1, GL_UNSIGNED_SHORT_5_5_5_1);
GLI_GL_SAME(GLI_DT_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_SHORT_4_4_4_4);
GLI_GL_SAME(GLI_TT_TEXTURE_2D, GL_TEXTURE_2D);
GLI_GL_SAME(GLI_TT_TEXTURE_2D_MULTISAMPLE, GL_TEXTURE_2D_MULTISAMPLE);
GLI_GL_SAME(GLI_TT_TEXTURE_EXTERNAL, GL_TEXTURE_EXTERNAL_OES);
GLI_GL_SAME(GLI_TT_RENDERBUFFER, GL_RENDERBUFFER);
GLI_GL_SAME(GLI_BT_FRAMEBUFFER, GL_FRAMEBUFFER);
GLI_GL_SAME(GLI_BT_DRAW_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER);
GLI_GL_SAME(GLI_BT_READ_FRAMEBUFFER, GL_READ_FRAMEBUFFER);
GLI_GL_SAME(GLI_BA_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0);
GLI_GL_SAME(GLI_BA_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT1);
GLI_GL_SAME(GLI_BA_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT2);
GLI_GL_SAME(GLI_BA_DEPTH_ATTACHMENT, GL_DEPTH_ATTACHMENT);
GLI_GL_SAME(GLI_EN_BLEND, GL_BLEND);
GLI_GL_SAME(GLI_EN_CULL_FACE, GL_CULL_FACE);
GLI_GL_SAME(GLI_EN_DEPTH_TEST, GL_DEPTH_TEST);
GLI_GL_SAME(GLI_EN_DEPTH_CLAMP, GL_DEPTH_CLAMP);
GLI_GL_SAME(GLI_EN_CLIP_DISTANCE0, GL_CLIP_DISTANCE0);
GLI_GL_SAME(GLI_EN_DITHER, GL_DITHER);
GLI_GL_SAME(GLI_EN_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_FILL);
GLI_GL_SAME(GLI_EN_SCISSOR_TEST, GL_SCISSOR_TEST);
GLI_GL_SAME(GLI_IA_READ_ONLY, GL_READ_ONLY);
GLI_GL_SAME(GLI_IA_WRITE_ONLY, GL_WRITE_ONLY);
GLI_GL_SAME(GLI_IA_READ_WRITE, GL_READ_WRITE);
GLI_GL_SAME(GLI_TP_FILTER_NEAREST, GL_NEAREST);
GLI_GL_SAME(GLI_TP_FILTER_LINEAR, GL_LINEAR);
GLI_GL_SAME(GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST);
GLI_GL_SAME(GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST);
GLI_GL_SAME(GLI_TP_WRAP_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
GLI_GL_SAME(GLI_TP_WRAP_REPEAT, GL_REPEAT);
GLI_GL_SAME(GLI_TP_WRAP_MIRRORED_REPEAT, GL_MIRRORED_REPEAT);
GLI_GL_SAME(GLI_CULLMODE_FRONT, GL_BACK);
GLI_GL_SAME(GLI_CULLMODE_BACK, GL_FRONT);
GLI_GL_SAME(GLI_CULLMODE_FRONT_AND_BACK, GL_FRONT_AND_BACK);
GLI_GL_SAME(GLI_COMPARE_LEQUAL, GL_LEQUAL);
GLI_GL_SAME(GLI_COMPARE_LESS, GL_LESS);
GLI_GL_SAME(GLI_COMPARE_ALWAYS, GL_ALWAYS);
GLI_GL_SAME(GLI_BLEND_ZERO, GL_ZERO);
GLI_GL_SAME(GLI_BLEND_ONE, GL_ONE);
GLI_GL_SAME(GLI_BLEND_SRC_ALPHA, GL_SRC_ALPHA);
GLI_GL_SAME(GLI_BLEND_DST_ALPHA, GL_DST_ALPHA);
GLI_GL_SAME(GLI_BLEND_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
GLI_GL_SAME(GLI_BLEND_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
GLI_GL_SAME(GLI_BLEND_ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
GLI_GL_SAME(GLI_BLEND_SRC1_COLOR, GL_SRC1_COLOR);
GLI_GL_SAME(GLI_BLEND_ONE_MINUS_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
GLI_GL_SAME(GLI_BLEND_SRC1_ALPHA, GL_SRC1_ALPHA);
GLI_GL_SAME(GLI_BLEND_ONE_MINUS_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA);
GLI_GL_SAME(GLI_DM_TRIANGLES, GL_TRIANGLES);
GLI_GL_SAME(GLI_DM_TRIANGLE_STRIP, GL_TRIANGLE_STRIP);
GLI_GL_SAME(GLI_DM_LINES, GL_LINES);
GLI_GL_SAME(GLI_BM_COLOR_BUFFER, GL_COLOR_BUFFER_BIT);
GLI_GL_SAME(GLI_BM_DEPTH_BUFFER, GL_DEPTH_BUFFER_BIT);
GLI_GL_SAME(GLI_BM_STENCIL_BUFFER, GL_STENCIL_BUFFER_BIT);

/* graphics::ClampMode's */
enum { GLI_CLAMP_CLIPPING, GLI_CLAMP_NO_NEAR_PLANE, GLI_CLAMP_NONE };

/* the programs first: the table's program entries take the context as
 * its gli_glsl_programs (GLSL/glsl_Programs.h) */
typedef struct gl_context
{
	gli_glsl_programs programs;
	gli_gl_info info;
	u32 clampMode;
	gli_gl_cache cache;
	bool cacheMade;
	gli_gl_textures textures;
	gli_gl_buffer_objects bufferObjects;
	gli_fb_formats fbTexFormats;
	gli_gl_drawer *drawer;
} gl_context;

#define CTX(_p) ((gl_context *)(_p))
#define CCTX(_p) ((const gl_context *)(_p))

static bool builder_obsolete(void *_p)
{
	gl_context *c = CTX(_p);
	return c->programs.builder == NULL || gli_glsl_builder_obsolete(c->programs.builder);
}

/* new programs for options that have changed, made before the old go */
static void reset_builder(void *_p)
{
	gl_context *c = CTX(_p);
	gli_glsl_builder *builder;
	gli_glsl_special *special;
	if (!builder_obsolete(_p))
		return;
	builder = gli_glsl_builder_new(&c->info, &c->cache.useProgram);
	if (c->programs.builder != NULL)
		gli_glsl_builder_delete(c->programs.builder);
	c->programs.builder = builder;
	special = gli_glsl_special_new(&c->info, &c->cache.useProgram, c->programs.builder);
	if (c->programs.special != NULL)
		gli_glsl_special_delete(c->programs.special);
	c->programs.special = special;
}

static void free_impl(void *_p)
{
	gl_context *c = CTX(_p);
	if (c->programs.builder != NULL)
		gli_glsl_builder_delete(c->programs.builder);
	if (c->programs.special != NULL)
		gli_glsl_special_delete(c->programs.special);
	free(c);
}

static void init(void *_p)
{
	gl_context *c = CTX(_p);
	c->clampMode = GLI_CLAMP_CLIPPING;
	gli_gl_info_init(&c->info);

	/* GLES2 knows only GL_FRAMEBUFFER */
	if (c->info.isGLES2)
		gli_ctx.drawFramebuffer = gli_ctx.readFramebuffer = GLI_BT_FRAMEBUFFER;

	if (!c->cacheMade) {
		gli_gl_cache_init(&c->cache, &c->info);
		c->cacheMade = 1;
	}

	gli_gl_textures_init(&c->textures, &c->info, &c->cache.bindTexture);

	{
		static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
		c->fbTexFormats = unset;
	}
	gli_gl_fb_formats(&c->info, &c->fbTexFormats);
	c->bufferObjects.info = &c->info;
	c->bufferObjects.bindFramebuffer = &c->cache.bindFramebuffer;
	c->bufferObjects.bindRenderbuffer = &c->cache.bindRenderbuffer;
	c->bufferObjects.enableScissor = &c->cache.enables[GLI_GL_ENABLE_SCISSOR_TEST];

	if ((c->info.isGLESX && (c->info.bufferStorage && c->info.majorVersion * 10 + c->info.minorVersion >= 32)) || !c->info.isGLESX)
		c->drawer = gli_gl_drawer_new_buffered(&c->info, &c->cache.attribs, &c->cache.bindBuffer);
	else
		c->drawer = gli_gl_drawer_new_unbuffered(&c->info, &c->cache.attribs);

	reset_builder(_p);
}

static void destroy(void *_p)
{
	gl_context *c = CTX(_p);
	gli_gl_textures_free(&c->textures);

	if (c->drawer != NULL)
		c->drawer->ops->destroy(c->drawer);
	c->drawer = NULL;
	if (c->programs.builder != NULL)
		gli_glsl_builder_delete(c->programs.builder);
	c->programs.builder = NULL;

	c->cacheMade = 0;
}

static void set_clamp_mode(void *_p, u32 _mode)
{
	gl_context *c = CTX(_p);
	if (!c->info.isGLESX) {
		switch (_mode) {
		case GLI_CLAMP_CLIPPING:
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP], 0);
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_CLIP_DISTANCE0], 0);
			break;
		case GLI_CLAMP_NO_NEAR_PLANE:
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP], 1);
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_CLIP_DISTANCE0], 1);
			break;
		case GLI_CLAMP_NONE:
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP], 1);
			gli_gl_enable_set(&c->cache.enables[GLI_GL_ENABLE_CLIP_DISTANCE0], 0);
			break;
		}
	}
	c->clampMode = _mode;
}

static u32 get_clamp_mode(void *_p)
{
	return CTX(_p)->clampMode;
}

static void enable(void *_p, u32 _parameter, bool _enable)
{
	gli_gl_enable_set(gli_gl_cache_enable_of(&CTX(_p)->cache, _parameter), _enable);
}

static u32 is_enabled(void *_p, u32 _parameter)
{
	return gli_gl_cache_enable_of(&CTX(_p)->cache, _parameter)->state;
}

static void cull_face(void *_p, u32 _mode)
{
	gli_gl_cache_cull_face(&CTX(_p)->cache, _mode);
}

static void enable_depth_write(void *_p, bool _enable)
{
	gli_gl_cache_depth_mask(&CTX(_p)->cache, _enable);
}

static void set_depth_compare(void *_p, u32 _mode)
{
	gli_gl_cache_depth_compare(&CTX(_p)->cache, _mode);
}

static void set_viewport(void *_p, s32 _x, s32 _y, s32 _width, s32 _height)
{
	gli_gl_cache_viewport(&CTX(_p)->cache, _x, _y, _width, _height);
}

static void set_scissor(void *_p, s32 _x, s32 _y, s32 _width, s32 _height)
{
	gli_gl_cache_scissor(&CTX(_p)->cache, _x, _y, _width, _height);
}

static void set_blending(void *_p, u32 _sfactor, u32 _dfactor)
{
	gli_gl_cache_blending(&CTX(_p)->cache, _sfactor, _dfactor);
}

static void set_blending_separate(void *_p, u32 _sfactorcolor, u32 _dfactorcolor, u32 _sfactoralpha, u32 _dfactoralpha)
{
	gli_gl_cache_blending_separate(&CTX(_p)->cache, _sfactorcolor, _dfactorcolor, _sfactoralpha, _dfactoralpha);
}

static void set_blend_color(void *_p, f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	gli_gl_cache_blend_color(&CTX(_p)->cache, _red, _green, _blue, _alpha);
}

static void clear_color_buffer(void *_p, f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	gl_context *c = CTX(_p);
	gli_gl_enable *scissor = &c->cache.enables[GLI_GL_ENABLE_SCISSOR_TEST];
	gli_gl_enable_set(scissor, 0);

	if (c->info.isGLES2) {
		gli_gl_cache_clear_color(&c->cache, _red, _green, _blue, _alpha);
		glClear(GL_COLOR_BUFFER_BIT);
	} else {
		GLfloat values[4];
		values[0] = _red;
		values[1] = _green;
		values[2] = _blue;
		values[3] = _alpha;
		glClearBufferfv(GL_COLOR, 0, values);
	}

	gli_gl_enable_set(scissor, 1);
}

static void clear_depth_buffer(void *_p)
{
	gl_context *c = CTX(_p);
	gli_gl_enable *scissor = &c->cache.enables[GLI_GL_ENABLE_SCISSOR_TEST];
	gli_gl_enable_set(scissor, 0);

	if (c->info.renderer == GLI_GL_POWERVR) {
		gli_gl_cache_depth_mask(&c->cache, 0);
		glClear(GL_DEPTH_BUFFER_BIT);
	}

	gli_gl_cache_depth_mask(&c->cache, 1);
	glClear(GL_DEPTH_BUFFER_BIT);

	gli_gl_enable_set(scissor, 1);
}

static void set_polygon_offset(void *_p, f32 _factor, f32 _units)
{
	(void)_p;
	glPolygonOffset(_factor, _units);
}

/*---------------Textures-------------*/

static u32 create_texture(void *_p, u32 _target)
{
	return gli_gl_create_texture(&CTX(_p)->textures, _target);
}

static void delete_texture(void *_p, u32 _name)
{
	gli_gl_delete_texture(&CTX(_p)->textures, _name);
}

static void init_2d_texture(void *_p, const gli_init_texture_params *_params)
{
	gli_gl_init_texture(&CTX(_p)->textures, _params);
}

static void update_2d_texture(void *_p, const gli_update_texture_params *_params)
{
	gli_gl_update_texture(&CTX(_p)->textures, _params);
}

static void *lend_texture_data(void *_p, size_t _bytes)
{
	(void)_p;
	(void)_bytes;
	return NULL;
}

static void set_texture_parameters(void *_p, const gli_tex_parameters *_params)
{
	gli_gl_set_texture_parameters(&CTX(_p)->textures, _params);
}

static void bind_texture(void *_p, const gli_bind_texture_params *_params)
{
	gli_gl_cache_bind_texture(&CTX(_p)->cache.bindTexture, _params->textureUnitIndex, _params->target, _params->texture);
}

static void set_texture_unpack_alignment(void *_p, s32 _param)
{
	gli_gl_cache_unpack_alignment(&CTX(_p)->cache, _param);
}

static s32 get_texture_unpack_alignment(const void *_p)
{
	GLint unpackAlignment;
	(void)_p;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
	return unpackAlignment;
}

static s32 get_max_texture_size(const void *_p)
{
	GLint maxTextureSize;
	(void)_p;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	return maxTextureSize;
}

static f32 get_max_anisotropy(const void *_p)
{
	GLfloat maxAniso = 0.0f;
	if (CCTX(_p)->info.anisotropic_filtering)
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	return maxAniso;
}

static void bind_image_texture(void *_p, const gli_bind_image_texture_params *_params)
{
	(void)_p;
	glBindImageTexture(_params->imageUnit, _params->texture, 0, GL_FALSE, 0, (GLenum)_params->accessMode,
		(GLenum)_params->textureFormat);
}

static u32 convert_internal_texture_format(const void *_p, u32 _format)
{
	if (!CCTX(_p)->info.isGLES2)
		return _format;

	switch (_format) {
	case GL_RGB8:
		return GL_RGB;
	case GL_RGBA8:
	case GL_RGBA4:
	case GL_RGB5_A1:
		return GL_RGBA;
	}

	return _format;
}

static void texture_barrier(void *_p)
{
	gl_context *c = CTX(_p);
	if (c->info.texture_barrier)
		glTextureBarrier();
	else if (c->info.texture_barrierNV)
		glTextureBarrierNV();
}

/*---------------Frame buffers-------------*/

static void get_framebuffer_texture_formats(void *_p, gli_fb_formats *_formats)
{
	*_formats = CTX(_p)->fbTexFormats;
}

static u32 create_framebuffer(void *_p)
{
	return gli_gl_create_framebuffer(&CTX(_p)->bufferObjects);
}

static void delete_framebuffer(void *_p, u32 _name)
{
	GLuint fbo = _name;
	if (fbo != 0) {
		glDeleteFramebuffers(1, &fbo);
		gli_gl_cache2_reset_name(&CTX(_p)->cache.bindFramebuffer);
	}
}

static void bind_framebuffer(void *_p, u32 _target, u32 _name)
{
	gl_context *c = CTX(_p);
	if (c->info.renderer == GLI_GL_VIDEOCORE) {
		gli_gl_cache_depth_mask(&c->cache, 1);
		glClear(GL_DEPTH_BUFFER_BIT);
	}
	gli_gl_cache_bind_framebuffer(&c->cache.bindFramebuffer, _target, _name);
}

static void add_render_target(void *_p, const gli_fb_render_target *_params)
{
	gli_gl_add_render_target(&CTX(_p)->bufferObjects, _params);
}

static u32 create_renderbuffer(void *_p)
{
	(void)_p;
	return gli_gl_create_renderbuffer();
}

static void delete_renderbuffer(void *_p, u32 _name)
{
	GLuint rbo = _name;
	if (rbo != 0) {
		glDeleteRenderbuffers(1, &rbo);
		gli_gl_cache2_reset_name(&CTX(_p)->cache.bindRenderbuffer);
	}
}

static void init_renderbuffer(void *_p, const gli_init_renderbuffer_params *_params)
{
	gli_gl_init_renderbuffer(&CTX(_p)->bufferObjects, _params);
}

static bool blit_framebuffers(void *_p, const gli_blit_params *_params)
{
	return gli_gl_blit_framebuffers(&CTX(_p)->bufferObjects, _params);
}

static void set_draw_buffers(void *_p, u32 _num)
{
	static const GLenum targets[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
	(void)_p;
	glDrawBuffers(_num, targets);
}

static struct gli_prb *create_pixel_read_buffer(void *_p, size_t _bytes)
{
	gl_context *c = CTX(_p);
	if (c->info.isGLES2)
		return NULL;
	return gli_gl_prb_new(&c->cache.bindBuffer, _bytes);
}

static void *create_color_buffer_reader(void *_p, void *_texture)
{
	gl_context *c = CTX(_p);
	struct gli_tex *texture = (struct gli_tex *)_texture;
	if (c->info.bufferStorage && c->info.renderer != GLI_GL_INTEL)
		return gli_gl_cbr_new_buffer_storage(texture, &c->cache.bindBuffer);

	if (!c->info.isGLES2)
		return gli_gl_cbr_new_pixel_buffer(texture, &c->cache.bindBuffer);

	return gli_gl_cbr_new_read_pixels(texture);
}

/*---------------Shaders-------------*/

static bool is_uber_only(void *_p)
{
	gl_context *c = CTX(_p);
	return c->programs.builder != NULL && gli_glsl_builder_uber_only(c->programs.builder);
}

static bool save_shaders_storage(void *_p, const struct gli_combiner_table *_combiners)
{
	gl_context *c = CTX(_p);
	return gli_glsl_save_storage(&c->info, &c->cache.useProgram, _combiners);
}

static bool load_shaders_storage(void *_p, struct gli_combiner_table *_combiners)
{
	gl_context *c = CTX(_p);
	return gli_glsl_load_storage(&c->info, &c->cache.useProgram, _combiners);
}

static void get_combiner_journal_path(void *_p, char *_path, size_t _size, u32 *_version)
{
	gl_context *c = CTX(_p);
	gli_glsl_journal_path(&c->info, &c->cache.useProgram, _path, _size, _version);
}

static void reset_shader_program(void *_p)
{
	gli_gl_use_program(&CTX(_p)->cache.useProgram, 0);
}

/*---------------Drawing-------------*/

static void draw_triangles(void *_p, const gli_draw_triangles_params *_params)
{
	gli_gl_drawer *d = CTX(_p)->drawer;
	d->ops->draw_triangles(d, _params);
}

static void draw_rects(void *_p, const gli_draw_rects_params *_params)
{
	gli_gl_drawer *d = CTX(_p)->drawer;
	d->ops->draw_rects(d, _params);
}

static void draw_line(void *_p, f32 _width, void *_vertices)
{
	gli_gl_drawer *d = CTX(_p)->drawer;
	d->ops->draw_line(d, _width, (struct SPVertex *)_vertices);
}

static f32 get_max_line_width(void *_p)
{
	GLfloat lineWidthRange[2];
	(void)_p;
	lineWidthRange[0] = lineWidthRange[1] = 0.0f;
	glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineWidthRange);
	return lineWidthRange[1];
}

static s32 get_max_msaa_level(void *_p)
{
	GLint maxSamples = 0;
	if (CTX(_p)->info.msaa)
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
	return maxSamples;
}

static bool is_supported(const void *_p, u32 _feature)
{
	const gli_gl_info *info = &CCTX(_p)->info;
	switch (_feature) {
	case GLI_FEATURE_BLIT_FRAMEBUFFER:
		return !info->isGLES2;
	case GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER:
		return info->isGLESX;
	case GLI_FEATURE_MULTISAMPLING:
		return info->msaa;
	case GLI_FEATURE_IMAGE_TEXTURES:
		return info->imageTextures;
	case GLI_FEATURE_SHADER_PROGRAM_BINARY:
		return info->shaderStorage;
	case GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES:
		return info->depthTexture;
	case GLI_FEATURE_INTEGER_TEXTURES:
		return !info->isGLES2;
	case GLI_FEATURE_CLIP_CONTROL:
		return !info->isGLESX;
	case GLI_FEATURE_FRAMEBUFFER_FETCH:
		return info->ext_fetch;
	case GLI_FEATURE_TEXTURE_BARRIER:
		return info->texture_barrier || info->texture_barrierNV;
	case GLI_FEATURE_DUAL_SOURCE_BLENDING:
		return info->dual_source_blending;
	}
	return 0;
}

static bool is_error(const void *_p)
{
	(void)_p;
	return gli_gl_has_error() != 0;
}

static bool is_framebuffer_error(const void *_p)
{
	(void)_p;
	return gli_gl_framebuffer_incomplete() != 0;
}

/* in gli_context_ops's order */
static const gli_context_ops gl_ops = {
	free_impl, init, destroy, set_clamp_mode, get_clamp_mode, enable, is_enabled, cull_face,
	enable_depth_write, set_depth_compare, set_viewport, set_scissor, set_blending, set_blending_separate,
	set_blend_color, clear_color_buffer, clear_depth_buffer, set_polygon_offset,
	create_texture, delete_texture, init_2d_texture, update_2d_texture, lend_texture_data,
	set_texture_parameters, bind_texture, set_texture_unpack_alignment, get_texture_unpack_alignment,
	get_max_texture_size, get_max_anisotropy, bind_image_texture, convert_internal_texture_format, texture_barrier,
	get_framebuffer_texture_formats, create_framebuffer, delete_framebuffer, bind_framebuffer,
	add_render_target, create_renderbuffer, delete_renderbuffer, init_renderbuffer, blit_framebuffers, set_draw_buffers,
	create_pixel_read_buffer, create_color_buffer_reader,
	builder_obsolete, is_uber_only, reset_builder,
	gli_glsl_ops_build, gli_glsl_ops_build_async, save_shaders_storage, load_shaders_storage,
	get_combiner_journal_path, gli_glsl_ops_depth_fog_shader, gli_glsl_ops_texrect_draw_shader,
	gli_glsl_ops_texrect_clear_shader, gli_glsl_ops_texrect_copy_shader, gli_glsl_ops_gamma_correction_shader,
	gli_glsl_ops_fxaa_shader,
	reset_shader_program,
	draw_triangles, draw_rects, draw_line, get_max_line_width, get_max_msaa_level, is_supported, is_error,
	is_framebuffer_error
};

const gli_context_ops *gli_gl_context_ops(void)
{
	return &gl_ops;
}

void *gli_gl_context_new(void)
{
	gl_context *c = (gl_context *)calloc(1, sizeof(*c));
	if (c != NULL)
		c->clampMode = GLI_CLAMP_CLIPPING;
	return c;
}
