/* test_gliden64_gl_context -- GLideN64's GL context
 * (Graphics/OpenGLContext/opengl_ContextImpl.c) on a stub GL and stub
 * modules: what each entry of its table does with them.
 *
 * Made, it starts clipping. Started, it reads the context's info, sets
 * both frame buffer targets to GL_FRAMEBUFFER on GLES 2 only, makes its
 * caches once until stopped, its textures and buffer objects on them,
 * the buffered drawer on desktop GL and GLES 3.2 with buffer storage
 * and the unbuffered one otherwise, and its programs. New programs are
 * made only when the options change, each before the old one goes;
 * stopped, the textures, drawer and builder go, the special shaders when
 * it is freed. Clamp modes turn depth clamp and clip distance on and off,
 * none on GLES. A colour clear turns the scissor test off around it, by
 * the clear colour on GLES 2 and glClearBufferfv otherwise; a depth clear
 * makes depth writes on, PowerVR clearing once with them off first.
 * VideoCore clears depth on every frame buffer bind. A deleted frame or
 * render buffer forgets its bind; 0 deletes nothing. GLES 2 has no pixel
 * read buffer; the colour readers by buffer storage (not on Intel), pixel
 * buffers or glReadPixels on GLES 2. The features, GLES 2's formats, the
 * anisotropy and sample queries, the texture barrier and draw buffers,
 * the special shaders by name, and the shader storage on its info and
 * program cache.
 *
 *   ./test_gliden64_gl_context
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/gli_context.h"
#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.h"
#include "Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.h"
#include "Graphics/OpenGLContext/opengl_ColorBufferReader.h"
#include "Graphics/OpenGLContext/opengl_Drawer.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Programs.h"
#include "gli_combiner.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

gli_context gli_ctx;
static gli_gl_info nextInfo;

/* ---- a log of what is called, in order ---- */

static char log_[4096];
static void say(const char *_s)
{
	if (strlen(log_) + strlen(_s) + 2 < sizeof(log_)) {
		strcat(log_, _s);
		strcat(log_, " ");
	}
}
static void clear_log(void) { log_[0] = '\0'; }
static int logged(const char *_s) { return strcmp(log_, _s) == 0; }

/* ---- stub modules ---- */

void gli_gl_info_init(gli_gl_info *_info) { *_info = nextInfo; say("info"); }
int gli_gl_has_error(void) { return 0; }
int gli_gl_framebuffer_incomplete(void) { return 1; }

static gli_gl_textures *texturesSeen;
static void *texturesBind;
bool gli_gl_textures_init(gli_gl_textures *_t, const gli_gl_info *_i, void *_b) { (void)_i; texturesSeen = _t; texturesBind = _b; say("textures"); return 1; }
void gli_gl_textures_free(gli_gl_textures *_t) { (void)_t; say("textures-free"); }
u32 gli_gl_create_texture(const gli_gl_textures *_t, u32 _target) { (void)_t; return _target + 1; }
static u32 deletedTexture;
void gli_gl_delete_texture(gli_gl_textures *_t, u32 _n) { (void)_t; deletedTexture = _n; }
static const void *lastParams;
void gli_gl_init_texture(gli_gl_textures *_t, const gli_init_texture_params *_p) { (void)_t; lastParams = _p; say("init-texture"); }
void gli_gl_update_texture(const gli_gl_textures *_t, const gli_update_texture_params *_p) { (void)_t; lastParams = _p; say("update-texture"); }
void gli_gl_set_texture_parameters(gli_gl_textures *_t, const gli_tex_parameters *_p) { (void)_t; lastParams = _p; say("tex-params"); }

static const gli_gl_buffer_objects *objectsSeen;
u32 gli_gl_create_framebuffer(const gli_gl_buffer_objects *_o) { objectsSeen = _o; return 77; }
u32 gli_gl_create_renderbuffer(void) { return 78; }
void gli_gl_init_renderbuffer(const gli_gl_buffer_objects *_o, const gli_init_renderbuffer_params *_p) { objectsSeen = _o; lastParams = _p; say("init-rb"); }
void gli_gl_add_render_target(const gli_gl_buffer_objects *_o, const gli_fb_render_target *_p) { objectsSeen = _o; lastParams = _p; say("target"); }
bool gli_gl_blit_framebuffers(const gli_gl_buffer_objects *_o, const gli_blit_params *_p) { objectsSeen = _o; lastParams = _p; return 1; }
static gli_fb_formats formatsOut;
void gli_gl_fb_formats(const gli_gl_info *_i, gli_fb_formats *_f) { (void)_i; *_f = formatsOut; say("formats"); }

static int prbs;
static void *prbBind;
gli_prb *gli_gl_prb_new(void *_b, size_t _bytes) { static gli_prb p; (void)_bytes; prbBind = _b; prbs++; return &p; }
static int cbrKind;
static gli_cbr cbrObj;
gli_cbr *gli_gl_cbr_new_buffer_storage(struct gli_tex *_t, void *_b) { (void)_t; (void)_b; cbrKind = 1; return &cbrObj; }
gli_cbr *gli_gl_cbr_new_pixel_buffer(struct gli_tex *_t, void *_b) { (void)_t; (void)_b; cbrKind = 2; return &cbrObj; }
gli_cbr *gli_gl_cbr_new_read_pixels(struct gli_tex *_t) { (void)_t; cbrKind = 3; return &cbrObj; }

