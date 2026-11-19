/* test_gliden64_glsl_storage: GLideN64's GL shader storage
 * (glsl_ShaderStorage.c) on in-memory files, a stub key journal, stub GL
 * calls and stub combiner programs, with the real GL program behind it
 * (glsl_CombinerProgramImpl.c).
 *
 * One file a game, named from the cache folder, the game's hash and GL
 * or GL ES, in the shaders folder or beside it, cut to the room a path
 * has. Its header: the magic, the keys' and the programs' versions, the
 * options' bits, GL's renderer and version. Then records: a key's (its
 * kind, the hardware lighting bit, the key), a program's (its kind, the
 * bit, the key, inputs, format, length and binary).
 *
 * Loaded: opened once, read only, mapped where it can be and read where
 * not; the programs given to GL with their attributes, made programs and
 * updated, those GL will not link and every key with no program compiled
 * after, once each, with the bit it was met with; a header of other
 * programs: no program read, the keys compiled, the file written again
 * with the header and the keys; another keys' version: nothing read; a
 * record cut short: what comes before read, the file cut to it; no file:
 * the files of before (programs, keys as text and the journal) read,
 * removed, and the file written. Nothing written where nothing changed.
 *
 * The journal opened on the file. Closed: the programs not stored yet
 * appended in one write, each as GL wrote it, the ubershader's stand-ins
 * as the program they became, those with no binary left out; none where
 * the builder is obsolete, GL has no binaries or no context; the file
 * written again first where the journal lost a key the file has not.
 * C89. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Graphics/OpenGLContext/GLFunctions.h"
#include "Graphics/OpenGLContext/opengl_GLInfo.h"
#include "Graphics/OpenGLContext/opengl_CachedFunctions.h"
#include "Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.h"
#include "Graphics/OpenGLContext/GLSL/glsl_Programs.h"
#include "Graphics/OpenGLContext/GLSL/glsl_StdHash.h"
#include "Graphics/gli_context.h"
#include "Graphics/CombinerStages.h"
#include "CombinerKey.h"
#include "CombinerKeyJournal.h"
#include "gli_combiner.h"
#include "RSP.h"
#include "GBI.h"
#include "Config.h"
#include "PluginAPI.h"
#include <file/file_path.h>
#include <streams/file_stream.h>

/* what is built with it allocates by test_malloc and test_realloc: the
 * one s_failAlloc (s_failRealloc) counts down to fails */
#undef malloc
#undef realloc
extern void *malloc(size_t);
extern void *realloc(void *, size_t);
static int s_failAlloc, s_failRealloc;
static int fails_at(int *_n) { return *_n > 0 && --*_n == 0; }
void *test_malloc(size_t _n) { return fails_at(&s_failAlloc) ? NULL : malloc(_n); }
void *test_realloc(void *_p, size_t _n) { return fails_at(&s_failRealloc) ? NULL : realloc(_p, _n); }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- files, in memory ---- */
#define FILES 8
static struct { char path[1100]; char *data; int64_t size; } s_files[FILES];
static int s_mkdirOk = 1;
static int s_writes;    /* whole files written */
static int s_appends;   /* writes through a stream */
static int s_opens;
static int s_handles;   /* streams open */
static int s_reads;     /* reads through a stream */
static int s_sorts;     /* the table handed back by key */
static int s_rwOpens;   /* opened to change */
static int s_truncates;
static int s_deletes;
static int s_mapped = 1;      /* streams opened for frequent access are mapped */
static int s_failRead;        /* reads come up short */
static const char *s_failWrite; /* a file whose writes fail */
static const char *s_failOpen;  /* a file that will not open */
static int s_failChange;        /* nor be opened to change */
static unsigned s_lastMode, s_lastHints;

static int file_index(const char *_path)
{
	int i;
	for (i = 0; i < FILES; ++i)
		if (s_files[i].data != NULL && strcmp(s_files[i].path, _path) == 0)
			return i;
	return -1;
}

static int new_file(const char *_path)
{
	int i = file_index(_path);
	if (i < 0)
		for (i = 0; i < FILES && s_files[i].data != NULL; ++i)
			;
	if (i >= FILES)
		return -1;
	free(s_files[i].data);
	strcpy(s_files[i].path, _path);
	s_files[i].data = (char *)malloc(1);
	s_files[i].size = 0;
	return i;
}

static void resize(int _i, int64_t _size)
{
	char *d = (char *)realloc(s_files[_i].data, (size_t)_size + 1);
	if (_size > s_files[_i].size)
		memset(d + s_files[_i].size, 0, (size_t)(_size - s_files[_i].size));
	s_files[_i].data = d;
	s_files[_i].size = _size;
}

bool filestream_write_file(const char *_path, const void *_data, int64_t _size)
{
	int i;
	++s_writes;
	if (s_failWrite != NULL && strstr(_path, s_failWrite) != NULL)
		return false;
	if ((i = new_file(_path)) < 0)
		return false;
	resize(i, _size);
	memcpy(s_files[i].data, _data, (size_t)_size);
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
	b[s_files[i].size] = 'a'; /* nothing past the end is read */
	*_buf = b;
	*_len = s_files[i].size;
	return 1;
}

bool filestream_exists(const char *_path) { return file_index(_path) >= 0; }

int filestream_delete(const char *_path)
{
	int i = file_index(_path);
	++s_deletes;
	if (i < 0)
		return -1;
	free(s_files[i].data);
	s_files[i].data = NULL;
	return 0;
}

struct RFILE { int file; int64_t pos; unsigned mode, hints; };

RFILE *filestream_open(const char *_path, unsigned _mode, unsigned _hints)
{
	RFILE *f;
	int i = file_index(_path);
	++s_opens;
	s_lastMode = _mode;
	s_lastHints = _hints;
	if (_mode & RETRO_VFS_FILE_ACCESS_WRITE)
		++s_rwOpens;
	if ((s_failOpen != NULL && strstr(_path, s_failOpen) != NULL) || (s_failChange && (_mode & RETRO_VFS_FILE_ACCESS_WRITE)))
		return NULL;
	if (i < 0 && (_mode == RETRO_VFS_FILE_ACCESS_READ || (_mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING)))
		return NULL;
	if (i < 0 && (i = new_file(_path)) < 0)
		return NULL;
	f = (RFILE *)calloc(1, sizeof(*f));
	++s_handles;
	f->file = i;
	f->mode = _mode;
	f->hints = _hints;
	return f;
}

int filestream_close(RFILE *_f)
{
	CHECK(_f != NULL);
	if (_f != NULL)
		--s_handles;
	free(_f);
	return 0;
}

const uint8_t *filestream_get_mapped_ptr(RFILE *_f, int64_t *_len)
{
	if (!s_mapped || !(_f->hints & RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS)) {
		*_len = 0;
		return NULL;
	}
	*_len = s_files[_f->file].size;
	return (const uint8_t *)s_files[_f->file].data;
}

int64_t filestream_get_size(RFILE *_f) { return s_files[_f->file].size; }

int64_t filestream_read(RFILE *_f, void *_data, int64_t _len)
{
	++s_reads;
	int64_t n = s_files[_f->file].size - _f->pos;
	if (n > _len)
		n = _len;
	if (s_failRead && n > 0)
		--n;
	memcpy(_data, s_files[_f->file].data + _f->pos, (size_t)n);
	_f->pos += n;
	return n;
}

