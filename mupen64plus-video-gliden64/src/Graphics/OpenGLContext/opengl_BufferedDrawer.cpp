#include <stdlib.h>
#include <Config.h>
#include <CRC.h>
#include "GLFunctions.h"
#include "opengl_Attributes.h"
#include "opengl_BufferedDrawer.h"

using namespace graphics;
using namespace opengl;

const u32 BufferedDrawer::m_bufMaxSize = 8 * 1024 * 1024; // 8 MB
/* a segment still holds the largest vertex batch (VERTBUFF_SIZE) */
const u32 BufferedDrawer::m_bufMinSize = 48 * 1024;

/* GLIDEN64_RING_BYTES shrinks the rings so a short test goes round them
 * many times: CI draws the same frames with a small ring as with the
 * full one, which every draw placed at a segment start or back at 0
 * must keep. */
u32 BufferedDrawer::_ringSize()
{
	const char * env = getenv("GLIDEN64_RING_BYTES");
	if (env == nullptr || *env == '\0')
		return m_bufMaxSize;
	const unsigned long bytes = strtoul(env, nullptr, 0);
	if (bytes < m_bufMinSize)
		return m_bufMinSize;
	return bytes > m_bufMaxSize ? m_bufMaxSize : u32(bytes);
}
#ifndef GL_DEBUG
const GLbitfield BufferedDrawer::m_bufAccessBits = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
const GLbitfield BufferedDrawer::m_bufMapBits = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
#else
const GLbitfield BufferedDrawer::m_bufAccessBits = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
const GLbitfield BufferedDrawer::m_bufMapBits = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;
#endif

BufferedDrawer::BufferedDrawer(const GLInfo & _glinfo, CachedVertexAttribArray * _cachedAttribArray, CachedBindBuffer * _bindBuffer)
: m_glInfo(_glinfo)
, m_cachedAttribArray(_cachedAttribArray)
, m_bindBuffer(_bindBuffer)
{
	m_vertices.resize(VERTBUFF_SIZE);
	const u32 ringSize = _ringSize();
	/* Init buffers for rects */
	glGenVertexArrays(1, &m_rectsBuffers.vao);
	glBindVertexArray(m_rectsBuffers.vao);
	_initBuffer(m_rectsBuffers.vbo, ringSize, sizeof(RectVertex));
	m_cachedAttribArray->enableVertexAttribArray(rectAttrib::position, true);
	m_cachedAttribArray->enableVertexAttribArray(rectAttrib::texcoord0, true);
	m_cachedAttribArray->enableVertexAttribArray(rectAttrib::texcoord1, true);
	glVertexAttribPointer(rectAttrib::position, 4, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (const GLvoid *)(offsetof(RectVertex, x)));
	glVertexAttribPointer(rectAttrib::texcoord0, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (const GLvoid *)(offsetof(RectVertex, s0)));
	glVertexAttribPointer(rectAttrib::texcoord1, 2, GL_FLOAT, GL_FALSE, sizeof(RectVertex), (const GLvoid *)(offsetof(RectVertex, s1)));

	/* Init buffers for triangles */
	glGenVertexArrays(1, &m_trisBuffers.vao);
	glBindVertexArray(m_trisBuffers.vao);
	_initBuffer(m_trisBuffers.vbo, ringSize, sizeof(Vertex));
	_initBuffer(m_trisBuffers.ebo, ringSize, sizeof(GLushort));
	m_cachedAttribArray->enableVertexAttribArray(triangleAttrib::position, true);
	m_cachedAttribArray->enableVertexAttribArray(triangleAttrib::color, true);
	m_cachedAttribArray->enableVertexAttribArray(triangleAttrib::texcoord, true);
	m_cachedAttribArray->enableVertexAttribArray(triangleAttrib::modify, true);
	m_cachedAttribArray->enableVertexAttribArray(triangleAttrib::numlights, false);
	glVertexAttribPointer(triangleAttrib::position, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid *)(offsetof(Vertex, x)));
	glVertexAttribPointer(triangleAttrib::color, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid *)(offsetof(Vertex, r)));
	glVertexAttribPointer(triangleAttrib::texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid *)(offsetof(Vertex, s)));
	glVertexAttribPointer(triangleAttrib::modify, 4, GL_BYTE, GL_TRUE, sizeof(Vertex), (const GLvoid *)(offsetof(Vertex, modify)));
}

