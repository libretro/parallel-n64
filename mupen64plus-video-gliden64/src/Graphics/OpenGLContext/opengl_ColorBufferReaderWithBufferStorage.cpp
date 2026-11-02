#include <algorithm>
#include <Config.h>
#include <Graphics/Context.h>
#include "opengl_ColorBufferReaderWithBufferStorage.h"

using namespace graphics;
using namespace opengl;

ColorBufferReaderWithBufferStorage::ColorBufferReaderWithBufferStorage(CachedTexture * _pTexture,
	CachedBindBuffer * _bindBuffer)
	: ColorBufferReader(_pTexture), m_bindBuffer(_bindBuffer)
{
	_initBuffers();
}

ColorBufferReaderWithBufferStorage::~ColorBufferReaderWithBufferStorage()
{
	_destroyBuffers();
}

void ColorBufferReaderWithBufferStorage::_initBuffers()
{
	m_numPBO = std::max(1u, config.frameBufferEmulation.copyToRDRAM);
	if (m_numPBO > _maxPBO)
		m_numPBO = _maxPBO;

	// Generate Pixel Buffer Objects
	glGenBuffers(m_numPBO, m_PBO);

	// Initialize Pixel Buffer Objects
	for (u32 index = 0; index < m_numPBO; ++index) {
		m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[index]));
		m_fence[index] = 0;
		glBufferStorage(GL_PIXEL_PACK_BUFFER, m_pTexture->textureBytes, nullptr, GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
		m_PBOData[index] = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_pTexture->textureBytes, GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	}

	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle::null);
}

void ColorBufferReaderWithBufferStorage::_destroyBuffers()
{
	glDeleteBuffers(m_numPBO, m_PBO);

	for (u32 index = 0; index < m_numPBO; ++index) {
		m_PBO[index] = 0;
		glDeleteSync(m_fence[index]);
	}
}

const u8 * ColorBufferReaderWithBufferStorage::_readPixels(const ReadColorBufferParams& _params, u32& _heightOffset,
	u32& _stride)
{
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle(m_PBO[0]));
	glReadPixels(_params.x0, _params.y0, m_pTexture->width, _params.height,
		GLenum(_params.colorFormat), GLenum(_params.colorType), 0);
	if (m_fence[0] != 0)
		glDeleteSync(m_fence[0]);
	m_fence[0] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	slotReady(0, true);
	_heightOffset = 0;
	_stride = m_pTexture->width;
	return reinterpret_cast<u8*>(m_PBOData[0]);
}

void ColorBufferReaderWithBufferStorage::readToSlot(u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size)
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

bool ColorBufferReaderWithBufferStorage::slotReady(u32 _slot, bool _wait)
{
	if (m_fence[_slot] == 0)
		return true;
	/* the poll flushes, so the fence is bound to signal; a wait has no
	 * limit, as the rows are not valid before it does */
	GLenum res = glClientWaitSync(m_fence[_slot], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
	while (_wait && res == GL_TIMEOUT_EXPIRED)
		res = glClientWaitSync(m_fence[_slot], 0, 1000000000);
	if (res == GL_TIMEOUT_EXPIRED)
		return false;
	glDeleteSync(m_fence[_slot]);
	m_fence[_slot] = 0;
	return true;
}

const u8 * ColorBufferReaderWithBufferStorage::_mapSlot(u32 _slot, u32, u32, u32 & _stride)
{
	_stride = m_pTexture->width;
	return reinterpret_cast<const u8*>(m_PBOData[_slot]);
}

void ColorBufferReaderWithBufferStorage::cleanUp()
{
	m_bindBuffer->bind(Parameter(GL_PIXEL_PACK_BUFFER), ObjectHandle::null);
}