int64_t filestream_seek(RFILE *_f, int64_t _offset, int _where)
{
	_f->pos = (_where == RETRO_VFS_SEEK_POSITION_END ? s_files[_f->file].size : 0) + _offset;
	return 0;
}

int64_t filestream_write(RFILE *_f, const void *_data, int64_t _len)
{
	int i = _f->file;
	++s_appends;
	if (!(_f->mode & RETRO_VFS_FILE_ACCESS_WRITE) ||
			(s_failWrite != NULL && strstr(s_files[i].path, s_failWrite) != NULL))
		return -1;
	if (_f->pos + _len > s_files[i].size)
		resize(i, _f->pos + _len);
	memcpy(s_files[i].data + _f->pos, _data, (size_t)_len);
	_f->pos += _len;
	return _len;
}

int64_t filestream_truncate(RFILE *_f, int64_t _length)
{
	++s_truncates;
	CHECK(_f->mode & RETRO_VFS_FILE_ACCESS_WRITE);
	resize(_f->file, _length);
	return 0;
}

static const char *file_data(const char *_path, int64_t *_size)
{
	int i = file_index(_path);
	if (i < 0)
		return NULL;
	*_size = s_files[i].size;
	return s_files[i].data;
}

static void counts(void)
{
	s_writes = s_appends = s_opens = s_rwOpens = s_truncates = s_deletes = s_reads = s_sorts = 0;
}

static void drop_files(void)
{
	int i;
	for (i = 0; i < FILES; ++i) {
		free(s_files[i].data);
		s_files[i].data = NULL;
	}
	counts();
}

static void set_file(const char *_path, const void *_data, size_t _size)
{
	filestream_write_file(_path, _data, (int64_t)_size);
	counts();
}

static char s_mkdirAsked[1100];
bool path_mkdir(const char *_dir) { strcpy(s_mkdirAsked, _dir); return s_mkdirOk != 0; }
static const char *s_cacheDir = "/cache";
void gli_api_user_cache_path(char *_strPath, size_t _size)
{
	strncpy(_strPath, s_cacheDir, _size - 1);
	_strPath[_size - 1] = '\0';
}

RSPInfo RSP;

bool gli_GBI_hwlSupported;
static u32 s_options = 0x1234u;
u32 gli_combiner_options_bits(void) { return s_options; }

/* ---- the journal: what it was opened on, the stream it hands back,
 * whether it got every key; the keys the journal of before holds ---- */
static int s_journal;
static char s_journalPath[1100];
static int s_noJournal;
static RFILE *s_journalFile;
static int s_whole = 1;
combiner_journal_t *combiner_journal_append(const char *_path)
{
	strcpy(s_journalPath, _path);
	return s_noJournal ? NULL : (combiner_journal_t *)&s_journal;
}
RFILE *combiner_journal_detach(combiner_journal_t *_j, int *_whole)
{
	RFILE *f = s_journalFile;
	s_journalFile = NULL;
	*_whole = _j != NULL && s_whole;
	CHECK(_j == NULL || _j == (combiner_journal_t *)&s_journal);
	if (_j == NULL && f != NULL) {
		filestream_close(f);
		f = NULL;
	}
	return f;
}
static u64 s_oldJournal[4];
static unsigned s_oldJournalHwl[4];
static unsigned s_oldJournalCount;
static char s_oldJournalRead[1100];
unsigned combiner_journal_read(const char *_path, uint32_t _version, combiner_journal_key_t _cb, void *_ctx)
{
	unsigned i;
	strcpy(s_oldJournalRead, _path);
	CHECK(_version == 5);
	if (!filestream_exists(_path))
		return 0;
	for (i = 0; i < s_oldJournalCount; ++i)
		_cb(_ctx, s_oldJournal[i], s_oldJournalHwl[i]);
	return s_oldJournalCount;
}

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
#define PROGS 256
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
/* bit 40 of a key here marks a rect, bit 61 the hardware lighting */
bool gli_combiner_key_is_rect(u64 _key) { return ((_key >> 40) & 1) != 0; }
#define HWL (1ULL << 61)
bool gli_combiner_key_hwl(u64 _key) { return (_key & HWL) != 0; }

/* ---- the combiners: a table of what was put, the programs a key
 * compiles to, and the ubershader's stand-ins ---- */
static gli_cprog *s_put[PROGS];
static u32 s_putCount;
gli_cprog *gli_combiners_find(const gli_combiner_table *_table, u64 _key)
{
	u32 i;
	(void)_table;
	for (i = 0; i < s_putCount; ++i)
		if (s_put[i]->key == _key)
			return s_put[i];
	return NULL;
}
void gli_combiners_put(gli_combiner_table *_table, gli_cprog *_program)
{
	CHECK(gli_combiners_find(_table, _program->key) == NULL);
	s_put[s_putCount++] = _program;
	_table->count = s_putCount;
}
u32 gli_combiners_sorted(const gli_combiner_table *_table, gli_cprog **_out)
{
	u32 i, j;
	++s_sorts;
	CHECK(_table->count == s_putCount);
	for (i = 0; i < s_putCount; ++i) {
		gli_cprog *p = s_put[i];
		for (j = i; j > 0 && _out[j - 1]->key > p->key; --j)
			_out[j] = _out[j - 1];
		_out[j] = p;
	}
	return s_putCount;
}

typedef struct fake { gli_cprog base; int updates; int forced; int hwl; const gli_cprog *real; } fake;
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
	f->hwl = gli_GBI_hwlSupported ? 1 : 0;
	return &f->base;
}
static int s_asyncAsked;
const gli_cprog *gli_glsl_async_program(gli_cprog *_program)
{
	++s_asyncAsked;
	return ((fake *)_program)->real;
}

static gli_combiner_table s_table;

static void reset_combiners(void)
{
	u32 i;
	for (i = 0; i < s_putCount; ++i)
		if (s_put[i]->ops == &gli_glsl_cprog_ops)
			s_put[i]->ops->free_program(s_put[i]);
	s_putCount = 0;
	s_compiledCount = 0;
	s_table.count = 0;
}

static gli_gl_info s_info;
static gli_gl_cache1 s_cache;

/* named by the game's hash, which test_gliden64_std_hash holds */
static char s_name[64];
static char CACHE[96];
static char KEYS[96];
static char SHADERS[96];
static char JOURNAL[96];
#define HEAD_SIZE (sizeof(u64) + sizeof(int) + sizeof(GLenum) + sizeof(GLint))

/* ---- what a file holds ---- */
static size_t put(char *_out, const void *_v, size_t _n)
{
	memcpy(_out, _v, _n);
	return _n;
}

static size_t header_of(char *_out, const char *_magic, u32 _keys, u32 _format, u32 _options,
	const char *_renderer, const char *_version)
{
	size_t at = 0;
	u32 len;
	at += put(_out + at, _magic, 4);
	at += put(_out + at, &_keys, 4);
	at += put(_out + at, &_format, 4);
	at += put(_out + at, &_options, 4);
	len = (u32)strlen(_renderer);
	at += put(_out + at, &len, 4);
	at += put(_out + at, _renderer, len);
	len = (u32)strlen(_version);
	at += put(_out + at, &len, 4);
	at += put(_out + at, _version, len);
	return at;
}

/* this session's */
static size_t header(char *_out)
{
	return header_of(_out, "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub");
}

