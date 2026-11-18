/* test_gliden64_gl_cache -- the GL state GLideN64's context caches
 * (Graphics/OpenGLContext/opengl_CachedFunctions.h and .c) on a stub GL.
 *
 * Each value starts unknown, so its first set calls GL, and a set that
 * changes nothing calls nothing: enables, binds, cull face, depth mask and
 * compare, viewport, scissor, the blend functions and colour, the clear
 * colour (floats by their bits, so -0 is not 0), the program (which
 * starts as 0, as the C++ cache had it) and the unpack alignment (as -1).
 * An enable goes to glsm by its name; blending through glEnablei where GL
 * has it; GLES has no depth clamp, which then calls nothing; a cap the
 * cache does not know calls nothing. A texture bind is kept by unit and
 * name, not target; a reset bind is made again. The C entries the C
 * modules call pass through to the same caches.
 *
 *   ./test_gliden64_gl_cache
 */
#include <stdio.h>
#include <string.h>

#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/opengl_ColorBufferReader.h"
#include "Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.h"
#include "Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- GL: every call counted, the last one's arguments kept ---- */

static u32 calls;
static u32 last[4];
static f32 lastf[4];
static const char *lastName = "";

static void called(const char *_name, u32 _a, u32 _b, u32 _c, u32 _d)
{
	calls++;
	lastName = _name;
	last[0] = _a; last[1] = _b; last[2] = _c; last[3] = _d;
}

static void APIENTRY s_enable(GLenum _c) { called("enable", _c, 0, 0, 0); }
static void APIENTRY s_disable(GLenum _c) { called("disable", _c, 0, 0, 0); }
static void APIENTRY s_enablei(GLenum _c, GLuint _i) { called("enablei!", _c, _i, 0, 0); }
static void APIENTRY s_disablei(GLenum _c, GLuint _i) { called("disablei!", _c, _i, 0, 0); }
void rglEnablei(GLenum cap, GLuint index) { called("rglEnablei", cap, index, 0, 0); }
void rglDisablei(GLenum cap, GLuint index) { called("rglDisablei", cap, index, 0, 0); }
static GLenum activeUnit;
static void APIENTRY s_active(GLenum _t) { activeUnit = _t; called("active", _t, 0, 0, 0); }
static u32 activeThenBind;
static void APIENTRY s_bind_texture(GLenum _t, GLuint _n)
{
	activeThenBind = strcmp(lastName, "active") == 0;
	called("bindTexture", _t, _n, 0, 0);
}
static void APIENTRY s_bind_fb(GLenum _t, GLuint _n) { called("bindFramebuffer", _t, _n, 0, 0); }
static void APIENTRY s_bind_rb(GLenum _t, GLuint _n) { called("bindRenderbuffer", _t, _n, 0, 0); }
static void APIENTRY s_bind_buffer(GLenum _t, GLuint _n) { called("bindBuffer", _t, _n, 0, 0); }
static void APIENTRY s_cull(GLenum _m) { called("cullFace", _m, 0, 0, 0); }
static void APIENTRY s_depth_mask(GLboolean _f) { called("depthMask", _f, 0, 0, 0); }
static void APIENTRY s_depth_func(GLenum _f) { called("depthFunc", _f, 0, 0, 0); }
static void APIENTRY s_viewport(GLint _x, GLint _y, GLsizei _w, GLsizei _h) { called("viewport", (u32)_x, (u32)_y, (u32)_w, (u32)_h); }
static void APIENTRY s_scissor(GLint _x, GLint _y, GLsizei _w, GLsizei _h) { called("scissor", (u32)_x, (u32)_y, (u32)_w, (u32)_h); }
static void APIENTRY s_blend(GLenum _s, GLenum _d) { called("blendFunc", _s, _d, 0, 0); }
static void APIENTRY s_blend_sep(GLenum _a, GLenum _b, GLenum _c, GLenum _d) { called("blendFuncSeparate", _a, _b, _c, _d); }
static void APIENTRY s_blend_color(GLfloat _r, GLfloat _g, GLfloat _b, GLfloat _a)
{
	called("blendColor", 0, 0, 0, 0);
	lastf[0] = _r; lastf[1] = _g; lastf[2] = _b; lastf[3] = _a;
}
static void APIENTRY s_clear_color(GLfloat _r, GLfloat _g, GLfloat _b, GLfloat _a)
{
	called("clearColor", 0, 0, 0, 0);
	lastf[0] = _r; lastf[1] = _g; lastf[2] = _b; lastf[3] = _a;
}
static void APIENTRY s_use(GLuint _p) { called("useProgram", _p, 0, 0, 0); }
static void APIENTRY s_pixel_store(GLenum _p, GLint _v) { called("pixelStorei", _p, (u32)_v, 0, 0); }

