/* test_gliden64_glsl_storage: GLideN64's GL shader storage
 * (glsl_ShaderStorage.c) on in-memory files, stub GL calls and stub
 * combiner programs, with the real GL program behind it
 * (glsl_CombinerProgramImpl.c).
 *
 * The files named from the cache folder, the game's hash and GL or GL
 * ES, in the shaders folder or beside it; the keys journal's path and
 * version. Saved: the keys as text, by key, with the hardware lighting
 * flag; the shaders after them, unless the builder is obsolete, GL has
 * no binaries or no context: the header, the count, and each program's
 * key, inputs, format, length and binary, the ubershader's stand-ins as
 * the program they became, those without a binary left out. Loaded: the
 * shaders, each given to GL with its attributes, made a program and
 * updated, one GL will not link compiled from its key, a file cut short
 * stopping; any header that does not match, or no binaries, the keys
 * instead, each compiled and updated, the hardware lighting flag read,
 * the shaders saved again after. C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Programs.h"
#include "Graphics/gli_context.h"
#include "Graphics/CombinerStages.h"
#include "CombinerKey.h"
#include "gli_combiner.h"
#include "RSP.h"
#include "GBI.h"
#include "Config.h"
#include "PluginAPI.h"
#include <file/file_path.h>
#include <streams/file_stream.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- files, in memory ---- */
#define FILES 8
static struct { char path[1024]; char *data; int64_t size; } s_files[FILES];
static int s_mkdirOk = 1;
static int s_writes;

static int file_index(const char *_path)
{
	int i;
	for (i = 0; i < FILES; ++i)
		if (s_files[i].data != NULL && strcmp(s_files[i].path, _path) == 0)
			return i;
	return -1;
}

static const char *s_failWrite; /* a file whose writes fail */
bool filestream_write_file(const char *_path, const void *_data, int64_t _size)
{
	int i = file_index(_path);
	++s_writes;
	if (s_failWrite != NULL && strstr(_path, s_failWrite) != NULL)
		return false;
	if (i < 0)
		for (i = 0; i < FILES && s_files[i].data != NULL; ++i)
			;
	if (i >= FILES)
		return false;
	free(s_files[i].data);
	strcpy(s_files[i].path, _path);
	s_files[i].data = (char *)malloc((size_t)_size + 1);
	memcpy(s_files[i].data, _data, (size_t)_size);
	s_files[i].size = _size;
	return true;
}

int64_t filestream_read_file(const char *_path, void **_buf, int64_t *_len)
{
	int i = file_index(_path);
	char *b;
	if (i < 0) {
		*_buf = NULL;
		*_len = 0;
		return 0;
	}
	b = (char *)malloc((size_t)s_files[i].size + 1);
	memcpy(b, s_files[i].data, (size_t)s_files[i].size);
	b[s_files[i].size] = '\0';
	*_buf = b;
	*_len = s_files[i].size;
	return 1;
}

static const char *file_data(const char *_path, int64_t *_size)
{
	int i = file_index(_path);
	if (i < 0)
		return NULL;
	*_size = s_files[i].size;
	return s_files[i].data;
}

static void drop_files(void)
{
	int i;
	for (i = 0; i < FILES; ++i) {
		free(s_files[i].data);
		s_files[i].data = NULL;
	}
	s_writes = 0;
}

static void set_file(const char *_path, const void *_data, size_t _size)
{
	filestream_write_file(_path, _data, (int64_t)_size);
	s_writes = 0;
}

static char s_mkdirAsked[1024];
bool path_mkdir(const char *_dir) { strcpy(s_mkdirAsked, _dir); return s_mkdirOk != 0; }
static const char *s_cacheDir = "/cache";
void gli_api_user_cache_path(char *_strPath, size_t _size) { strncpy(_strPath, s_cacheDir, _size); }

RSPInfo RSP;
static char s_hashAsked[32];
u32 gli_glsl_rom_hash(const char *_romname) { strcpy(s_hashAsked, _romname); return 0x8f506950u; }

bool gli_GBI_hwlSupported;
static u32 s_options = 0x1234u;
u32 gli_combiner_options_bits(void) { return s_options; }