static size_t key_record(char *_out, u64 _key)
{
	_out[0] = 'K';
	_out[1] = (char)(gli_combiner_key_hwl(_key) ? 1 : 0);
	memcpy(_out + 2, &_key, 8);
	return 10;
}

/* a program's stored form, as the files of before held it */
static size_t entry(char *_out, u64 _key, int _inputs, GLenum _fmt, GLint _len, unsigned char _first)
{
	size_t at = 0;
	GLint i;
	at += put(_out + at, &_key, 8);
	at += put(_out + at, &_inputs, sizeof(int));
	at += put(_out + at, &_fmt, sizeof(GLenum));
	at += put(_out + at, &_len, sizeof(GLint));
	for (i = 0; i < _len; ++i)
		_out[at++] = (char)(_first + i);
	return at;
}

static size_t program_record(char *_out, u64 _key, int _inputs, GLenum _fmt, GLint _len, unsigned char _first)
{
	_out[0] = 'B';
	_out[1] = (char)(gli_combiner_key_hwl(_key) ? 1 : 0);
	return 2 + entry(_out + 2, _key, _inputs, _fmt, _len, _first);
}

static int file_is(const char *_path, const char *_data, size_t _size)
{
	int64_t size = 0;
	const char *d = file_data(_path, &size);
	return d != NULL && size == (int64_t)_size && memcmp(d, _data, _size) == 0;
}

static void load(bool _ok)
{
	CHECK(gli_glsl_load_storage(&s_info, &s_cache, &s_table) == _ok);
	CHECK(s_handles == 0);
}

static void close_storage(bool _ok)
{
	CHECK(gli_glsl_close_storage(&s_table, gli_glsl_open_journal()) == _ok);
	CHECK(s_handles == 0);
}

