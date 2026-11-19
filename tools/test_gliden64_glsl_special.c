/* test_gliden64_glsl_special: GLideN64's special GL shaders
 * (glsl_SpecialShadersFactory.c) on stub GL calls.
 *
 * Each of the six made for every GL it can be made on and every option it
 * reads: the depth fog (none on GL ES 2), the texrect drawer's draw and
 * clear, the texrect copy, gamma correction and FXAA, each from the
 * builder's headers, its end where it has one, and its own lines; the
 * uniforms it finds and the values it sets when made; what it reads; its
 * uniforms sent when it is used: the fog colour and the lookup tables'
 * units, the drawer's primitive depth when it changed, FXAA's texture
 * size when the shown buffer's changed; the drawer's texture size and
 * alpha test; and let go. Each case's calls and shader text are held to
 * those GLideN64's C++ made, by their hash. C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Types.h>
#include <Config.h>
#include <gDP.h>
#include <GBI.h>
#include <N64.h>
#include <FrameBuffer.h>
#include <gli_tex.h>
#include <gli_combiner.h>
#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Utils.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Programs.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every call and shader made, as text */
static char s_log[65536];
static size_t s_len;
static void logs(const char *s)
{
	size_t n = strlen(s);
	if (s_len + n + 2 < sizeof(s_log)) {
		memcpy(s_log + s_len, s, n);
		s_len += n;
		s_log[s_len++] = ';';
		s_log[s_len] = '\0';
	}
}
static void reset(void) { s_len = 0; s_log[0] = '\0'; }

/* the uniforms: a name's location is its place here plus 100; with
 * s_noOptional the drawer's and FXAA's optional ones are not there */
static const char *const s_names[] = {
	"uFogColor", "uZlutImage", "uTlutImage", "uDepthImage", "uTex0", "uTextureSize",
	"uEnableAlphaTest", "uPrimDepth", "uGammaCorrectionLevel", NULL
};
static int s_noOptional;
static int s_depthAtZero; /* uPrimDepth at location 0 */
static GLint APIENTRY s_get_uniform(GLuint p, const GLchar *n)
{
	char b[96];
	GLint loc = -1;
	int i;
	for (i = 0; s_names[i] != NULL; ++i)
		if (strcmp(s_names[i], n) == 0)
			loc = 100 + i;
	if (s_noOptional && (strcmp(n, "uTextureSize") == 0 || strcmp(n, "uPrimDepth") == 0))
		loc = -1;
	if (s_depthAtZero && strcmp(n, "uPrimDepth") == 0)
		loc = 0;
	sprintf(b, "loc %u %s=%d", p, n, (int)loc);
	logs(b);
	return loc;
}
static void APIENTRY s_u1i(GLint l, GLint a) { char b[64]; sprintf(b, "1i %d %d", (int)l, (int)a); logs(b); }
static void APIENTRY s_u1f(GLint l, GLfloat a) { char b[64]; sprintf(b, "1f %d %g", (int)l, a); logs(b); }
static void APIENTRY s_u2f(GLint l, GLfloat a, GLfloat c) { char b[64]; sprintf(b, "2f %d %g %g", (int)l, a, c); logs(b); }
static void APIENTRY s_u4fv(GLint l, GLsizei n, const GLfloat *v)
{ char b[96]; sprintf(b, "4fv %d %d %g %g %g %g", (int)l, (int)n, v[0], v[1], v[2], v[3]); logs(b); }
static void APIENTRY s_use(GLuint p) { char b[32]; sprintf(b, "use %u", p); logs(b); }
static void APIENTRY s_delete(GLuint p) { char b[32]; sprintf(b, "del %u", p); logs(b); }
static void APIENTRY s_blend(GLenum a, GLenum c) { char b[48]; sprintf(b, "blend %u %u", (unsigned)a, (unsigned)c); logs(b); }

PFNGLGETUNIFORMLOCATIONPROC g_glGetUniformLocation = s_get_uniform;
PFNGLUNIFORM1IPROC g_glUniform1i = s_u1i;
PFNGLUNIFORM1FPROC g_glUniform1f = s_u1f;
PFNGLUNIFORM2FPROC g_glUniform2f = s_u2f;
PFNGLUNIFORM4FVPROC g_glUniform4fv = s_u4fv;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;
PFNGLDELETEPROGRAMPROC g_glDeleteProgram = s_delete;
PFNGLBLENDFUNCPROC g_glBlendFunc = s_blend;

