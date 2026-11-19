/* GLideN64's GL shader storage: the combiner keys a game used and the
 * programs GL built for them, in GL's own binary form, read back at the
 * next start so nothing is compiled while it plays. C89.
 *
 * One file a game, of records appended to: a key as it is met (written by
 * the key journal, so a crash loses none), a key's program when the
 * session ends, only those not stored already. A header says what the
 * programs were built for; when that changes, the file is written again
 * with the keys alone. The files of before (.keys, .shaders and the
 * journal) are read once into it and removed. */
#include <stdlib.h>
#include <string.h>
#include <Types.h>
#include <RSP.h>
#include <GBI.h>
#include <PluginAPI.h>
#include <CombinerKey.h>
#include <CombinerKeyJournal.h>
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
#define CACHE_MAGIC "GLNC"
/* a program's record: its kind and four bits, then its stored form */
#define RECORD_PROGRAM 'B'

/* @_value in lower case hex written at @_out; how many characters */
static size_t put_hex(char *_out, u64 _value)
{
	static const char digits[] = "0123456789abcdef";
	char buf[16];
	int n = 0;
	size_t len = 0;
	do {
		buf[n++] = digits[_value & 0xF];
		_value >>= 4;
	} while (_value != 0);
	while (n > 0)
		_out[len++] = buf[--n];
	return len;
}

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