/* ---- the file's name ---- */
static void paths(void)
{
	static char buf[64];
	char want[1100];
	const size_t n = header(buf);
	int64_t size = 0;
	strcpy(RSP.romname, "SUPER MARIO 64");
	sprintf(s_name, "GLideN64.%x", (unsigned)gli_std_hash_string("SUPER MARIO 64"));
	sprintf(CACHE, "/cache/shaders/%s.OpenGL.cache", s_name);
	sprintf(KEYS, "/cache/shaders/%s.OpenGL.keys", s_name);
	sprintf(SHADERS, "/cache/shaders/%s.OpenGL.shaders", s_name);
	sprintf(JOURNAL, "/cache/shaders/%s.OpenGL.keys.journal", s_name);

	/* none there: one open, read only and mapped, and the header written */
	drop_files();
	load(true);
	CHECK(strcmp(s_mkdirAsked, "/cache/shaders") == 0);
	CHECK(file_is(CACHE, buf, n));
	CHECK(s_opens == 1 && s_rwOpens == 0 && s_writes == 1 && s_deletes == 0);
	CHECK(s_lastMode == RETRO_VFS_FILE_ACCESS_READ && s_lastHints == RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
	CHECK(s_putCount == 0 && s_compiledCount == 0);
	/* the journal on it */
	CHECK(gli_glsl_open_journal() == (combiner_journal_t *)&s_journal && strcmp(s_journalPath, CACHE) == 0);
	close_storage(true);
	/* closed: no journal until the next load; closing again fails, the
	 * journal's stream let go */
	CHECK(gli_glsl_open_journal() == NULL);
	close_storage(false);
	s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
	CHECK(!gli_glsl_close_storage(&s_table, (combiner_journal_t *)&s_journal) && s_handles == 0);

	s_info.isGLESX = 1;
	drop_files();
	load(true);
	sprintf(want, "/cache/shaders/%s.GLES.cache", s_name);
	CHECK(file_data(want, &size) != NULL);
	s_info.isGLESX = 0;
	/* the shaders folder not made: beside it */
	s_mkdirOk = 0;
	drop_files();
	load(true);
	sprintf(want, "/cache/%s.OpenGL.cache", s_name);
	CHECK(file_data(want, &size) != NULL);
	s_mkdirOk = 1;
	/* cut to the room a path has */
	{
		static char longDir[PLUGIN_PATH_SIZE - 13];
		memset(longDir, 'd', sizeof(longDir) - 1);
		longDir[0] = '/';
		s_cacheDir = longDir;
		drop_files();
		load(true);
		sprintf(want, "%s/shaders", longDir);
		CHECK(strcmp(s_mkdirAsked, want) == 0);
		sprintf(want, "%s/shaders/%s.OpenGL.cache", longDir, s_name);
		want[PLUGIN_PATH_SIZE - 1] = '\0';
		CHECK(file_data(want, &size) != NULL);
		CHECK(gli_glsl_open_journal() != NULL && strlen(s_journalPath) == PLUGIN_PATH_SIZE - 1);
		close_storage(true);
		/* one short of the room: whole; the room: cut by one */
		{
			static char fitDir[PLUGIN_PATH_SIZE];
			const size_t rest = strlen("/shaders/") + strlen(s_name) + strlen(".OpenGL.cache");
			int k;
			for (k = 0; k < 2; ++k) {
				const size_t dirLen = PLUGIN_PATH_SIZE - 1 - rest + (size_t)k;
				memset(fitDir, 'e', dirLen);
				fitDir[0] = '/';
				fitDir[dirLen] = '\0';
				s_cacheDir = fitDir;
				drop_files();
				load(true);
				sprintf(want, "%s/shaders/%s.OpenGL.cache", fitDir, s_name);
				want[PLUGIN_PATH_SIZE - 1] = '\0';
				CHECK(file_data(want, &size) != NULL && strlen(want) == PLUGIN_PATH_SIZE - 1);
				CHECK(want[PLUGIN_PATH_SIZE - 2] == (k == 0 ? 'e' : 'h'));
				close_storage(true);
			}
		}
		s_cacheDir = "/cache";
	}
	drop_files();
}

/* ---- loaded ---- */
static void records(void)
{
	static char buf[1024];
	size_t n = header(buf);
	int64_t size = 0;
	/* a key; a program, its key after; one GL will not link; a key met with
	 * hardware lighting; a program again; a rect's program */
	n += key_record(buf + n, 0x21ULL);
	n += program_record(buf + n, 0x22ULL, 1 << G_GCI_TEXEL0, 0x9001, 4, 0x40);
	n += key_record(buf + n, 0x22ULL);
	n += program_record(buf + n, 0x23ULL, 0, 0x9003, 1, 0x60);
	n += key_record(buf + n, HWL | 0x24ULL);
	n += program_record(buf + n, 0x22ULL, 0, 0x9009, 2, 0x70);
	n += program_record(buf + n, 0x10000000025ULL, 1 << G_GCI_SHADE, 0x9002, 2, 0x50);
	gli_ctx.shaderProgramBinary = 1;

	drop_files();
	reset_combiners();
	set_file(CACHE, buf, n);
	s_nextProgram = 30;
	s_noLink[31] = 1;
	s_binLen[30] = s_binLen[32] = 2;
	gli_GBI_hwlSupported = 0;
	reset();
	load(true);
	CHECK(strcmp(s_log,
		"create 30;attr 30 aPosition;attr 30 aColor;attr 30 aNumLights;attr 30 aModify;attr 30 aTexCoord;"
		"binary 30 9001 4 40;"
		"create 31;attr 31 aPosition;attr 31 aColor;attr 31 aNumLights;attr 31 aModify;binary 31 9003 1 60;del 31;"
		"create 32;attr 32 aRectPosition;binary 32 9002 2 50;") == 0);
	if (fails)
		printf("  log %s\n", s_log);
	CHECK(s_putCount == 5 && s_table.count == 5);
	CHECK(s_put[0]->ops == &gli_glsl_cprog_ops && s_put[0]->key == 0x22ULL);
	CHECK(((gli_glsl_cprog *)s_put[0])->program == 30 && ((gli_glsl_cprog *)s_put[0])->inputs == (1u << G_GCI_TEXEL0));
	CHECK(((gli_glsl_cprog *)s_put[0])->needUpdate == 0);
	CHECK(s_put[1]->ops == &gli_glsl_cprog_ops && s_put[1]->key == 0x10000000025ULL);
	CHECK(((gli_glsl_cprog *)s_put[1])->program == 32);
	/* the keys after the programs, in the file's order, each once */
	CHECK(s_compiledCount == 3);
	CHECK(s_put[2] == &s_compiled[0].base && s_compiled[0].base.key == 0x21ULL && s_compiled[0].hwl == 0);
	CHECK(s_put[3] == &s_compiled[1].base && s_compiled[1].base.key == 0x23ULL && s_compiled[1].hwl == 0);
	CHECK(s_put[4] == &s_compiled[2].base && s_compiled[2].base.key == (HWL | 0x24ULL) && s_compiled[2].hwl == 1);
	CHECK(s_compiled[0].updates == 1 && s_compiled[0].forced == 1 && s_compiled[2].forced == 1);
	CHECK(!gli_GBI_hwlSupported);
	/* a program GL would not take: written again as it closes, the keys
	 * and the programs GL made, so its new program is not stored beside
	 * it */
	{
		static char want[512];
		size_t w = header(want);
		w += key_record(want + w, 0x21ULL);
		w += key_record(want + w, 0x22ULL);
		w += key_record(want + w, 0x23ULL);
		w += key_record(want + w, 0x10000000025ULL);
		w += key_record(want + w, HWL | 0x24ULL);
		CHECK(file_is(CACHE, buf, n));
		CHECK(s_opens == 1 && s_writes == 0 && s_rwOpens == 0);
		/* closed: the compiled ones with nothing GL made of them left out */
		close_storage(true);
		CHECK(s_writes == 1);
		w += program_record(want + w, 0x22ULL, 1 << G_GCI_TEXEL0, 0x8000 + 30, 2, (unsigned char)(30 * 16));
		w += program_record(want + w, 0x10000000025ULL, 1 << G_GCI_SHADE, 0x8000 + 32, 2, (unsigned char)(32 * 16));
		CHECK(file_is(CACHE, want, w) && s_appends == 1);
		CHECK(s_asyncAsked == 3);
		s_asyncAsked = 0;
		s_noLink[31] = 0;

		/* the next session: all taken, read in place, nothing written; the
		 * journal's stream closed with nothing to add */
		reset_combiners();
		counts();
		s_nextProgram = 30;
		load(true);
		CHECK(s_putCount == 5 && s_compiledCount == 3);
		CHECK(s_opens == 1 && s_reads == 0 && s_writes == 0 && s_rwOpens == 0 && file_is(CACHE, want, w));
		s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
		counts();
		close_storage(true);
		CHECK(s_appends == 0 && s_rwOpens == 0 && file_is(CACHE, want, w));

		/* not mapped: read, the same */
		reset_combiners();
		counts();
		s_mapped = 0;
		reset();
		s_nextProgram = 30;
		load(true);
		CHECK(s_putCount == 5 && s_compiledCount == 3 && strstr(s_log, "binary 31 8020 2 00;") != NULL);
		CHECK(s_opens == 1 && s_writes == 0 && file_is(CACHE, want, w));
		close_storage(true);
	}
	/* not read whole: as none there, written again */
	reset_combiners();
	counts();
	s_failRead = 1;
	load(true);
	s_failRead = 0;
	CHECK(s_putCount == 0 && s_writes == 1 && file_data(CACHE, &size) != NULL && size == (int64_t)header(buf));
	close_storage(true);
	s_mapped = 1;

	/* not opened: as none there */
	reset_combiners();
	set_file(CACHE, buf, n);
	s_failOpen = ".cache";
	load(true);
	s_failOpen = NULL;
	CHECK(s_putCount == 0 && s_writes == 1);
	close_storage(true);

	/* a GL error: failed, what was read kept */
	reset_combiners();
	n = header(buf);
	n += key_record(buf + n, 0x21ULL);
	set_file(CACHE, buf, n);
	s_glError = 1;
	load(false);
	s_glError = 0;
	CHECK(s_putCount == 1);
	close_storage(true);

	/* no binaries: the programs' keys */
	reset_combiners();
	n = header(buf);
	n += key_record(buf + n, 0x21ULL);
	n += program_record(buf + n, 0x22ULL, 0, 0x9001, 1, 0x40);
	set_file(CACHE, buf, n);
	gli_ctx.shaderProgramBinary = 0;
	reset();
	load(true);
	CHECK(strstr(s_log, "binary") == NULL);
	CHECK(s_compiledCount == 2 && s_compiled[0].base.key == 0x21ULL && s_compiled[1].base.key == 0x22ULL);
	CHECK(s_writes == 0 && file_is(CACHE, buf, n));
	close_storage(true);
	CHECK(s_appends == 0 && s_sorts == 0 && file_is(CACHE, buf, n));
	gli_ctx.shaderProgramBinary = 1;

	/* programs stored highest key first: none stored again */
	reset_combiners();
	n = header(buf);
	n += program_record(buf + n, 0x50ULL, 0, 0x9001, 1, 0x40);
	n += program_record(buf + n, 0x40ULL, 0, 0x9002, 1, 0x40);
	n += program_record(buf + n, 0x30ULL, 0, 0x9003, 1, 0x40);
	set_file(CACHE, buf, n);
	s_nextProgram = 20;
	s_binLen[20] = s_binLen[21] = s_binLen[22] = 2;
	load(true);
	CHECK(s_putCount == 3 && s_compiledCount == 0);
	counts();
	close_storage(true);
	/* every program stored: the table not walked */
	CHECK(s_appends == 0 && s_sorts == 0 && file_is(CACHE, buf, n));
	/* and one new: that one alone stored */
	reset_combiners();
	s_nextProgram = 20;
	load(true);
	gli_combiners_put(&s_table, gli_glsl_cprog_new(0x45ULL, 23, &s_cache, 0, &s_info));
	s_binLen[23] = 2;
	close_storage(true);
	{
		static char more[512];
		size_t m = n;
		memcpy(more, buf, n);
		m += program_record(more + m, 0x45ULL, 0, 0x8000 + 23, 2, (unsigned char)(23 * 16));
		CHECK(s_appends == 1 && file_is(CACHE, more, m));
	}
	s_binLen[23] = 0;
	set_file(CACHE, buf, n);
	/* more than the first room for them, highest first: none stored again */
	{
		static char many[8192];
		static GLint lens[PROGS];
		size_t m = header(many);
		int k;
		reset_combiners();
		for (k = 0; k < 200; ++k)
			m += program_record(many + m, 0x1000ULL + 200 - (u64)k, 0, 0x9001, 1, 0x40);
		set_file(CACHE, many, m);
		memcpy(lens, s_binLen, sizeof(lens));
		for (k = 0; k < PROGS; ++k)
			s_binLen[k] = 1;
		s_nextProgram = 1;
		load(true);
		CHECK(s_putCount == 200 && s_compiledCount == 0);
		close_storage(true);
		CHECK(s_appends == 0 && file_is(CACHE, many, m));
		memcpy(s_binLen, lens, sizeof(lens));
	}
	/* no room to note them: stored again as it closes */
	reset_combiners();
	set_file(CACHE, buf, n);
	s_nextProgram = 20;
	s_failRealloc = 1;
	load(true);
	CHECK(s_failRealloc == 0 && s_putCount == 3);
	close_storage(true);
	CHECK(s_appends == 1);
	reset_combiners();
}

/* a header of other programs, of other keys, and none whole */
static void headers(void)
{
	static const struct { const char *magic; u32 keys, format, options; const char *renderer, *version; } other[] = {
		{ "GLNC", 5, 0x3D, 0x1234u, "Stub Renderer", "4.6 Stub" },
		{ "GLNC", 5, 0x3E, 0x1235u, "Stub Renderer", "4.6 Stub" },
		{ "GLNC", 5, 0x3E, 0x1234u, "Stub Rendered", "4.6 Stub" },
		{ "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer", "4.5 Stub" },
		{ "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer!", "4.6 Stub" },
		{ "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stu" },
	};
	static char buf[512], want[512];
	size_t n, w;
	int i;
	gli_ctx.shaderProgramBinary = 1;
	/* programs read not; the keys compiled; the header and keys written */
	for (i = 0; i < (int)(sizeof(other) / sizeof(other[0])); ++i) {
		drop_files();
		reset_combiners();
		n = header_of(buf, other[i].magic, other[i].keys, other[i].format, other[i].options,
			other[i].renderer, other[i].version);
		n += program_record(buf + n, HWL | 0x32ULL, 0, 0x9001, 1, 0x40);
		n += key_record(buf + n, 0x31ULL);
		set_file(CACHE, buf, n);
		reset();
		load(true);
		CHECK(strstr(s_log, "binary") == NULL);
		CHECK(s_compiledCount == 2 && s_compiled[0].base.key == (HWL | 0x32ULL) && s_compiled[0].hwl == 1);
		CHECK(file_is(CACHE, buf, n) && s_writes == 0);
		/* written again as it closes, the programs after; the journal's
		 * stream let go first */
		if (i == 0)
			s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
		close_storage(true);
		w = header(want);
		w += key_record(want + w, 0x31ULL);
		w += key_record(want + w, HWL | 0x32ULL);
		CHECK(file_is(CACHE, want, w) && s_writes == 1);
	}
	/* another magic, keys' version, or a header cut short: nothing read */
	n = header(buf);
	n += key_record(buf + n, 0x31ULL);
	for (i = 0; i < (int)n; ++i) {
		drop_files();
		reset_combiners();
		set_file(CACHE, buf, (size_t)i);
		load(true);
		CHECK(s_compiledCount == 0 && file_is(CACHE, want, header(want)));
	}
	for (i = 0; i < 2; ++i) {
		drop_files();
		reset_combiners();
		n = header_of(buf, i == 0 ? "GLNB" : "GLNC", i == 0 ? 5 : 4, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub");
		n += key_record(buf + n, 0x31ULL);
		set_file(CACHE, buf, n);
		load(true);
		CHECK(s_compiledCount == 0 && file_is(CACHE, want, header(want)));
	}
	/* not written: the keys still compiled */
	drop_files();
	reset_combiners();
	n = header_of(buf, "GLNC", 5, 0x3D, 0x1234u, "Stub Renderer", "4.6 Stub");
	n += key_record(buf + n, 0x31ULL);
	set_file(CACHE, buf, n);
	s_failWrite = ".cache";
	load(true);
	s_failWrite = NULL;
	CHECK(s_compiledCount == 1 && file_is(CACHE, buf, n));
	/* nor the journal's keys kept as the file's: written again when the
	 * journal lost one */
	s_whole = 0;
	close_storage(true);
	s_whole = 1;
	w = header(want);
	w += key_record(want + w, 0x31ULL);
	CHECK(file_is(CACHE, want, w));
	reset_combiners();
}

/* cut short by a crash: what came before read, the file cut to it */
static void torn(void)
{
	static char buf[512];
	size_t n = header(buf), whole[3], cut;
	int64_t size = 0;
	int part;
	gli_ctx.shaderProgramBinary = 1;
	/* the header alone: nothing to do */
	drop_files();
	reset_combiners();
	set_file(CACHE, buf, n);
	load(true);
	CHECK(s_writes == 0 && s_truncates == 0 && s_rwOpens == 0 && file_is(CACHE, buf, n));
	close_storage(true);
	CHECK(s_writes == 0 && s_appends == 0 && file_is(CACHE, buf, n));
	whole[0] = n;
	n += key_record(buf + n, 0x41ULL);
	whole[1] = n;
	n += program_record(buf + n, 0x42ULL, 0, 0x9001, 6, 0x40);
	whole[2] = n;
	for (part = 0; part < 2; ++part)
		for (cut = whole[part] + 1; cut < whole[part + 1]; ++cut) {
			drop_files();
			reset_combiners();
			set_file(CACHE, buf, cut);
			load(true);
			CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)whole[part]);
			CHECK(s_truncates == 1 && s_rwOpens == 1 && s_writes == 0);
			CHECK(s_putCount == (u32)part && s_compiledCount == (u32)part);
			CHECK(memcmp(file_data(CACHE, &size), buf, whole[part]) == 0);
			close_storage(true);
		}
	/* a record of no kind: read to it */
	drop_files();
	reset_combiners();
	buf[whole[1]] = 'Z';
	set_file(CACHE, buf, whole[2]);
	load(true);
	CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)whole[1] && s_compiledCount == 1);
	close_storage(true);
	/* a program of no binary: read to it */
	drop_files();
	reset_combiners();
	n = whole[1];
	n += program_record(buf + n, 0x42ULL, 0, 0x9001, 0, 0x40);
	n += key_record(buf + n, 0x43ULL);
	set_file(CACHE, buf, n);
	load(true);
	CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)whole[1] && s_compiledCount == 1);
	close_storage(true);
	/* passed over as another's, the same */
	drop_files();
	reset_combiners();
	gli_ctx.shaderProgramBinary = 0;
	set_file(CACHE, buf, n);
	load(true);
	CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)whole[1] && s_compiledCount == 1);
	close_storage(true);
	gli_ctx.shaderProgramBinary = 1;
	/* not opened to cut: left as it is */
	drop_files();
	reset_combiners();
	set_file(CACHE, buf, whole[1] + 3);
	s_failChange = 1;
	load(true);
	s_failChange = 0;
	CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)whole[1] + 3 && s_compiledCount == 1);
	close_storage(true);
	reset_combiners();
	drop_files();
}