/* a rect program: its two sources logged whole, the strings each is made
 * of joined */
static GLuint s_next;
static int s_parts;  /* the most strings a source was made of */
static void log_source(const char *_what, const GLchar *const *_s, GLsizei _n)
{
	static char b[16384];
	size_t len = strlen(_what);
	GLsizei i;
	memcpy(b, _what, len);
	for (i = 0; i < _n; ++i) {
		size_t n = strlen(_s[i]);
		if (len + n + 1 < sizeof(b)) {
			memcpy(b + len, _s[i], n);
			len += n;
		}
	}
	b[len] = '\0';
	if (_n > s_parts)
		s_parts = _n;
	logs(b);
}
GLuint gli_glsl_rect_program(const GLchar *const *_vertex, GLsizei _vertexCount,
	const GLchar *const *_fragment, GLsizei _fragmentCount)
{
	char b[32];
	log_source("vs:", _vertex, _vertexCount);
	log_source("fs:", _fragment, _fragmentCount);
	sprintf(b, "program=%u", s_next);
	logs(b);
	return s_next++;
}

/* the builder's headers and end */
static int s_otherPart;
const char *gli_glsl_builder_part(const gli_glsl_builder *_builder, int _part)
{
	(void)_builder;
	switch (_part) {
	case GLI_GLSL_VERTEX_HEADER: return "<vertex header>\n";
	case GLI_GLSL_FRAGMENT_HEADER: return "<fragment header>\n";
	case GLI_GLSL_FRAGMENT_END: return "<fragment end>\n";
	}
	s_otherPart = 1;
	return "?";
}

/* what the shaders read of the emulation */
struct Config config;
struct gDPInfo gDP;
N64Regs REG;
static u32 s_viOrigin = 0x1234;
static struct FrameBuffer s_fb;
static struct gli_tex s_fbTexture;
static struct FrameBuffer *s_shown;
static u32 s_asked;
struct FrameBuffer *gli_fb_find(u32 _address) { s_asked = _address; logs("find"); return s_shown; }
void gli_palette_update(void) { logs("palette"); }

/* the shaders of one case, the bits of @_case: GL ES 2, image textures,
 * framebuffer fetch, ARM's depth fetch; the N64 depth compare, fragment
 * depth writes, standard bilinear filtering, forced gamma; the optional
 * uniforms gone */
static void set_case(gli_gl_info *_info, unsigned _case)
{
	memset(_info, 0, sizeof(*_info));
	_info->isGLES2 = (_case & 1) != 0;
	_info->imageTextures = (_case & 2) != 0;
	_info->ext_fetch = (_case & 4) != 0;
	_info->fetch_depth = (_case & 8) != 0;
	memset(&config, 0, sizeof(config));
	config.frameBufferEmulation.N64DepthCompare = (_case & 16) != 0;
	config.generalEmulation.enableFragmentDepthWrite = (_case & 32) != 0;
	config.texture.bilinearMode = (_case & 64) != 0 ? BILINEAR_STANDARD : 0;
	config.gammaCorrection.force = (_case & 128) != 0;
	config.gammaCorrection.level = 1.5f;
	s_noOptional = (_case & 256) != 0;
}
#define CASES 512

static void log_reads(gli_cprog *_p)
{
	char b[96];
	sprintf(b, "reads tex=%d tile0=%d tile1=%d shade=%d lod=%d hw=%d key=%u size=%d alpha=%d",
		(int)_p->ops->usesTexture(_p), (int)_p->ops->usesTile(_p, 0), (int)_p->ops->usesTile(_p, 1),
		(int)_p->ops->usesShade(_p), (int)_p->ops->usesLOD(_p), (int)_p->ops->usesHwLighting(_p),
		(unsigned)_p->key, _p->ops->setTextureSize != NULL, _p->ops->setEnableAlphaTest != NULL);
	logs(b);
}

/* used: what it sends, and whether it marks the combiner changed */
static void activate(gli_cprog *_p)
{
	char b[32];
	gDP.changed = 0;
	_p->ops->activate(_p);
	sprintf(b, "changed=%u", (unsigned)(gDP.changed & CHANGED_COMBINE));
	logs(b);
}

