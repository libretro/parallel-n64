#include <algorithm>
#include <fstream>
#include <assert.h>
#include <string.h>
#include <Combiner.h>
#include <Graphics/OpenGLContext/opengl_CachedFunctions.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include "glsl_Utils.h"
#include "glsl_CombinerProgramImpl.h"

using namespace glsl;

CombinerProgramImpl::CombinerProgramImpl(const CombinerKey & _key,
	GLuint _program,
	opengl::CachedUseProgram * _useProgram,
	const CombinerInputs & _inputs,
	CombinerUniforms && _uniforms)
: graphics::CombinerProgram(&graphics::CombinerProgramOps<CombinerProgramImpl>::table, _key.getMux())
, m_bNeedUpdate(true)
, m_key(_key)
, m_program(_program)
, m_useProgram(_useProgram)
, m_inputs(_inputs)
, m_uniforms(std::move(_uniforms))
{
}


CombinerProgramImpl::~CombinerProgramImpl()
{
	m_useProgram->useProgram(graphics::ObjectHandle::null);
	glDeleteProgram(GLuint(m_program));
}

void CombinerProgramImpl::activate()
{
	m_useProgram->useProgram(m_program);
}

void CombinerProgramImpl::update(bool _force)
{
	_force |= m_bNeedUpdate;
	m_bNeedUpdate = false;
	m_useProgram->useProgram(m_program);
	m_uniforms.update(_force);
}

const CombinerKey & CombinerProgramImpl::getKey() const
{
	return m_key;
}

bool CombinerProgramImpl::usesTexture() const
{
	return m_inputs.usesTexture();
}

bool CombinerProgramImpl::usesTile(u32 _t) const
{
	return m_inputs.usesTile(_t);
}

bool CombinerProgramImpl::usesShade() const
{
	return m_inputs.usesShade();
}

bool CombinerProgramImpl::usesLOD() const
{
	return m_inputs.usesLOD();
}

bool CombinerProgramImpl::usesHwLighting() const
{
	return m_inputs.usesHwLighting();
}

bool CombinerProgramImpl::getBinaryForm(std::vector<char> & _buffer)
{
	/* key, inputs, binary format and length, then the binary, appended to
	 * @_buffer: GL writes the binary where it is kept */
	const size_t head = sizeof(u64) + sizeof(int) + sizeof(GLenum) + sizeof(GLint);
	const size_t at = _buffer.size();
	GLint  binaryLength;
	GLenum binaryFormat;
	glGetProgramiv(GLuint(m_program), GL_PROGRAM_BINARY_LENGTH, &binaryLength);

	if (binaryLength < 1)
		return false;

	_buffer.resize(at + head + binaryLength);
	glGetProgramBinary(GLuint(m_program), binaryLength, &binaryLength, &binaryFormat, _buffer.data() + at + head);
	if (opengl::Utils::isGLError()) {
		_buffer.resize(at);
		return false;
	}
	_buffer.resize(at + head + binaryLength);

	{
		const u64 key = m_key.getMux();
		const int inputs(m_inputs);
		char * out = _buffer.data() + at;
		memcpy(out, &key, sizeof(key));
		out += sizeof(key);
		memcpy(out, &inputs, sizeof(inputs));
		out += sizeof(inputs);
		memcpy(out, &binaryFormat, sizeof(binaryFormat));
		out += sizeof(binaryFormat);
		memcpy(out, &binaryLength, sizeof(binaryLength));
	}

	return true;
}