/* the files of before: read once into the file, then removed */
static void before(void)
{
	static const char keysText[] = "  0x5\r\n0X1\n3\n0x0000000000000031\n\t0x33\n0034 zz\n";
	static char buf[1024], want[512];
	size_t n, w;
	u32 v;
	gli_ctx.shaderProgramBinary = 1;

	drop_files();
	reset_combiners();
	/* the programs, one GL will not link; the keys, met with hardware
	 * lighting; the journal */
	n = 0;
	v = 0x3E; n += put(buf + n, &v, 4);
	v = 0x1234u; n += put(buf + n, &v, 4);
	v = 13; n += put(buf + n, &v, 4);
	n += put(buf + n, "Stub Renderer", 13);
	v = 8; n += put(buf + n, &v, 4);
	n += put(buf + n, "4.6 Stub", 8);
	v = 2; n += put(buf + n, &v, 4);
	n += entry(buf + n, 0x31ULL, 0, 0x9001, 3, 0x40);
	n += entry(buf + n, HWL | 0x32ULL, 0, 0x9002, 1, 0x50);
	set_file(SHADERS, buf, n);
	set_file(KEYS, keysText, sizeof(keysText) - 1);
	set_file(JOURNAL, "j", 1);
	s_oldJournal[0] = 0x35ULL; s_oldJournalHwl[0] = 1;
	s_oldJournal[1] = 0x33ULL; s_oldJournalHwl[1] = 0;
	s_oldJournalCount = 2;
	s_nextProgram = 50;
	s_noLink[51] = 1;
	reset();
	load(true);
	CHECK(strstr(s_log, "binary 50 9001 3 40;") != NULL && strstr(s_log, "binary 51 9002 1 50;del 51;") != NULL);
	CHECK(s_putCount == 5 && s_put[0]->key == 0x31ULL && s_put[0]->ops == &gli_glsl_cprog_ops);
	CHECK(s_compiledCount == 4);
	CHECK(s_compiled[0].base.key == (HWL | 0x32ULL) && s_compiled[0].hwl == 1);
	CHECK(s_compiled[1].base.key == 0x33ULL && s_compiled[1].hwl == 1);
	CHECK(s_compiled[2].base.key == 0x34ULL && s_compiled[2].hwl == 1);
	CHECK(s_compiled[3].base.key == 0x35ULL && s_compiled[3].hwl == 1);
	CHECK(strcmp(s_oldJournalRead, JOURNAL) == 0);
	CHECK(!filestream_exists(SHADERS) && !filestream_exists(KEYS) && !filestream_exists(JOURNAL));
	w = header(want);
	w += key_record(want + w, 0x31ULL);
	w += key_record(want + w, 0x33ULL);
	w += key_record(want + w, 0x34ULL);
	w += key_record(want + w, 0x35ULL);
	w += key_record(want + w, HWL | 0x32ULL);
	CHECK(file_is(CACHE, want, w) && s_writes == 1);
	s_noLink[51] = 0;
	/* the program read is stored as it closes; the keys written are the
	 * file's, so a journal that lost one finds none missing */
	s_binLen[50] = 3;
	counts();
	s_whole = 0;
	close_storage(true);
	s_whole = 1;
	CHECK(s_writes == 0);
	{
		int64_t size = 0;
		CHECK(file_data(CACHE, &size) != NULL && size == (int64_t)(w + 2 + HEAD_SIZE + 3) && s_appends == 1);
	}
	s_binLen[50] = 0;

	/* a program of before GL will not link, met without the bit: compiled
	 * without it */
	{
		static char one[256];
		size_t m = 0;
		v = 0x3E; m += put(one + m, &v, 4);
		v = 0x1234u; m += put(one + m, &v, 4);
		v = 13; m += put(one + m, &v, 4);
		m += put(one + m, "Stub Renderer", 13);
		v = 8; m += put(one + m, &v, 4);
		m += put(one + m, "4.6 Stub", 8);
		v = 1; m += put(one + m, &v, 4);
		m += entry(one + m, 0x37ULL, 0, 0x9001, 1, 0x40);
		drop_files();
		reset_combiners();
		set_file(SHADERS, one, m);
		s_nextProgram = 52;
		s_noLink[52] = 1;
		load(true);
		s_noLink[52] = 0;
		CHECK(s_compiledCount == 1 && s_compiled[0].base.key == 0x37ULL && s_compiled[0].hwl == 0);
		close_storage(true);
	}
	/* the programs of others, or without binaries: the keys alone */
	drop_files();
	reset_combiners();
	memcpy(buf + 4, "\x35\x12", 2);
	set_file(SHADERS, buf, n);
	set_file(KEYS, keysText, sizeof(keysText) - 1);
	reset();
	load(true);
	CHECK(strstr(s_log, "binary") == NULL && s_compiledCount == 3 && s_compiled[0].base.key == 0x31ULL);
	CHECK(!filestream_exists(SHADERS));
	close_storage(true);
	memcpy(buf + 4, "\x34\x12", 2);
	drop_files();
	reset_combiners();
	set_file(SHADERS, buf, n);
	gli_ctx.shaderProgramBinary = 0;
	reset();
	load(true);
	CHECK(strstr(s_log, "binary") == NULL && s_compiledCount == 0 && !filestream_exists(SHADERS));
	close_storage(true);
	gli_ctx.shaderProgramBinary = 1;
	/* another renderer, or one the file's is the start of: the keys alone */
	{
		static const char *const renderers[2] = { "Stub Renderex", "Stub Rendere" };
		static char other[256];
		int k;
		for (k = 0; k < 2; ++k) {
			size_t m = 0;
			const u32 rl = (u32)strlen(renderers[k]);
			v = 0x3E; m += put(other + m, &v, 4);
			v = 0x1234u; m += put(other + m, &v, 4);
			m += put(other + m, &rl, 4);
			m += put(other + m, renderers[k], rl);
			v = 8; m += put(other + m, &v, 4);
			m += put(other + m, "4.6 Stub", 8);
			v = 1; m += put(other + m, &v, 4);
			m += entry(other + m, 0x31ULL, 0, 0x9001, 3, 0x40);
			drop_files();
			reset_combiners();
			set_file(SHADERS, other, m);
			reset();
			load(true);
			CHECK(strstr(s_log, "binary") == NULL && s_putCount == 0);
			close_storage(true);
		}
	}
	/* more programs than counted: those counted */
	drop_files();
	reset_combiners();
	v = 1;
	memcpy(buf + 4 * 4 + 13 + 8, &v, 4);
	set_file(SHADERS, buf, n);
	load(true);
	CHECK(s_putCount == 1 && s_compiledCount == 0);
	close_storage(true);
	v = 2;
	memcpy(buf + 4 * 4 + 13 + 8, &v, 4);
	/* more keys than counted: those counted */
	drop_files();
	reset_combiners();
	set_file(KEYS, "5\n0\n1\n1\n2\n", 10);
	load(true);
	CHECK(s_compiledCount == 1 && s_compiled[0].base.key == 1);
	close_storage(true);
	/* hex in either case, with its end where the file ends */
	drop_files();
	reset_combiners();
	set_file(KEYS, "5\n0\n2\n0x9aF0\nfA", 15);
	load(true);
	CHECK(s_compiledCount == 2 && s_compiled[0].base.key == 0x9AF0ULL && s_compiled[1].base.key == 0xFAULL);
	close_storage(true);
	/* a 0x with nothing after it at the end: no key */
	drop_files();
	reset_combiners();
	set_file(KEYS, "5\n0\n2\n1\n0x", 10);
	load(true);
	CHECK(s_compiledCount == 1 && s_compiled[0].base.key == 1);
	close_storage(true);
	/* the programs cut short: those before */
	drop_files();
	reset_combiners();
	set_file(SHADERS, buf, n - 1);
	load(true);
	CHECK(s_putCount == 1 && s_compiledCount == 0);
	close_storage(true);
	/* the keys of another version, or no count: none */
	drop_files();
	reset_combiners();
	set_file(KEYS, "4\n0\n1\n1\n", 8);
	load(true);
	CHECK(s_compiledCount == 0 && !filestream_exists(KEYS) && file_is(CACHE, want, header(want)));
	close_storage(true);
	drop_files();
	reset_combiners();
	set_file(KEYS, "5\n0\n", 4);
	load(true);
	CHECK(s_compiledCount == 0);
	close_storage(true);
	/* fewer keys than counted: those there */
	drop_files();
	reset_combiners();
	set_file(KEYS, "5\n0\n9\n1\n2\n", 10);
	load(true);
	CHECK(s_compiledCount == 2 && s_compiled[1].hwl == 0);
	close_storage(true);
	/* the journal alone */
	drop_files();
	reset_combiners();
	set_file(JOURNAL, "j", 1);
	s_oldJournalCount = 1;
	load(true);
	CHECK(s_compiledCount == 1 && s_compiled[0].base.key == 0x35ULL && !filestream_exists(JOURNAL));
	close_storage(true);
	/* a file of now there: those of before left alone */
	drop_files();
	reset_combiners();
	set_file(JOURNAL, "j", 1);
	set_file(CACHE, want, header(want));
	load(true);
	CHECK(s_compiledCount == 0 && filestream_exists(JOURNAL) && s_deletes == 0);
	close_storage(true);
	s_oldJournalCount = 0;
	reset_combiners();
	drop_files();
}