static void named(const char *_name)
{
	char b[32];
	sprintf(b, "[%s]", _name);
	logs(b);
}

static gli_cprog *made(gli_cprog *_p)
{
	if (_p != NULL) {
		log_reads(_p);
		_p->ops->update(_p, 1);
	}
	return _p;
}

static void let_go(gli_cprog *_p)
{
	if (_p != NULL)
		_p->ops->free_program(_p);
}

/* every shader of the case, made, used and let go */
static void run_case(unsigned _case)
{
	gli_gl_info info;
	gli_gl_cache1 useProgram;
	gli_glsl_programs programs;
	gli_cprog *p;
	char b[32];

	set_case(&info, _case);
	useProgram.v = 0;
	programs.builder = NULL;
	programs.special = gli_glsl_special_new(&info, &useProgram, programs.builder);
	CHECK(programs.special != NULL);
	s_next = 10;
	reset();

	named("depth fog");
	p = made(gli_glsl_ops_depth_fog_shader(&programs));
	if (p != NULL) {
		gDP.fogColor.r = 0.25f;
		gDP.fogColor.g = 0.5f;
		gDP.fogColor.b = 0.75f;
		gDP.fogColor.a = 1.0f;
		activate(p);
		gDP.fogColor.r = 0.125f;
		activate(p);
	}
	let_go(p);

	named("texrect draw");
	p = made(gli_glsl_ops_texrect_draw_shader(&programs));
	gDP.otherMode.depthSource = 0;
	gDP.primDepth.z = 0.5f;
	activate(p);            /* 0, as it was */
	gDP.otherMode.depthSource = G_ZS_PRIM;
	activate(p);            /* the primitive's */
	activate(p);            /* the same */
	gDP.primDepth.z = 0.25f;
	activate(p);
	gDP.otherMode.depthSource = 0;
	activate(p);
	useProgram.v = 0;
	gDP.changed = 0;
	p->ops->setTextureSize(p, 64, 32);
	sprintf(b, "changed=%u", (unsigned)(gDP.changed & CHANGED_COMBINE));
	logs(b);
	useProgram.v = 0;
	gDP.changed = 0;
	p->ops->setEnableAlphaTest(p, 1);
	sprintf(b, "changed=%u", (unsigned)(gDP.changed & CHANGED_COMBINE));
	logs(b);
	p->ops->setEnableAlphaTest(p, 0);
	let_go(p);

	named("texrect clear");
	p = made(gli_glsl_ops_texrect_clear_shader(&programs));
	activate(p);
	let_go(p);

	named("texrect copy");
	p = made(gli_glsl_ops_texrect_copy_shader(&programs));
	activate(p);
	let_go(p);

	named("gamma");
	p = made(gli_glsl_ops_gamma_correction_shader(&programs));
	activate(p);
	let_go(p);

	named("fxaa");
	p = made(gli_glsl_ops_fxaa_shader(&programs));
	s_shown = NULL;
	activate(p);            /* no buffer shown */
	s_shown = &s_fb;
	s_fb.fb.m_pTexture = NULL;
	activate(p);            /* one without its texture */
	s_fb.fb.m_pTexture = &s_fbTexture;
	s_fbTexture.realWidth = 320;
	s_fbTexture.realHeight = 240;
	activate(p);            /* sent */
	activate(p);            /* the same */
	s_fbTexture.realWidth = 640;
	activate(p);            /* the width */
	s_fbTexture.realHeight = 480;
	activate(p);            /* the height */
	s_shown = NULL;
	activate(p);
	let_go(p);
	CHECK(s_asked == s_viOrigin);

	gli_glsl_special_delete(programs.special);
}

/* FNV-1a */
static u32 hash(const char *_s)
{
	u32 h = 2166136261U;
	for (; *_s != '\0'; ++_s) {
		h ^= (u8)*_s;
		h *= 16777619U;
	}
	return h;
}

/* each case's log, as GLideN64's C++ wrote it */
static const u32 s_expected[CASES] = {
#include "test_gliden64_glsl_special.h"
};