/* a drawer */
static int drawerKind, drawerDestroyed, drawCalls;
static void d_tri(gli_gl_drawer *_d, const gli_draw_triangles_params *_p) { (void)_d; lastParams = _p; drawCalls++; }
static void d_rect(gli_gl_drawer *_d, const gli_draw_rects_params *_p) { (void)_d; lastParams = _p; drawCalls += 10; }
static f32 lineWidth;
static void d_line(gli_gl_drawer *_d, f32 _w, struct SPVertex *_v) { (void)_d; lineWidth = _w; lastParams = _v; drawCalls += 100; }
static void d_destroy(gli_gl_drawer *_d) { (void)_d; drawerDestroyed++; say("drawer-free"); }
static const gli_gl_drawer_ops drawerOps = { d_tri, d_rect, d_line, d_destroy };
static gli_gl_drawer drawerObj = { &drawerOps };
static void *drawerAttribs, *drawerBind;
gli_gl_drawer *gli_gl_drawer_new_buffered(const gli_gl_info *_i, gli_gl_attribs *_a, void *_b)
{
	(void)_i; drawerKind = 1; drawerAttribs = _a; drawerBind = _b; say("drawer"); return &drawerObj;
}
gli_gl_drawer *gli_gl_drawer_new_unbuffered(const gli_gl_info *_i, gli_gl_attribs *_a)
{
	(void)_i; drawerKind = 2; drawerAttribs = _a; drawerBind = NULL; say("drawer"); return &drawerObj;
}

/* the programs */
struct gli_glsl_builder { int id; int obsolete; int uber; };
struct gli_glsl_special { int id; };
static struct gli_glsl_builder builders[8];
static struct gli_glsl_special specials[8];
static int buildersMade, specialsMade, buildersFreed, specialsFreed, lastFreedBuilder;
static const gli_glsl_builder *specialFrom;
static gli_gl_cache1 *programCacheSeen;
gli_glsl_builder *gli_glsl_builder_new(const gli_gl_info *_i, struct gli_gl_cache1 *_u)
{
	gli_glsl_builder *b = &builders[buildersMade & 7];
	(void)_i;
	programCacheSeen = _u;
	b->id = ++buildersMade;
	b->obsolete = 0;
	b->uber = 0;
	say("builder");
	return b;
}
void gli_glsl_builder_delete(gli_glsl_builder *_b) { lastFreedBuilder = _b->id; buildersFreed++; say("builder-free"); }
bool gli_glsl_builder_obsolete(const gli_glsl_builder *_b) { return _b->obsolete != 0; }
bool gli_glsl_builder_uber_only(const gli_glsl_builder *_b) { return _b->uber != 0; }
static int lastAsync;
static u64 lastKey;
static struct gli_cprog progObj;
static const gli_glsl_builder *builtBy;
static const gli_glsl_special *specialBy;
struct gli_cprog *gli_glsl_ops_build(void *_p, struct Combiner *_c, struct Combiner *_a, u64 _key)
{
	(void)_c; (void)_a;
	builtBy = ((gli_glsl_programs *)_p)->builder;
	lastAsync = 0;
	lastKey = _key;
	return &progObj;
}
struct gli_cprog *gli_glsl_ops_build_async(void *_p, struct Combiner *_c, struct Combiner *_a, u64 _key)
{
	(void)_c; (void)_a;
	builtBy = ((gli_glsl_programs *)_p)->builder;
	lastAsync = 1;
	lastKey = _key;
	return &progObj;
}
gli_glsl_special *gli_glsl_special_new(const gli_gl_info *_i, struct gli_gl_cache1 *_u, const gli_glsl_builder *_b)
{
	gli_glsl_special *s = &specials[specialsMade & 7];
	(void)_i; (void)_u;
	specialFrom = _b;
	s->id = ++specialsMade;
	say("special");
	return s;
}
void gli_glsl_special_delete(gli_glsl_special *_s) { (void)_s; specialsFreed++; say("special-free"); }
enum { GLI_GLSL_DEPTH_FOG, GLI_GLSL_TEXRECT_DRAW, GLI_GLSL_TEXRECT_CLEAR, GLI_GLSL_TEXRECT_COPY, GLI_GLSL_GAMMA_CORRECTION, GLI_GLSL_FXAA };
static int lastWhich = -1;
static struct gli_cprog *special_made(void *_p, int _which) { specialBy = ((gli_glsl_programs *)_p)->special; lastWhich = _which; return &progObj; }
struct gli_cprog *gli_glsl_ops_depth_fog_shader(void *_p) { return special_made(_p, GLI_GLSL_DEPTH_FOG); }
struct gli_cprog *gli_glsl_ops_texrect_draw_shader(void *_p) { return special_made(_p, GLI_GLSL_TEXRECT_DRAW); }
struct gli_cprog *gli_glsl_ops_texrect_clear_shader(void *_p) { return special_made(_p, GLI_GLSL_TEXRECT_CLEAR); }
struct gli_cprog *gli_glsl_ops_texrect_copy_shader(void *_p) { return special_made(_p, GLI_GLSL_TEXRECT_COPY); }
struct gli_cprog *gli_glsl_ops_gamma_correction_shader(void *_p) { return special_made(_p, GLI_GLSL_GAMMA_CORRECTION); }
struct gli_cprog *gli_glsl_ops_fxaa_shader(void *_p) { return special_made(_p, GLI_GLSL_FXAA); }
static const gli_gl_info *storageInfo;
static gli_gl_cache1 *storageCache;
bool gli_glsl_save_storage(const gli_gl_info *_i, struct gli_gl_cache1 *_u, const struct gli_combiner_table *_c)
{
	(void)_c; storageInfo = _i; storageCache = _u; say("save"); return 1;
}
bool gli_glsl_load_storage(const gli_gl_info *_i, struct gli_gl_cache1 *_u, struct gli_combiner_table *_c)
{
	(void)_c; storageInfo = _i; storageCache = _u; say("load"); return 0;
}
void gli_glsl_journal_path(const gli_gl_info *_i, struct gli_gl_cache1 *_u, char *_p, size_t _s, u32 *_v)
{
	storageInfo = _i; storageCache = _u;
	strncpy(_p, "journal", _s);
	*_v = 5;
}

