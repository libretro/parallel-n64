/* test_gliden64_glsl_builder: GLideN64's GL combiner programs
 * (glsl_CombinerProgramBuilder.c) on stub GL calls and stub programs.
 *
 * Each case a GL (GL ES 2, GL ES 3, GL; its version, perspective,
 * image textures, framebuffer fetch, interlocks and parallel compiles),
 * the options the shaders are written for (LOD, noise, legacy blending,
 * fragment depth writes, hardware lighting, the N64 depth compare,
 * multisampling, the bilinear mode and halos removal), whether lighting is
 * allowed, binaries kept, the ubershader turned off, delayed or forced,
 * programs not linking or not made: the builder made, its ubershaders
 * built and finished, its headers and end; then keys of every cycle type,
 * rect or not, each filter mode, from random muxes, each built now and
 * used, then left to the ubershader: what it reads, used before and after
 * its own program is done, another stand-in taking the ubershader over,
 * its program waited for, let go; the builder obsolete when the options
 * change, and let go. Each case's calls and shader text are held to
 * those GLideN64's C++ made, by their hash. C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Types.h>
#include <Config.h>
#include <gDP.h>
#include <GBI.h>
#include <CombinerMux.h>
#include <CombinerKey.h>
#include <gli_combiner.h>
#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Programs.h"
#include "Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every call and shader made: hashed, or printed in dump mode */
static int s_dump;
static u64 s_hash;
static unsigned long s_bytes;
static void hash_bytes(const char *_s, size_t _n)
{
	size_t i;
	for (i = 0; i < _n; ++i) {
		s_hash ^= (unsigned char)_s[i];
		s_hash *= ((u64)0x100U << 32) | 0x1b3U;
	}
	s_bytes += (unsigned long)_n;
}
static void logs(const char *_s)
{
	hash_bytes(_s, strlen(_s));
	hash_bytes(";", 1);
	if (s_dump)
		printf("%s;\n", _s);
}
static void logf1(const char *_f, long _a)
{
	char b[160];
	sprintf(b, _f, _a);
	logs(b);
}
static void logf2(const char *_f, long _a, long _c)
{
	char b[160];
	sprintf(b, _f, _a, _c);
	logs(b);
}

/* GL: shaders from 1000, programs from 1 */
static GLuint s_nextShader, s_nextProgram;
static int s_mostStrings;
static int s_linkFail;     /* every _th program does not link */
static int s_asked[4096];  /* completion asked, by program */

static GLuint APIENTRY s_create_shader(GLenum _type) { logf2("shader %ld type %lx", (long)s_nextShader, (long)_type); return s_nextShader++; }
static void APIENTRY s_shader_source(GLuint _shader, GLsizei _count, const GLchar *const *_s, const GLint *_len)
{
	GLsizei i;
	logf2("source %ld strings %ld", (long)_shader, _len == NULL ? 0L : 1L);
	/* the text, whatever strings it comes in */
	for (i = 0; i < _count; ++i) {
		hash_bytes(_s[i], strlen(_s[i]));
		if (s_dump)
			fputs(_s[i], stdout);
	}
	hash_bytes(";", 1);
	if (_count > s_mostStrings)
		s_mostStrings = _count;
}
static void APIENTRY s_compile(GLuint _shader) { logf1("compile %ld", (long)_shader); }
static GLuint APIENTRY s_create_program(void) { logf1("program %ld", (long)s_nextProgram); return s_nextProgram++; }
static void APIENTRY s_attach(GLuint _p, GLuint _s) { logf2("attach %ld %ld", (long)_p, (long)_s); }
static void APIENTRY s_parameteri(GLuint _p, GLenum _n, GLint _v) { char b[64]; sprintf(b, "param %u %x %d", _p, _n, _v); logs(b); }
static void APIENTRY s_link(GLuint _p) { logf1("link %ld", (long)_p); }
static void APIENTRY s_delete_shader(GLuint _s) { logf1("delete shader %ld", (long)_s); }
static void APIENTRY s_delete_program(GLuint _p) { logf1("delete program %ld", (long)_p); }
static void APIENTRY s_bind_attrib(GLuint _p, GLuint _i, const GLchar *_n) { char b[96]; sprintf(b, "attrib %u %u %s", _p, _i, _n); logs(b); }
static void APIENTRY s_get_programiv(GLuint _p, GLenum _n, GLint *_v)
{
	if (_n == GL_LINK_STATUS)
		*_v = (s_linkFail != 0 && _p % (GLuint)s_linkFail == 0) ? GL_FALSE : GL_TRUE;
	else if (_n == 0x91B1) /* completion: not done the first time asked */
		*_v = _p < 4096 && s_asked[_p]++ == 0 ? GL_FALSE : GL_TRUE;
	else
		*_v = 0;
	logf2("get %ld %lx", (long)_p, (long)_n);
	logf1("= %ld", (long)*_v);
}
static void APIENTRY s_get_shaderiv(GLuint _s, GLenum _n, GLint *_v) { *_v = GL_TRUE; logf2("get shader %ld %lx", (long)_s, (long)_n); }
static GLint APIENTRY s_uniform_location(GLuint _p, const GLchar *_n)
{
	char b[96];
	/* uCmbFlags not there in every program */
	const GLint loc = strcmp(_n, "uCmbFlags") == 0 && _p % 3 == 0 ? -1 : (GLint)(strlen(_n) * 7 + _p % 5);
	sprintf(b, "location %u %s = %d", _p, _n, (int)loc);
	logs(b);
	return loc;
}
static void APIENTRY s_uniform1i(GLint _l, GLint _v) { logf2("uniform %ld %ld", (long)_l, (long)_v); }
static void APIENTRY s_use(GLuint _p) { logf1("use %ld", (long)_p); }