/* ---- the context ---- */
static int s_obsolete;
static bool ctx_obsolete(void *_impl) { (void)_impl; return s_obsolete != 0; }
static gli_context_ops s_ctxOps;
gli_context gli_ctx;

/* ---- GL ---- */
static char s_log[8192];
static void logs(const char *s)
{
	if (strlen(s_log) + strlen(s) + 2 < sizeof(s_log)) {
		strcat(s_log, s);
		strcat(s_log, ";");
	}
}
static void reset(void) { s_log[0] = '\0'; }

static const char *s_renderer = "Stub Renderer";
static const char *s_version = "4.6 Stub";
static const GLubyte *APIENTRY s_get_string(GLenum e)
{
	return (const GLubyte *)(e == GL_RENDERER ? s_renderer : e == GL_VERSION ? s_version : NULL);
}

/* each GL program: the binary length GL says, whether it links */
#define PROGS 64
static GLint s_binLen[PROGS];
static int s_noLink[PROGS];
static GLuint s_nextProgram = 1;
static GLuint s_errorProgram; /* GL errs taking this program's binary */
static GLuint s_lastBinary;
static GLuint APIENTRY s_create_program(void) { char b[32]; sprintf(b, "create %u", s_nextProgram); logs(b); return s_nextProgram++; }
static void APIENTRY s_delete_program(GLuint p) { char b[32]; sprintf(b, "del %u", p); logs(b); }
static void APIENTRY s_get_programiv(GLuint p, GLenum e, GLint *v)
{
	if (e == GL_PROGRAM_BINARY_LENGTH)
		*v = s_binLen[p % PROGS];
	else if (e == GL_LINK_STATUS)
		*v = s_noLink[p % PROGS] ? GL_FALSE : GL_TRUE;
	else
		*v = 0;
}
static void APIENTRY s_get_binary(GLuint p, GLsizei size, GLsizei *len, GLenum *fmt, void *out)
{
	GLint i;
	s_lastBinary = p;
	for (i = 0; i < s_binLen[p % PROGS] && i < size; ++i)
		((unsigned char *)out)[i] = (unsigned char)(p * 16 + i);
	*len = s_binLen[p % PROGS] < size ? s_binLen[p % PROGS] : size;
	*fmt = 0x8000 + p;
}
static void APIENTRY s_program_binary(GLuint p, GLenum fmt, const void *bin, GLsizei len)
{
	char b[96];
	sprintf(b, "binary %u %x %d %02x", p, (unsigned)fmt, (int)len, len > 0 ? ((const unsigned char *)bin)[0] : 0);
	logs(b);
}
static void APIENTRY s_bind_attrib(GLuint p, GLuint i, const GLchar *n)
{ char b[96]; sprintf(b, "attr %u %s", p, n); (void)i; logs(b); }
static GLint APIENTRY s_get_uniform(GLuint p, const GLchar *n) { (void)p; (void)n; return -1; }
static void APIENTRY s_use(GLuint p) { (void)p; }

PFNGLGETSTRINGPROC g_glGetString = s_get_string;
PFNGLCREATEPROGRAMPROC g_glCreateProgram = s_create_program;
PFNGLDELETEPROGRAMPROC g_glDeleteProgram = s_delete_program;
PFNGLGETPROGRAMIVPROC g_glGetProgramiv = s_get_programiv;
PFNGLGETPROGRAMBINARYPROC g_glGetProgramBinary = s_get_binary;
PFNGLPROGRAMBINARYPROC g_glProgramBinary = s_program_binary;
PFNGLBINDATTRIBLOCATIONPROC g_glBindAttribLocation = s_bind_attrib;
PFNGLGETUNIFORMLOCATIONPROC g_glGetUniformLocation = s_get_uniform;
PFNGLUSEPROGRAMPROC g_glUseProgram = s_use;
PFNGLUNIFORM1IPROC g_glUniform1i;
PFNGLUNIFORM2IPROC g_glUniform2i;
PFNGLUNIFORM4IPROC g_glUniform4i;
PFNGLUNIFORM1FPROC g_glUniform1f;
PFNGLUNIFORM2FPROC g_glUniform2f;
PFNGLUNIFORM3FVPROC g_glUniform3fv;
PFNGLUNIFORM4FVPROC g_glUniform4fv;