/* ---- stub GL ---- */

static u32 enables[16], disables[16];
static void APIENTRY s_enable(GLenum _c) { enables[_c & 15]++; say("enable"); }
static void APIENTRY s_disable(GLenum _c) { disables[_c & 15]++; say("disable"); }
void rglEnablei(GLenum cap, GLuint index) { (void)cap; (void)index; }
void rglDisablei(GLenum cap, GLuint index) { (void)cap; (void)index; }
static GLbitfield clears;
static void APIENTRY s_clear(GLbitfield _m) { clears |= _m; say(_m == GL_DEPTH_BUFFER_BIT ? "clear-depth" : "clear-color"); }
static GLfloat clearValues[4];
static void APIENTRY s_clear_bufferfv(GLenum _b, GLint _d, const GLfloat *_v)
{
	check(_b == GL_COLOR && _d == 0, "the colour buffer 0 cleared");
	memcpy(clearValues, _v, sizeof(clearValues));
	say("clear-buffer");
}
static GLfloat clearColor[4];
static void APIENTRY s_clear_color(GLfloat _r, GLfloat _g, GLfloat _b, GLfloat _a)
{
	clearColor[0] = _r; clearColor[1] = _g; clearColor[2] = _b; clearColor[3] = _a;
	say("clear-color-set");
}
static GLboolean depthMask = 7;
static void APIENTRY s_depth_mask(GLboolean _f) { depthMask = _f; say(_f ? "mask-on" : "mask-off"); }
static GLuint deletedFbo, deletedRbo;
static void APIENTRY s_del_fb(GLsizei _n, const GLuint *_b) { (void)_n; deletedFbo = *_b; say("del-fb"); }
static void APIENTRY s_del_rb(GLsizei _n, const GLuint *_b) { (void)_n; deletedRbo = *_b; say("del-rb"); }
static u32 fbBinds;
static void APIENTRY s_bind_fb(GLenum _t, GLuint _n) { (void)_t; (void)_n; fbBinds++; say("bind-fb"); }
static GLenum queried;
static void APIENTRY s_get_floatv(GLenum _p, GLfloat *_v) { queried = _p; _v[0] = 1.0f; if (_p == GL_ALIASED_LINE_WIDTH_RANGE) _v[1] = 9.5f; else _v[0] = 16.0f; }
static void APIENTRY s_get_integerv(GLenum _p, GLint *_v) { queried = _p; *_v = _p == GL_MAX_SAMPLES ? 8 : _p == GL_MAX_TEXTURE_SIZE ? 4096 : 2; }
static int barriers, barriersNV;
static void APIENTRY s_barrier(void) { barriers++; }
static void APIENTRY s_barrier_nv(void) { barriersNV++; }
static GLsizei drawBuffers;
static GLenum drawBufferList[4];
static void APIENTRY s_draw_buffers(GLsizei _n, const GLenum *_b) { drawBuffers = _n; memcpy(drawBufferList, _b, sizeof(drawBufferList)); }
static GLuint imageUnit, imageTex;
static GLenum imageAccess, imageFormat;
static void APIENTRY s_bind_image(GLuint _u, GLuint _t, GLint _l, GLboolean _layered, GLint _layer, GLenum _a, GLenum _f)
{
	check(_l == 0 && _layered == GL_FALSE && _layer == 0, "level 0, not layered");
	imageUnit = _u; imageTex = _t; imageAccess = _a; imageFormat = _f;
}
static GLfloat offsetFactor, offsetUnits;
static void APIENTRY s_polygon_offset(GLfloat _f, GLfloat _u) { offsetFactor = _f; offsetUnits = _u; }
static u32 programUsed = 99;
static void APIENTRY s_use(GLuint _p) { programUsed = _p; }
static void APIENTRY s_cull(GLenum _m) { (void)_m; say("cull"); }
static void APIENTRY s_viewport(GLint _x, GLint _y, GLsizei _w, GLsizei _h) { (void)_x; (void)_y; (void)_w; (void)_h; say("viewport"); }
static void APIENTRY s_pixel_store(GLenum _p, GLint _v) { (void)_p; (void)_v; say("unpack"); }
static void APIENTRY s_active(GLenum _t) { (void)_t; }
static void APIENTRY s_bind_texture(GLenum _t, GLuint _n) { (void)_t; (void)_n; say("bind-texture"); }

