#include <algorithm>
#include <Config.h>
#include <Graphics/Context.h>
#include "opengl_ColorBufferReaderWithPixelBuffer.h"

using namespace graphics;
using namespace opengl;

ColorBufferReaderWithPixelBuffer::ColorBufferReaderWithPixelBuffer(CachedTexture *_pTexture,
																   CachedBindBuffer *_bindBuffer)
	: ColorBufferReader(_pTexture), m_bindBuffer(_bindBuffer)
{
	_initBuffers();
}


ColorBufferReaderWithPixelBuffer::~ColorBufferReaderWithPixelBuffer()
{
	_destroyBuffers();
}

void ColorBufferReaderWithPixelBuffer::_destroyBuffers()
{
	glDeleteBuffers(m_numPBO, m_PBO);

	for (u32 index = 0; index < m_numPBO; ++index) {
		m_PBO[index] = 0;
		if (m_fence[index] != 0)
			glDeleteSync(m_fence[index]);
		m_fence[index] = 0;
	}
}

void ColorBufferReaderWithPixelBuffer::_initBuffers()
{
	m_numPBO = std::max(1u, config.frameBufferEmulation.copyToRDRAM);
	if (m_numPBO > _maxPBO)
		m_numPBO = _maxPBO;

	// Generate Pixel Buffer Objects
	glGenBuffers(m_numPBO, m_PBO);
	m_mapped = false;

	// Initialize Pixel Buffer Objects
	for (u32 i = 0; i < m_numPBO; ++i) {
		m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[i]));
		glBufferData(GL_PIXEL_PACK_BUFFER, m_pTexture->textureBytes, nullptr, GL_DYNAMIC_READ);
		m_fence[i] = 0;
	}
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle::null);
}

const u8 * ColorBufferReaderWithPixelBuffer::_readPixels(const ReadColorBufferParams& _params, u32& _heightOffset,
	u32& _stride)
{
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[0]));
	glReadPixels(_params.x0, _params.y0, m_pTexture->width, _params.height,
		GLenum(_params.colorFormat), GLenum(_params.colorType), 0);
	_heightOffset = 0;
	_stride = m_pTexture->width;
	m_mapped = true;
	return reinterpret_cast<u8*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
		m_pTexture->width * _params.height * _params.colorFormatBytes, GL_MAP_READ_BIT));
}

void ColorBufferReaderWithPixelBuffer::readToSlot(u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size)
{
	ColorFormatParam format;
	DatatypeParam type;
	u32 bytes;
	_slotFormat(_size, format, type, bytes);

	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[_slot]));
	glReadPixels(_x0, _y0, m_pTexture->width, _height, GLenum(format), GLenum(type), 0);
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle::null);
	if (m_fence[_slot] != 0)
		glDeleteSync(m_fence[_slot]);
	m_fence[_slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

bool ColorBufferReaderWithPixelBuffer::slotReady(u32 _slot, bool _wait)
{
	if (m_fence[_slot] == 0)
		return true;
	/* mapping before the fence signals would stall in the driver */
	GLenum res = glClientWaitSync(m_fence[_slot], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
	while (_wait && res == GL_TIMEOUT_EXPIRED)
		res = glClientWaitSync(m_fence[_slot], 0, 1000000000);
	if (res == GL_TIMEOUT_EXPIRED)
		return false;
	glDeleteSync(m_fence[_slot]);
	m_fence[_slot] = 0;
	return true;
}

const u8 * ColorBufferReaderWithPixelBuffer::_mapSlot(u32 _slot, u32 _height, u32 _bytes, u32 & _stride)
{
	_stride = m_pTexture->width;
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[_slot]));
	m_mapped = true;
	return reinterpret_cast<const u8*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
		m_pTexture->width * _height * _bytes, GL_MAP_READ_BIT));
}

void ColorBufferReaderWithPixelBuffer::cleanUp()
{
	if (m_mapped)
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	m_mapped = false;
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle::null);
}