/* the storage file for this game, of @_ext, into @_path of @_size */
static void storage_path(const gli_gl_info *_info, const char *_ext, char *_path, size_t _size)
{
	static const char folderName[] = "/" SHADER_STORAGE_FOLDER_NAME;
	static const char prefix[] = "/GLideN64.";
	char folder[PLUGIN_PATH_SIZE];
	char hex[16];
	const char *api = _info->isGLESX ? ".GLES." : ".OpenGL.";
	size_t folderLen, len;

	gli_api_user_cache_path(folder, sizeof(folder));
	folderLen = strlen(folder);

	len = put_str(_path, 0, _size, folder, folderLen);
	/* the folder made, or the files beside it, written over */
	put_str(_path, len, _size, folderName, sizeof(folderName) - 1);
	if (path_mkdir(_path))
		len = put_str(_path, len, _size, folderName, sizeof(folderName) - 1);

	len = put_str(_path, len, _size, prefix, sizeof(prefix) - 1);
	len = put_str(_path, len, _size, hex, put_hex(hex, gli_std_hash_string(RSP.romname)));
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
	if (get(&in, &version, sizeof(version)) && version == FORMAT_VERSION &&
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

/* what the files of before held, into @_combiners, and the files gone */
static void old_files(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	char shaders[PLUGIN_PATH_SIZE], keys[PLUGIN_PATH_SIZE], journal[PLUGIN_PATH_SIZE];
	storage_path(_info, "shaders", shaders, sizeof(shaders));
	storage_path(_info, "keys", keys, sizeof(keys));
	storage_path(_info, "keys.journal", journal, sizeof(journal));
	if (!filestream_exists(shaders) && !filestream_exists(keys) && !filestream_exists(journal))
		return;
	old_shaders(shaders, _info, _useProgram, _combiners);
	old_keys(keys, _combiners);
	combiner_journal_read(journal, KEYS_FORMAT_VERSION, old_journal_key, _combiners);
	filestream_delete(shaders);
	filestream_delete(keys);
	filestream_delete(journal);
}

/*---------------The file-------------*/

/* The file this session reads and appends to: what its header is, the
 * keys it holds and those of them whose programs it holds, lowest first */
static struct
{
	char path[PLUGIN_PATH_SIZE];
	char header[1024];
	size_t headerLen;
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

/* The header: what the programs are for, into s_store; 0 long when it
 * does not fit */
static void make_header(void)
{
	static const u32 keysVersion = KEYS_FORMAT_VERSION, formatVersion = FORMAT_VERSION;
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
	s_store.headerLen = 0;
	if (sizeof(s_store.header) < 4 + 5 * sizeof(u32) + rendererLen + versionLen)
		return;
	put(s_store.header, &len, CACHE_MAGIC, 4);
	put(s_store.header, &len, &keysVersion, sizeof(keysVersion));
	put(s_store.header, &len, &formatVersion, sizeof(formatVersion));
	put(s_store.header, &len, &optionsBits, sizeof(optionsBits));
	put(s_store.header, &len, &rendererLen, sizeof(rendererLen));
	put(s_store.header, &len, renderer, rendererLen);
	put(s_store.header, &len, &versionLen, sizeof(versionLen));
	put(s_store.header, &len, version, versionLen);
	s_store.headerLen = len;
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
 * end. Its programs go to GL first when @_programs; the keys of the rest
 * are compiled after, so a key met before its program was stored is not
 * built twice. @_stale set where GL would not take a program: its key's
 * new one is stored, so the old must go. */
static size_t read_records(const char *_file, size_t _size, size_t _at, bool _programs,
	const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram, gli_combiner_table *_combiners,
	bool *_stale)
{
	reader in;
	size_t end = _at;
	u32 room = 0, keyRoom = 0;
	int pass;

	for (pass = 0; pass < 2; ++pass) {
		in.p = _file + _at;
		in.end = _file + (pass == 0 ? _size : end);
		for (;;) {
			const char *kind = take(&in, 2);
			u64 key;
			gli_cprog *p = NULL;
			if (kind == NULL || !get(&in, &key, sizeof(key)))
				break;
			if (kind[0] == RECORD_PROGRAM) {
				int cut = 0;
				if (pass == 0 && _programs && gli_combiners_find(_combiners, key) == NULL) {
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

bool gli_glsl_load_storage(const gli_gl_info *_info, struct gli_gl_cache1 *_useProgram,
	gli_combiner_table *_combiners)
{
	RFILE *f;
	const char *file = NULL;
	char *copy = NULL;
	int64_t size = 0;
	size_t end = 0;
	bool same = false, stale = false;

	forget();
	storage_path(_info, "cache", s_store.path, sizeof(s_store.path));
	make_header();

	/* one open; the file lent where it can be mapped, read once where not */
	f = filestream_open(s_store.path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
	if (f != NULL) {
		file = (const char *)filestream_get_mapped_ptr(f, &size);
		if (file == NULL && (size = filestream_get_size(f)) > 0 &&
				(copy = (char *)malloc((size_t)size)) != NULL && filestream_read(f, copy, size) == size)
			file = copy;
		if (file != NULL) {
			reader in;
			const char *magic;
			u32 keysVersion = 0, len;
			in.p = file;
			in.end = file + size;
			same = s_store.headerLen != 0 && (size_t)size >= s_store.headerLen &&
				memcmp(file, s_store.header, s_store.headerLen) == 0;
			/* the keys hold for other programs, while their meaning does */
			if ((magic = take(&in, 4)) != NULL && memcmp(magic, CACHE_MAGIC, 4) == 0 &&
					get(&in, &keysVersion, sizeof(keysVersion)) && keysVersion == KEYS_FORMAT_VERSION &&
					take(&in, 2 * sizeof(u32)) != NULL &&
					get(&in, &len, sizeof(len)) && take(&in, len) != NULL &&
					get(&in, &len, sizeof(len)) && take(&in, len) != NULL)
				end = read_records(file, (size_t)size, (size_t)(in.p - file),
					same && gli_ctx.shaderProgramBinary, _info, _useProgram, _combiners, &stale);
		}
		free(copy);
		filestream_close(f);
	}

	if (end == 0)
		old_files(_info, _useProgram, _combiners);
	s_store.again = end != 0 && (!same || stale);
	if (end == 0) {
		/* not there, of before, or no header whole: written now, for the
		 * journal to append to */
		u32 count;
		gli_cprog **programs = sorted_programs(_combiners, &count);
		if (programs != NULL || _combiners->count == 0)
			rewrite(programs, count);
		free(programs);
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
		if (room != 0 && (out = (char *)malloc(room)) != NULL)
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