PFNGLENABLEPROC g_glEnable = s_enable;
PFNGLDISABLEPROC g_glDisable = s_disable;
PFNGLENABLEIPROC g_glEnablei;
PFNGLDISABLEIPROC g_glDisablei;
PFNGLCLEARPROC g_glClear = s_clear;
PFNGLCLEARBUFFERFVPROC g_glClearBufferfv = s_clear_bufferfv;
PFNGLCLEARCOLORPROC g_glClearColor = s_clear_color;
PFNGLDEPTHMASKPROC g_glDepthMask = s_depth_mask;
PFNGLDELETEFRAMEBUFFERSPROC g_glDeleteFramebuffers = s_del_fb;
PFNGLDELETERENDERBUFFERSPROC g_glDeleteRenderbuffers = s_del_rb;
PFNGLBINDFRAMEBUFFERPROC g_glBindFramebuffer = s_bind_fb;
PFNGLGETFLOATVPROC g_glGetFloatv = s_get_floatv;
PFNGLGETINTEGERVPROC g_glGetIntegerv = s_get_integerv;
PFNGLTEXTUREBARRIERPROC g_glTextureBarrier = s_barrier;
PFNGLTEXTUREBARRIERNVPROC g_glTextureBarrierNV = s_barrier_nv;
PFNGLDRAWBUFFERSPROC g_glDrawBuffers = s_draw_buffers;
PFNGLBINDIMAGETEXTUREPROC g_glBindImageTexture = s_bind_image;
PFNGLPOLYGONOFFSETPROC g_glPolygonOffset = s_polygon_offset;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;
PFNGLCULLFACEPROC g_glCullFace = s_cull;
PFNGLVIEWPORTPROC g_glViewport = s_viewport;
PFNGLPIXELSTOREIPROC g_glPixelStorei = s_pixel_store;
PFNGLACTIVETEXTUREPROC g_glActiveTexture = s_active;
PFNGLBINDTEXTUREPROC g_glBindTexture = s_bind_texture;
/* the rest the cache could call, which the tests do not reach */
PFNGLDEPTHFUNCPROC g_glDepthFunc;
PFNGLSCISSORPROC g_glScissor;
PFNGLBLENDFUNCPROC g_glBlendFunc;
PFNGLBLENDFUNCSEPARATEPROC g_glBlendFuncSeparate;
PFNGLBLENDCOLORPROC g_glBlendColor;
PFNGLBINDRENDERBUFFERPROC g_glBindRenderbuffer;
PFNGLBINDBUFFERPROC g_glBindBuffer;

/* ---- the context's own cache, found through what it hands out ---- */

static const gli_context_ops *ops;

static gli_gl_cache *cache_of(void)
{
	/* the textures are handed the texture bind, the cache's own */
	return (gli_gl_cache *)((char *)texturesBind - offsetof(gli_gl_cache, bindTexture));
}

static void *start(const gli_gl_info *_info)
{
	void *c;
	nextInfo = *_info;
	gli_ctx.drawFramebuffer = GLI_BT_DRAW_FRAMEBUFFER;
	gli_ctx.readFramebuffer = GLI_BT_READ_FRAMEBUFFER;
	c = gli_gl_context_new();
	check(c != NULL, "a context");
	check(ops->getClampMode(c) == 0, "clipping when made");
	ops->init(c);
	return c;
}

static void stop(void *_c)
{
	ops->destroy(_c);
	ops->free_impl(_c);
}

static gli_gl_info desktop(void)
{
	gli_gl_info i;
	memset(&i, 0, sizeof(i));
	i.majorVersion = 4;
	i.minorVersion = 5;
	i.bufferStorage = 1;
	i.renderer = GLI_GL_OTHER;
	return i;
}