PFNGLCREATESHADERPROC g_glCreateShader = s_create_shader;
PFNGLSHADERSOURCEPROC g_glShaderSource = s_shader_source;
PFNGLCOMPILESHADERPROC g_glCompileShader = s_compile;
PFNGLCREATEPROGRAMPROC g_glCreateProgram = s_create_program;
PFNGLATTACHSHADERPROC g_glAttachShader = s_attach;
PFNGLPROGRAMPARAMETERIPROC g_glProgramParameteri = s_parameteri;
PFNGLLINKPROGRAMPROC g_glLinkProgram = s_link;
PFNGLDELETESHADERPROC g_glDeleteShader = s_delete_shader;
PFNGLDELETEPROGRAMPROC g_glDeleteProgram = s_delete_program;
PFNGLBINDATTRIBLOCATIONPROC g_glBindAttribLocation = s_bind_attrib;
PFNGLGETPROGRAMIVPROC g_glGetProgramiv = s_get_programiv;
PFNGLGETSHADERIVPROC g_glGetShaderiv = s_get_shaderiv;
PFNGLGETUNIFORMLOCATIONPROC g_glGetUniformLocation = s_uniform_location;
PFNGLUNIFORM1IPROC g_glUniform1i = s_uniform1i;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;

/* the programs made of them: what each is made with and what is done to it */
typedef struct stub_program
{
	gli_cprog base;
	GLuint program;
	u32 inputs;
} stub_program;
#define STUB(_p) ((stub_program *)(_p))
static void stub_activate(void *_p) { logf1("activate %ld", (long)STUB(_p)->program); }
static void stub_update(void *_p, bool _force) { logf2("update %ld %ld", (long)STUB(_p)->program, (long)_force); }
static void stub_free(void *_p) { logf1("free %ld", (long)STUB(_p)->program); glDeleteProgram(STUB(_p)->program); free(_p); }
static bool stub_lod(const void *_p) { return gli_inputs_lod(((const stub_program *)_p)->inputs); }
static bool stub_texture(const void *_p) { return gli_inputs_texture(((const stub_program *)_p)->inputs); }
static bool stub_tile(const void *_p, u32 _t) { return gli_inputs_tile(((const stub_program *)_p)->inputs, _t); }
static bool stub_shade(const void *_p) { return gli_inputs_shade(((const stub_program *)_p)->inputs); }
static bool stub_hwl(const void *_p) { return gli_inputs_hw_lighting(((const stub_program *)_p)->inputs); }
const gli_cprog_ops gli_glsl_cprog_ops = { stub_activate, stub_update, stub_lod, stub_free, stub_texture,
	stub_tile, stub_shade, stub_hwl, NULL, NULL };
static int s_cprogFail; /* every _th not made */
static int s_cprogs;
gli_cprog *gli_glsl_cprog_new(u64 _key, GLuint _program, struct gli_gl_cache1 *_useProgram, u32 _inputs,
	const struct gli_gl_info *_glinfo)
{
	stub_program *p;
	char b[128];
	sprintf(b, "cprog %08x%08x %u inputs %x", (unsigned)(_key >> 32), (unsigned)_key, _program, (unsigned)_inputs);
	logs(b);
	(void)_useProgram;
	(void)_glinfo;
	if (s_cprogFail != 0 && ++s_cprogs % s_cprogFail == 0) {
		glDeleteProgram(_program);
		return NULL;
	}
	p = (stub_program *)calloc(1, sizeof(*p));
	p->base.ops = &gli_glsl_cprog_ops;
	p->base.key = _key;
	p->program = _program;
	p->inputs = _inputs;
	return &p->base;
}