void BufferedDrawer::_initBuffer(Buffer & _buffer, GLuint _bufSize, GLuint _stride)
{
	_buffer.ring.init(_bufSize, _stride);
	_buffer.size = _buffer.ring.size();
	glGenBuffers(1, &_buffer.handle);
	m_bindBuffer->bind(Parameter(_buffer.type), ObjectHandle(_buffer.handle));
	if (m_glInfo.bufferStorage) {
		glBufferStorage(_buffer.type, _bufSize, nullptr, m_bufAccessBits);
		_buffer.data = (GLubyte*)glMapBufferRange(_buffer.type, 0, _bufSize, m_bufMapBits);
	} else {
		glBufferData(_buffer.type, _bufSize, nullptr, GL_DYNAMIC_DRAW);
	}
}

BufferedDrawer::~BufferedDrawer()
{
	m_bindBuffer->bind(Parameter(GL_ARRAY_BUFFER), ObjectHandle::null);
	m_bindBuffer->bind(Parameter(GL_ELEMENT_ARRAY_BUFFER), ObjectHandle::null);
	Buffer * all[3] = { &m_rectsBuffers.vbo, &m_trisBuffers.vbo, &m_trisBuffers.ebo };
	for (Buffer * b : all)
		for (GLsync & fence : b->fences)
			if (fence != 0)
				glDeleteSync(fence);
	GLuint buffers[3] = { m_rectsBuffers.vbo.handle, m_trisBuffers.vbo.handle, m_trisBuffers.ebo.handle };
	glDeleteBuffers(3, buffers);
	glBindVertexArray(0);
	GLuint arrays[2] = { m_rectsBuffers.vao, m_trisBuffers.vao };
	glDeleteVertexArrays(2, arrays);
}

