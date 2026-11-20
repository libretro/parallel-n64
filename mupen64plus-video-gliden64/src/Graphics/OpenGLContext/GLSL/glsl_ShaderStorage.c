/* GLideN64's GL shader storage: the combiner keys met and the programs GL
 * built for them, in GL's own binary form, read back at the next start so
 * nothing is compiled while a game plays. C89.
 *
 * One file every game shares (GLideN64.OpenGL.cache, GLideN64.GLES.cache):
 * the N64's combiners are fixed function, a key the same whatever the
 * game. Records appended to: a key as it is met (written by the key
 * journal, so a crash loses none), the programs not stored already when a
 * session ends, behind a record of the options they were built with: a
 * game whose settings differ loads none of another's programs and keeps
 * them all the same. A header says which GL built the programs; when that
 * changes, the file is written again with the keys alone. The files of
 * before, a file a game (GLideN64.<name>.OpenGL.cache) and before those
 * .keys, .shaders and a journal, are read once into it and removed. */
#include <stdlib.h>
#include <string.h>
#include <Types.h>
#include <GBI.h>
#include <PluginAPI.h>
#include <CombinerKey.h>
#include <CombinerKeyJournal.h>
#include <Config.h>
#include <gli_combiner.h>
#include <file/file_path.h>
#include <retro_dirent.h>
#include <streams/file_stream.h>
#include <Graphics/gli_context.h>
#include <Graphics/CombinerStages.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include "glsl_Utils.h"
#include "glsl_CombinerProgramImpl.h"
#include "glsl_Programs.h"

#define SHADER_STORAGE_FOLDER_NAME "shaders"
#define FORMAT_VERSION 0x3FU
/* a game's file of before, whose header named the options too */
#define OLD_FORMAT_VERSION 0x3EU
#define KEYS_FORMAT_VERSION 0x05U
#define CACHE_MAGIC "GLNC"
/* a program's record: its kind and four bits, then its stored form */
#define RECORD_PROGRAM 'B'
/* the options the program records after it were built with: its kind, a
 * byte, then the options where a key would be */
#define RECORD_OPTIONS 'O'
/* before any options record: none */
#define NO_OPTIONS (~(u64)0)