static int s_glError;
int gli_gl_has_error(void) { return s_glError || (s_errorProgram != 0 && s_lastBinary == s_errorProgram); }

unsigned combinerValueGroups(const CombinerNeeds *_needs) { (void)_needs; return 0; }
void updateCombinerValues(CombinerValues *_values, unsigned _groups) { (void)_values; (void)_groups; }
u32 gli_combiner_key_cycle_type(u64 _key) { (void)_key; return 0; }
/* bit 40 of a key here marks a rect */
bool gli_combiner_key_is_rect(u64 _key) { return ((_key >> 40) & 1) != 0; }

/* ---- the combiners: a table of what was put, the programs a key
 * compiles to, and the ubershader's stand-ins ---- */
static gli_cprog *s_put[PROGS];
static u32 s_putCount;
void gli_combiners_put(gli_combiner_table *_table, gli_cprog *_program)
{
	(void)_table;
	s_put[s_putCount++] = _program;
}
/* sorted as the test lays them out */
static gli_cprog *s_sorted[PROGS];
u32 gli_combiners_sorted(const gli_combiner_table *_table, gli_cprog **_out)
{
	memcpy(_out, s_sorted, _table->count * sizeof(*_out));
	return _table->count;
}

typedef struct fake { gli_cprog base; int updates; int forced; const gli_cprog *real; } fake;
static void fake_update(void *_p, bool _force) { ((fake *)_p)->updates++; ((fake *)_p)->forced += _force ? 1 : 0; }
static gli_cprog_ops s_fakeOps;
static fake s_compiled[PROGS];
static u32 s_compiledCount;
gli_cprog *gli_combiner_compile(u64 _key, bool _mayDefer)
{
	fake *f = &s_compiled[s_compiledCount++];
	CHECK(!_mayDefer);
	memset(f, 0, sizeof(*f));
	f->base.ops = &s_fakeOps;
	f->base.key = _key;
	return &f->base;
}
static int s_asyncAsked;
const gli_cprog *gli_glsl_async_program(gli_cprog *_program)
{
	++s_asyncAsked;
	return ((fake *)_program)->real;
}

static void reset_combiners(void)
{
	u32 i;
	for (i = 0; i < s_putCount; ++i)
		if (s_put[i]->ops == &gli_glsl_cprog_ops)
			s_put[i]->ops->free_program(s_put[i]);
	s_putCount = 0;
	s_compiledCount = 0;
}

static gli_gl_info s_info;
static gli_gl_cache1 s_cache;

#define KEYS "/cache/shaders/GLideN64.8f506950.OpenGL.keys"
#define SHADERS "/cache/shaders/GLideN64.8f506950.OpenGL.shaders"
#define HEAD_SIZE (sizeof(u64) + sizeof(int) + sizeof(GLenum) + sizeof(GLint))

static void paths(void)
{
	char path[1024];
	u32 version = 0;
	strcpy(RSP.romname, "SUPER MARIO 64");
	gli_glsl_journal_path(&s_info, &s_cache, path, sizeof(path), &version);
	CHECK(version == 5);
	CHECK(strcmp(path, "/cache/shaders/GLideN64.8f506950.OpenGL.keys.journal") == 0);
	CHECK(strcmp(s_mkdirAsked, "/cache/shaders") == 0);
	CHECK(strcmp(s_hashAsked, "SUPER MARIO 64") == 0);
	s_info.isGLESX = 1;
	gli_glsl_journal_path(&s_info, &s_cache, path, sizeof(path), &version);
	CHECK(strcmp(path, "/cache/shaders/GLideN64.8f506950.GLES.keys.journal") == 0);
	s_info.isGLESX = 0;
	/* the shaders folder not made: beside it */
	s_mkdirOk = 0;
	gli_glsl_journal_path(&s_info, &s_cache, path, sizeof(path), &version);
	CHECK(strcmp(path, "/cache/GLideN64.8f506950.OpenGL.keys.journal") == 0);
	s_mkdirOk = 1;
	/* cut to the room given */
	{
		char small[12];
		gli_glsl_journal_path(&s_info, &s_cache, small, sizeof(small), &version);
		CHECK(strcmp(small, "/cache/shad") == 0);
		/* a cache folder longer than the room */
		s_cacheDir = "/a/cache/folder/longer/than/that";
		gli_glsl_journal_path(&s_info, &s_cache, small, sizeof(small), &version);
		CHECK(strcmp(small, "/a/cache/fo") == 0);
		s_cacheDir = "/cache";
	}
	{
		/* the room the folder takes exactly, and none */
		char exact[14];
		char none[1] = { 'x' };
		gli_glsl_journal_path(&s_info, &s_cache, exact, sizeof(exact), &version);
		CHECK(strcmp(exact, "/cache/shader") == 0);
		gli_glsl_journal_path(&s_info, &s_cache, none, 0, &version);
		CHECK(none[0] == 'x' && version == 5);
	}
}

