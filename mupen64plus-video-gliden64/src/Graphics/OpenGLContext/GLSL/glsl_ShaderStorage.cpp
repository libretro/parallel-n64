#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <vector>

#include <Graphics/CombinerProgram.h>
#include <Graphics/Context.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include <Types.h>
#include <RSP.h>
#include <PluginAPI.h>
#include <Combiner.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include "glsl_Utils.h"
#include "glsl_ShaderStorage.h"
#include "glsl_CombinerProgramImpl.h"
#include "glsl_CombinerProgramUniformFactory.h"

using namespace glsl;

#define SHADER_STORAGE_FOLDER_NAME "shaders"

/* _value in lower case hex, zero padded to _digits */
static void _appendHex(std::string & _s, u64 _value, int _digits)
{
	static const char digits[] = "0123456789abcdef";
	char buf[16];
	int n = 0;
	do {
		buf[n++] = digits[_value & 0xF];
		_value >>= 4;
	} while (_value != 0 && n < 16);
	for (int i = n; i < _digits; ++i)
		_s += '0';
	while (n > 0)
		_s += buf[--n];
}

/* one line of the keys file */
static void _appendKeysLine(std::string & _s, u64 _value, int _digits)
{
	_s += "0x";
	_appendHex(_s, _value, _digits);
	_s += '\n';
}

/* One hex number, with or without 0x, after any white space */
static bool _parseHex(const char *& _p, const char * _end, u64 & _value)
{
	while (_p < _end && (*_p == ' ' || *_p == '\t' || *_p == '\r' || *_p == '\n'))
		++_p;
	if (_end - _p >= 2 && _p[0] == '0' && (_p[1] == 'x' || _p[1] == 'X'))
		_p += 2;
	const char * start = _p;
	_value = 0;
	for (; _p < _end; ++_p) {
		const char c = *_p;
		u32 d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		_value = (_value << 4) | d;
	}
	return _p != start;
}

/* Reads from a file held in memory; a short read fails and stays failed */
struct StorageReader
{
	const char * p;
	const char * end;
	bool ok;

	StorageReader(const void * _data, size_t _size)
		: p(static_cast<const char *>(_data)), end(p + _size), ok(true) {}

	const char * take(size_t _size)
	{
		if (!ok || static_cast<size_t>(end - p) < _size) {
			ok = false;
			return nullptr;
		}
		const char * at = p;
		p += _size;
		return at;
	}

	template <typename T>
	bool get(T & _value)
	{
		const char * at = take(sizeof(T));
		if (at != nullptr)
			memcpy(&_value, at, sizeof(T));
		return at != nullptr;
	}
};

static
std::string getStorageFileName(const opengl::GLInfo & _glinfo, const char * _fileExtension)
{
	char strCacheFolderPath[PLUGIN_PATH_SIZE];
	gli_api_user_cache_path(strCacheFolderPath, sizeof(strCacheFolderPath));

	std::string path(strCacheFolderPath);
	path += "/" SHADER_STORAGE_FOLDER_NAME;
	if (!path_mkdir(path.c_str()))
		path = strCacheFolderPath;

	path += "/GLideN64.";
	_appendHex(path, static_cast<u32>(std::hash<std::string>()(RSP.romname)), 0);
	path += _glinfo.isGLESX ? ".GLES." : ".OpenGL.";
	path += _fileExtension;
	return path;
}