static void test_life(void)
{
	gli_gl_info i = desktop();
	void *c;
	gli_gl_cache *cache;
	int made;

	clear_log();
	c = start(&i);
	check(logged("info textures formats drawer builder special "), "started in order");
	check(drawerKind == 1, "desktop GL: buffered");
	cache = cache_of();
	check(drawerAttribs == &cache->attribs && drawerBind == &cache->bindBuffer, "the drawer on the cache's arrays and buffer bind");
	check(programCacheSeen == &cache->useProgram, "the programs on the cache's program");
	check(specialFrom == &builders[0], "the special shaders from the builder");
	check(gli_ctx.drawFramebuffer == GLI_BT_DRAW_FRAMEBUFFER && gli_ctx.readFramebuffer == GLI_BT_READ_FRAMEBUFFER,
		"desktop GL keeps its frame buffer targets");
	check(ops->createFramebuffer(c) == 77, "a frame buffer");
	check(objectsSeen->info != NULL && objectsSeen->bindFramebuffer == &cache->bindFramebuffer
		&& objectsSeen->bindRenderbuffer == &cache->bindRenderbuffer
		&& objectsSeen->enableScissor == &cache->enables[GLI_GL_ENABLE_SCISSOR_TEST], "buffer objects on the cache");

	/* programs: none new while the options hold */
	made = buildersMade;
	check(!ops->isCombinerProgramBuilderObsolete(c), "the builder current");
	clear_log();
	ops->resetCombinerProgramBuilder(c);
	check(buildersMade == made && log_[0] == '\0', "kept while the options hold");
	builders[0].obsolete = 1;
	check(ops->isCombinerProgramBuilderObsolete(c), "obsolete once they change");
	clear_log();
	ops->resetCombinerProgramBuilder(c);
	check(logged("builder builder-free special special-free "), "new programs, each before the old goes");
	check(lastFreedBuilder == 1 && specialFrom == &builders[1], "the special shaders from the new builder");
	check(!ops->isCombinerUberOnly(c), "not uber only");
	builders[1].uber = 1;
	check(ops->isCombinerUberOnly(c), "uber only as the builder says");

	/* started again: the cache kept, and the clamp mode back to clipping */
	ops->enable(c, GLI_EN_DITHER, 1);
	ops->setClampMode(c, 2);
	clear_log();
	ops->init(c);
	check(cache_of() == cache && ops->isEnabled(c, GLI_EN_DITHER) == 1, "started again: the cache kept");
	check(ops->getClampMode(c) == 0, "and clipping again");

	clear_log();
	ops->destroy(c);
	check(logged("textures-free drawer-free builder-free "), "stopped: textures, drawer, builder");
	check(ops->isCombinerProgramBuilderObsolete(c) && !ops->isCombinerUberOnly(c), "no builder once stopped");
	clear_log();
	ops->free_impl(c);
	check(logged("special-free "), "freed: the special shaders");

	/* stopped and started: the cache made again */
	c = start(&i);
	ops->enable(c, GLI_EN_DITHER, 1);
	ops->destroy(c);
	ops->init(c);
	check(ops->isEnabled(c, GLI_EN_DITHER) == GLI_PARAM_INVALID, "stopped and started: the cache made again");
	stop(c);

	/* GLES 3.2 with buffer storage buffered, else unbuffered */
	i.isGLESX = 1;
	i.majorVersion = 3; i.minorVersion = 2;
	c = start(&i);
	check(drawerKind == 1, "GLES 3.2 with storage: buffered");
	stop(c);
	i.minorVersion = 1;
	c = start(&i);
	check(drawerKind == 2 && drawerBind == NULL, "GLES 3.1: unbuffered");
	stop(c);
	i.minorVersion = 2;
	i.bufferStorage = 0;
	c = start(&i);
	check(drawerKind == 2, "GLES 3.2 without storage: unbuffered");
	stop(c);

	i.bufferStorage = 1;
	c = start(&i);
	check(gli_ctx.drawFramebuffer == GLI_BT_DRAW_FRAMEBUFFER && gli_ctx.readFramebuffer == GLI_BT_READ_FRAMEBUFFER,
		"GLES 3 keeps its frame buffer targets");
	stop(c);

	/* GLES 2's frame buffer targets */
	i.isGLES2 = 1;
	i.majorVersion = 2; i.minorVersion = 0;
	c = start(&i);
	check(gli_ctx.drawFramebuffer == GLI_BT_FRAMEBUFFER && gli_ctx.readFramebuffer == GLI_BT_FRAMEBUFFER,
		"GLES 2: both targets GL_FRAMEBUFFER");
	stop(c);
}

static void test_clamp(void)
{
	gli_gl_info i = desktop();
	void *c = start(&i);
	const u32 dc = SGL_DEPTH_CLAMP & 15, cd = SGL_CLIP_DISTANCE0 & 15;
	memset(enables, 0, sizeof(enables));
	memset(disables, 0, sizeof(disables));
	ops->setClampMode(c, 1);
	check(enables[dc] == 1 && enables[cd] == 1 && ops->getClampMode(c) == 1, "no near clipping: depth clamp, clip distance on");
	ops->setClampMode(c, 2);
	check(enables[dc] == 1 && disables[dc] == 0 && disables[cd] == 1 && ops->getClampMode(c) == 2,
		"no clipping: clip distance off, depth clamp on");
	ops->setClampMode(c, 0);
	check(disables[dc] == 1 && disables[cd] == 1 && ops->getClampMode(c) == 0, "clipping: both off");
	stop(c);

	i.isGLESX = 1;
	c = start(&i);
	memset(enables, 0, sizeof(enables));
	memset(disables, 0, sizeof(disables));
	ops->setClampMode(c, 1);
	check(enables[dc] == 0 && enables[cd] == 0 && ops->getClampMode(c) == 1, "none on GLES, the mode kept");
	stop(c);
}

static void test_clears(void)
{
	gli_gl_info i = desktop();
	void *c = start(&i);
	const u32 sc = SGL_SCISSOR_TEST & 15;

	/* the scissor test on before, so a clear turns it off and on */
	ops->enable(c, GLI_EN_SCISSOR_TEST, 1);
	clear_log();
	ops->clearColorBuffer(c, 0.25f, 0.5f, 0.75f, 1.0f);
	check(logged("disable clear-buffer enable "), "a colour clear: scissor off, glClearBufferfv, on");
	check(clearValues[0] == 0.25f && clearValues[1] == 0.5f && clearValues[2] == 0.75f && clearValues[3] == 1.0f, "its colour");
	(void)sc;
	clear_log();
	ops->clearDepthBuffer(c);
	check(logged("disable mask-on clear-depth enable "), "a depth clear: depth writes on");
	clear_log();
	ops->clearDepthBuffer(c);
	check(logged("disable clear-depth enable "), "writes already on");
	stop(c);

	i.renderer = GLI_GL_POWERVR;
	c = start(&i);
	ops->enable(c, GLI_EN_SCISSOR_TEST, 1);
	clear_log();
	ops->clearDepthBuffer(c);
	check(logged("disable mask-off clear-depth mask-on clear-depth enable "), "PowerVR: once with writes off first");
	stop(c);

	i.renderer = GLI_GL_OTHER;
	i.isGLESX = 1;
	c = start(&i);
	ops->enable(c, GLI_EN_SCISSOR_TEST, 1);
	clear_log();
	ops->clearColorBuffer(c, 0.0f, 0.0f, 0.0f, 0.5f);
	check(logged("disable clear-buffer enable "), "GLES 3: glClearBufferfv");
	stop(c);

	i.isGLES2 = 1;
	c = start(&i);
	ops->enable(c, GLI_EN_SCISSOR_TEST, 1);
	clear_log();
	ops->clearColorBuffer(c, 0.0f, 0.0f, 0.0f, 0.5f);
	check(logged("disable clear-color-set clear-color enable "), "GLES 2: the clear colour, then glClear");
	check(clearColor[3] == 0.5f, "its colour");
	clear_log();
	ops->clearColorBuffer(c, 0.0f, 0.0f, 0.0f, 0.5f);
	check(logged("disable clear-color enable "), "the same colour not set again");
	stop(c);
}