PFNGLENABLEPROC g_glEnable = s_enable;
PFNGLDISABLEPROC g_glDisable = s_disable;
PFNGLENABLEIPROC g_glEnablei;
PFNGLDISABLEIPROC g_glDisablei;
PFNGLACTIVETEXTUREPROC g_glActiveTexture = s_active;
PFNGLBINDTEXTUREPROC g_glBindTexture = s_bind_texture;
PFNGLBINDFRAMEBUFFERPROC g_glBindFramebuffer = s_bind_fb;
PFNGLBINDRENDERBUFFERPROC g_glBindRenderbuffer = s_bind_rb;
PFNGLBINDBUFFERPROC g_glBindBuffer = s_bind_buffer;
PFNGLCULLFACEPROC g_glCullFace = s_cull;
PFNGLDEPTHMASKPROC g_glDepthMask = s_depth_mask;
PFNGLDEPTHFUNCPROC g_glDepthFunc = s_depth_func;
PFNGLVIEWPORTPROC g_glViewport = s_viewport;
PFNGLSCISSORPROC g_glScissor = s_scissor;
PFNGLBLENDFUNCPROC g_glBlendFunc = s_blend;
PFNGLBLENDFUNCSEPARATEPROC g_glBlendFuncSeparate = s_blend_sep;
PFNGLBLENDCOLORPROC g_glBlendColor = s_blend_color;
PFNGLCLEARCOLORPROC g_glClearColor = s_clear_color;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;
PFNGLPIXELSTOREIPROC g_glPixelStorei = s_pixel_store;

static gli_gl_cache cache;
static gli_gl_info info;

/* a set: whether it called GL, once, by @_name */
static int set_calls(u32 _before, const char *_name)
{
	return calls == _before + 1 && strcmp(lastName, _name) == 0;
}