/*
Storage has text format:
line_1 Version in hex form
line_2 Hardware per pixel lighting support flag
line_3 Count - numbers of combiners keys in hex form
line_4..line_Count+3  combiners keys in hex form, one key per line
*/
bool ShaderStorage::_saveCombinerKeys(const graphics::Combiners & _combiners) const
{
	const std::string keysFileName = getStorageFileName(m_glinfo, "keys");

	std::vector<u64> keysData;
	keysData.reserve(_combiners.size());
	for (auto cur = _combiners.begin(); cur != _combiners.end(); ++cur)
		keysData.push_back(cur->first.getMux());
	std::sort(keysData.begin(), keysData.end());

	std::string keysOut;
	keysOut.reserve(3 * 11 + keysData.size() * 19);
	_appendKeysLine(keysOut, m_keysFormatVersion, 8);
	_appendKeysLine(keysOut, static_cast<u32>(gli_GBI_hwlSupported), 8);
	_appendKeysLine(keysOut, keysData.size(), 8);
	for (u64 key : keysData)
		_appendKeysLine(keysOut, key, 16);

	return filestream_write_file(keysFileName.c_str(), keysOut.data(), static_cast<int64_t>(keysOut.size()));
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
bool ShaderStorage::saveShadersStorage(const graphics::Combiners & _combiners) const
{
	if (!_saveCombinerKeys(_combiners))
		return false;

	if (gfxContext.isCombinerProgramBuilderObsolete())
		// Created shaders are obsolete due to changes in config, but we saved combiners keys.
		return true;

	if (!graphics::Context::ShaderProgramBinary)
		// Shaders storage is not supported, but we saved combiners keys.
		return true;

	if (glGetString(GL_RENDERER) == nullptr)
		// No current context any more; but we saved combiners keys.
		return true;

	const std::string shadersFileName = getStorageFileName(m_glinfo, "shaders");

	/* the whole file is built here and written in one go */
	std::vector<char> shadersOut;
	auto put = [&shadersOut](const void * _data, size_t _size) {
		const char * data = static_cast<const char *>(_data);
		shadersOut.insert(shadersOut.end(), data, data + _size);
	};

	put(&m_formatVersion, sizeof(m_formatVersion));

	const u32 configOptionsBitSet = graphics::CombinerProgram::getShaderCombinerOptionsBits();
	put(&configOptionsBitSet, sizeof(configOptionsBitSet));

	const char * strRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	u32 len = static_cast<u32>(strlen(strRenderer));
	put(&len, sizeof(len));
	put(strRenderer, len);

	const char * strGLVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	len = static_cast<u32>(strlen(strGLVersion));
	put(&len, sizeof(len));
	put(strGLVersion, len);

	/* the count goes in front of the shaders it counts */
	const size_t countAt = shadersOut.size();
	u32 totalWritten = 0;
	put(&totalWritten, sizeof(totalWritten));

	std::vector<char> data;
	for (auto cur = _combiners.begin(); cur != _combiners.end(); ++cur)
	{
		if (cur->second->getBinaryForm(data))
		{
			put(data.data(), data.size());
			++totalWritten;
		}
	}
	memcpy(shadersOut.data() + countAt, &totalWritten, sizeof(totalWritten));

	return filestream_write_file(shadersFileName.c_str(), shadersOut.data(), static_cast<int64_t>(shadersOut.size()));
}

static
CombinerProgramImpl * _readCombinerProgram(StorageReader & _in,
	CombinerKey & _cmbKey,
	CombinerProgramUniformFactory & _uniformFactory,
	opengl::CachedUseProgram * _useProgram)
{
	int inputs = 0;
	GLenum binaryFormat = 0;
	GLint  binaryLength = 0;
	_in.get(inputs);
	_in.get(binaryFormat);
	_in.get(binaryLength);
	const char * binary = binaryLength > 0 ? _in.take(static_cast<size_t>(binaryLength)) : nullptr;
	if (binary == nullptr) {
		_in.ok = false;
		return nullptr;
	}
	CombinerInputs cmbInputs(inputs);

	GLuint program = glCreateProgram();
	const bool isRect = _cmbKey.isRectKey();
	glsl::Utils::locateAttributes(program, isRect, cmbInputs.usesTexture());
	glProgramBinary(program, binaryFormat, binary, binaryLength);
	if (!glsl::Utils::checkProgramLinkStatus(program, true)) {
		glDeleteProgram(program);
		return nullptr;
	}

	CombinerUniforms uniforms;
	_uniformFactory.buildUniforms(program, cmbInputs, _cmbKey, uniforms);

	return new CombinerProgramImpl(_cmbKey, program, _useProgram, cmbInputs, std::move(uniforms));
}

bool ShaderStorage::_loadFromCombinerKeys(graphics::Combiners & _combiners)
{
	const std::string keysFileName = getStorageFileName(m_glinfo, "keys");
	void * file = nullptr;
	int64_t size = 0;
	if (!filestream_read_file(keysFileName.c_str(), &file, &size)) {
		free(file);
		return false;
	}

	const char * p = static_cast<const char *>(file);
	const char * end = p + size;
	u64 version, hwlSupport, szCombiners, key;
	if (!_parseHex(p, end, version) || version != m_keysFormatVersion ||
			!_parseHex(p, end, hwlSupport) || !_parseHex(p, end, szCombiners)) {
		free(file);
		return false;
	}
	gli_GBI_hwlSupported = hwlSupport != 0;

	for (u64 i = 0; i < szCombiners && _parseHex(p, end, key); ++i) {
		graphics::CombinerProgram * pCombiner = Combiner_Compile(CombinerKey(key, false));
		pCombiner->update(true);
		_combiners[pCombiner->getKey()] = pCombiner;
	}
	free(file);

	if (opengl::Utils::isGLError())
		return false;

	if (graphics::Context::ShaderProgramBinary)
		// Restore shaders storage
		return saveShadersStorage(_combiners);

	return true;
}

bool ShaderStorage::loadShadersStorage(graphics::Combiners & _combiners)
{
	if (!graphics::Context::ShaderProgramBinary)
		// Shaders storage is not supported, load from combiners keys.
		return _loadFromCombinerKeys(_combiners);

	const std::string shadersFileName = getStorageFileName(m_glinfo, "shaders");
	const u32 configOptionsBitSet = graphics::CombinerProgram::getShaderCombinerOptionsBits();

	void * file = nullptr;
	int64_t size = 0;
	if (!filestream_read_file(shadersFileName.c_str(), &file, &size)) {
		free(file);
		return _loadFromCombinerKeys(_combiners);
	}
	StorageReader in(file, static_cast<size_t>(size));

	/* any mismatch, or a file cut short in its header, falls back to the keys */
	u32 version = 0, optionsSet = 0, len = 0;
	const char * str;
	bool match = in.get(version) && version == m_formatVersion &&
		in.get(optionsSet) && optionsSet == configOptionsBitSet;

	const char * strRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	match = match && in.get(len) && (str = in.take(len)) != nullptr && strncmp(strRenderer, str, len) == 0;

	const char * strGLVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	match = match && in.get(len) && (str = in.take(len)) != nullptr && strncmp(strGLVersion, str, len) == 0;

	if (!match || !in.get(len)) {
		free(file);
		return _loadFromCombinerKeys(_combiners);
	}

	CombinerProgramUniformFactory uniformFactory(m_glinfo);

	for (u32 i = 0; i < len; ++i) {
		u64 mux;
		if (!in.get(mux))
			break;
		CombinerKey cmbKey(mux, false);

		CombinerProgramImpl * pCombiner = _readCombinerProgram(in, cmbKey, uniformFactory, m_useProgram);

		if (pCombiner != nullptr) {
			pCombiner->update(true);
			_combiners[pCombiner->getKey()] = pCombiner;
		} else {
			if (!in.ok)
				break;
			graphics::CombinerProgram * pCombinerFromKey = Combiner_Compile(cmbKey);
			pCombinerFromKey->update(true);
			_combiners[cmbKey] = pCombinerFromKey;
		}
	}

	free(file);
	return !opengl::Utils::isGLError();
}


std::string ShaderStorage::getKeysJournalPath(u32 & _version) const
{
	_version = m_keysFormatVersion;
	return getStorageFileName(m_glinfo, "keys.journal");
}

ShaderStorage::ShaderStorage(const opengl::GLInfo & _glinfo, opengl::CachedUseProgram * _useProgram)
: m_glinfo(_glinfo)
, m_useProgram(_useProgram)
{
}