/* a key's program in the table, as compiled */
/* a key's GL program in the table, with a binary */
static void compile_one(u64 _key)
{
	s_binLen[60] = 4;
	gli_combiners_put(&s_table, gli_glsl_cprog_new(_key, 60, &s_cache, 0, &s_info));
}

/* a header as long as there is room for, and one longer; no memory for
 * the file written again */
static void room(void)
{
	static char renderer[1024], want[1100];
	size_t w;
	int64_t size = 0;
	const size_t most = 1024 - 4 - 5 * 4 - 8;
	memset(renderer, 'r', most);
	renderer[most] = '\0';
	s_renderer = renderer;
	drop_files();
	reset_combiners();
	load(true);
	w = header_of(want, "GLNC", 5, 0x3E, 0x1234u, renderer, "4.6 Stub");
	CHECK(w == 1024 && file_is(CACHE, want, w));
	close_storage(true);
	renderer[most] = 'r';
	renderer[most + 1] = '\0';
	drop_files();
	load(true);
	CHECK(file_data(CACHE, &size) == NULL && s_writes == 0);
	close_storage(true);
	/* no header to write again with: a key the journal lost, failed, the
	 * file of before left */
	drop_files();
	reset_combiners();
	set_file(CACHE, want, header_of(want, "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub"));
	load(true);
	compile_one(0x61ULL);
	s_whole = 0;
	close_storage(false);
	s_whole = 1;
	CHECK(file_is(CACHE, want, header_of(want, "GLNC", 5, 0x3E, 0x1234u, "Stub Renderer", "4.6 Stub")));
	s_renderer = "Stub Renderer";
	/* nor memory for it */
	for (w = 2; w <= 3; ++w) {
		drop_files();
		reset_combiners();
		load(true);
		compile_one(0x61ULL);
		s_whole = 0;
		s_failAlloc = (int)w;
		close_storage(false);
		s_whole = 1;
		CHECK(s_failAlloc == 0 && file_is(CACHE, want, header(want)) && s_appends == 0);
	}
	/* nor to sort the one program by */
	drop_files();
	reset_combiners();
	load(true);
	compile_one(0x61ULL);
	s_failAlloc = 1;
	close_storage(false);
	CHECK(s_failAlloc == 0 && file_is(CACHE, want, header(want)) && s_appends == 0);
	reset_combiners();
	s_binLen[60] = 0;
	/* no context at load: empty strings */
	drop_files();
	s_renderer = NULL;
	s_version = NULL;
	load(true);
	s_renderer = "Stub Renderer";
	s_version = "4.6 Stub";
	{
		size_t e = header_of(want, "GLNC", 5, 0x3E, 0x1234u, "", "");
		CHECK(file_is(CACHE, want, e));
	}
	close_storage(true);
	/* the file's buffer, or its keys' */
	for (w = 1; w <= 2; ++w) {
		drop_files();
		s_failAlloc = (int)w;
		load(true);
		CHECK(s_failAlloc == 0 && file_data(CACHE, &size) == NULL);
		close_storage(true);
	}
	drop_files();
}