static void test_enables(void)
{
	static const u32 caps[8] = {
		GLI_EN_BLEND, GLI_EN_CULL_FACE, GLI_EN_DEPTH_TEST, GLI_EN_DEPTH_CLAMP, GLI_EN_CLIP_DISTANCE0,
		GLI_EN_DITHER, GLI_EN_POLYGON_OFFSET_FILL, GLI_EN_SCISSOR_TEST
	};
	static const u32 sgl[8] = {
		SGL_BLEND, SGL_CULL_FACE, SGL_DEPTH_TEST, SGL_DEPTH_CLAMP, SGL_CLIP_DISTANCE0,
		SGL_DITHER, SGL_POLYGON_OFFSET_FILL, SGL_SCISSOR_TEST
	};
	u32 i, c;
	memset(&info, 0, sizeof(info));
	gli_gl_cache_init(&cache, &info);

	for (i = 0; i < 8; ++i) {
		gli_gl_enable *e = gli_gl_cache_enable_of(&cache, caps[i]);
		check(e == &cache.enables[i], "each cap its own enable");
		check(e->state == GLI_PARAM_INVALID, "unknown to start");
		c = calls;
		gli_gl_enable_set(e, 1);
		check(set_calls(c, "enable") && last[0] == sgl[i], "on: glEnable by glsm's name");
		check(e->state == 1, "held on");
		c = calls;
		gli_gl_enable_set(e, 1);
		check(calls == c, "on again: nothing");
		gli_gl_enable_set(e, 0);
		check(set_calls(c, "disable") && last[0] == sgl[i] && e->state == 0, "off: glDisable");
		c = calls;
		gli_gl_enable_set(e, 0);
		check(calls == c, "off again: nothing");
	}
	/* a first off is made too */
	gli_gl_cache_init(&cache, &info);
	c = calls;
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_DITHER], 0);
	check(set_calls(c, "disable") && last[0] == SGL_DITHER, "a first off made");

	/* blending through glsm's glEnablei where GL has one */
	g_glEnablei = s_enablei;
	g_glDisablei = s_disablei;
	gli_gl_cache_init(&cache, &info);
	c = calls;
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_BLEND], 1);
	check(set_calls(c, "rglEnablei") && last[0] == SGL_BLEND && last[1] == 0, "blending on through glsm, buffer 0");
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_BLEND], 0);
	check(set_calls(c + 1, "rglDisablei") && last[0] == SGL_BLEND && last[1] == 0, "and off");
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_CULL_FACE], 1);
	check(set_calls(c + 2, "enable"), "the rest by glEnable still");
	g_glEnablei = NULL;
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_BLEND], 1);
	check(set_calls(c + 3, "enable") && last[0] == SGL_BLEND, "glEnable without it");
	g_glDisablei = NULL;
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_BLEND], 0);
	check(set_calls(c + 4, "disable") && last[0] == SGL_BLEND, "glDisable without it");

	/* an unknown cap, GLES's depth clamp: nothing */
	c = calls;
	gli_gl_enable_set(gli_gl_cache_enable_of(&cache, 0x1234), 1);
	check(calls == c && gli_gl_cache_enable_of(&cache, 0x1234) == &cache.enables[GLI_GL_ENABLE_OTHER],
		"an unknown cap calls nothing");
	info.isGLESX = 1;
	gli_gl_cache_init(&cache, &info);
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP], 1);
	check(calls == c && cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP].state == GLI_PARAM_INVALID, "GLES has no depth clamp");
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_CLIP_DISTANCE0], 1);
	check(set_calls(c, "enable"), "the rest it has");
	info.isGLESX = 0;
	gli_gl_cache_init(&cache, &info);
	gli_gl_enable_set(&cache.enables[GLI_GL_ENABLE_DEPTH_CLAMP], 1);
	check(set_calls(c + 1, "enable") && last[0] == SGL_DEPTH_CLAMP, "GL has");

	/* the C entry */
	c = calls;
	gli_gl_enable_cached(&cache.enables[GLI_GL_ENABLE_SCISSOR_TEST], 1);
	gli_gl_enable_cached(&cache.enables[GLI_GL_ENABLE_SCISSOR_TEST], 5);
	check(calls == c + 1 && last[0] == SGL_SCISSOR_TEST && cache.enables[GLI_GL_ENABLE_SCISSOR_TEST].state == 1,
		"the C entry, any non-zero on");
	gli_gl_enable_cached(&cache.enables[GLI_GL_ENABLE_SCISSOR_TEST], 0);
	check(set_calls(c + 1, "disable") && cache.enables[GLI_GL_ENABLE_SCISSOR_TEST].state == 0, "and off");
}