/* three GL programs and two stand-ins, laid out by key */
static gli_cprog *s_gl[3];
static fake s_standIn[2];

static void make_programs(void)
{
	s_nextProgram = 10;
	s_binLen[10] = 5;
	s_binLen[11] = 0;   /* no binary: left out */
	s_binLen[12] = 3;
	s_binLen[13] = 2;   /* the stand-in's own */
	s_gl[0] = gli_glsl_cprog_new(0x1ULL, s_nextProgram++, &s_cache, 1u << G_GCI_TEXEL0, &s_info);
	s_gl[1] = gli_glsl_cprog_new(0x2ULL, s_nextProgram++, &s_cache, 0, &s_info);
	s_gl[2] = gli_glsl_cprog_new(0x10000000000ULL | 0x3ULL, s_nextProgram++, &s_cache, 1u << G_GCI_SHADE, &s_info);
	memset(s_standIn, 0, sizeof(s_standIn));
	s_standIn[0].base.ops = &s_fakeOps;
	s_standIn[0].base.key = 0x4ULL;
	s_standIn[0].real = gli_glsl_cprog_new(0x4ULL, s_nextProgram++, &s_cache, 1u << G_GCI_TEXEL1, &s_info);
	s_standIn[1].base.ops = &s_fakeOps;
	s_standIn[1].base.key = 0xABCDEF0123456789ULL;
	s_standIn[1].real = NULL; /* still on the ubershader */
	s_sorted[0] = s_gl[0];
	s_sorted[1] = s_gl[1];
	s_sorted[2] = s_gl[2];
	s_sorted[3] = &s_standIn[0].base;
	s_sorted[4] = &s_standIn[1].base;
}

static void free_programs(void)
{
	int i;
	for (i = 0; i < 3; ++i)
		s_gl[i]->ops->free_program(s_gl[i]);
	s_standIn[0].real->ops->free_program((gli_cprog *)s_standIn[0].real);
}

static const char s_keysText[] =
	"0x00000005\n0x00000001\n0x00000005\n"
	"0x0000000000000001\n0x0000000000000002\n0x0000010000000003\n0x0000000000000004\n0xabcdef0123456789\n";

