/* test_gliden64_buffer_objects -- GLideN64's GL frame buffers and render
 * buffers (Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.c)
 * on a stub GL.
 *
 * A frame buffer and a render buffer are GL's new names. A render buffer's
 * storage is set with it bound through the render buffer cache; a render
 * target is added with its frame buffer bound through the frame buffer
 * cache, as a render buffer or as level 0 of a texture. A blit binds its
 * read and draw frame buffers, turns the scissor test off around it,
 * moves Adreno's x a pixel, passes the rest as given and holds when GL has
 * no error; GLES 2 has none, and calls nothing. The frame buffer formats
 * are desktop GL's, GLES 3's (Adreno 530's own) or GLES 2's, by the
 * extensions it has.
 *
 *   ./test_gliden64_buffer_objects
 */
#include <stdio.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

/* ---- the caches ---- */

static int fbCache, rbCache, scissorCache;
static u32 fbBinds, rbBinds, readFb, drawFb, rbBound, rbTarget, fbTarget, fbBound;
static int scissor = 1, badCache;
static u32 calls;  /* every GL call and bind, in order */

void gli_gl_bind_framebuffer(void *_cache, u32 _target, u32 _name)
{
	if (_cache != &fbCache)
		badCache = 1;
	fbBinds++;
	calls++;
	if (_target == GLI_BT_READ_FRAMEBUFFER)
		readFb = _name;
	else if (_target == GLI_BT_DRAW_FRAMEBUFFER)
		drawFb = _name;
	fbTarget = _target;
	fbBound = _name;
}

void gli_gl_bind_renderbuffer(void *_cache, u32 _target, u32 _name)
{
	if (_cache != &rbCache)
		badCache = 1;
	rbBinds++;
	calls++;
	rbTarget = _target;
	rbBound = _name;
}

static u32 scissorOffAt, scissorOnAt;
void gli_gl_enable_cached(void *_cache, int _enable)
{
	if (_cache != &scissorCache)
		badCache = 1;
	scissor = _enable;
	calls++;
	if (_enable)
		scissorOnAt = calls;
	else
		scissorOffAt = calls;
}

/* ---- GL ---- */

static GLuint nextName = 10;
static void APIENTRY gen_framebuffers(GLsizei _n, GLuint *_b) { check(_n == 1, "one frame buffer"); *_b = nextName++; calls++; }
static void APIENTRY gen_renderbuffers(GLsizei _n, GLuint *_b) { check(_n == 1, "one render buffer"); *_b = nextName++; calls++; }

static GLenum storageTarget, storageFormat;
static GLsizei storageW, storageH;
static u32 storageBoundRb;
static void APIENTRY renderbuffer_storage(GLenum _t, GLenum _f, GLsizei _w, GLsizei _h)
{
	storageTarget = _t;
	storageFormat = _f;
	storageW = _w;
	storageH = _h;
	storageBoundRb = rbBound;
	calls++;
}

static GLenum attTarget, attAttachment, attTexTarget;
static GLuint attName, attBoundFb;
static GLint attLevel = -1;
static u32 renderbufferAttaches, textureAttaches;
static void APIENTRY framebuffer_renderbuffer(GLenum _t, GLenum _a, GLenum _rt, GLuint _rb)
{
	attTarget = _t; attAttachment = _a; attTexTarget = _rt; attName = _rb; attBoundFb = fbBound;
	renderbufferAttaches++;
	calls++;
}
static void APIENTRY framebuffer_texture2d(GLenum _t, GLenum _a, GLenum _tt, GLuint _tex, GLint _level)
{
	attTarget = _t; attAttachment = _a; attTexTarget = _tt; attName = _tex; attLevel = _level; attBoundFb = fbBound;
	textureAttaches++;
	calls++;
}

static GLint blit[8];
static GLbitfield blitMask;
static GLenum blitFilter;
static u32 blits, blitAt, blitRead, blitDraw;
static int blitScissor;
static void APIENTRY blit_framebuffer(GLint _sx0, GLint _sy0, GLint _sx1, GLint _sy1,
	GLint _dx0, GLint _dy0, GLint _dx1, GLint _dy1, GLbitfield _mask, GLenum _filter)
{
	blit[0] = _sx0; blit[1] = _sy0; blit[2] = _sx1; blit[3] = _sy1;
	blit[4] = _dx0; blit[5] = _dy0; blit[6] = _dx1; blit[7] = _dy1;
	blitMask = _mask;
	blitFilter = _filter;
	blitRead = readFb;
	blitDraw = drawFb;
	blitScissor = scissor;
	blits++;
	blitAt = ++calls;
}

