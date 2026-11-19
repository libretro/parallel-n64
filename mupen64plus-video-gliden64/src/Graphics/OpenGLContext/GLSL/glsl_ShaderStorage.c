/* GLideN64's GL shader storage: the combiner keys a game used, as text,
 * and the programs GL built for them, in GL's own binary form, read back
 * at the next start so nothing is compiled while it plays. C89. */
#include <stdlib.h>
#include <string.h>
#include <Types.h>
#include <RSP.h>
#include <GBI.h>
#include <PluginAPI.h>
#include <CombinerKey.h>
#include <Config.h>
#include <gli_combiner.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <Graphics/gli_context.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include "glsl_Utils.h"
#include "glsl_CombinerProgramImpl.h"
#include "glsl_Programs.h"
#include "glsl_StdHash.h"

#define SHADER_STORAGE_FOLDER_NAME "shaders"
#define FORMAT_VERSION 0x3EU
#define KEYS_FORMAT_VERSION 0x05U

/* @_value in lower case hex, zero padded to @_digits, written at @_out;
 * how many characters */
static size_t put_hex(char *_out, u64 _value, int _digits)
{
	static const char digits[] = "0123456789abcdef";
	char buf[16];
	int n = 0, i;
	size_t len = 0;
	do {
		buf[n++] = digits[_value & 0xF];
		_value >>= 4;
	} while (_value != 0 && n < 16);
	for (i = n; i < _digits; ++i)
		_out[len++] = '0';
	while (n > 0)
		_out[len++] = buf[--n];
	return len;
}

/* one line of the keys file: 0x, the number, a new line */
static size_t put_keys_line(char *_out, u64 _value, int _digits)
{
	size_t len;
	_out[0] = '0';
	_out[1] = 'x';
	len = 2 + put_hex(_out + 2, _value, _digits);
	_out[len++] = '\n';
	return len;
}