static void save(void)
{
	gli_combiner_table table;
	int64_t size = 0;
	const char *d;
	size_t at;
	u32 v;
	memset(&table, 0, sizeof(table));
	table.count = 5;
	make_programs();
	gli_GBI_hwlSupported = 1;

	/* obsolete: the keys alone */
	drop_files();
	s_obsolete = 1;
	gli_ctx.shaderProgramBinary = 1;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	d = file_data(KEYS, &size);
	CHECK(d != NULL && size == (int64_t)strlen(s_keysText) && memcmp(d, s_keysText, (size_t)size) == 0);
	CHECK(file_data(SHADERS, &size) == NULL);
	CHECK(s_writes == 1);
	s_obsolete = 0;
	/* no binaries: the keys alone */
	drop_files();
	gli_ctx.shaderProgramBinary = 0;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	CHECK(file_data(SHADERS, &size) == NULL && s_writes == 1);
	gli_ctx.shaderProgramBinary = 1;
	/* no context: the keys alone */
	drop_files();
	s_renderer = NULL;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	CHECK(file_data(SHADERS, &size) == NULL && s_writes == 1);
	s_renderer = "Stub Renderer";

	/* the whole file */
	drop_files();
	s_asyncAsked = 0;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	CHECK(s_writes == 2);
	CHECK(s_asyncAsked == 2);
	d = file_data(SHADERS, &size);
	CHECK(d != NULL);
	if (d == NULL)
		return;
	at = 0;
	memcpy(&v, d + at, 4); at += 4; CHECK(v == 0x3E);
	memcpy(&v, d + at, 4); at += 4; CHECK(v == 0x1234u);
	memcpy(&v, d + at, 4); at += 4; CHECK(v == 13);
	CHECK(memcmp(d + at, "Stub Renderer", 13) == 0); at += 13;
	memcpy(&v, d + at, 4); at += 4; CHECK(v == 8);
	CHECK(memcmp(d + at, "4.6 Stub", 8) == 0); at += 8;
	memcpy(&v, d + at, 4); at += 4; CHECK(v == 3);
	{
		/* key 1 (program 10, 5 bytes), key 3 (program 12, 3 bytes), key 4
		 * as its stand-in's program 13, 2 bytes */
		static const u64 keys[3] = { 0x1ULL, 0x10000000003ULL, 0x4ULL };
		static const GLuint progs[3] = { 10, 12, 13 };
		static const int inputs[3] = { 1 << G_GCI_TEXEL0, 1 << G_GCI_SHADE, 1 << G_GCI_TEXEL1 };
		int i;
		for (i = 0; i < 3; ++i) {
			u64 key;
			int in;
			GLenum fmt;
			GLint len, j;
			int ok = 1;
			memcpy(&key, d + at, 8); at += 8;
			memcpy(&in, d + at, sizeof(in)); at += sizeof(in);
			memcpy(&fmt, d + at, sizeof(fmt)); at += sizeof(fmt);
			memcpy(&len, d + at, sizeof(len)); at += sizeof(len);
			CHECK(key == keys[i]);
			CHECK(in == inputs[i]);
			CHECK(fmt == 0x8000u + progs[i]);
			CHECK(len == s_binLen[progs[i]]);
			for (j = 0; j < len; ++j)
				ok &= (unsigned char)d[at + j] == (unsigned char)(progs[i] * 16 + j);
			CHECK(ok);
			at += (size_t)len;
		}
	}
	CHECK((int64_t)at == size);

	/* GL failing one binary: the others, the file no longer */
	drop_files();
	s_errorProgram = 12;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	d = file_data(SHADERS, &size);
	CHECK(d != NULL && size == (int64_t)(4 * 5 + 13 + 8 + 2 * HEAD_SIZE + 5 + 2));
	if (d != NULL) {
		memcpy(&v, d + 4 * 4 + 13 + 8, 4);
		CHECK(v == 2);
	}
	s_errorProgram = 0;
	/* the keys not written: failed, no shaders */
	drop_files();
	s_failWrite = ".keys";
	CHECK(!gli_glsl_save_storage(&s_info, &s_cache, &table));
	CHECK(file_data(SHADERS, &size) == NULL);
	s_failWrite = ".shaders";
	CHECK(!gli_glsl_save_storage(&s_info, &s_cache, &table));
	CHECK(file_data(KEYS, &size) != NULL);
	s_failWrite = NULL;

	/* no programs: the keys and an empty storage */
	drop_files();
	table.count = 0;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	d = file_data(KEYS, &size);
	CHECK(d != NULL && size == 33 && memcmp(d, "0x00000005\n0x00000001\n0x00000000\n", 33) == 0);
	d = file_data(SHADERS, &size);
	CHECK(d != NULL && size == 4 * 5 + 13 + 8);
	gli_GBI_hwlSupported = 0;
	table.count = 0;
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	d = file_data(KEYS, &size);
	CHECK(d != NULL && memcmp(d, "0x00000005\n0x00000000\n", 22) == 0);
	free_programs();
}

static size_t header(char *_out, u32 _version, u32 _options, const char *_renderer, const char *_glversion, u32 _count);
static size_t entry(char *_out, u64 _key, int _inputs, GLenum _fmt, GLint _len, unsigned char _first);