static void test_frame_buffers(void)
{
	gli_gl_info i = desktop();
	void *c = start(&i);
	gli_gl_cache *cache = cache_of();
	gli_blit_params blit;
	gli_fb_render_target target;
	gli_init_renderbuffer_params rb;

	clear_log();
	ops->bindFramebuffer(c, GLI_BT_DRAW_FRAMEBUFFER, 5);
	ops->bindFramebuffer(c, GLI_BT_DRAW_FRAMEBUFFER, 5);
	check(logged("bind-fb "), "a frame buffer bound once");
	clear_log();
	ops->deleteFramebuffer(c, 5);
	check(logged("del-fb ") && deletedFbo == 5 && cache->bindFramebuffer.v1 == GLI_PARAM_INVALID, "deleted, its bind forgotten");
	ops->bindFramebuffer(c, GLI_BT_DRAW_FRAMEBUFFER, 5);
	check(logged("del-fb bind-fb "), "and made again");
	clear_log();
	ops->deleteFramebuffer(c, 0);
	ops->deleteRenderbuffer(c, 0);
	check(log_[0] == '\0', "0 deletes nothing");
	cache->bindRenderbuffer.v1 = GL_RENDERBUFFER;
	cache->bindRenderbuffer.v2 = 9;
	ops->deleteRenderbuffer(c, 9);
	check(logged("del-rb ") && deletedRbo == 9 && cache->bindRenderbuffer.v1 == GLI_PARAM_INVALID, "a render buffer deleted");
	check(ops->createRenderbuffer(c) == 78, "a render buffer made");
	ops->initRenderbuffer(c, &rb);
	check(lastParams == &rb, "its storage");
	ops->addFrameBufferRenderTarget(c, &target);
	check(lastParams == &target, "a render target");
	check(ops->blitFramebuffers(c, &blit) && lastParams == &blit, "a blit");
	stop(c);

	i.renderer = GLI_GL_VIDEOCORE;
	c = start(&i);
	clear_log();
	ops->bindFramebuffer(c, GLI_BT_DRAW_FRAMEBUFFER, 5);
	ops->bindFramebuffer(c, GLI_BT_DRAW_FRAMEBUFFER, 5);
	check(logged("mask-on clear-depth bind-fb clear-depth "), "VideoCore clears depth on every bind");
	stop(c);

	/* read buffers and colour readers */
	i.renderer = GLI_GL_OTHER;
	c = start(&i);
	cache = cache_of();
	prbs = 0;
	check(ops->createPixelReadBuffer(c, 64) != NULL && prbs == 1 && prbBind == &cache->bindBuffer, "a pixel read buffer");
	check(ops->createColorBufferReader(c, NULL) == &cbrObj && cbrKind == 1, "buffer storage's reader");
	stop(c);
	i.renderer = GLI_GL_INTEL;
	c = start(&i);
	ops->createColorBufferReader(c, NULL);
	check(cbrKind == 2, "Intel: pixel buffers");
	stop(c);
	i.renderer = GLI_GL_OTHER;
	i.bufferStorage = 0;
	c = start(&i);
	ops->createColorBufferReader(c, NULL);
	check(cbrKind == 2, "no storage: pixel buffers");
	stop(c);
	i.isGLES2 = 1;
	c = start(&i);
	ops->createColorBufferReader(c, NULL);
	check(cbrKind == 3, "GLES 2: glReadPixels");
	prbs = 0;
	check(ops->createPixelReadBuffer(c, 64) == NULL && prbs == 0, "GLES 2: no pixel read buffer");
	stop(c);

	/* formats as the buffer objects give them */
	memset(&formatsOut, 0, sizeof(formatsOut));
	formatsOut.colorFormat = 0x1908;
	i = desktop();
	c = start(&i);
	{
		gli_fb_formats f;
		memset(&f, 0xFF, sizeof(f));
		ops->getFramebufferTextureFormats(c, &f);
		check(memcmp(&f, &formatsOut, sizeof(f)) == 0, "the frame buffer formats");
	}
	stop(c);
}