/* One hex number, with or without 0x, after any white space */
static int parse_hex(const char **_p, const char *_end, u64 *_value)
{
	const char *p = *_p;
	const char *start;
	u64 value = 0;
	while (p < _end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		++p;
	if (_end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	start = p;
	for (; p < _end; ++p) {
		const char c = *p;
		u32 d;
		if (c >= '0' && c <= '9') d = (u32)(c - '0');
		else if (c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') d = (u32)(c - 'A' + 10);
		else break;
		value = (value << 4) | d;
	}
	*_p = p;
	*_value = value;
	return p != start;
}

/* Reads from a file held in memory; a short read takes nothing */
typedef struct reader
{
	const char *p;
	const char *end;
} reader;

static const char *take(reader *_in, size_t _size)
{
	const char *at;
	if ((size_t)(_in->end - _in->p) < _size)
		return NULL;
	at = _in->p;
	_in->p += _size;
	return at;
}

static int get(reader *_in, void *_value, size_t _size)
{
	const char *at = take(_in, _size);
	if (at != NULL)
		memcpy(_value, at, _size);
	return at != NULL;
}

/* @_n bytes of @_s put at @_len in @_path of @_size, as many as fit with
 * its end; the new length */
static size_t put_str(char *_path, size_t _len, size_t _size, const char *_s, size_t _n)
{
	if (_len + _n >= _size)
		_n = _size - 1 - _len;
	memcpy(_path + _len, _s, _n);
	_path[_len + _n] = '\0';
	return _len + _n;
}

/* the storage file for this game, of @_ext, into @_path of @_size */
static void storage_path(const gli_gl_info *_info, const char *_ext, char *_path, size_t _size)
{
	static const char folderName[] = "/" SHADER_STORAGE_FOLDER_NAME;
	static const char prefix[] = "/GLideN64.";
	char folder[PLUGIN_PATH_SIZE];
	char hex[16];
	const char *api = _info->isGLESX ? ".GLES." : ".OpenGL.";
	size_t folderLen, len;

	if (_size == 0)
		return;
	gli_api_user_cache_path(folder, sizeof(folder));
	folderLen = strlen(folder);

	len = put_str(_path, 0, _size, folder, folderLen);
	/* the folder made, or the files beside it, written over */
	put_str(_path, len, _size, folderName, sizeof(folderName) - 1);
	if (path_mkdir(_path))
		len = put_str(_path, len, _size, folderName, sizeof(folderName) - 1);

	len = put_str(_path, len, _size, prefix, sizeof(prefix) - 1);
	len = put_str(_path, len, _size, hex, put_hex(hex, gli_std_hash_string(RSP.romname), 0));
	len = put_str(_path, len, _size, api, strlen(api));
	put_str(_path, len, _size, _ext, strlen(_ext));
}

/* the programs by key, lowest first, from a buffer taken here; NULL when
 * there are none or no memory */
static gli_cprog **sorted_programs(const gli_combiner_table *_combiners, u32 *_count)
{
	gli_cprog **programs;
	*_count = 0;
	if (_combiners->count == 0)
		return NULL;
	programs = (gli_cprog **)malloc(_combiners->count * sizeof(*programs));
	if (programs != NULL)
		*_count = gli_combiners_sorted(_combiners, programs);
	return programs;
}

/*
Storage has text format:
line_1 Version in hex form
line_2 Hardware per pixel lighting support flag
line_3 Count - numbers of combiners keys in hex form
line_4..line_Count+3  combiners keys in hex form, one key per line
*/
static bool save_keys(const gli_gl_info *_info, const gli_combiner_table *_combiners)
{
	char path[PLUGIN_PATH_SIZE];
	u32 count, i;
	gli_cprog **programs = sorted_programs(_combiners, &count);
	char *out;
	size_t len = 0;
	bool ok;

	if (programs == NULL && _combiners->count != 0)
		return false;
	out = (char *)malloc(3 * 11 + (size_t)count * 19);
	if (out == NULL) {
		free(programs);
		return false;
	}
	len += put_keys_line(out + len, KEYS_FORMAT_VERSION, 8);
	len += put_keys_line(out + len, gli_GBI_hwlSupported ? 1 : 0, 8);
	len += put_keys_line(out + len, count, 8);
	for (i = 0; i < count; ++i)
		len += put_keys_line(out + len, programs[i]->key, 16);
	free(programs);

	storage_path(_info, "keys", path, sizeof(path));
	ok = filestream_write_file(path, out, (int64_t)len);
	free(out);
	return ok;
}

/* the GL program a stored one is: a key's own, or the one the ubershader
 * stood in for, waited for; NULL when there is none */
static const gli_cprog *gl_program(gli_cprog *_p)
{
	if (_p->ops == &gli_glsl_cprog_ops)
		return _p;
	return gli_glsl_async_program(_p);
}

static void put(char *_out, size_t *_len, const void *_data, size_t _size)
{
	memcpy(_out + *_len, _data, _size);
	*_len += _size;
}

/*
Storage format:
uint32 - format version;
uint32 - bitset of config options, which may change how shader is created.
uint32 - len of renderer string
char * - renderer string
uint32 - len of GL version string
char * - GL version string
uint32 - number of shaders
shaders in binary form
*/
bool gli_glsl_save_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	const gli_combiner_table *_combiners)
{
	static const u32 formatVersion = FORMAT_VERSION;
	char path[PLUGIN_PATH_SIZE];
	const char *strRenderer;
	const char *strGLVersion;
	u32 rendererLen, versionLen, count, i, written = 0;
	u32 optionsBits;
	const gli_cprog **gl;
	gli_cprog **programs;
	size_t room, len = 0, countAt;
	char *out;
	bool ok;

	(void)_useProgram;
	if (!save_keys(_info, _combiners))
		return false;

	if (gli_ctx.ops->isCombinerProgramBuilderObsolete(gli_ctx.impl))
		/* Created shaders are obsolete due to changes in config, but we saved combiners keys. */
		return true;

	if (!gli_ctx.shaderProgramBinary)
		/* Shaders storage is not supported, but we saved combiners keys. */
		return true;

	strRenderer = (const char *)glGetString(GL_RENDERER);
	if (strRenderer == NULL)
		/* No current context any more; but we saved combiners keys. */
		return true;
	strGLVersion = (const char *)glGetString(GL_VERSION);
	rendererLen = (u32)strlen(strRenderer);
	versionLen = (u32)strlen(strGLVersion);
	optionsBits = gli_combiner_options_bits();

	programs = sorted_programs(_combiners, &count);
	if (programs == NULL && _combiners->count != 0)
		return false;
	/* each program's GL program, and the room the whole file takes, so it
	 * is built in one buffer and GL writes each binary straight into it */
	gl = (const gli_cprog **)programs;
	room = 5 * sizeof(u32) + rendererLen + versionLen;
	for (i = 0; i < count; ++i) {
		gl[i] = gl_program(programs[i]);
		if (gl[i] != NULL)
			room += gli_glsl_cprog_binary_size(gl[i]);
	}
	out = (char *)malloc(room);
	if (out == NULL) {
		free(programs);
		return false;
	}

	put(out, &len, &formatVersion, sizeof(formatVersion));
	put(out, &len, &optionsBits, sizeof(optionsBits));
	put(out, &len, &rendererLen, sizeof(rendererLen));
	put(out, &len, strRenderer, rendererLen);
	put(out, &len, &versionLen, sizeof(versionLen));
	put(out, &len, strGLVersion, versionLen);
	/* the count goes in front of the shaders it counts */
	countAt = len;
	len += sizeof(written);

	for (i = 0; i < count; ++i) {
		size_t n;
		if (gl[i] == NULL)
			continue;
		n = gli_glsl_cprog_binary_write(gl[i], out + len, room - len);
		if (n != 0) {
			len += n;
			++written;
		}
	}
	free(programs);
	memcpy(out + countAt, &written, sizeof(written));

	storage_path(_info, "shaders", path, sizeof(path));
	ok = filestream_write_file(path, out, (int64_t)len);
	free(out);
	return ok;
}

/* a program read back: its inputs, binary format and length, and the
 * binary given to GL; NULL when GL takes it not, or, @_short set, when the
 * file ends before it does */
static gli_cprog *read_program(reader *_in, u64 _key, const gli_gl_info *_info,
	struct gli_gl_cache1 *_useProgram, int *_short)
{
	int inputs = 0;
	GLenum binaryFormat = 0;
	GLint binaryLength = 0;
	const char *binary;
	GLuint program;

	get(_in, &inputs, sizeof(inputs));
	get(_in, &binaryFormat, sizeof(binaryFormat));
	get(_in, &binaryLength, sizeof(binaryLength));
	binary = binaryLength > 0 ? take(_in, (size_t)binaryLength) : NULL;
	if (binary == NULL) {
		*_short = 1;
		return NULL;
	}

	program = glCreateProgram();
	gli_glsl_locate_attributes(program, gli_combiner_key_is_rect(_key), gli_inputs_texture((u32)inputs));
	glProgramBinary(program, binaryFormat, binary, binaryLength);
	if (!gli_glsl_link_ok(program, 1)) {
		glDeleteProgram(program);
		return NULL;
	}
	return gli_glsl_cprog_new(_key, program, _useProgram, (u32)inputs, _info);
}

/* a key's program compiled, its uniforms set, and kept */
static void compile_key(gli_combiner_table *_combiners, u64 _key)
{
	gli_cprog *p = gli_combiner_compile(_key, false);
	p->ops->update(p, true);
	gli_combiners_put(_combiners, p);
}

static bool load_keys(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	char path[PLUGIN_PATH_SIZE];
	void *file = NULL;
	int64_t size = 0;
	const char *p;
	const char *end;
	u64 version, hwlSupport, szCombiners, key, i;

	storage_path(_info, "keys", path, sizeof(path));
	if (!filestream_read_file(path, &file, &size)) {
		free(file);
		return false;
	}

	p = (const char *)file;
	end = p + size;
	if (!parse_hex(&p, end, &version) || version != KEYS_FORMAT_VERSION ||
			!parse_hex(&p, end, &hwlSupport) || !parse_hex(&p, end, &szCombiners)) {
		free(file);
		return false;
	}
	gli_GBI_hwlSupported = hwlSupport != 0;

	for (i = 0; i < szCombiners && parse_hex(&p, end, &key); ++i)
		compile_key(_combiners, key);
	free(file);

	if (gli_gl_has_error())
		return false;

	if (gli_ctx.shaderProgramBinary)
		/* Restore shaders storage */
		return gli_glsl_save_storage(_info, _useProgram, _combiners);

	return true;
}

/* @_len bytes of @_in, which must be the string @_str */
static int take_string(reader *_in, const char *_str)
{
	u32 len = 0;
	const char *str;
	return _str != NULL && get(_in, &len, sizeof(len)) && (str = take(_in, len)) != NULL &&
		strncmp(_str, str, len) == 0;
}

bool gli_glsl_load_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	char path[PLUGIN_PATH_SIZE];
	void *file = NULL;
	int64_t size = 0;
	reader in;
	u32 version = 0, optionsSet = 0, len = 0, i;
	int match;

	if (!gli_ctx.shaderProgramBinary)
		/* Shaders storage is not supported, load from combiners keys. */
		return load_keys(_info, _useProgram, _combiners);

	storage_path(_info, "shaders", path, sizeof(path));
	if (!filestream_read_file(path, &file, &size)) {
		free(file);
		return load_keys(_info, _useProgram, _combiners);
	}
	in.p = (const char *)file;
	in.end = in.p + (size_t)size;

	/* any mismatch, or a file cut short in its header, falls back to the keys */
	match = get(&in, &version, sizeof(version)) && version == FORMAT_VERSION &&
		get(&in, &optionsSet, sizeof(optionsSet)) && optionsSet == gli_combiner_options_bits();
	match = match && take_string(&in, (const char *)glGetString(GL_RENDERER));
	match = match && take_string(&in, (const char *)glGetString(GL_VERSION));

	if (!match || !get(&in, &len, sizeof(len))) {
		free(file);
		return load_keys(_info, _useProgram, _combiners);
	}

	for (i = 0; i < len; ++i) {
		u64 key;
		gli_cprog *p;
		int cut = 0;
		if (!get(&in, &key, sizeof(key)))
			break;
		p = read_program(&in, key, _info, _useProgram, &cut);
		if (p != NULL) {
			p->ops->update(p, true);
			gli_combiners_put(_combiners, p);
		} else {
			if (cut)
				break;
			compile_key(_combiners, key);
		}
	}

	free(file);
	return !gli_gl_has_error();
}

void gli_glsl_journal_path(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	char *_path, size_t _size, u32 *_version)
{
	(void)_useProgram;
	*_version = KEYS_FORMAT_VERSION;
	storage_path(_info, "keys.journal", _path, _size);
}