/* a few of the calls spelled out */
static void spelled_out(void)
{
	/* the depth fog on GL with no depth compare: its shader made, its
	 * uniforms found, then the fog colour and the tables' units sent */
	run_case(0);
	CHECK(strstr(s_log, "use 0;reads tex=1 tile0=1 tile1=0 shade=0 lod=0 hw=0 key=0 size=0 alpha=0;use 10;4fv") != NULL);
	CHECK(strstr(s_log, "program=10;use 10;loc 10 uFogColor=100;loc 10 uZlutImage=101;loc 10 uTlutImage=102;"
		"loc 10 uDepthImage=103;use 0;") != NULL);
	CHECK(strstr(s_log, "use 10;4fv 100 1 0.25 0.5 0.75 1;1i 101 4;1i 102 5;1i 103 0;blend 770 771;palette;changed=64;") != NULL);
	CHECK(strstr(s_log, "fs:<fragment header>\nuniform lowp usampler2D uZlutImage;\n") != NULL);
	CHECK(strstr(s_log, "  highp float bufZ = texelFetch(uDepthImage,coord, 0).r;") != NULL);
	CHECK(strstr(s_log, "use 0;del 10;") != NULL);
	/* the drawer: its depth sent when it changes */
	CHECK(strstr(s_log, "use 0;reads tex=1 tile0=1 tile1=0 shade=0 lod=0 hw=0 key=0 size=1 alpha=1;use 11;") != NULL);
	CHECK(strstr(s_log, "use 11;loc 11 uTex0=104;1i 104 0;loc 11 uTextureSize=105;loc 11 uEnableAlphaTest=106;"
		"loc 11 uPrimDepth=107;use 0;") != NULL);
	CHECK(strstr(s_log, "use 11;changed=64;1f 107 0.5;changed=64;changed=64;1f 107 0.25;changed=64;"
		"1f 107 0;changed=64;use 11;2f 105 64 32;changed=64;use 11;1i 106 1;changed=64;1i 106 0;use 0;del 11;") != NULL);
	/* FXAA: its size when the buffer shown is another's */
	CHECK(strstr(s_log, "use 15;find;changed=64;find;changed=64;find;2f 105 320 240;changed=64;find;changed=64;"
		"find;2f 105 640 240;changed=64;find;2f 105 640 480;changed=64;find;changed=64;use 0;del 15;") != NULL);
	CHECK(strstr(s_log, "<fragment end>\n;program=15;") != NULL);
	/* gamma correction at 2 unless forced */
	CHECK(strstr(s_log, "loc 14 uGammaCorrectionLevel=108;1f 108 2;use 0;") != NULL);
	run_case(128);
	CHECK(strstr(s_log, "loc 14 uGammaCorrectionLevel=108;1f 108 1.5;use 0;") != NULL);
	/* none on GL ES 2 */
	run_case(1);
	CHECK(strstr(s_log, "[depth fog];[texrect draw];vs:") != NULL);
	/* without the optional uniforms the drawer sends neither */
	run_case(256);
	CHECK(strstr(s_log, "use 11;changed=64;changed=64;changed=64;changed=64;changed=64;changed=0;use 11;1i 106 1;") != NULL);
	/* a location of 0 is one */
	s_depthAtZero = 1;
	run_case(0);
	s_depthAtZero = 0;
	CHECK(strstr(s_log, "use 11;changed=64;1f 0 0.5;changed=64;") != NULL);
	CHECK(s_otherPart == 0);
}

int main(int argc, char **argv)
{
	unsigned c;
	int dump = argc > 1 && strcmp(argv[1], "dump") == 0;
	int hashes = argc > 1 && strcmp(argv[1], "hashes") == 0;
	REG.VI_ORIGIN = &s_viOrigin;
	for (c = 0; c < CASES; ++c) {
		run_case(c);
		if (dump)
			printf("== %u\n%s\n", c, s_log);
		else if (hashes)
			printf("0x%08xU,%s", (unsigned)hash(s_log), (c & 7) == 7 ? "\n" : " ");
		else if (hash(s_log) != s_expected[c]) {
			printf("FAIL case %u: 0x%08x\n", c, (unsigned)hash(s_log));
			fails++;
		}
	}
	if (dump || hashes)
		return 0;
	spelled_out();
	/* the most strings a shader is made of fit */
	CHECK(s_parts > 1 && s_parts < 96);
	if (fails == 0)
		printf("test_gliden64_glsl_special: ok\n");
	return fails != 0;
}