static void test_queries(void)
{
	gli_gl_info i = desktop();
	void *c = start(&i);
	u32 f;
	char path[16];
	u32 version = 0;
	gli_bind_image_texture_params image;

	check(ops->getMaxTextureSize(c) == 4096 && queried == GL_MAX_TEXTURE_SIZE, "largest texture");
	check(ops->getTextureUnpackAlignment(c) == 2 && queried == GL_UNPACK_ALIGNMENT, "unpack alignment read back");
	queried = 0;
	check(ops->getMaxAnisotropy(c) == 0.0f && queried == 0, "no anisotropy query without the extension");
	check(ops->getMaxMSAALevel(c) == 0 && queried == 0, "no sample query without multisampling");
	check(ops->getMaxLineWidth(c) == 9.5f && queried == GL_ALIASED_LINE_WIDTH_RANGE, "the widest line");
	check(ops->convertInternalTextureFormat(c, GL_RGBA8) == GL_RGBA8, "formats as they are off GLES 2");
	ops->textureBarrier(c);
	check(barriers == 0 && barriersNV == 0, "no texture barrier without one");
	ops->setDrawBuffers(c, 3);
	check(drawBuffers == 3 && drawBufferList[0] == GL_COLOR_ATTACHMENT0 && drawBufferList[3] == GL_COLOR_ATTACHMENT3, "draw buffers");
	image.imageUnit = 2; image.texture = 3; image.accessMode = GL_READ_WRITE; image.textureFormat = GL_RGBA8;
	ops->bindImageTexture(c, &image);
	check(imageUnit == 2 && imageTex == 3 && imageAccess == GL_READ_WRITE && imageFormat == GL_RGBA8, "an image texture");
	ops->setPolygonOffset(c, -1.0f, 2.0f);
	check(offsetFactor == -1.0f && offsetUnits == 2.0f, "polygon offset");
	check(ops->lendTextureData(c, 64) == NULL, "no texture data lent");
	check(!ops->isError(c) && ops->isFramebufferError(c), "GL's errors as the utils say");

	/* features off */
	for (f = 0; f < GLI_FEATURE_COUNT; ++f) {
		const bool want = f == GLI_FEATURE_BLIT_FRAMEBUFFER || f == GLI_FEATURE_INTEGER_TEXTURES || f == GLI_FEATURE_CLIP_CONTROL;
		check(ops->isSupported(c, f) == want, "desktop GL bare: blits, integer textures, clip control");
	}
	check(!ops->isSupported(c, GLI_FEATURE_COUNT), "no feature past the last");

	/* shaders and storage on the info and program cache */
	{
		gli_gl_cache *cache = cache_of();
		check(ops->createCombinerProgram(c, NULL, NULL, 0x123) == &progObj && !lastAsync && lastKey == 0x123, "a program now");
		check(ops->createCombinerProgramAsync(c, NULL, NULL, 0x456) == &progObj && lastAsync && lastKey == 0x456, "one deferred");
		check(builtBy == &builders[(buildersMade - 1) & 7], "by the context's builder");
		ops->createDepthFogShader(c);
		check(lastWhich == GLI_GLSL_DEPTH_FOG, "depth fog");
		ops->createTexrectDrawerDrawShader(c);
		check(lastWhich == GLI_GLSL_TEXRECT_DRAW, "texrect draw");
		ops->createTexrectDrawerClearShader(c);
		check(lastWhich == GLI_GLSL_TEXRECT_CLEAR, "texrect clear");
		ops->createTexrectCopyShader(c);
		check(lastWhich == GLI_GLSL_TEXRECT_COPY, "texrect copy");
		ops->createGammaCorrectionShader(c);
		check(lastWhich == GLI_GLSL_GAMMA_CORRECTION, "gamma");
		ops->createFXAAShader(c);
		check(lastWhich == GLI_GLSL_FXAA, "FXAA");
		check(specialBy == &specials[(specialsMade - 1) & 7], "by the context's special shaders");
		check(ops->saveShadersStorage(c, NULL) && storageCache == &cache->useProgram && storageInfo != NULL, "storage saved");
		check(!ops->loadShadersStorage(c, NULL) && storageCache == &cache->useProgram, "and loaded");
		ops->getCombinerJournalPath(c, path, sizeof(path), &version);
		check(strcmp(path, "journal") == 0 && version == 5, "the journal's path");
		programUsed = 99;
		cache->useProgram.v = 4;
		ops->resetShaderProgram(c);
		check(programUsed == 0, "no program");
	}
	stop(c);

	/* each feature by its own flag */
	{
		static const u32 feature[] = { GLI_FEATURE_MULTISAMPLING, GLI_FEATURE_IMAGE_TEXTURES, GLI_FEATURE_SHADER_PROGRAM_BINARY,
			GLI_FEATURE_DEPTH_FRAMEBUFFER_TEXTURES, GLI_FEATURE_FRAMEBUFFER_FETCH, GLI_FEATURE_DUAL_SOURCE_BLENDING,
			GLI_FEATURE_TEXTURE_BARRIER, GLI_FEATURE_TEXTURE_BARRIER };
		u32 k, bad = 0;
		for (k = 0; k < 8; ++k) {
			gli_gl_info one = desktop();
			bool *flag[8];
			flag[0] = &one.msaa; flag[1] = &one.imageTextures; flag[2] = &one.shaderStorage; flag[3] = &one.depthTexture;
			flag[4] = &one.ext_fetch; flag[5] = &one.dual_source_blending; flag[6] = &one.texture_barrier;
			flag[7] = &one.texture_barrierNV;
			*flag[k] = 1;
			c = start(&one);
			for (f = 0; f < GLI_FEATURE_COUNT; ++f) {
				const bool base = f == GLI_FEATURE_BLIT_FRAMEBUFFER || f == GLI_FEATURE_INTEGER_TEXTURES || f == GLI_FEATURE_CLIP_CONTROL;
				if (ops->isSupported(c, f) != (base || f == feature[k]))
					bad++;
			}
			stop(c);
		}
		check(bad == 0, "each feature by its own flag");
	}

	/* everything on */
	i.anisotropic_filtering = 1;
	i.msaa = 1;
	i.texture_barrierNV = 1;
	i.imageTextures = 1;
	i.shaderStorage = 1;
	i.depthTexture = 1;
	i.ext_fetch = 1;
	i.dual_source_blending = 1;
	c = start(&i);
	check(ops->getMaxAnisotropy(c) == 16.0f && queried == GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, "anisotropy with the extension");
	check(ops->getMaxMSAALevel(c) == 8 && queried == GL_MAX_SAMPLES, "samples with multisampling");
	ops->textureBarrier(c);
	check(barriersNV == 1 && barriers == 0, "NV's barrier");
	for (f = 0; f < GLI_FEATURE_COUNT; ++f)
		check(ops->isSupported(c, f) == (f != GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER), "each feature as the info has it");
	check(ops->isSupported(c, GLI_FEATURE_TEXTURE_BARRIER), "NV's barrier counts");
	stop(c);
	i.texture_barrier = 1;
	c = start(&i);
	ops->textureBarrier(c);
	check(barriers == 1 && barriersNV == 1, "GL's own barrier first");
	stop(c);

	/* GLES */
	i = desktop();
	i.isGLESX = 1;
	c = start(&i);
	check(ops->isSupported(c, GLI_FEATURE_WEAK_BLIT_FRAMEBUFFER) && !ops->isSupported(c, GLI_FEATURE_CLIP_CONTROL)
		&& ops->isSupported(c, GLI_FEATURE_BLIT_FRAMEBUFFER), "GLES: weak blits, no clip control");
	stop(c);
	i.isGLES2 = 1;
	c = start(&i);
	check(!ops->isSupported(c, GLI_FEATURE_BLIT_FRAMEBUFFER) && !ops->isSupported(c, GLI_FEATURE_INTEGER_TEXTURES), "GLES 2: neither");
	check(ops->convertInternalTextureFormat(c, GL_RGB8) == GL_RGB && ops->convertInternalTextureFormat(c, GL_RGBA8) == GL_RGBA
		&& ops->convertInternalTextureFormat(c, GL_RGBA4) == GL_RGBA && ops->convertInternalTextureFormat(c, GL_RGB5_A1) == GL_RGBA
		&& ops->convertInternalTextureFormat(c, GL_R8) == GL_R8, "GLES 2's formats");
	stop(c);
}