/* what the builder reads of the emulation */
struct Config config;
struct gDPInfo gDP;
bool gli_GBI_hwlSupported;
unsigned LegacySm64ToolsHacks;
static u32 s_options;
static bool s_hwlAllowed, s_cacheSupported;
u32 gli_combiner_options_bits(void) { return s_options; }
bool gli_isHWLightingAllowed(void) { return s_hwlAllowed; }
bool gli_combiner_shader_cache_supported(void) { return s_cacheSupported; }
bool gli_combiner_is_rect_mode(void) { return false; }

static u64 s_rand;
static u32 rnd(void)
{
	s_rand = s_rand * (((u64)0x5851f42dU << 32) | 0x4c957f2dU) + 1442695040888963407ULL;
	return (u32)(s_rand >> 33);
}
#define BIT() ((rnd() & 1) != 0)

static void set_env(const char *_name, const char *_value)
{
#ifdef _WIN32
	_putenv_s(_name, _value != NULL ? _value : "");
#else
	if (_value != NULL)
		setenv(_name, _value, 1);
	else
		unsetenv(_name);
#endif
}

/* the GL and options of case @_case */
static void set_case(gli_gl_info *_info, unsigned _case)
{
	static const char *const env[4] = { NULL, "off", "delay", "1" };
	static const GLint gl[4][2] = { { 3, 3 }, { 4, 1 }, { 4, 2 }, { 4, 6 } };
	static const GLint es[3][2] = { { 3, 0 }, { 3, 1 }, { 3, 2 } };
	int i;
	s_rand = (u64)_case * 2654435761U + 7;
	memset(_info, 0, sizeof(*_info));
	i = (int)(rnd() % 4);
	if (i == 0)
		_info->isGLES2 = true;
	else if (i == 1) {
		_info->isGLESX = true;
		i = (int)(rnd() % 3);
		_info->majorVersion = es[i][0];
		_info->minorVersion = es[i][1];
	} else {
		i = (int)(rnd() % 4);
		_info->majorVersion = gl[i][0];
		_info->minorVersion = gl[i][1];
	}
	if (_info->isGLES2) {
		_info->majorVersion = 2;
		_info->minorVersion = 0;
	}
	_info->noPerspective = BIT();
	_info->imageTextures = BIT();
	_info->ext_fetch = BIT();
	_info->fragment_interlock = BIT();
	_info->fragment_interlockNV = BIT();
	_info->fragment_ordering = BIT();
	_info->parallelShaderCompile = BIT();

	memset(&config, 0, sizeof(config));
	config.generalEmulation.enableLOD = BIT();
	config.generalEmulation.enableNoise = BIT();
	config.generalEmulation.enableLegacyBlending = BIT();
	config.generalEmulation.enableFragmentDepthWrite = BIT();
	config.generalEmulation.enableHWLighting = BIT();
	config.frameBufferEmulation.N64DepthCompare = BIT();
	config.video.multisampling = (int)(rnd() % 3); /* none, or any count above it */
	config.texture.bilinearMode = BIT() ? BILINEAR_STANDARD : BILINEAR_3POINT;
	config.texture.enableHalosRemoval = BIT();

	s_hwlAllowed = BIT();
	s_cacheSupported = BIT();
	g_glProgramParameteri = BIT() ? s_parameteri : NULL;
	s_linkFail = BIT() ? 7 : 0;
	s_cprogFail = (rnd() % 4) == 0 ? 11 : 0;
	s_cprogs = 0;
	s_options = rnd();
	set_env("GLIDEN64_UBERSHADER", env[rnd() % 4]);
	s_nextShader = 1000;
	s_nextProgram = 1;
	memset(s_asked, 0, sizeof(s_asked));
}

/* a key: a random mux, its mode bits random */
static u64 random_key(u32 *_cycle)
{
	gDPCombine c;
	u32 flags;
	c.mux = ((u64)rnd() << 32) | rnd();
	*_cycle = rnd() % 4;
	flags = (rnd() & 1) | (*_cycle << 1) | ((rnd() & 3) << 3) | ((rnd() & 1) << 5);
	c.muxs0 = (c.muxs0 & 0x00FFFFFFU) | (flags << 24);
	return c.mux;
}

static void log_reads(gli_cprog *_p)
{
	char b[96];
	sprintf(b, "reads tex=%d tile0=%d tile1=%d shade=%d lod=%d hw=%d size=%d alpha=%d",
		(int)_p->ops->usesTexture(_p), (int)_p->ops->usesTile(_p, 0), (int)_p->ops->usesTile(_p, 1),
		(int)_p->ops->usesShade(_p), (int)_p->ops->usesLOD(_p), (int)_p->ops->usesHwLighting(_p),
		_p->ops->setTextureSize != NULL, _p->ops->setEnableAlphaTest != NULL);
	logs(b);
}