static void load_keys(void)
{
	static const char text[] = "  0x5\r\n0X1\n3\n0x00000000000000AB\n\t0xcd\n00ef zz\n";
	gli_combiner_table table;
	memset(&table, 0, sizeof(table));

	/* without binaries: from the keys */
	drop_files();
	reset_combiners();
	gli_ctx.shaderProgramBinary = 0;
	gli_GBI_hwlSupported = 0;
	set_file(KEYS, text, sizeof(text) - 1);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(gli_GBI_hwlSupported);
	CHECK(s_compiledCount == 3 && s_putCount == 3);
	CHECK(s_compiled[0].base.key == 0xAB && s_compiled[1].base.key == 0xCD && s_compiled[2].base.key == 0xEF);
	CHECK(s_put[0] == &s_compiled[0].base && s_put[2] == &s_compiled[2].base);
	CHECK(s_compiled[0].updates == 1 && s_compiled[0].forced == 1 && s_compiled[2].forced == 1);
	CHECK(s_writes == 0);
	/* a good shaders file there, unread without binaries */
	reset_combiners();
	{
		static char buf[256];
		size_t n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 1);
		n += entry(buf + n, 0x21ULL, 0, 0x9001, 1, 0x40);
		set_file(SHADERS, buf, n);
		CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
		CHECK(s_compiledCount == 3 && s_putCount == 3 && s_put[0]->key == 0xAB);
	}
	drop_files();
	set_file(KEYS, text, sizeof(text) - 1);

	/* fewer keys than counted: those there */
	reset_combiners();
	set_file(KEYS, "5\n0\n9\n1\n2\n", 10);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_compiledCount == 2 && !gli_GBI_hwlSupported);
	/* a count of none */
	reset_combiners();
	set_file(KEYS, "5\n0\n0\n1\n2\n", 10);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_compiledCount == 0);
	/* another version, or no count: nothing */
	reset_combiners();
	set_file(KEYS, "4\n0\n1\n1\n", 8);
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_compiledCount == 0);
	set_file(KEYS, "5\n0\n", 4);
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	set_file(KEYS, "5\n", 2);
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	set_file(KEYS, "x5\n0\n1\n1\n", 9);
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	/* no file */
	drop_files();
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	/* a GL error after: failed */
	set_file(KEYS, "5\n0\n1\n7\n", 8);
	s_glError = 1;
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_compiledCount == 1);
	s_glError = 0;

	/* with binaries but no shaders file: the keys, then saved again */
	reset_combiners();
	drop_files();
	gli_ctx.shaderProgramBinary = 1;
	set_file(KEYS, "5\n1\n2\n1\n2\n", 10);
	table.count = 2;
	s_binLen[0] = 0;
	{
		static fake saved[2];
		memset(saved, 0, sizeof(saved));
		saved[0].base.ops = &s_fakeOps; saved[0].base.key = 1;
		saved[1].base.ops = &s_fakeOps; saved[1].base.key = 2;
		s_sorted[0] = &saved[0].base;
		s_sorted[1] = &saved[1].base;
		CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	}
	CHECK(s_compiledCount == 2);
	CHECK(s_writes == 2);
	{
		int64_t size = 0;
		CHECK(file_data(SHADERS, &size) != NULL);
	}
	table.count = 0;
	reset_combiners();
}

/* a shaders file: the header, then @_n entries */
static size_t header(char *_out, u32 _version, u32 _options, const char *_renderer, const char *_glversion, u32 _count)
{
	size_t at = 0;
	u32 len;
	memcpy(_out + at, &_version, 4); at += 4;
	memcpy(_out + at, &_options, 4); at += 4;
	len = (u32)strlen(_renderer);
	memcpy(_out + at, &len, 4); at += 4;
	memcpy(_out + at, _renderer, len); at += len;
	len = (u32)strlen(_glversion);
	memcpy(_out + at, &len, 4); at += 4;
	memcpy(_out + at, _glversion, len); at += len;
	memcpy(_out + at, &_count, 4); at += 4;
	return at;
}