/* ---- closed ---- */

/* three GL programs and two stand-ins */
static gli_cprog *s_gl[3];
static fake s_standIn[2];

static void make_programs(void)
{
	s_nextProgram = 10;
	s_binLen[10] = 5;
	s_binLen[11] = 0;   /* no binary: left out */
	s_binLen[12] = 3;
	s_binLen[13] = 2;   /* the stand-in's own */
	s_gl[0] = gli_glsl_cprog_new(HWL | 0x1ULL, s_nextProgram++, &s_cache, 1u << G_GCI_TEXEL0, &s_info);
	s_gl[1] = gli_glsl_cprog_new(0x2ULL, s_nextProgram++, &s_cache, 0, &s_info);
	s_gl[2] = gli_glsl_cprog_new(0x10000000000ULL | 0x3ULL, s_nextProgram++, &s_cache, 1u << G_GCI_SHADE, &s_info);
	memset(s_standIn, 0, sizeof(s_standIn));
	s_standIn[0].base.ops = &s_fakeOps;
	s_standIn[0].base.key = 0x4ULL;
	s_standIn[0].real = gli_glsl_cprog_new(0x4ULL, s_nextProgram++, &s_cache, 1u << G_GCI_TEXEL1, &s_info);
	s_standIn[1].base.ops = &s_fakeOps;
	s_standIn[1].base.key = 0xABCDEF0123456789ULL & ~HWL;
	s_standIn[1].real = NULL; /* still on the ubershader */
	/* put out of order */
	gli_combiners_put(&s_table, &s_standIn[1].base);
	gli_combiners_put(&s_table, s_gl[2]);
	gli_combiners_put(&s_table, s_gl[0]);
	gli_combiners_put(&s_table, &s_standIn[0].base);
	gli_combiners_put(&s_table, s_gl[1]);
}

static void free_stand_in(void)
{
	s_standIn[0].real->ops->free_program((gli_cprog *)s_standIn[0].real);
}

/* the programs' records it appends, by key: key 4 as its stand-in's
 * program 13 (2 bytes), key 3 (program 12, 3 bytes), key 1 (program 10,
 * 5 bytes) */
static size_t stored(char *_out, int _without)
{
	static const u64 keys[3] = { 0x4ULL, 0x10000000003ULL, HWL | 0x1ULL };
	static const GLuint progs[3] = { 13, 12, 10 };
	static const int inputs[3] = { 1 << G_GCI_TEXEL1, 1 << G_GCI_SHADE, 1 << G_GCI_TEXEL0 };
	size_t n = 0;
	int i;
	for (i = 0; i < 3; ++i)
		if (progs[i] != (GLuint)_without)
			n += program_record(_out + n, keys[i], inputs[i], 0x8000u + progs[i], s_binLen[progs[i]],
				(unsigned char)(progs[i] * 16));
	return n;
}

