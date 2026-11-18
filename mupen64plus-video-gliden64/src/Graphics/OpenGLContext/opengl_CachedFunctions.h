#ifndef GLI_GL_CACHED_FUNCTIONS_H
#define GLI_GL_CACHED_FUNCTIONS_H

/* The GL state the context last set, so a call that changes nothing is
 * not made: enables, binds, the depth, blend and clear state, the
 * viewport and scissor, the program and the unpack alignment. C89;
 * inline, for the context's calls to have them in place. A value starts,
 * and is reset to, GLI_PARAM_INVALID, a name 0, as the C++ caches had it. */

#include <string.h>
#include <retro_inline.h>
#include <Graphics/gli_context.h>
#include "GLFunctions.h"
#include "opengl_GLInfo.h"
#include "opengl_Attributes.h"
#include "opengl_AttribArrays.h"

#ifdef __cplusplus
extern "C" {
#endif

/* glsm holds the state across frames (glsmsym.h) */
void rglEnablei(GLenum cap, GLuint index);
void rglDisablei(GLenum cap, GLuint index);

typedef struct gli_gl_cache1 { u32 v; } gli_gl_cache1;
typedef struct gli_gl_cache2 { u32 v1, v2; } gli_gl_cache2;
typedef struct gli_gl_cache4 { u32 v1, v2, v3, v4; } gli_gl_cache4;

/* an enable, the GLI_GL_ENABLE_* @index of @cap: @cap GLI_PARAM_INVALID
 * where the context has none, which then calls nothing */
typedef struct gli_gl_enable
{
	u32 cap;
	u32 state;
	u32 index;
} gli_gl_enable;

enum
{
	GLI_GL_ENABLE_BLEND,
	GLI_GL_ENABLE_CULL_FACE,
	GLI_GL_ENABLE_DEPTH_TEST,
	GLI_GL_ENABLE_DEPTH_CLAMP,
	GLI_GL_ENABLE_CLIP_DISTANCE0,
	GLI_GL_ENABLE_DITHER,
	GLI_GL_ENABLE_POLYGON_OFFSET_FILL,
	GLI_GL_ENABLE_SCISSOR_TEST,
	GLI_GL_ENABLE_OTHER,   /* any other cap: held, calls nothing */
	GLI_GL_ENABLES
};

typedef struct gli_gl_cache
{
	gli_gl_enable enables[GLI_GL_ENABLES];
	gli_gl_cache2 bindTexture;       /* unit, name */
	gli_gl_cache2 bindFramebuffer;   /* target, name */
	gli_gl_cache2 bindRenderbuffer;
	gli_gl_cache2 bindBuffer;
	gli_gl_cache1 cullFace;
	gli_gl_cache1 depthMask;
	gli_gl_cache1 depthCompare;
	gli_gl_cache4 viewport;
	gli_gl_cache4 scissor;
	gli_gl_cache2 blending;
	gli_gl_cache4 blendingSeparate;
	gli_gl_cache4 blendColor;        /* the floats' bits */
	gli_gl_cache4 clearColor;
	gli_gl_attribs attribs;
	gli_gl_cache1 useProgram;        /* a name */
	s32 unpackAlignment;
} gli_gl_cache;

static INLINE void gli_gl_cache2_reset_name(gli_gl_cache2 *_c)
{
	_c->v1 = GLI_PARAM_INVALID;
	_c->v2 = 0;
}

static INLINE void gli_gl_cache4_reset(gli_gl_cache4 *_c)
{
	_c->v1 = _c->v2 = _c->v3 = _c->v4 = GLI_PARAM_INVALID;
}

/* every value unknown; GLES has no depth clamp */
static INLINE void gli_gl_cache_init(gli_gl_cache *_cache, const gli_gl_info *_info)
{
	static const u32 caps[GLI_GL_ENABLES] = {
		GLI_EN_BLEND, GLI_EN_CULL_FACE, GLI_EN_DEPTH_TEST, GLI_EN_DEPTH_CLAMP, GLI_EN_CLIP_DISTANCE0,
		GLI_EN_DITHER, GLI_EN_POLYGON_OFFSET_FILL, GLI_EN_SCISSOR_TEST, GLI_PARAM_INVALID
	};
	u32 i;
	for (i = 0; i < GLI_GL_ENABLES; ++i) {
		_cache->enables[i].cap = caps[i];
		_cache->enables[i].state = GLI_PARAM_INVALID;
		_cache->enables[i].index = i;
	}
	if (_info->isGLESX)
		_cache->enables[GLI_GL_ENABLE_DEPTH_CLAMP].cap = GLI_PARAM_INVALID;
	gli_gl_cache2_reset_name(&_cache->bindTexture);
	gli_gl_cache2_reset_name(&_cache->bindFramebuffer);
	gli_gl_cache2_reset_name(&_cache->bindRenderbuffer);
	gli_gl_cache2_reset_name(&_cache->bindBuffer);
	_cache->cullFace.v = GLI_PARAM_INVALID;
	_cache->depthMask.v = GLI_PARAM_INVALID;
	_cache->depthCompare.v = GLI_PARAM_INVALID;
	gli_gl_cache4_reset(&_cache->viewport);
	gli_gl_cache4_reset(&_cache->scissor);
	_cache->blending.v1 = _cache->blending.v2 = GLI_PARAM_INVALID;
	gli_gl_cache4_reset(&_cache->blendingSeparate);
	gli_gl_cache4_reset(&_cache->blendColor);
	gli_gl_cache4_reset(&_cache->clearColor);
	gli_gl_attribs_reset(&_cache->attribs);
	_cache->useProgram.v = 0;
	_cache->unpackAlignment = -1;
}

static INLINE int gli_gl_cache1_update(gli_gl_cache1 *_c, u32 _v)
{
	if (_c->v == _v)
		return 0;
	_c->v = _v;
	return 1;
}

static INLINE int gli_gl_cache2_update(gli_gl_cache2 *_c, u32 _v1, u32 _v2)
{
	if (_c->v1 == _v1 && _c->v2 == _v2)
		return 0;
	_c->v1 = _v1;
	_c->v2 = _v2;
	return 1;
}

static INLINE int gli_gl_cache4_update(gli_gl_cache4 *_c, u32 _v1, u32 _v2, u32 _v3, u32 _v4)
{
	if (_c->v1 == _v1 && _c->v2 == _v2 && _c->v3 == _v3 && _c->v4 == _v4)
		return 0;
	_c->v1 = _v1;
	_c->v2 = _v2;
	_c->v3 = _v3;
	_c->v4 = _v4;
	return 1;
}

static INLINE u32 gli_gl_float_bits(f32 _f)
{
	u32 u;
	memcpy(&u, &_f, sizeof(u));
	return u;
}

/* ---- enables ---- */

static INLINE gli_gl_enable *gli_gl_cache_enable_of(gli_gl_cache *_cache, u32 _cap)
{
	switch (_cap) {
	case GLI_EN_BLEND: return &_cache->enables[GLI_GL_ENABLE_BLEND];
	case GLI_EN_CULL_FACE: return &_cache->enables[GLI_GL_ENABLE_CULL_FACE];
	case GLI_EN_DEPTH_TEST: return &_cache->enables[GLI_GL_ENABLE_DEPTH_TEST];
	case GLI_EN_DEPTH_CLAMP: return &_cache->enables[GLI_GL_ENABLE_DEPTH_CLAMP];
	case GLI_EN_CLIP_DISTANCE0: return &_cache->enables[GLI_GL_ENABLE_CLIP_DISTANCE0];
	case GLI_EN_DITHER: return &_cache->enables[GLI_GL_ENABLE_DITHER];
	case GLI_EN_POLYGON_OFFSET_FILL: return &_cache->enables[GLI_GL_ENABLE_POLYGON_OFFSET_FILL];
	case GLI_EN_SCISSOR_TEST: return &_cache->enables[GLI_GL_ENABLE_SCISSOR_TEST];
	}
	return &_cache->enables[GLI_GL_ENABLE_OTHER];
}

static INLINE void gli_gl_enable_set(gli_gl_enable *_e, int _enable)
{
	const u32 on = _enable ? 1u : 0u;
	if (_e->cap == GLI_PARAM_INVALID || _e->state == on)
		return;
	_e->state = on;
	/* glEnable takes glsm's names (GLFunctions.h): a case a cap */
	if (on) {
		switch (_e->index) {
		case GLI_GL_ENABLE_BLEND:
			/* through glsm, which puts it back when the core's state is
			 * bound again at the next frame: called straight, blending
			 * stayed off while this cache held it on */
			if (IS_GL_FUNCTION_VALID(glEnablei))
				rglEnablei(SGL_BLEND, 0);
			else
				glEnable(GL_BLEND);
			break;
		case GLI_GL_ENABLE_CULL_FACE: glEnable(GL_CULL_FACE); break;
		case GLI_GL_ENABLE_DEPTH_TEST: glEnable(GL_DEPTH_TEST); break;
		case GLI_GL_ENABLE_DEPTH_CLAMP: glEnable(GL_DEPTH_CLAMP); break;
		case GLI_GL_ENABLE_CLIP_DISTANCE0: glEnable(GL_CLIP_DISTANCE0); break;
		case GLI_GL_ENABLE_DITHER: glEnable(GL_DITHER); break;
		case GLI_GL_ENABLE_POLYGON_OFFSET_FILL: glEnable(GL_POLYGON_OFFSET_FILL); break;
		case GLI_GL_ENABLE_SCISSOR_TEST: glEnable(GL_SCISSOR_TEST); break;
		}
	} else {
		switch (_e->index) {
		case GLI_GL_ENABLE_BLEND:
			if (IS_GL_FUNCTION_VALID(glDisablei))
				rglDisablei(SGL_BLEND, 0);
			else
				glDisable(GL_BLEND);
			break;
		case GLI_GL_ENABLE_CULL_FACE: glDisable(GL_CULL_FACE); break;
		case GLI_GL_ENABLE_DEPTH_TEST: glDisable(GL_DEPTH_TEST); break;
		case GLI_GL_ENABLE_DEPTH_CLAMP: glDisable(GL_DEPTH_CLAMP); break;
		case GLI_GL_ENABLE_CLIP_DISTANCE0: glDisable(GL_CLIP_DISTANCE0); break;
		case GLI_GL_ENABLE_DITHER: glDisable(GL_DITHER); break;
		case GLI_GL_ENABLE_POLYGON_OFFSET_FILL: glDisable(GL_POLYGON_OFFSET_FILL); break;
		case GLI_GL_ENABLE_SCISSOR_TEST: glDisable(GL_SCISSOR_TEST); break;
		}
	}
}

/* ---- binds ---- */

static INLINE void gli_gl_cache_bind_texture(gli_gl_cache2 *_c, u32 _unit, u32 _target, u32 _name)
{
	if (gli_gl_cache2_update(_c, _unit, _name)) {
		glActiveTexture(GL_TEXTURE0 + _unit);
		glBindTexture((GLenum)_target, _name);
	}
}

static INLINE void gli_gl_cache_bind_framebuffer(gli_gl_cache2 *_c, u32 _target, u32 _name)
{
	if (gli_gl_cache2_update(_c, _target, _name))
		glBindFramebuffer((GLenum)_target, _name);
}

static INLINE void gli_gl_cache_bind_renderbuffer(gli_gl_cache2 *_c, u32 _target, u32 _name)
{
	if (gli_gl_cache2_update(_c, _target, _name))
		glBindRenderbuffer((GLenum)_target, _name);
}

static INLINE void gli_gl_cache_bind_buffer(gli_gl_cache2 *_c, u32 _target, u32 _name)
{
	if (gli_gl_cache2_update(_c, _target, _name))
		glBindBuffer((GLenum)_target, _name);
}

/* ---- the rest ---- */

static INLINE void gli_gl_cache_cull_face(gli_gl_cache *_cache, u32 _mode)
{
	if (gli_gl_cache1_update(&_cache->cullFace, _mode))
		glCullFace((GLenum)_mode);
}

static INLINE void gli_gl_cache_depth_mask(gli_gl_cache *_cache, int _enable)
{
	if (gli_gl_cache1_update(&_cache->depthMask, _enable ? 1u : 0u))
		glDepthMask(_enable ? GL_TRUE : GL_FALSE);
}

static INLINE void gli_gl_cache_depth_compare(gli_gl_cache *_cache, u32 _mode)
{
	if (gli_gl_cache1_update(&_cache->depthCompare, _mode))
		glDepthFunc((GLenum)_mode);
}

static INLINE void gli_gl_cache_viewport(gli_gl_cache *_cache, s32 _x, s32 _y, s32 _width, s32 _height)
{
	if (gli_gl_cache4_update(&_cache->viewport, (u32)_x, (u32)_y, (u32)_width, (u32)_height))
		glViewport(_x, _y, _width, _height);
}

static INLINE void gli_gl_cache_scissor(gli_gl_cache *_cache, s32 _x, s32 _y, s32 _width, s32 _height)
{
	if (gli_gl_cache4_update(&_cache->scissor, (u32)_x, (u32)_y, (u32)_width, (u32)_height))
		glScissor(_x, _y, _width, _height);
}

static INLINE void gli_gl_cache_blending(gli_gl_cache *_cache, u32 _sfactor, u32 _dfactor)
{
	if (gli_gl_cache2_update(&_cache->blending, _sfactor, _dfactor))
		glBlendFunc((GLenum)_sfactor, (GLenum)_dfactor);
}

static INLINE void gli_gl_cache_blending_separate(gli_gl_cache *_cache, u32 _sfactorcolor, u32 _dfactorcolor,
	u32 _sfactoralpha, u32 _dfactoralpha)
{
	if (gli_gl_cache4_update(&_cache->blendingSeparate, _sfactorcolor, _dfactorcolor, _sfactoralpha, _dfactoralpha))
		glBlendFuncSeparate((GLenum)_sfactorcolor, (GLenum)_dfactorcolor, (GLenum)_sfactoralpha, (GLenum)_dfactoralpha);
}

static INLINE void gli_gl_cache_blend_color(gli_gl_cache *_cache, f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	if (gli_gl_cache4_update(&_cache->blendColor, gli_gl_float_bits(_red), gli_gl_float_bits(_green),
			gli_gl_float_bits(_blue), gli_gl_float_bits(_alpha)))
		glBlendColor(_red, _green, _blue, _alpha);
}

static INLINE void gli_gl_cache_clear_color(gli_gl_cache *_cache, f32 _red, f32 _green, f32 _blue, f32 _alpha)
{
	if (gli_gl_cache4_update(&_cache->clearColor, gli_gl_float_bits(_red), gli_gl_float_bits(_green),
			gli_gl_float_bits(_blue), gli_gl_float_bits(_alpha)))
		glClearColor(_red, _green, _blue, _alpha);
}

static INLINE void gli_gl_use_program(gli_gl_cache1 *_c, u32 _program)
{
	if (gli_gl_cache1_update(_c, _program))
		glUseProgram(_program);
}

static INLINE void gli_gl_cache_unpack_alignment(gli_gl_cache *_cache, s32 _param)
{
	if (_cache->unpackAlignment == _param)
		return;
	_cache->unpackAlignment = _param;
	glPixelStorei(GL_UNPACK_ALIGNMENT, _param);
}

#ifdef __cplusplus
}
#endif

#endif