static size_t entry(char *_out, u64 _key, int _inputs, GLenum _fmt, GLint _len, unsigned char _first)
{
	size_t at = 0;
	GLint i;
	memcpy(_out + at, &_key, 8); at += 8;
	memcpy(_out + at, &_inputs, sizeof(int)); at += sizeof(int);
	memcpy(_out + at, &_fmt, sizeof(GLenum)); at += sizeof(GLenum);
	memcpy(_out + at, &_len, sizeof(GLint)); at += sizeof(GLint);
	for (i = 0; i < _len; ++i)
		_out[at++] = (char)(_first + i);
	return at;
}

static void load_shaders(void)
{
	static char buf[1024];
	gli_combiner_table table;
	size_t n;
	memset(&table, 0, sizeof(table));
	gli_ctx.shaderProgramBinary = 1;

	/* three programs: a triangle with a texture, a rect without, and one GL
	 * will not link, compiled from its key */
	drop_files();
	reset_combiners();
	n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 3);
	n += entry(buf + n, 0x21ULL, 1 << G_GCI_TEXEL0, 0x9001, 4, 0x40);
	n += entry(buf + n, 0x10000000022ULL, 1 << G_GCI_SHADE, 0x9002, 2, 0x50);
	n += entry(buf + n, 0x23ULL, 0, 0x9003, 1, 0x60);
	set_file(SHADERS, buf, n);
	s_nextProgram = 30;
	s_noLink[32] = 1;
	reset();
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(strcmp(s_log,
		"create 30;attr 30 aPosition;attr 30 aColor;attr 30 aNumLights;attr 30 aModify;attr 30 aTexCoord;"
		"binary 30 9001 4 40;"
		"create 31;attr 31 aRectPosition;binary 31 9002 2 50;"
		"create 32;attr 32 aPosition;attr 32 aColor;attr 32 aNumLights;attr 32 aModify;binary 32 9003 1 60;del 32;") == 0);
	if (strcmp(s_log, "") != 0 && fails)
		printf("  log %s\n", s_log);
	CHECK(s_putCount == 3);
	CHECK(s_put[0]->ops == &gli_glsl_cprog_ops && s_put[0]->key == 0x21ULL);
	CHECK(((gli_glsl_cprog *)s_put[0])->program == 30 && ((gli_glsl_cprog *)s_put[0])->inputs == (1u << G_GCI_TEXEL0));
	CHECK(((gli_glsl_cprog *)s_put[0])->needUpdate == 0);
	CHECK(s_put[1]->ops == &gli_glsl_cprog_ops && s_put[1]->key == 0x10000000022ULL);
	CHECK(((gli_glsl_cprog *)s_put[1])->program == 31 && ((gli_glsl_cprog *)s_put[1])->needUpdate == 0);
	CHECK(s_put[2] == &s_compiled[0].base && s_compiled[0].base.key == 0x23ULL && s_compiled[0].forced == 1);
	CHECK(s_compiledCount == 1);
	CHECK(s_writes == 0);
	s_noLink[32] = 0;

	/* cut short in the second entry's binary: the first, then stopped */
	reset_combiners();
	n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 3);
	n += entry(buf + n, 0x21ULL, 0, 0x9001, 4, 0x40);
	n += entry(buf + n, 0x22ULL, 0, 0x9002, 8, 0x50);
	set_file(SHADERS, buf, n - 1);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 1 && s_compiledCount == 0);
	/* cut in an entry's key, and an entry of no binary: stopped */
	reset_combiners();
	set_file(SHADERS, buf, header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 3) + 4);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 0 && s_compiledCount == 0);
	reset_combiners();
	n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 2);
	n += entry(buf + n, 0x21ULL, 0, 0x9001, 0, 0x40);
	n += entry(buf + n, 0x22ULL, 0, 0x9002, 1, 0x50);
	set_file(SHADERS, buf, n);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 0 && s_compiledCount == 0);
	/* more than counted: those counted */
	reset_combiners();
	n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 1);
	n += entry(buf + n, 0x21ULL, 0, 0x9001, 1, 0x40);
	n += entry(buf + n, 0x22ULL, 0, 0x9002, 1, 0x50);
	set_file(SHADERS, buf, n);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 1 && s_put[0]->key == 0x21ULL);
	/* GL's strings empty, and the file's */
	reset_combiners();
	s_renderer = "";
	s_version = "";
	n = header(buf, 0x3E, 0x1234u, "", "", 1);
	n += entry(buf + n, 0x21ULL, 0, 0x9001, 1, 0x40);
	set_file(SHADERS, buf, n);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 1 && s_put[0]->key == 0x21ULL && s_compiledCount == 0);
	s_renderer = "Stub Renderer";
	s_version = "4.6 Stub";
	/* fewer than counted: those there */
	reset_combiners();
	n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 5);
	n += entry(buf + n, 0x21ULL, 0, 0x9001, 1, 0x40);
	set_file(SHADERS, buf, n);
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 1);
	/* a GL error: failed, what was read kept */
	reset_combiners();
	s_glError = 1;
	CHECK(!gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 1);
	s_glError = 0;

	/* any header that does not match: the keys instead */
	{
		static const struct { u32 version, options; const char *renderer, *glversion; } bad[] = {
			{ 0x3D, 0x1234u, "Stub Renderer", "4.6 Stub" },
			{ 0x3E, 0x1235u, "Stub Renderer", "4.6 Stub" },
			{ 0x3E, 0x1234u, "Stub Rendered", "4.6 Stub" },
			{ 0x3E, 0x1234u, "Stub Renderer", "4.5 Stub" },
		};
		int i;
		for (i = 0; i < 4; ++i) {
			reset_combiners();
			drop_files();
			set_file(KEYS, "5\n0\n1\n77\n", 9);
			n = header(buf, bad[i].version, bad[i].options, bad[i].renderer, bad[i].glversion, 1);
			n += entry(buf + n, 0x21ULL, 0, 0x9001, 1, 0x40);
			set_file(SHADERS, buf, n);
			CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
			CHECK(s_compiledCount == 1 && s_compiled[0].base.key == 0x77);
			CHECK(s_putCount == 1);
		}
		/* cut in the header, at each place */
		n = header(buf, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub", 1);
		for (i = 0; i < (int)n; i += 3) {
			reset_combiners();
			drop_files();
			set_file(KEYS, "5\n0\n1\n77\n", 9);
			set_file(SHADERS, buf, (size_t)i);
			gli_glsl_load_storage(&s_info, &s_cache, &table);
			CHECK(s_compiledCount == 1);
		}
		/* a renderer string GL no longer has */
		reset_combiners();
		set_file(SHADERS, buf, n);
		/* the keys again: each load from them saved the storage over them */
		set_file(KEYS, "5\n0\n1\n77\n", 9);
		s_renderer = NULL;
		gli_glsl_load_storage(&s_info, &s_cache, &table);
		CHECK(s_compiledCount == 1);
		s_renderer = "Stub Renderer";
	}
	reset_combiners();
}