#define KEYS 16

static void run_case(unsigned _case)
{
	gli_gl_info info;
	gli_gl_cache1 cache;
	gli_glsl_programs programs;
	gli_cprog *held = NULL;
	int k;
	set_case(&info, _case);
	cache.v = 0;
	programs.special = NULL;
	programs.builder = gli_glsl_builder_new(&info, &cache);
	logs("made");
	logs(gli_glsl_builder_part(programs.builder, GLI_GLSL_VERTEX_HEADER));
	logs(gli_glsl_builder_part(programs.builder, GLI_GLSL_FRAGMENT_HEADER));
	logs(gli_glsl_builder_part(programs.builder, GLI_GLSL_FRAGMENT_END));
	logf2("uber only %ld obsolete %ld", (long)gli_glsl_builder_uber_only(programs.builder),
		(long)gli_glsl_builder_obsolete(programs.builder));

	for (k = 0; k < KEYS; ++k) {
		Combiner color, alpha;
		u32 cycle;
		const u64 key = random_key(&cycle);
		gli_cprog *p;
		logf2("key %lx%08lx", (long)(key >> 32), (long)(key & 0xFFFFFFFFU));
		gli_combiner_decode(key & ~((u64)0xFFU << 56), cycle, &color, &alpha);
		if (k % 5 == 3) {
			color.numStages = 1;
			alpha.numStages = 1;
		}
		p = gli_glsl_ops_build(&programs, &color, &alpha, key);
		if (p != NULL) {
			log_reads(p);
			p->ops->activate(p);
			p->ops->update(p, true);
			p->ops->free_program(p);
		} else
			logs("not built");

		gli_combiner_decode(key & ~((u64)0xFFU << 56), cycle, &color, &alpha);
		/* a single stage two cycles read: the first again */
		if (k % 5 == 3) {
			color.numStages = 1;
			alpha.numStages = 1;
		}
		/* one of them a single stage */
		if (k % 7 == 5)
			alpha.numStages = 1;
		p = gli_glsl_ops_build_async(&programs, &color, &alpha, key);
		if (p == NULL) {
			logs("not deferred");
			continue;
		}
		log_reads(p);
		if (k % 4 == 2) {
			/* let go before its program is done */
			p->ops->free_program(p);
			continue;
		}
		if (k % 4 == 1) {
			/* its program waited for before it was ever used */
			const gli_cprog *real = gli_glsl_async_program(p);
			logf1("waited first %ld", real != NULL ? (long)((const stub_program *)real)->program : -1L);
		}
		p->ops->activate(p);
		p->ops->update(p, false);
		p->ops->activate(p);
		p->ops->update(p, true);
		if (held != NULL) {
			/* the other takes the ubershader back */
			held->ops->activate(held);
			p->ops->activate(p);
			if ((k & 1) != 0) {
				const gli_cprog *real = gli_glsl_async_program(held);
				logf1("waited %ld", real != NULL ? (long)((const stub_program *)real)->program : -1L);
				held->ops->activate(held);
			}
			held->ops->free_program(held);
		}
		held = p;
	}
	if (held != NULL) {
		held->ops->update(held, false);
		held->ops->free_program(held);
	}
	s_options ^= 1;
	logf1("obsolete %ld", (long)gli_glsl_builder_obsolete(programs.builder));
	gli_glsl_builder_delete(programs.builder);
	logs("deleted");
}

#define CASES 1000
#include "test_gliden64_glsl_builder.h"

int main(int argc, char **argv)
{
	unsigned c;
	const int hashes = argc > 1 && strcmp(argv[1], "hashes") == 0;
	s_dump = argc > 2 && strcmp(argv[1], "dump") == 0;
	if (s_dump) {
		run_case((unsigned)atoi(argv[2]));
		return 0;
	}
	if (hashes)
		printf("static const u64 s_want[%d] = {\n", CASES);
	for (c = 0; c < CASES; ++c) {
		s_hash = ((u64)0xcbf29ce4U << 32) | 0x84222325U;
		run_case(c);
		if (hashes)
			printf("\t((u64)0x%08xU << 32) | 0x%08xU,\n", (unsigned)(s_hash >> 32), (unsigned)s_hash);
		else if (s_hash != s_want[c]) {
			printf("FAIL case %u: %08x%08x\n", c, (unsigned)(s_hash >> 32), (unsigned)s_hash);
			++fails;
		}
	}
	if (hashes) {
		printf("};\n");
		return 0;
	}
	/* room for the largest shader written */
	CHECK(s_mostStrings < 2048 * 3 / 4);
	if (fails) {
		printf("test_gliden64_glsl_builder: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_glsl_builder: ok (%lu bytes, %d strings at most)\n", s_bytes, s_mostStrings);
	return 0;
}