static void test_binds(void)
{
	u32 c;
	gli_gl_cache_init(&cache, &info);

	c = calls;
	gli_gl_cache_bind_texture(&cache.bindTexture, 1, GL_TEXTURE_2D, 0);
	check(calls == c + 2 && activeThenBind && strcmp(lastName, "bindTexture") == 0 && last[0] == GL_TEXTURE_2D && last[1] == 0,
		"a first texture bind, its unit made active first, even name 0");
	check(activeUnit == GL_TEXTURE1, "unit 1 as GL_TEXTURE1");
	gli_gl_cache_bind_texture(&cache.bindTexture, 1, GL_TEXTURE_2D, 0);
	check(calls == c + 2, "again: nothing");
	gli_gl_cache_bind_texture(&cache.bindTexture, 1, GL_TEXTURE_2D_MULTISAMPLE, 0);
	check(calls == c + 2, "kept by unit and name, not target");
	gli_gl_cache_bind_texture(&cache.bindTexture, 3, GL_TEXTURE_2D, 0);
	check(calls == c + 4, "another unit");
	gli_gl_cache_bind_texture(&cache.bindTexture, 3, GL_TEXTURE_2D, 9);
	check(calls == c + 6 && last[1] == 9, "another name");
	gli_gl_bind_texture_reset(&cache.bindTexture);
	gli_gl_cache_bind_texture(&cache.bindTexture, 3, GL_TEXTURE_2D, 9);
	check(calls == c + 8, "made again once reset");
	gli_gl_bind_texture(&cache.bindTexture, 3, GL_TEXTURE_2D, 9);
	check(calls == c + 8, "the C entry, the same cache");
	gli_gl_bind_texture(&cache.bindTexture, 2, GL_TEXTURE_2D, 9);
	check(calls == c + 10, "the C entry binds");

	gli_gl_cache_init(&cache, &info);
	c = calls;
	gli_gl_cache_bind_texture(&cache.bindTexture, 0, GL_TEXTURE_2D, 0);
	check(calls == c + 2 && activeUnit == GL_TEXTURE0, "even unit 0, name 0 to start");
	gli_gl_bind_texture_reset(&cache.bindTexture);
	gli_gl_cache_bind_texture(&cache.bindTexture, 0, GL_TEXTURE_2D, 0);
	check(calls == c + 4, "and once reset");

	c = calls;
	gli_gl_cache_bind_framebuffer(&cache.bindFramebuffer, GL_DRAW_FRAMEBUFFER, 0);
	check(set_calls(c, "bindFramebuffer") && last[0] == GL_DRAW_FRAMEBUFFER && last[1] == 0, "a first frame buffer bind");
	gli_gl_bind_framebuffer(&cache.bindFramebuffer, GL_DRAW_FRAMEBUFFER, 0);
	check(calls == c + 1, "again: nothing");
	gli_gl_bind_framebuffer(&cache.bindFramebuffer, GL_READ_FRAMEBUFFER, 0);
	check(set_calls(c + 1, "bindFramebuffer") && last[0] == GL_READ_FRAMEBUFFER, "another target");
	gli_gl_cache2_reset_name(&cache.bindFramebuffer);
	gli_gl_cache_bind_framebuffer(&cache.bindFramebuffer, GL_READ_FRAMEBUFFER, 0);
	check(calls == c + 3, "made again once reset");

	c = calls;
	gli_gl_bind_renderbuffer(&cache.bindRenderbuffer, GL_RENDERBUFFER, 4);
	gli_gl_cache_bind_renderbuffer(&cache.bindRenderbuffer, GL_RENDERBUFFER, 4);
	check(set_calls(c, "bindRenderbuffer") && last[1] == 4, "render buffers");
	gli_gl_cache2_reset_name(&cache.bindRenderbuffer);
	gli_gl_bind_renderbuffer(&cache.bindRenderbuffer, GL_RENDERBUFFER, 4);
	check(calls == c + 2, "made again once reset");

	c = calls;
	gli_gl_bind_buffer(&cache.bindBuffer, GL_PIXEL_PACK_BUFFER, 6);
	gli_gl_cache_bind_buffer(&cache.bindBuffer, GL_PIXEL_PACK_BUFFER, 6);
	check(set_calls(c, "bindBuffer") && last[0] == GL_PIXEL_PACK_BUFFER && last[1] == 6, "buffers");
	gli_gl_bind_buffer(&cache.bindBuffer, GL_ARRAY_BUFFER, 6);
	check(calls == c + 2 && last[0] == GL_ARRAY_BUFFER, "by target too");
}