static void test_passing(void)
{
	gli_gl_info i = desktop();
	void *c = start(&i);
	gli_init_texture_params ip;
	gli_update_texture_params up;
	gli_tex_parameters tp;
	gli_bind_texture_params bp;
	gli_draw_triangles_params dt;
	gli_draw_rects_params dr;
	gli_gl_cache *cache = cache_of();

	check(ops->createTexture(c, 10) == 11, "a texture by its target");
	ops->deleteTexture(c, 12);
	check(deletedTexture == 12, "deleted");
	ops->init2DTexture(c, &ip);
	check(lastParams == &ip, "storage");
	ops->update2DTexture(c, &up);
	check(lastParams == &up, "update");
	ops->setTextureParameters(c, &tp);
	check(lastParams == &tp, "parameters");
	bp.textureUnitIndex = 1; bp.target = GL_TEXTURE_2D; bp.texture = 5;
	clear_log();
	ops->bindTexture(c, &bp);
	ops->bindTexture(c, &bp);
	check(logged("bind-texture ") && cache->bindTexture.v1 == 1 && cache->bindTexture.v2 == 5, "a texture bound once");
	clear_log();
	ops->setTextureUnpackAlignment(c, 4);
	ops->setTextureUnpackAlignment(c, 4);
	check(logged("unpack "), "unpack alignment once");
	clear_log();
	ops->cullFace(c, GL_BACK);
	ops->cullFace(c, GL_BACK);
	ops->setViewport(c, 0, 0, 10, 10);
	ops->setViewport(c, 0, 0, 10, 10);
	check(logged("cull viewport "), "cull face and viewport once");
	ops->enableDepthWrite(c, 0);
	check(depthMask == GL_FALSE, "depth writes off");
	ops->enable(c, GLI_EN_DITHER, 1);
	check(ops->isEnabled(c, GLI_EN_DITHER) == 1 && ops->isEnabled(c, GLI_EN_BLEND) == GLI_PARAM_INVALID, "enables held");
	drawCalls = 0;
	ops->drawTriangles(c, &dt);
	check(drawCalls == 1 && lastParams == &dt, "triangles to the drawer");
	ops->drawRects(c, &dr);
	check(drawCalls == 11 && lastParams == &dr, "rects");
	ops->drawLine(c, 2.5f, &dt);
	check(drawCalls == 111 && lineWidth == 2.5f && lastParams == &dt, "a line");
	stop(c);
}

int main(void)
{
	ops = gli_gl_context_ops();
	test_life();
	test_clamp();
	test_clears();
	test_frame_buffers();
	test_queries();
	test_passing();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("gl context: ok\n");
	return 0;
}