static const char *extensions = "";
static const GLubyte *APIENTRY get_string(GLenum _name)
{
	return (const GLubyte *)(_name == GL_EXTENSIONS ? extensions : "");
}
static void APIENTRY get_integerv(GLenum _name, GLint *_v) { (void)_name; *_v = 0; }
static const GLubyte *APIENTRY get_stringi(GLenum _name, GLuint _i) { (void)_name; (void)_i; return (const GLubyte *)""; }

PFNGLGENFRAMEBUFFERSPROC g_glGenFramebuffers = gen_framebuffers;
PFNGLGENRENDERBUFFERSPROC g_glGenRenderbuffers = gen_renderbuffers;
PFNGLRENDERBUFFERSTORAGEPROC g_glRenderbufferStorage = renderbuffer_storage;
PFNGLFRAMEBUFFERRENDERBUFFERPROC g_glFramebufferRenderbuffer = framebuffer_renderbuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC g_glFramebufferTexture2D = framebuffer_texture2d;
PFNGLBLITFRAMEBUFFERPROC g_glBlitFramebuffer = blit_framebuffer;
PFNGLGETSTRINGPROC g_glGetString = get_string;
PFNGLGETINTEGERVPROC g_glGetIntegerv = get_integerv;
PFNGLGETSTRINGIPROC g_glGetStringi = get_stringi;
PFNGLGETERRORPROC g_glGetError;

static gli_gl_info info;
static gli_gl_buffer_objects objects;

static void test_names(void)
{
	u32 a, b;
	a = gli_gl_create_framebuffer(&objects);
	b = gli_gl_create_framebuffer(&objects);
	check(a != 0 && b == a + 1, "frame buffers are GL's new names");
	a = gli_gl_create_renderbuffer();
	check(a == b + 1, "a render buffer is GL's new name");
}

static void test_renderbuffer(void)
{
	gli_init_renderbuffer_params p;
	p.handle = 42;
	p.target = GL_RENDERBUFFER;
	p.format = GL_DEPTH_COMPONENT24;
	p.width = 320;
	p.height = 240;
	rbBinds = 0;
	gli_gl_init_renderbuffer(&objects, &p);
	check(rbBinds == 1 && rbTarget == GL_RENDERBUFFER && storageBoundRb == 42, "the render buffer bound for its storage");
	check(storageTarget == GL_RENDERBUFFER && storageFormat == GL_DEPTH_COMPONENT24 && storageW == 320 && storageH == 240,
		"its storage as asked");
}

static void test_render_target(void)
{
	gli_fb_render_target p;
	p.bufferHandle = 7;
	p.bufferTarget = GL_DRAW_FRAMEBUFFER;
	p.attachment = GL_DEPTH_ATTACHMENT;
	p.textureTarget = GLI_TT_RENDERBUFFER;
	p.textureHandle = 42;
	fbBinds = 0;
	renderbufferAttaches = textureAttaches = 0;
	gli_gl_add_render_target(&objects, &p);
	check(fbBinds == 1 && fbTarget == GL_DRAW_FRAMEBUFFER, "the frame buffer bound for its target");
	check(renderbufferAttaches == 1 && textureAttaches == 0 && attBoundFb == 7 && attTarget == GL_DRAW_FRAMEBUFFER
		&& attAttachment == GL_DEPTH_ATTACHMENT && attTexTarget == GL_RENDERBUFFER && attName == 42,
		"a render buffer attached as one");

	p.bufferHandle = 8;
	p.bufferTarget = GL_FRAMEBUFFER;
	p.attachment = GL_COLOR_ATTACHMENT0;
	p.textureTarget = GL_TEXTURE_2D;
	p.textureHandle = 99;
	gli_gl_add_render_target(&objects, &p);
	check(textureAttaches == 1 && renderbufferAttaches == 1 && attBoundFb == 8 && attTarget == GL_FRAMEBUFFER
		&& attAttachment == GL_COLOR_ATTACHMENT0 && attTexTarget == GL_TEXTURE_2D && attName == 99 && attLevel == 0,
		"a texture attached at level 0");
}