static void test_state(void)
{
	u32 c;
	gli_gl_cache_init(&cache, &info);

	c = calls;
	gli_gl_cache_cull_face(&cache, GL_BACK);
	gli_gl_cache_cull_face(&cache, GL_BACK);
	check(set_calls(c, "cullFace") && last[0] == GL_BACK, "cull face once");
	gli_gl_cache_cull_face(&cache, GL_FRONT);
	check(calls == c + 2 && last[0] == GL_FRONT, "and when it changes");

	c = calls;
	gli_gl_cache_depth_mask(&cache, 1);
	gli_gl_cache_depth_mask(&cache, 7);
	check(set_calls(c, "depthMask") && last[0] == GL_TRUE, "depth mask, any non-zero true");
	gli_gl_cache_depth_mask(&cache, 0);
	check(calls == c + 2 && last[0] == GL_FALSE, "and false");

	c = calls;
	gli_gl_cache_depth_compare(&cache, GL_LEQUAL);
	gli_gl_cache_depth_compare(&cache, GL_LEQUAL);
	check(set_calls(c, "depthFunc") && last[0] == GL_LEQUAL, "depth compare");

	c = calls;
	gli_gl_cache_viewport(&cache, 1, -2, 3, 4);
	gli_gl_cache_viewport(&cache, 1, -2, 3, 4);
	check(set_calls(c, "viewport") && last[0] == 1 && (s32)last[1] == -2 && last[2] == 3 && last[3] == 4, "viewport");
	gli_gl_cache_viewport(&cache, 1, -2, 3, 5);
	check(calls == c + 2, "by its last value too");
	gli_gl_cache_viewport(&cache, 0, -2, 3, 5);
	check(calls == c + 3, "and its first");

	c = calls;
	gli_gl_cache_scissor(&cache, 5, 6, 7, 8);
	gli_gl_cache_scissor(&cache, 5, 6, 7, 8);
	check(set_calls(c, "scissor") && last[0] == 5 && last[3] == 8, "scissor");
	gli_gl_cache_scissor(&cache, 5, 9, 7, 8);
	gli_gl_cache_scissor(&cache, 5, 9, 10, 8);
	check(calls == c + 3, "by each value");
	gli_gl_cache_viewport(&cache, 20, 21, 22, 23);
	gli_gl_cache_scissor(&cache, 20, 21, 22, 23);
	check(calls == c + 5 && strcmp(lastName, "scissor") == 0, "viewport and scissor held apart");

	c = calls;
	gli_gl_cache_blending(&cache, GL_ONE, GL_ZERO);
	gli_gl_cache_blending(&cache, GL_ONE, GL_ZERO);
	check(set_calls(c, "blendFunc") && last[0] == GL_ONE && last[1] == GL_ZERO, "blend function");
	gli_gl_cache_blending(&cache, GL_ONE, GL_ONE);
	check(calls == c + 2, "its second factor");

	c = calls;
	gli_gl_cache_blending_separate(&cache, 1, 2, 3, 4);
	gli_gl_cache_blending_separate(&cache, 1, 2, 3, 4);
	check(set_calls(c, "blendFuncSeparate") && last[0] == 1 && last[1] == 2 && last[2] == 3 && last[3] == 4, "separate");
	gli_gl_cache_blending_separate(&cache, 1, 2, 5, 4);
	check(calls == c + 2, "its alpha factor");

	c = calls;
	gli_gl_cache_blend_color(&cache, 0.5f, 0.25f, 0.0f, 1.0f);
	gli_gl_cache_blend_color(&cache, 0.5f, 0.25f, 0.0f, 1.0f);
	check(set_calls(c, "blendColor") && lastf[0] == 0.5f && lastf[1] == 0.25f && lastf[3] == 1.0f, "blend colour");
	gli_gl_cache_blend_color(&cache, 0.5f, 0.25f, -0.0f, 1.0f);
	check(calls == c + 2, "by the floats' bits: -0 is not 0");
	gli_gl_cache_blend_color(&cache, 0.5f, 0.25f, -0.0f, 0.5f);
	check(calls == c + 3 && lastf[3] == 0.5f, "its alpha");

	c = calls;
	gli_gl_cache_clear_color(&cache, 0.0f, 0.0f, 0.0f, 0.0f);
	gli_gl_cache_clear_color(&cache, 0.0f, 0.0f, 0.0f, 0.0f);
	check(set_calls(c, "clearColor"), "clear colour, transparent black once");
	gli_gl_cache_clear_color(&cache, 0.0f, 0.0f, 0.0f, 1.0f);
	check(calls == c + 2 && lastf[3] == 1.0f, "its alpha");

	c = calls;
	gli_gl_use_program(&cache.useProgram, 0);
	check(calls == c, "program 0 to start: nothing");
	gli_gl_use_program(&cache.useProgram, 12);
	gli_gl_use_program(&cache.useProgram, 12);
	check(set_calls(c, "useProgram") && last[0] == 12, "a program once");
	gli_gl_use_program(&cache.useProgram, 0);
	check(calls == c + 2 && last[0] == 0, "back to none");

	c = calls;
	gli_gl_cache_unpack_alignment(&cache, 4);
	gli_gl_cache_unpack_alignment(&cache, 4);
	check(set_calls(c, "pixelStorei") && last[0] == GL_UNPACK_ALIGNMENT && last[1] == 4, "unpack alignment");
	gli_gl_cache_unpack_alignment(&cache, 1);
	check(calls == c + 2 && last[1] == 1, "when it changes");
	gli_gl_cache_init(&cache, &info);
	c = calls;
	gli_gl_cache_unpack_alignment(&cache, -1);
	check(calls == c, "-1 to start");
}

int main(void)
{
	test_enables();
	test_binds();
	test_state();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("gl cache: ok\n");
	return 0;
}