namespace {
	/* the draws so far, marked when the writes leave a segment */
	struct SegmentFences
	{
		GLsync * fences;
		void fence(u32 _seg)
		{
			if (fences[_seg] != 0)
				glDeleteSync(fences[_seg]);
			fences[_seg] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		}
		void wait(u32 _seg)
		{
			if (fences[_seg] == 0)
				return;
			GLenum res = glClientWaitSync(fences[_seg], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
			while (res == GL_TIMEOUT_EXPIRED)
				res = glClientWaitSync(fences[_seg], 0, 1000000000);
			glDeleteSync(fences[_seg]);
			fences[_seg] = 0;
		}
	};
}

GLubyte * BufferedDrawer::_reserve(Buffer & _buffer, u32 _dataSize)
{
	SegmentFences fences = { _buffer.fences };
	const u32 offset = _buffer.ring.place(u32(_buffer.offset), _dataSize, fences);
	if (offset != u32(_buffer.offset)) {
		/* a segment start, or 0: the draws index from there */
		_buffer.offset = offset;
		_buffer.pos = GLint(_buffer.ring.element(offset));
	}
	return m_glInfo.bufferStorage ? &_buffer.data[_buffer.offset] : nullptr;
}

void BufferedDrawer::_commit(Buffer & _buffer, u32 _count, u32 _dataSize)
{
#ifdef GL_DEBUG
	if (m_glInfo.bufferStorage) {
		m_bindBuffer->bind(Parameter(_buffer.type), ObjectHandle(_buffer.handle));
		glFlushMappedBufferRange(_buffer.type, _buffer.offset, _dataSize);
	}
#endif
	_buffer.offset += _dataSize;
	_buffer.pos += _count;
}

void BufferedDrawer::_updateBuffer(Buffer & _buffer, u32 _count, u32 _dataSize, const void * _data)
{
	GLubyte * dst = _reserve(_buffer, _dataSize);
	if (dst != nullptr) {
		memcpy(dst, _data, _dataSize);
	} else {
		/* unsynchronized: the segment fences keep the GPU's data apart */
		m_bindBuffer->bind(Parameter(_buffer.type), ObjectHandle(_buffer.handle));
		void* buffer_pointer = glMapBufferRange(_buffer.type, _buffer.offset, _dataSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		memcpy(buffer_pointer, _data, _dataSize);
		glUnmapBuffer(_buffer.type);
	}
	_commit(_buffer, _count, _dataSize);
}

void BufferedDrawer::_updateRectBuffer(const graphics::Context::DrawRectParameters & _params)
{
	const BuffersType type = BuffersType::rects;
	if (m_type != type) {
		glBindVertexArray(m_rectsBuffers.vao);
		m_type = type;
	}

	Buffer & buffer = m_rectsBuffers.vbo;
	const u32 dataSize = _params.verticesCount * static_cast<u32>(sizeof(RectVertex));

	if (m_glInfo.bufferStorage) {
		_updateBuffer(buffer, _params.verticesCount, dataSize, _params.vertices);
		return;
	}

	const u64 crc = CRC_Calculate(UINT64_MAX, _params.vertices, dataSize);
	auto iter = m_rectBufferOffsets.find(crc);
	if (iter != m_rectBufferOffsets.end()) {
		buffer.pos = iter->second;
		return;
	}

	const GLintptr prevOffset = buffer.offset;
	_updateBuffer(buffer, _params.verticesCount, dataSize, _params.vertices);
	if (buffer.offset < prevOffset)
		m_rectBufferOffsets.clear();

	buffer.pos = static_cast<GLint>(buffer.offset / sizeof(RectVertex));
	m_rectBufferOffsets[crc] = buffer.pos;
}


void BufferedDrawer::drawRects(const graphics::Context::DrawRectParameters & _params)
{
	_updateRectBuffer(_params);

	m_cachedAttribArray->enableVertexAttribArray(rectAttrib::texcoord0, _params.texrect);
	m_cachedAttribArray->enableVertexAttribArray(rectAttrib::texcoord1, _params.texrect);

	glDrawArrays(GLenum(_params.mode), m_rectsBuffers.vbo.pos - _params.verticesCount, _params.verticesCount);
}

void BufferedDrawer::_convertFromSPVertex(Vertex * _dst, bool _flatColors, u32 _count, const SPVertex * _data)
{
	for (u32 i = 0; i < _count; ++i) {
		const SPVertex & src = _data[i];
		Vertex & dst = _dst[i];
		dst.x = src.x;
		dst.y = src.y;
		dst.z = src.z;
		dst.w = src.w;
		if (_flatColors) {
			dst.r = src.flat_r;
			dst.g = src.flat_g;
			dst.b = src.flat_b;
			dst.a = src.flat_a;
		} else {
			dst.r = src.r;
			dst.g = src.g;
			dst.b = src.b;
			dst.a = src.a;
		}
		dst.s = src.s;
		dst.t = src.t;
		dst.modify = src.modify;
	}
}

/* Converted straight into the mapped ring where there is one. */
void BufferedDrawer::_updateVertices(bool _flatColors, u32 _count, const SPVertex * _data)
{
	Buffer & vboBuffer = m_trisBuffers.vbo;
	const u32 vboDataSize = _count * static_cast<u32>(sizeof(Vertex));
	GLubyte * dst = _reserve(vboBuffer, vboDataSize);
	if (dst != nullptr) {
		_convertFromSPVertex(reinterpret_cast<Vertex*>(dst), _flatColors, _count, _data);
		_commit(vboBuffer, _count, vboDataSize);
		return;
	}
	if (_count > m_vertices.size())
		m_vertices.resize(_count);
	_convertFromSPVertex(m_vertices.data(), _flatColors, _count, _data);
	_updateBuffer(vboBuffer, _count, vboDataSize, m_vertices.data());
}

void BufferedDrawer::_updateTrianglesBuffers(const graphics::Context::DrawTriangleParameters & _params)
{
	const BuffersType type = BuffersType::triangles;

	if (m_type != type) {
		glBindVertexArray(m_trisBuffers.vao);
		m_type = type;
	}

	_updateVertices(_params.flatColors, _params.verticesCount, _params.vertices);

	if (_params.elements == nullptr)
		return;

	const u32 eboDataSize = static_cast<u32>(sizeof(GLushort)) * _params.elementsCount;
	Buffer & eboBuffer = m_trisBuffers.ebo;
	_updateBuffer(eboBuffer, _params.elementsCount, eboDataSize, _params.elements);
}

void BufferedDrawer::drawTriangles(const graphics::Context::DrawTriangleParameters & _params)
{
	_updateTrianglesBuffers(_params);

	if (gli_isHWLightingAllowed())
		glVertexAttrib1f(triangleAttrib::numlights, GLfloat(_params.vertices[0].HWLight));

	if (_params.elements == nullptr) {
		glDrawArrays(GLenum(_params.mode), m_trisBuffers.vbo.pos - _params.verticesCount, _params.verticesCount);
		return;
	}

	glDrawRangeElementsBaseVertex(GLenum(_params.mode), 0, _params.verticesCount - 1, _params.elementsCount, GL_UNSIGNED_SHORT,
		(u16*)nullptr + m_trisBuffers.ebo.pos - _params.elementsCount, m_trisBuffers.vbo.pos - _params.verticesCount);
}

void BufferedDrawer::drawLine(f32 _width, SPVertex * _vertices)
{
	const BuffersType type = BuffersType::triangles;

	if (m_type != type) {
		glBindVertexArray(m_trisBuffers.vao);
		m_type = type;
	}

	_updateVertices(false, 2, _vertices);

	glLineWidth(_width);
	glDrawArrays(GL_LINES, m_trisBuffers.vbo.pos - 2, 2);
}