/* saved and read back: the same programs */
static void round_trip(void)
{
	gli_combiner_table table;
	memset(&table, 0, sizeof(table));
	table.count = 5;
	drop_files();
	gli_ctx.shaderProgramBinary = 1;
	make_programs();
	CHECK(gli_glsl_save_storage(&s_info, &s_cache, &table));
	free_programs();
	reset_combiners();
	s_nextProgram = 40;
	table.count = 0;
	reset();
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &table));
	CHECK(s_putCount == 3 && s_compiledCount == 0);
	CHECK(s_put[0]->key == 0x1ULL && s_put[1]->key == 0x10000000003ULL && s_put[2]->key == 0x4ULL);
	CHECK(((gli_glsl_cprog *)s_put[2])->inputs == (1u << G_GCI_TEXEL1));
	CHECK(strstr(s_log, "binary 40 800a 5 a0;") != NULL);
	CHECK(strstr(s_log, "binary 41 800c 3 c0;") != NULL);
	CHECK(strstr(s_log, "binary 42 800d 2 d0;") != NULL);
	CHECK(strstr(s_log, "attr 41 aRectPosition;") != NULL);
	reset_combiners();
	drop_files();
}

int main(void)
{
	s_ctxOps.isCombinerProgramBuilderObsolete = ctx_obsolete;
	gli_ctx.ops = &s_ctxOps;
	s_fakeOps.update = fake_update;
	paths();
	save();
	load_keys();
	load_shaders();
	round_trip();
	if (fails) {
		printf("test_gliden64_glsl_storage: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_glsl_storage: ok\n");
	return 0;
}