/* One hex number, with or without 0x, after any white space */
static int parse_hex(const char **_p, const char *_end, u64 *_value)
{
	const char *p = *_p;
	const char *start;
	u64 value = 0;
	while (p != _end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		++p;
	if (_end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	start = p;
	for (; p != _end; ++p) {
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

static void put(char *_out, size_t *_len, const void *_data, size_t _size)
{
	memcpy(_out + *_len, _data, _size);
	*_len += _size;
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

/* the shaders folder, made, or the cache folder where it cannot be,
 * into @_path of @_size; its length */
static size_t storage_folder(char *_path, size_t _size)
{
	static const char folderName[] = "/" SHADER_STORAGE_FOLDER_NAME;
	char folder[PLUGIN_PATH_SIZE];
	size_t len;

	gli_api_user_cache_path(folder, sizeof(folder));
	len = put_str(_path, 0, _size, folder, strlen(folder));
	/* the folder made, or the files beside it, written over */
	put_str(_path, len, _size, folderName, sizeof(folderName) - 1);
	if (path_mkdir(_path))
		len = put_str(_path, len, _size, folderName, sizeof(folderName) - 1);
	return len;
}

static const char *api_name(const gli_gl_info *_info)
{
	return _info->isGLESX ? "GLES." : "OpenGL.";
}

/* the file every game shares, into @_path of @_size */
static void storage_path(const gli_gl_info *_info, char *_path, size_t _size)
{
	static const char prefix[] = "/GLideN64.";
	const char *api = api_name(_info);
	size_t len = storage_folder(_path, _size);
	len = put_str(_path, len, _size, prefix, sizeof(prefix) - 1);
	len = put_str(_path, len, _size, api, strlen(api));
	put_str(_path, len, _size, "cache", 5);
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

/* the GL program a stored one is: a key's own, or the one the ubershader
 * stood in for, waited for; NULL when there is none */
static const gli_cprog *gl_program(gli_cprog *_p)
{
	if (_p->ops == &gli_glsl_cprog_ops)
		return _p;
	return gli_glsl_async_program(_p);
}

/* a key's program compiled, its uniforms set, and kept, with the four
 * bits it was met with */
static void compile_key(gli_combiner_table *_combiners, u64 _key, unsigned _hwl)
{
	const bool hwl = gli_GBI_hwlSupported;
	gli_cprog *p;
	gli_GBI_hwlSupported = (_hwl & 1) != 0;
	p = gli_combiner_compile(_key, false);
	gli_GBI_hwlSupported = hwl;
	p->ops->update(p, true);
	gli_combiners_put(_combiners, p);
}

/* a program from its stored form at @_in (inputs, binary format, length
 * and binary), given to GL; NULL when GL takes it not, or, @_short set,
 * when the file ends before it does */
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

/*---------------The files of before-------------*/

/* @_len bytes of @_in, which must be the string @_str */
static int take_string(reader *_in, const char *_str)
{
	u32 len = 0;
	const char *str;
	return _str != NULL && get(_in, &len, sizeof(len)) && (str = take(_in, len)) != NULL &&
		strlen(_str) == len && strncmp(_str, str, len) == 0;
}

/* the keys file of before: a version, the hardware lighting flag, a
 * count and the keys, as hex lines */
static void old_keys(const char *_path, gli_combiner_table *_combiners)
{
	void *file = NULL;
	int64_t size = 0;
	const char *p;
	const char *end;
	u64 version, hwlSupport, szCombiners, key, i;

	if (!filestream_exists(_path) || !filestream_read_file(_path, &file, &size)) {
		free(file);
		return;
	}
	p = (const char *)file;
	end = p + size;
	if (parse_hex(&p, end, &version) && version == KEYS_FORMAT_VERSION &&
			parse_hex(&p, end, &hwlSupport) && parse_hex(&p, end, &szCombiners))
		for (i = 0; i < szCombiners && parse_hex(&p, end, &key); ++i)
			if (gli_combiners_find(_combiners, key) == NULL)
				compile_key(_combiners, key, hwlSupport != 0 ? 1 : 0);
	free(file);
}

/* the shaders file of before: its header, the count and the programs */
static void old_shaders(const char *_path, const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	void *file = NULL;
	int64_t size = 0;
	reader in;
	u32 version = 0, optionsSet = 0, len = 0, i;

	if (!gli_ctx.shaderProgramBinary || !filestream_exists(_path) ||
			!filestream_read_file(_path, &file, &size)) {
		free(file);
		return;
	}
	in.p = (const char *)file;
	in.end = in.p + (size_t)size;
	if (get(&in, &version, sizeof(version)) && version == OLD_FORMAT_VERSION &&
			get(&in, &optionsSet, sizeof(optionsSet)) && optionsSet == gli_combiner_options_bits() &&
			take_string(&in, (const char *)glGetString(GL_RENDERER)) &&
			take_string(&in, (const char *)glGetString(GL_VERSION)) && get(&in, &len, sizeof(len)))
		for (i = 0; i < len; ++i) {
			u64 key;
			gli_cprog *p;
			int cut = 0;
			if (!get(&in, &key, sizeof(key)))
				break;
			p = read_program(&in, key, _info, _useProgram, &cut);
			if (cut)
				break;
			if (p == NULL)
				compile_key(_combiners, key, gli_combiner_key_hwl(key) ? 1 : 0);
			else {
				p->ops->update(p, true);
				gli_combiners_put(_combiners, p);
			}
		}
	free(file);
}

static void old_journal_key(void *_ctx, uint64_t _key, unsigned _hwl)
{
	gli_combiner_table *combiners = (gli_combiner_table *)_ctx;
	if (gli_combiners_find(combiners, _key) == NULL)
		compile_key(combiners, _key, _hwl);
}

/* what a game's three files of before held (@_base with shaders, keys
 * and keys.journal after it), into @_combiners */
static void old_files(const char *_base, const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	char shaders[PLUGIN_PATH_SIZE], keys[PLUGIN_PATH_SIZE], journal[PLUGIN_PATH_SIZE];
	size_t len = put_str(shaders, 0, sizeof(shaders), _base, strlen(_base));
	put_str(shaders, len, sizeof(shaders), "shaders", 7);
	len = put_str(keys, 0, sizeof(keys), _base, strlen(_base));
	put_str(keys, len, sizeof(keys), "keys", 4);
	len = put_str(journal, 0, sizeof(journal), _base, strlen(_base));
	put_str(journal, len, sizeof(journal), "keys.journal", 12);
	old_shaders(shaders, _info, _useProgram, _combiners);
	old_keys(keys, _combiners);
	combiner_journal_read(journal, KEYS_FORMAT_VERSION, old_journal_key, _combiners);
}

/*---------------The file-------------*/

/* The file this session reads and appends to: what its header is, the
 * keys it holds and those of them whose programs it holds, lowest first */
static struct
{
	char path[PLUGIN_PATH_SIZE];
	char header[1024];
	size_t headerLen;
	/* a game's file of before's, for this GL and these options */
	char oldHeader[1024];
	size_t oldHeaderLen;
	u64 *keys;
	u32 keyCount;
	u64 *programs;
	u32 programCount;
	/* of other programs, or holding one GL no longer takes: written again
	 * as it closes */
	bool again;
} s_store;

static int compare_keys(const void *_a, const void *_b)
{
	const u64 a = *(const u64 *)_a, b = *(const u64 *)_b;
	return a < b ? -1 : a > b ? 1 : 0;
}

static bool has_key(const u64 *_keys, u32 _count, u64 _key)
{
	return _keys != NULL && bsearch(&_key, _keys, _count, sizeof(u64), compare_keys) != NULL;
}

static void forget(void)
{
	free(s_store.keys);
	free(s_store.programs);
	s_store.keys = s_store.programs = NULL;
	s_store.keyCount = s_store.programCount = 0;
}

/* A header: which GL the programs are for, and for a game's file of
 * before the options too, into @_out of @_size; 0 long when it does not
 * fit */
static size_t make_header(char *_out, size_t _size, u32 _formatVersion, bool _options)
{
	static const u32 keysVersion = KEYS_FORMAT_VERSION;
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *version = (const char *)glGetString(GL_VERSION);
	const u32 optionsBits = gli_combiner_options_bits();
	u32 rendererLen, versionLen;
	size_t len = 0;
	if (renderer == NULL)
		renderer = "";
	if (version == NULL)
		version = "";
	rendererLen = (u32)strlen(renderer);
	versionLen = (u32)strlen(version);
	if (_size < 4 + (_options ? 5 : 4) * sizeof(u32) + rendererLen + versionLen)
		return 0;
	put(_out, &len, CACHE_MAGIC, 4);
	put(_out, &len, &keysVersion, sizeof(keysVersion));
	put(_out, &len, &_formatVersion, sizeof(_formatVersion));
	if (_options)
		put(_out, &len, &optionsBits, sizeof(optionsBits));
	put(_out, &len, &rendererLen, sizeof(rendererLen));
	put(_out, &len, renderer, rendererLen);
	put(_out, &len, &versionLen, sizeof(versionLen));
	put(_out, &len, version, versionLen);
	return len;
}

/* The file written again: the header and a key record of each of the
 * @_count @_programs, none of their programs; what it holds kept */
static bool rewrite(gli_cprog *const *_programs, u32 _count)
{
	size_t len = 0;
	char *out;
	u32 i;
	bool ok;
	forget();
	if (s_store.headerLen == 0)
		return false;
	out = (char *)malloc(s_store.headerLen + (size_t)_count * COMBINER_JOURNAL_RECORD_SIZE);
	s_store.keys = (u64 *)malloc(((size_t)_count + 1) * sizeof(u64));
	if (out == NULL || s_store.keys == NULL) {
		free(out);
		forget();
		return false;
	}
	put(out, &len, s_store.header, s_store.headerLen);
	for (i = 0; i < _count; ++i) {
		const u64 key = _programs[i]->key;
		out[len++] = COMBINER_JOURNAL_RECORD_KEY;
		out[len++] = (char)(gli_combiner_key_hwl(key) ? 1 : 0);
		put(out, &len, &key, sizeof(key));
		s_store.keys[i] = key;
	}
	ok = filestream_write_file(s_store.path, out, (int64_t)len);
	free(out);
	if (ok)
		s_store.keyCount = _count;
	else
		forget();
	return ok;
}

/* @_key noted in @_keys, of @_count, room for @_room, which grows */
static void note(u64 **_keys, u32 *_count, u32 *_room, u64 _key)
{
	if (*_count == *_room) {
		u64 *more = (u64 *)realloc(*_keys, ((size_t)*_room * 2 + 64) * sizeof(u64));
		if (more == NULL)
			return;
		*_keys = more;
		*_room = *_room * 2 + 64;
	}
	(*_keys)[(*_count)++] = _key;
}

/* The records of @_file, @_size bytes, from @_at; where the whole ones
 * end. Its programs of these options go to GL first when @_programs,
 * those before an options record being of @_options; the keys of the rest
 * are compiled after, so a key met before its program was stored is not
 * built twice. @_stale set where GL would not take a program: its key's
 * new one is stored, so the old must go. */
static size_t read_records(const char *_file, size_t _size, size_t _at, bool _programs, u64 _options,
	const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram, gli_combiner_table *_combiners,
	bool *_stale)
{
	const u64 mine = gli_combiner_options_bits();
	reader in;
	size_t end = _at;
	u32 room = 0, keyRoom = 0;
	int pass;

	for (pass = 0; pass < 2; ++pass) {
		u64 options = _options;
		in.p = _file + _at;
		in.end = _file + (pass == 0 ? _size : end);
		for (;;) {
			const char *kind = take(&in, 2);
			u64 key;
			gli_cprog *p = NULL;
			if (kind == NULL || !get(&in, &key, sizeof(key)))
				break;
			if (kind[0] == RECORD_OPTIONS) {
				options = key;
				if (pass == 0)
					end = (size_t)(in.p - _file);
				continue;
			}
			if (kind[0] == RECORD_PROGRAM) {
				int cut = 0;
				if (pass == 0 && _programs && options == mine && gli_combiners_find(_combiners, key) == NULL) {
					p = read_program(&in, key, _info, _useProgram, &cut);
					if (p == NULL && !cut)
						*_stale = true;
				} else {
					/* its stored form passed over */
					int inputs;
					GLenum format;
					GLint length = 0;
					if (!get(&in, &inputs, sizeof(inputs)) || !get(&in, &format, sizeof(format)) ||
							!get(&in, &length, sizeof(length)) || length < 1 ||
							take(&in, (size_t)length) == NULL)
						cut = 1;
				}
				if (cut)
					break;
			} else if (kind[0] != COMBINER_JOURNAL_RECORD_KEY)
				break;
			if (pass == 1) {
				note(&s_store.keys, &s_store.keyCount, &keyRoom, key);
				/* the four bits a key was met with are in it */
				if (gli_combiners_find(_combiners, key) == NULL)
					compile_key(_combiners, key, gli_combiner_key_hwl(key) ? 1 : 0);
				continue;
			}
			end = (size_t)(in.p - _file);
			if (p == NULL)
				continue;
			p->ops->update(p, true);
			gli_combiners_put(_combiners, p);
			note(&s_store.programs, &s_store.programCount, &room, key);
		}
	}
	if (s_store.programs != NULL)
		qsort(s_store.programs, s_store.programCount, sizeof(u64), compare_keys);
	/* a key twice is found as well */
	if (s_store.keys != NULL)
		qsort(s_store.keys, s_store.keyCount, sizeof(u64), compare_keys);
	return end;
}

/* @_file of @_size, of the magic, a keys version this build reads and
 * @_words words after it, and two strings: where its records start, 0
 * where it is not one */
static size_t records_at(const char *_file, int64_t _size, unsigned _words)
{
	reader in;
	const char *magic;
	u32 keysVersion = 0, len;
	in.p = _file;
	in.end = _file + _size;
	/* the keys hold for other programs, while their meaning does */
	if ((magic = take(&in, 4)) != NULL && memcmp(magic, CACHE_MAGIC, 4) == 0 &&
			get(&in, &keysVersion, sizeof(keysVersion)) && keysVersion == KEYS_FORMAT_VERSION &&
			take(&in, _words * sizeof(u32)) != NULL &&
			get(&in, &len, sizeof(len)) && take(&in, len) != NULL &&
			get(&in, &len, sizeof(len)) && take(&in, len) != NULL)
		return (size_t)(in.p - _file);
	return 0;
}

/* @_path held, mapped where it can be, read once where not: @_f and
 * @_copy to let go */
static const char *hold(const char *_path, RFILE **_f, char **_copy, int64_t *_size)
{
	const char *file = NULL;
	*_copy = NULL;
	*_size = 0;
	*_f = filestream_open(_path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
	if (*_f == NULL)
		return NULL;
	file = (const char *)filestream_get_mapped_ptr(*_f, _size);
	if (file == NULL && (*_size = filestream_get_size(*_f)) > 0 &&
			(*_copy = (char *)malloc((size_t)*_size)) != NULL && filestream_read(*_f, *_copy, *_size) == *_size)
		file = *_copy;
	return file;
}

static void let_go(RFILE *_f, char *_copy)
{
	free(_copy);
	if (_f != NULL)
		filestream_close(_f);
}

/* a game's file of before: its keys, and its programs where they were
 * built by this GL with these options */
static void old_cache(const char *_path, const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	RFILE *f;
	char *copy;
	int64_t size;
	const char *file = hold(_path, &f, &copy, &size);
	size_t at;
	bool stale = false;
	if (file != NULL && (at = records_at(file, size, 2)) != 0) {
		const bool same = s_store.oldHeaderLen != 0 && (size_t)size >= s_store.oldHeaderLen &&
			memcmp(file, s_store.oldHeader, s_store.oldHeaderLen) == 0;
		read_records(file, (size_t)size, at, same && gli_ctx.shaderProgramBinary,
			gli_combiner_options_bits(), _info, _useProgram, _combiners, &stale);
	}
	let_go(f, copy);
}

/* the names of before in @_folder, of @_api: a game's file, its three
 * files of before that, and those of a crash's left behind */
static bool old_name(const char *_name, const char *_api, size_t *_base)
{
	static const char *const ext[] = { "cache", "shaders", "keys.journal", "keys" };
	const size_t n = strlen(_name), a = strlen(_api);
	const char *dot;
	unsigned i;
	if (strncmp(_name, "GLideN64.", 9) != 0 || (dot = strchr(_name + 9, '.')) == NULL || dot == _name + 9 ||
			strncmp(dot + 1, _api, a) != 0)
		return false;
	for (i = 0; i < sizeof(ext) / sizeof(*ext); ++i)
		if ((size_t)(dot + 1 + a - _name) + strlen(ext[i]) == n && strcmp(dot + 1 + a, ext[i]) == 0) {
			*_base = (size_t)(dot + 1 + a - _name);
			return true;
		}
	return false;
}

/* Every game's files of before in @_folder, read into @_combiners; their
 * names kept in @_names (@_count of them), for when the file is written.
 * A game's three files are read once, at the first of them. */
static void old_games(const char *_folder, const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners, char ***_names, u32 *_count)
{
	struct RDIR *d = retro_opendir(_folder);
	const char *api = api_name(_info);
	char **names = NULL;
	u32 count = 0, room = 0, i, j;
	*_names = NULL;
	*_count = 0;
	if (d == NULL)
		return;
	/* the names first: what is read is not listed again */
	while (retro_readdir(d)) {
		const char *name = retro_dirent_get_name(d);
		size_t base, n;
		char *copy;
		if (name == NULL || !old_name(name, api, &base))
			continue;
		if (count == room) {
			char **more = (char **)realloc(names, ((size_t)room * 2 + 16) * sizeof(*names));
			if (more == NULL)
				break;
			names = more;
			room = room * 2 + 16;
		}
		n = strlen(name);
		if ((copy = (char *)malloc(n + 1)) == NULL)
			break;
		memcpy(copy, name, n + 1);
		names[count++] = copy;
	}
	retro_closedir(d);
	for (i = 0; i < count; ++i) {
		char path[PLUGIN_PATH_SIZE];
		size_t base;
		bool seen = false;
		if (!old_name(names[i], api, &base))
			continue;
		fill_pathname_join_special(path, _folder, names[i], sizeof(path));
		if (strcmp(names[i] + base, "cache") == 0) {
			old_cache(path, _info, _useProgram, _combiners);
			continue;
		}
		/* the three of a game read once */
		for (j = 0; j < i && !seen; ++j)
			seen = strncmp(names[j], names[i], base) == 0 && strcmp(names[j] + base, "cache") != 0;
		if (!seen) {
			/* the path but for its ending */
			path[strlen(path) - strlen(names[i] + base)] = '\0';
			old_files(path, _info, _useProgram, _combiners);
		}
	}
	*_names = names;
	*_count = count;
}

bool gli_glsl_load_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	RFILE *f;
	const char *file;
	char *copy;
	int64_t size = 0;
	size_t end = 0;
	bool same = false, stale = false;

	forget();
	storage_path(_info, s_store.path, sizeof(s_store.path));
	s_store.headerLen = make_header(s_store.header, sizeof(s_store.header), FORMAT_VERSION, false);
	s_store.oldHeaderLen = make_header(s_store.oldHeader, sizeof(s_store.oldHeader), OLD_FORMAT_VERSION, true);

	/* one open; the file lent where it can be mapped, read once where not */
	file = hold(s_store.path, &f, &copy, &size);
	if (file != NULL) {
		size_t at;
		same = s_store.headerLen != 0 && (size_t)size >= s_store.headerLen &&
			memcmp(file, s_store.header, s_store.headerLen) == 0;
		if ((at = records_at(file, size, 1)) != 0)
			end = read_records(file, (size_t)size, at, same && gli_ctx.shaderProgramBinary, NO_OPTIONS,
				_info, _useProgram, _combiners, &stale);
	}
	let_go(f, copy);

	s_store.again = end != 0 && (!same || stale);
	if (end == 0) {
		/* not there, or no header whole: what the files of before held
		 * read in, and it written now, for the journal to append to; they
		 * go once it is */
		char folder[PLUGIN_PATH_SIZE];
		char **old;
		u32 count, oldCount, i;
		gli_cprog **programs;
		storage_folder(folder, sizeof(folder));
		old_games(folder, _info, _useProgram, _combiners, &old, &oldCount);
		programs = sorted_programs(_combiners, &count);
		if ((programs != NULL || _combiners->count == 0) && rewrite(programs, count))
			for (i = 0; i < oldCount; ++i) {
				char path[PLUGIN_PATH_SIZE];
				fill_pathname_join_special(path, folder, old[i], sizeof(path));
				filestream_delete(path);
			}
		free(programs);
		for (i = 0; i < oldCount; ++i)
			free(old[i]);
		free(old);
	} else if (!s_store.again) {
		/* cut short by a crash past its last whole record */
		if ((int64_t)end < size && (f = filestream_open(s_store.path,
				RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,
				RETRO_VFS_FILE_ACCESS_HINT_NONE)) != NULL) {
			filestream_truncate(f, (int64_t)end);
			filestream_close(f);
		}
	}

	return !gli_gl_has_error();
}

struct combiner_journal *gli_glsl_open_journal(void)
{
	return s_store.path[0] != '\0' ? combiner_journal_append(s_store.path) : NULL;
}

bool gli_glsl_close_storage(const gli_combiner_table *_combiners, struct combiner_journal *_journal)
{
	int whole;
	RFILE *f = combiner_journal_detach(_journal, &whole);
	gli_cprog **programs = NULL;
	u32 count = 0, i;
	char *out = NULL;
	bool ok = true;
	/* programs are worth storing */
	const bool binaries = gli_ctx.shaderProgramBinary && glGetString(GL_RENDERER) != NULL &&
		!gli_ctx.ops->isCombinerProgramBuilderObsolete(gli_ctx.impl);

	if (s_store.path[0] == '\0' || (whole && !s_store.again &&
			(!binaries || _combiners->count == s_store.programCount))) {
		/* nothing to add: every key is in the file, and every program
		 * worth storing */
		if (f != NULL)
			filestream_close(f);
		forget();
		ok = s_store.path[0] != '\0';
		s_store.path[0] = '\0';
		return ok;
	}
	programs = sorted_programs(_combiners, &count);
	if (programs == NULL && _combiners->count != 0) {
		if (f != NULL)
			filestream_close(f);
		forget();
		s_store.path[0] = '\0';
		return false;
	}

	/* written again where the load said so, or where the journal did not
	 * get a key into the file */
	if (s_store.again) {
		if (f != NULL)
			filestream_close(f);
		f = NULL;
		ok = rewrite(programs, count);
	} else if (!whole)
		for (i = 0; i < count; ++i)
			if (!has_key(s_store.keys, s_store.keyCount, programs[i]->key)) {
				if (f != NULL)
					filestream_close(f);
				f = NULL;
				ok = rewrite(programs, count);
				break;
			}

	/* the programs not stored yet, when they are worth storing, in one
	 * buffer GL writes each binary straight into, behind its kind */
	if (ok && binaries) {
		const gli_cprog **gl = (const gli_cprog **)programs;
		size_t room = 0, len = 0;
		for (i = 0; i < count; ++i) {
			size_t n = 0;
			gl[i] = has_key(s_store.programs, s_store.programCount, programs[i]->key) ?
				NULL : gl_program(programs[i]);
			if (gl[i] != NULL && (n = gli_glsl_cprog_binary_size(gl[i])) == 0)
				gl[i] = NULL;
			room += n != 0 ? 2 + n : 0;
		}
		/* behind the options they were built with */
		if (room != 0 && (out = (char *)malloc(room + COMBINER_JOURNAL_RECORD_SIZE)) != NULL) {
			const u64 options = gli_combiner_options_bits();
			out[0] = RECORD_OPTIONS;
			out[1] = 0;
			memcpy(out + 2, &options, sizeof(options));
			len = COMBINER_JOURNAL_RECORD_SIZE;
			room += COMBINER_JOURNAL_RECORD_SIZE;
		}
		if (out != NULL)
			for (i = 0; i < count; ++i) {
				size_t n;
				if (gl[i] == NULL)
					continue;
				out[len] = RECORD_PROGRAM;
				out[len + 1] = (char)(gli_combiner_key_hwl(gl[i]->key) ? 1 : 0);
				n = gli_glsl_cprog_binary_write(gl[i], out + len + 2, room - len - 2);
				if (n != 0)
					len += 2 + n;
			}
		/* the options record alone: nothing to store */
		if (len == COMBINER_JOURNAL_RECORD_SIZE)
			len = 0;
		if (len != 0 && f == NULL)
			f = filestream_open(s_store.path, RETRO_VFS_FILE_ACCESS_READ_WRITE |
				RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		if (len != 0) {
			ok = f != NULL && filestream_seek(f, 0, RETRO_VFS_SEEK_POSITION_END) >= 0 &&
				filestream_write(f, out, (int64_t)len) == (int64_t)len;
		} else if (room != 0)
			ok = false;
	}
	free(out);
	free(programs);
	if (f != NULL)
		filestream_close(f);
	forget();
	s_store.path[0] = '\0';
	return ok;
}