static void closes(void)
{
	static char buf[1024], want[1024], scratch[256];
	size_t n, w;
	gli_ctx.shaderProgramBinary = 1;

	/* the journal wrote the keys as they were met, and hands its stream
	 * over at the end */
	drop_files();
	reset_combiners();
	load(true);
	make_programs();
	n = header(buf);
	n += key_record(buf + n, 0xABCDEF0123456789ULL & ~HWL);
	n += key_record(buf + n, 0x10000000003ULL);
	n += key_record(buf + n, HWL | 0x1ULL);
	n += key_record(buf + n, 0x4ULL);
	n += key_record(buf + n, 0x2ULL);
	set_file(CACHE, buf, n);
	s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
	filestream_seek(s_journalFile, 0, RETRO_VFS_SEEK_POSITION_END);
	counts();
	s_asyncAsked = 0;
	close_storage(true);
	memcpy(want, buf, n);
	w = n + stored(want + n, 0);
	CHECK(file_is(CACHE, want, w));
	CHECK(s_appends == 1 && s_opens == 0 && s_writes == 0);
	CHECK(s_asyncAsked == 2);
	CHECK(gli_glsl_open_journal() == NULL);
	free_stand_in();
	reset_combiners();

	/* read back: the programs, the keys of those it had not compiled */
	s_nextProgram = 40;
	s_binLen[40] = s_binLen[41] = s_binLen[42] = 1;
	reset();
	counts();
	load(true);
	CHECK(s_putCount == 5 && s_compiledCount == 2);
	CHECK(s_put[0]->key == 0x4ULL && s_put[1]->key == 0x10000000003ULL && s_put[2]->key == (HWL | 0x1ULL));
	CHECK(((gli_glsl_cprog *)s_put[0])->inputs == (1u << G_GCI_TEXEL1));
	CHECK(strstr(s_log, "binary 40 800d 2 d0;") != NULL);
	CHECK(strstr(s_log, "create 41;attr 41 aRectPosition;binary 41 800c 3 c0;") != NULL);
	CHECK(strstr(s_log, "binary 42 800a 5 a0;") != NULL);
	CHECK(s_compiled[0].base.key == (0xABCDEF0123456789ULL & ~HWL) && s_compiled[1].base.key == 0x2ULL);
	CHECK(s_writes == 0 && s_rwOpens == 0);
	/* and closed again: nothing to add */
	close_storage(true);
	CHECK(file_is(CACHE, want, w) && s_appends == 0 && s_rwOpens == 0);
	reset_combiners();

	/* no stream from the journal: opened to append */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	close_storage(true);
	CHECK(file_is(CACHE, want, w) && s_rwOpens == 1 && s_appends == 1);
	free_stand_in();
	reset_combiners();
	/* nor a journal, every key in the file: the same */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	CHECK(gli_glsl_close_storage(&s_table, NULL));
	CHECK(file_is(CACHE, want, w) && s_writes == 0);
	free_stand_in();
	reset_combiners();
	/* not opened: failed, nothing written */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_failOpen = ".cache";
	close_storage(false);
	s_failOpen = NULL;
	CHECK(file_is(CACHE, buf, n));
	free_stand_in();
	reset_combiners();
	/* a write failing: failed */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_failWrite = ".cache";
	close_storage(false);
	s_failWrite = NULL;
	free_stand_in();
	reset_combiners();

	/* GL failing one binary: the others */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_errorProgram = 12;
	close_storage(true);
	s_errorProgram = 0;
	memcpy(want, buf, n);
	w = n + stored(want + n, 12);
	CHECK(file_is(CACHE, want, w));
	free_stand_in();
	reset_combiners();

	/* no memory to sort them by: failed, and closed */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
	s_failAlloc = 1;
	close_storage(false);
	CHECK(s_failAlloc == 0 && file_is(CACHE, buf, n) && gli_glsl_open_journal() == NULL);
	free_stand_in();
	reset_combiners();

	/* GL failing every binary: failed */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_glError = 1;
	close_storage(false);
	s_glError = 0;
	CHECK(file_is(CACHE, buf, n));
	free_stand_in();
	reset_combiners();

	/* nothing stored where the builder is obsolete, GL has no binaries or
	 * no context any more */
	{
		int i;
		for (i = 0; i < 3; ++i) {
			set_file(CACHE, buf, n);
			load(true);
			reset_combiners();
			make_programs();
			counts();
			if (i == 0)
				s_obsolete = 1;
			else if (i == 1)
				gli_ctx.shaderProgramBinary = 0;
			else
				s_renderer = NULL;
			close_storage(true);
			s_obsolete = 0;
			gli_ctx.shaderProgramBinary = 1;
			s_renderer = "Stub Renderer";
			CHECK(file_is(CACHE, buf, n) && s_appends == 0 && s_rwOpens == 0 && s_writes == 0);
			free_stand_in();
			reset_combiners();
		}
	}

	/* a key the journal lost: the file written again, then the programs */
	n = header(buf);
	n += key_record(buf + n, 0x10000000003ULL);
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_journalFile = filestream_open(CACHE, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
	counts();
	s_whole = 0;
	close_storage(true);
	w = header(want);
	w += key_record(want + w, 0x2ULL);
	w += key_record(want + w, 0x4ULL);
	w += key_record(want + w, 0x10000000003ULL);
	w += key_record(want + w, HWL | 0x1ULL);
	w += key_record(want + w, 0xABCDEF0123456789ULL & ~HWL);
	w += stored(want + w, 0);
	CHECK(file_is(CACHE, want, w) && s_writes == 1 && s_appends == 1);
	free_stand_in();
	reset_combiners();
	/* lost, only the highest key the file has not: written again too */
	{
		size_t m = header(scratch);
		m += key_record(scratch + m, 0x2ULL);
		m += key_record(scratch + m, 0x4ULL);
		m += key_record(scratch + m, 0x10000000003ULL);
		m += key_record(scratch + m, HWL | 0x1ULL);
		set_file(CACHE, scratch, m);
		load(true);
		reset_combiners();
		make_programs();
		close_storage(true);
		CHECK(file_is(CACHE, want, w) && s_writes == 1);
		free_stand_in();
		reset_combiners();
	}
	/* lost, but none the file has not: appended to */
	set_file(CACHE, want, w - stored(scratch, 0));
	load(true);
	reset_combiners();
	make_programs();
	counts();
	close_storage(true);
	s_whole = 1;
	CHECK(file_is(CACHE, want, w) && s_writes == 0 && s_appends == 1);
	free_stand_in();
	reset_combiners();
	/* lost, and not written again: failed, nothing more */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	make_programs();
	s_whole = 0;
	s_failWrite = ".cache";
	close_storage(false);
	s_failWrite = NULL;
	s_whole = 1;
	CHECK(file_is(CACHE, buf, n));
	free_stand_in();
	reset_combiners();

	/* no programs */
	set_file(CACHE, buf, n);
	load(true);
	reset_combiners();
	close_storage(true);
	CHECK(file_is(CACHE, buf, n) && s_appends == 0);
	drop_files();
}

int main(void)
{
	s_ctxOps.isCombinerProgramBuilderObsolete = ctx_obsolete;
	gli_ctx.ops = &s_ctxOps;
	s_fakeOps.update = fake_update;
	paths();
	records();
	headers();
	torn();
	before();
	room();
	closes();
	if (fails) {
		printf("test_gliden64_glsl_storage: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_glsl_storage: ok\n");
	return 0;
}