static void test_blit(void)
{
	gli_blit_params p;
	p.readBuffer = 3;
	p.drawBuffer = 4;
	p.srcX0 = 10; p.srcY0 = 11; p.srcX1 = 12; p.srcY1 = 13;
	p.dstX0 = 20; p.dstY0 = 21; p.dstX1 = 22; p.dstY1 = 23;
	p.mask = GL_COLOR_BUFFER_BIT;
	p.filter = GL_LINEAR;

	info.renderer = GLI_GL_OTHER;
	blits = 0;
	scissor = 1;
	check(gli_gl_blit_framebuffers(&objects, &p), "a blit holds without a GL error");
	check(blits == 1 && blitRead == 3 && blitDraw == 4, "from the read frame buffer to the draw one");
	check(blit[0] == 10 && blit[1] == 11 && blit[2] == 12 && blit[3] == 13 && blit[4] == 20 && blit[5] == 21
		&& blit[6] == 22 && blit[7] == 23 && blitMask == GL_COLOR_BUFFER_BIT && blitFilter == GL_LINEAR, "as given");
	check(blitScissor == 0 && scissor == 1 && scissorOffAt < blitAt && scissorOnAt > blitAt, "no scissor test while it blits");

	info.renderer = GLI_GL_ADRENO;
	gli_gl_blit_framebuffers(&objects, &p);
	check(blit[0] == 11 && blit[4] == 21 && blit[2] == 12 && blit[6] == 22 && blit[1] == 11, "Adreno's x a pixel on");
	info.renderer = GLI_GL_ADRENO530;
	gli_gl_blit_framebuffers(&objects, &p);
	check(blit[0] == 10 && blit[4] == 20, "not on Adreno 530");

	info.isGLES2 = 1;
	blits = 0;
	fbBinds = 0;
	check(!gli_gl_blit_framebuffers(&objects, &p) && blits == 0 && fbBinds == 0, "none on GLES 2");
	info.isGLES2 = 0;
}

static int same_formats(const gli_fb_formats *_a, const gli_fb_formats *_b)
{
	return memcmp(_a, _b, sizeof(*_a)) == 0;
}

static void test_formats(void)
{
	static const gli_fb_formats unset = GLI_FB_FORMATS_DEFAULT;
	gli_fb_formats got, want;

	info.isGLESX = 0;
	info.isGLES2 = 0;
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GL, 0);
	check(same_formats(&got, &want), "desktop GL's formats");

	info.isGLESX = 1;
	info.renderer = GLI_GL_ADRENO;
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GLES3, 0);
	check(same_formats(&got, &want), "GLES 3's");
	info.renderer = GLI_GL_ADRENO530;
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GLES3, GLI_FB_ADRENO530);
	check(same_formats(&got, &want), "Adreno 530's own");

	info.isGLES2 = 1;
	info.majorVersion = 2;
	info.renderer = GLI_GL_OTHER;
	extensions = "GL_OES_rgb8_rgba8";
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GLES2, GLI_FB_OES_RGBA8);
	check(same_formats(&got, &want), "GLES 2's with eight bit colour");
	extensions = "GL_OES_depth_texture";
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GLES2, GLI_FB_OES_DEPTH);
	check(same_formats(&got, &want), "GLES 2's with depth textures");
	extensions = "";
	got = unset; want = unset;
	gli_gl_fb_formats(&info, &got);
	gli_fb_formats_gl(&want, GLI_FB_GLES2, 0);
	check(same_formats(&got, &want), "GLES 2's bare");
	got = unset;
	gli_fb_formats_gl(&want, GLI_FB_GLES2, GLI_FB_OES_DEPTH | GLI_FB_OES_RGBA8);
	extensions = "GL_OES_depth_texture GL_OES_rgb8_rgba8";
	gli_gl_fb_formats(&info, &got);
	check(same_formats(&got, &want), "GLES 2's with both");
}

int main(void)
{
	memset(&info, 0, sizeof(info));
	objects.info = &info;
	objects.bindFramebuffer = &fbCache;
	objects.bindRenderbuffer = &rbCache;
	objects.enableScissor = &scissorCache;
	test_names();
	test_renderbuffer();
	test_render_target();
	test_blit();
	test_formats();
	check(!badCache, "each call through its own cache");
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("buffer objects: ok\n");
	return 0;
}
