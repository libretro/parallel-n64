#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "OpenGL.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "VI.h"
#include "Textures.h"
#include "Combiner.h"
#include "GLSLCombiner.h"
#include "Config.h"
#include "Debug.h"
#include "PostProcessor.h"
#include "FrameBufferInfo.h"

#include "../../Graphics/RDP/gDP_state.h"
#include "../../Graphics/image_convert.h"

#include "m64p_plugin.h"

using namespace std;

#ifndef HAVE_OPENGLES2
class FrameBufferToRDRAM
{
public:
	FrameBufferToRDRAM() :
		m_FBO(0),
      m_pTexture(NULL),
      m_pCurFrameBuffer(NULL),
      m_curIndex(-1),
      m_frameCount(-1)
	{
		m_PBO[0] = m_PBO[1] = m_PBO[2] = 0;
	}

	void Init();
	void Destroy();

   void copyToRDRAM(uint32_t _address, bool _sync);
	void copyChunkToRDRAM(uint32_t _address);

	void CopyToRDRAM(uint32_t _address, bool _sync);

private:
	union RGBA {
		struct {
			uint8_t r, g, b, a;
		};
		uint32_t raw;
	};

   bool _prepareCopy(uint32_t _address);
	void _copy(uint32_t _startAddress, uint32_t _endAddress, bool _sync);

	GLuint m_FBO;
	CachedTexture * m_pTexture;
   FrameBuffer * m_pCurFrameBuffer;
	uint32_t m_curIndex;
   uint32_t m_frameCount;
	GLuint m_PBO[3];
};

class DepthBufferToRDRAM
{
public:
	DepthBufferToRDRAM() :
		m_FBO(0),
      m_PBO(0),
      m_frameCount(-1),
      m_pColorTexture(NULL),
      m_pDepthTexture(NULL),
      m_pCurDepthBuffer(NULL)
	{}

	void Init();
	void Destroy();

   bool copyToRDRAM(uint32_t _address);
	bool copyChunkToRDRAM(uint32_t _address);

private:
   bool _prepareCopy(uint32_t _address);
	bool _copy(uint32_t _startAddress, uint32_t _endAddress);

	// Convert pixel from video memory to N64 depth buffer format.
	static uint16_t _FloatToUInt16(float _z);

	GLuint m_FBO;
	GLuint m_PBO;
   uint32_t m_frameCount;
	CachedTexture * m_pColorTexture;
	CachedTexture * m_pDepthTexture;
   DepthBuffer * m_pCurDepthBuffer;
};
#endif // HAVE_OPENGLES2

class RDRAMtoFrameBuffer
{
public:
	RDRAMtoFrameBuffer() : m_pTexture(NULL), m_PBO(0) {}

	void Init();
	void Destroy();

	void CopyFromRDRAM( uint32_t _address, bool _bUseAlpha);

private:
	CachedTexture * m_pTexture;
#ifndef HAVE_OPENGLES2
	GLuint m_PBO;
#else
	GLubyte* m_PBO;
#endif
};

#ifndef HAVE_OPENGLES2
FrameBufferToRDRAM g_fbToRDRAM;
DepthBufferToRDRAM g_dbToRDRAM;
#endif
RDRAMtoFrameBuffer g_RDRAMtoFB;

FrameBuffer::FrameBuffer() :
	m_startAddress(0), m_endAddress(0), m_size(0), m_width(0), m_height(0), m_fillcolor(0), m_validityChecked(0),
	m_scaleX(0), m_scaleY(0),
	m_copiedToRdram(false), m_fingerprint(false), m_cleared(false), m_changed(false), m_cfb(false),
	m_isDepthBuffer(false), m_isPauseScreen(false), m_isOBScreen(false), m_needHeightCorrection(false),
	m_postProcessed(0), m_pLoadTile(NULL),
	m_pDepthBuffer(NULL), m_pResolveTexture(NULL), m_resolveFBO(0), m_resolved(false)
{
	m_pTexture = textureCache().addFrameBufferTexture();
	glGenFramebuffers(1, &m_FBO);
}

FrameBuffer::FrameBuffer(FrameBuffer && _other) :
	m_startAddress(_other.m_startAddress), m_endAddress(_other.m_endAddress),
	m_size(_other.m_size), m_width(_other.m_width), m_height(_other.m_height), m_fillcolor(_other.m_fillcolor),
	m_validityChecked(_other.m_validityChecked), m_scaleX(_other.m_scaleX), m_scaleY(_other.m_scaleY), m_copiedToRdram(_other.m_copiedToRdram), m_fingerprint(_other.m_fingerprint), m_cleared(_other.m_cleared), m_changed(_other.m_changed),
	m_cfb(_other.m_cfb), m_isDepthBuffer(_other.m_isDepthBuffer), m_isPauseScreen(_other.m_isPauseScreen), m_isOBScreen(_other.m_isOBScreen), m_needHeightCorrection(_other.m_needHeightCorrection), m_postProcessed(_other.m_postProcessed),
	m_FBO(_other.m_FBO), m_pLoadTile(_other.m_pLoadTile), m_pTexture(_other.m_pTexture), m_pDepthBuffer(_other.m_pDepthBuffer),
	m_pResolveTexture(_other.m_pResolveTexture), m_resolveFBO(_other.m_resolveFBO), m_resolved(_other.m_resolved), m_RdramCopy(_other.m_RdramCopy)
{
	_other.m_FBO = 0;
	_other.m_pTexture = NULL;
	_other.m_pLoadTile = NULL;
	_other.m_pDepthBuffer = NULL;
	_other.m_pResolveTexture = NULL;
	_other.m_resolveFBO = 0;
	_other.m_RdramCopy.clear();
}


FrameBuffer::~FrameBuffer()
{
	if (m_FBO != 0)
		glDeleteFramebuffers(1, &m_FBO);
	if (m_pTexture != NULL)
		textureCache().removeFrameBufferTexture(m_pTexture);
	if (m_resolveFBO != 0)
		glDeleteFramebuffers(1, &m_resolveFBO);
	if (m_pResolveTexture != NULL)
		textureCache().removeFrameBufferTexture(m_pResolveTexture);
}

void FrameBuffer::_initTexture(uint16_t _format, uint16_t _size, CachedTexture *_pTexture)
{
	_pTexture->width = (uint32_t)(m_width * m_scaleX);
	_pTexture->height = (uint32_t)(m_height * m_scaleY);
	_pTexture->format = _format;
	_pTexture->size = _size;
	_pTexture->clampS = 1;
	_pTexture->clampT = 1;
	_pTexture->address = m_startAddress;
	_pTexture->clampWidth = m_width;
	_pTexture->clampHeight = m_height;
	_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	_pTexture->maskS = 0;
	_pTexture->maskT = 0;
	_pTexture->mirrorS = 0;
	_pTexture->mirrorT = 0;
	_pTexture->realWidth = _pTexture->width;
	_pTexture->realHeight = _pTexture->height;
	_pTexture->textureBytes = _pTexture->realWidth * _pTexture->realHeight;
	if (_size > G_IM_SIZ_8b)
		_pTexture->textureBytes *= fboFormats.colorFormatBytes;
	else
		_pTexture->textureBytes *= fboFormats.monochromeFormatBytes;
	textureCache().addFrameBufferTextureSize(_pTexture->textureBytes);
}

void FrameBuffer::_setAndAttachTexture(uint16_t _size, CachedTexture *_pTexture)
{
	glBindTexture(GL_TEXTURE_2D, _pTexture->glName);
	if (_size > G_IM_SIZ_8b)
		glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.colorInternalFormat, _pTexture->realWidth, _pTexture->realHeight, 0, fboFormats.colorFormat, fboFormats.colorType, NULL);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.monochromeInternalFormat, _pTexture->realWidth, _pTexture->realHeight, 0, fboFormats.monochromeFormat, fboFormats.monochromeType, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _pTexture->glName, 0);
}

bool FrameBuffer::_isMarioTennisScoreboard() const
{
	if ((config.generalEmulation.hacks&hack_scoreboard) != 0) {
		if (VI.PAL)
			return m_startAddress == 0x13b480 || m_startAddress == 0x26a530;
		else
			return m_startAddress == 0x13ba50 || m_startAddress == 0x264430;
	}
	return (config.generalEmulation.hacks&hack_scoreboardJ) != 0 && (m_startAddress == 0x134080 || m_startAddress == 0x1332f8);
}

void FrameBuffer::init(uint32_t _address, uint32_t _endAddress, uint16_t _format, uint16_t _size, uint16_t _width, uint16_t _height, bool _cfb)
{
	OGLVideo & ogl = video();
	m_startAddress = _address;
	m_endAddress = _endAddress;
	m_width = _width;
	m_height = _height;
	m_size = _size;
	if (m_width != VI.width && config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		m_scaleX = 1.0f;
		m_scaleY = 1.0f;
	} else {
		m_scaleX = ogl.getScaleX();
		m_scaleY = ogl.getScaleY();
	}
	m_fillcolor = 0;
	m_cfb = _cfb;
	m_needHeightCorrection = _width != VI.width && _width != *gfx_info.VI_WIDTH_REG;
	m_cleared = false;
	m_fingerprint = false;

	_initTexture(_format, _size, m_pTexture);
	glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

#ifdef GL_MULTISAMPLING_SUPPORT
	if (config.video.multisampling != 0) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_pTexture->glName);
#if defined(HAVE_OPENGLES_3_1)
		if (_size > G_IM_SIZ_8b)
			glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, config.video.multisampling, GL_RGBA8, m_pTexture->realWidth, m_pTexture->realHeight, false);
		else
			glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, config.video.multisampling, fboFormats.monochromeInternalFormat, m_pTexture->realWidth, m_pTexture->realHeight, false);
#else
		if (_size > G_IM_SIZ_8b)
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, config.video.multisampling, GL_RGBA8, m_pTexture->realWidth, m_pTexture->realHeight, false);
		else
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, config.video.multisampling, fboFormats.monochromeInternalFormat, m_pTexture->realWidth, m_pTexture->realHeight, false);
#endif
		m_pTexture->frameBufferTexture = CachedTexture::fbMultiSample;
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_pTexture->glName, 0);

		m_pResolveTexture = textureCache().addFrameBufferTexture();
		_initTexture(_format, _size, m_pResolveTexture);
		glGenFramebuffers(1, &m_resolveFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);
		_setAndAttachTexture(_size, m_pResolveTexture);
		assert(checkFBO());

		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
	} else
#endif // GL_MULTISAMPLING_SUPPORT
		_setAndAttachTexture(_size, m_pTexture);
}

void FrameBuffer::reinit(uint16_t _height)
{
	const uint16_t format = m_pTexture->format;
	const uint32_t endAddress = m_startAddress + ((m_width * _height) << m_size >> 1) - 1;
	if (m_pTexture != NULL)
		textureCache().removeFrameBufferTexture(m_pTexture);
	if (m_resolveFBO != 0)
		glDeleteFramebuffers(1, &m_resolveFBO);
	if (m_pResolveTexture != NULL)
		textureCache().removeFrameBufferTexture(m_pResolveTexture);
	m_pTexture = textureCache().addFrameBufferTexture();
	init(m_startAddress, endAddress, format, m_size, m_width, _height, m_cfb);
}

inline
uint32_t _cutHeight(uint32_t _address, uint32_t _height, uint32_t _stride)
{
	if (_address > RDRAMSize)
		return 0;
	if (_address + _stride * _height > RDRAMSize)
		return (RDRAMSize - _address) / _stride;
	return _height;
}

void FrameBuffer::copyRdram()
{
	const uint32_t stride = m_width << m_size >> 1;
	const uint32_t height = _cutHeight(m_startAddress, m_height, stride);
	if (height == 0)
		return;
	const uint32_t dataSize = stride * height;

	// Auxiliary frame buffer
	if (m_width != VI.width && config.frameBufferEmulation.copyAuxToRDRAM == 0) {
		// Write small amount of data to the start of the buffer.
		// This is necessary for auxilary buffers: game can restore content of RDRAM when buffer is not needed anymore
		// Thus content of RDRAM on moment of buffer creation will be the same as when buffer becomes obsolete.
		// Validity check will see that the RDRAM is the same and thus the buffer is valid, which is false.
		const uint32_t twoPercent = dataSize / 200;
		uint32_t start = m_startAddress >> 2;
		uint32_t * pData = (uint32_t*)RDRAM;
		for (uint32_t i = 0; i < twoPercent; ++i) {
			if (i < 4)
				pData[start++] = fingerprint[i];
			else
				pData[start++] = 0;
		}
		m_cleared = false;
		m_fingerprint = true;
		return;
	}
	m_RdramCopy.resize(dataSize);
	memcpy(m_RdramCopy.data(), RDRAM + m_startAddress, dataSize);
}

bool FrameBuffer::isValid() const
{
	if (m_validityChecked == video().getBuffersSwapCount())
		return true; // Already checked

	const uint32_t * const pData = (const uint32_t*)RDRAM;

	if (m_cleared) {
		const uint32_t color = m_fillcolor & 0xFFFEFFFE;
		const uint32_t start = m_startAddress >> 2;
		const uint32_t end = m_endAddress >> 2;
		uint32_t wrongPixels = 0;
		for (uint32_t i = start; i < end; ++i) {
			if ((pData[i] & 0xFFFEFFFE) != color)
				++wrongPixels;
		}
		return wrongPixels < (m_endAddress - m_startAddress) / 400; // threshold level 1% of dwords
	} else if (m_fingerprint) {
			//check if our fingerprint is still there
			uint32_t start = m_startAddress >> 2;
			for (uint32_t i = 0; i < 4; ++i)
				if ((pData[start++] & 0xFFFEFFFE) != (fingerprint[i] & 0xFFFEFFFE))
					return false;
			return true;
	} else if (!m_RdramCopy.empty()) {
		const uint32_t * const pCopy = (const uint32_t*)m_RdramCopy.data();
		const uint32_t size = m_RdramCopy.size();
		const uint32_t size_dwords = size >> 2;
		uint32_t start = m_startAddress >> 2;
		uint32_t wrongPixels = 0;
		for (uint32_t i = 0; i < size_dwords; ++i) {
			if ((pData[start++] & 0xFFFEFFFE) != (pCopy[i] & 0xFFFEFFFE))
				++wrongPixels;
		}
		return wrongPixels < size / 400; // threshold level 1% of dwords
	}
	return true; // No data to decide
}

void FrameBuffer::resolveMultisampledTexture()
{
#ifdef GL_MULTISAMPLING_SUPPORT
	if (m_resolved)
		return;
	glScissor(0, 0, m_pTexture->realWidth, m_pTexture->realHeight);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFBO);
	glBlitFramebuffer(
		0, 0, m_pTexture->realWidth, m_pTexture->realHeight,
		0, 0, m_pResolveTexture->realWidth, m_pResolveTexture->realHeight,
		GL_COLOR_BUFFER_BIT, GL_NEAREST
		);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBufferList().getCurrent()->m_FBO);
	gDP.changed |= CHANGED_SCISSOR;
	m_resolved = true;
#endif
}

CachedTexture * FrameBuffer::getTexture()
{
	if (config.video.multisampling == 0)
		return m_pTexture;
	if (m_resolved)
		return m_pResolveTexture;

	resolveMultisampledTexture();
	return m_pResolveTexture;
}

FrameBufferList & FrameBufferList::get()
{
	static FrameBufferList frameBufferList;
	return frameBufferList;
}

void FrameBufferList::init()
{
	 m_pCurrent = NULL;
	 m_pCopy = NULL;
	 glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void FrameBufferList::destroy() {
	m_list.clear();
	m_pCurrent = NULL;
	m_pCopy = NULL;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void FrameBufferList::setBufferChanged()
{
	gDP.colorImage.changed = true;
	if (m_pCurrent != NULL) {
		m_pCurrent->m_changed = true;
		m_pCurrent->m_copiedToRdram = false;
	}
}

void FrameBufferList::correctHeight()
{
	if (m_pCurrent == NULL)
		return;
	if (m_pCurrent->m_changed) {
		m_pCurrent->m_needHeightCorrection = false;
		return;
	}
	if (m_pCurrent->m_needHeightCorrection && m_pCurrent->m_width == gDP.scissor.lrx) {
		if (m_pCurrent->m_height != gDP.scissor.lry) {
			m_pCurrent->reinit((uint32_t)gDP.scissor.lry);

			if (m_pCurrent->_isMarioTennisScoreboard())
				g_RDRAMtoFB.CopyFromRDRAM(m_pCurrent->m_startAddress + 4, false);
			gSP.changed |= CHANGED_VIEWPORT;
		}
		m_pCurrent->m_needHeightCorrection = false;
	}
}

void FrameBufferList::clearBuffersChanged()
{
	gDP.colorImage.changed = false;
	FrameBuffer * pBuffer = frameBufferList().findBuffer(*gfx_info.VI_ORIGIN_REG);
	if (pBuffer != NULL)
		pBuffer->m_changed = false;
}

FrameBuffer * FrameBufferList::findBuffer(uint32_t _startAddress)
{
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter)
	if (iter->m_startAddress <= _startAddress && iter->m_endAddress >= _startAddress) // [  {  ]
		return &(*iter);
	return NULL;
}

FrameBuffer * FrameBufferList::_findBuffer(uint32_t _startAddress, uint32_t _endAddress, uint32_t _width)
{
	if (m_list.empty())
		return NULL;

	FrameBuffers::iterator iter = m_list.end();
	do {
		--iter;
		if ((iter->m_startAddress <= _startAddress && iter->m_endAddress >= _startAddress) || // [  {  ]
			(_startAddress <= iter->m_startAddress && _endAddress >= iter->m_startAddress)) { // {  [  }

			if (_startAddress != iter->m_startAddress || _width != iter->m_width) {
				m_list.erase(iter);
				return _findBuffer(_startAddress, _endAddress, _width);
			}

			return &(*iter);
		}
	} while (iter != m_list.begin());
	return NULL;
}

FrameBuffer * FrameBufferList::findTmpBuffer(uint32_t _address)
{
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter)
		if (iter->m_startAddress > _address || iter->m_endAddress < _address)
				return &(*iter);
	return NULL;
}

void FrameBufferList::saveBuffer(uint32_t _address, uint16_t _format, uint16_t _size, uint16_t _width, uint16_t _height, bool _cfb)
{
	if (m_pCurrent != NULL && config.frameBufferEmulation.copyAuxToRDRAM != 0) {
		if (m_pCurrent->m_width != VI.width) {
			FrameBuffer_CopyToRDRAM(m_pCurrent->m_startAddress, true);
			removeBuffer(m_pCurrent->m_startAddress);
		}
	}

	if (VI.width == 0 || _height == 0) {
		m_pCurrent = NULL;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		return;
	}

	OGLVideo & ogl = video();
	if (m_pCurrent != NULL) {
		if (gDP.colorImage.height > 0) {
			if (m_pCurrent->m_width == VI.width || m_pCurrent->m_needHeightCorrection) {
				gDP.colorImage.height = MIN(gDP.colorImage.height, VI.height);
				m_pCurrent->m_endAddress = MIN(RDRAMSize, m_pCurrent->m_startAddress + (((m_pCurrent->m_width * gDP.colorImage.height) << m_pCurrent->m_size >> 1) - 1));
			}
			if (!m_pCurrent->_isMarioTennisScoreboard() && !m_pCurrent->m_isDepthBuffer && !m_pCurrent->m_copiedToRdram && !m_pCurrent->m_cfb && !m_pCurrent->m_cleared && m_pCurrent->m_RdramCopy.empty() && gDP.colorImage.height > 1) {
				m_pCurrent->copyRdram();
			}
		}
		m_pCurrent = _findBuffer(m_pCurrent->m_startAddress, m_pCurrent->m_endAddress, m_pCurrent->m_width);
	}

	const uint32_t endAddress = _address + ((_width * _height) << _size >> 1) - 1;
	if (m_pCurrent == NULL || m_pCurrent->m_startAddress != _address || m_pCurrent->m_width != _width)
		m_pCurrent = findBuffer(_address);
	if (m_pCurrent != NULL) {
		if ((m_pCurrent->m_startAddress != _address) ||
			(m_pCurrent->m_width != _width) ||
			//(current->height != height) ||
			//(current->size != size) ||  // TODO FIX ME
			(m_pCurrent->m_scaleX != ogl.getScaleX()) ||
			(m_pCurrent->m_scaleY != ogl.getScaleY()))
		{
			removeBuffer(m_pCurrent->m_startAddress);
			m_pCurrent = NULL;
		} else {
			m_pCurrent->m_resolved = false;
			glBindFramebuffer(GL_FRAMEBUFFER, m_pCurrent->m_FBO);
			if (m_pCurrent->m_size != _size) {
				float fillColor[4];
				gln64gDPGetFillColor(fillColor);
				ogl.getRender().clearColorBuffer(fillColor);
				m_pCurrent->m_size = _size;
				m_pCurrent->m_pTexture->format = _format;
				m_pCurrent->m_pTexture->size = _size;
				if (m_pCurrent->m_pResolveTexture != NULL) {
					m_pCurrent->m_pResolveTexture->format = _format;
					m_pCurrent->m_pResolveTexture->size = _size;
				}
				if (m_pCurrent->m_copiedToRdram)
					m_pCurrent->copyRdram();
			}
		}
	}
	const bool bNew = m_pCurrent == NULL;
	if  (bNew) {
		// Wasn't found or removed, create a new one
		m_list.emplace_front();
		FrameBuffer & buffer = m_list.front();
		buffer.init(_address, endAddress, _format, _size, _width, _height, _cfb);
		m_pCurrent = &buffer;

		if (m_pCurrent->_isMarioTennisScoreboard())
			g_RDRAMtoFB.CopyFromRDRAM(m_pCurrent->m_startAddress + 4, false);
	}

	if (_address == gDP.depthImageAddress)
		depthBufferList().saveBuffer(_address);
	else
		attachDepthBuffer();

#ifdef DEBUG
	DebugMsg( DEBUG_HIGH | DEBUG_HANDLED, "FrameBuffer_SaveBuffer( 0x%08X ); depth buffer is 0x%08X\n",
		address, (depthBuffer.top != NULL && depthBuffer.top->renderbuf > 0) ? depthBuffer.top->address : 0
	);
#endif

	m_pCurrent->m_isDepthBuffer = _address == gDP.depthImageAddress;
	m_pCurrent->m_isPauseScreen = m_pCurrent->m_isOBScreen = false;
	m_pCurrent->m_postProcessed = 0;
}

void FrameBufferList::copyAux()
{
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter) {
		if (iter->m_width != VI.width && iter->m_height != VI.height)
			FrameBuffer_CopyToRDRAM(iter->m_startAddress, true);
	}
}

void FrameBufferList::removeAux()
{
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter) {
		while (iter->m_width != VI.width && iter->m_height != VI.height) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = NULL;
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			iter = m_list.erase(iter);
			if (iter == m_list.end())
				return;
		}
	}
}

void FrameBufferList::removeBuffer(uint32_t _address )
{
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter)
		if (iter->m_startAddress == _address) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = NULL;
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			m_list.erase(iter);
			return;
		}
}

void FrameBufferList::removeBuffers(uint32_t _width)
{
	m_pCurrent = NULL;
	for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter) {
		while (iter->m_width == _width) {
			if (&(*iter) == m_pCurrent) {
				m_pCurrent = NULL;
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			}
			iter = m_list.erase(iter);
			if (iter == m_list.end())
				return;
		}
	}
}


void FrameBufferList::fillBufferInfo(FrameBufferInfo * _pinfo, uint32_t _size)
{
   uint32_t idx = 0;
   for (FrameBuffers::iterator iter = m_list.begin(); iter != m_list.end(); ++iter)
   {
      if (iter->m_width == VI.width && !iter->m_cfb)
      {
         _pinfo[idx].addr = iter->m_startAddress;
         _pinfo[idx].width = iter->m_width;
         _pinfo[idx].height = iter->m_height;
         _pinfo[idx++].size = iter->m_size;

         if (idx >= _size)
            return;
      }
   }
}

void FrameBufferList::attachDepthBuffer()
{
	if (m_pCurrent == NULL)
		return;

	DepthBuffer * pDepthBuffer = depthBufferList().getCurrent();
	if (m_pCurrent->m_FBO > 0 && pDepthBuffer != NULL) {
		pDepthBuffer->initDepthImageTexture(m_pCurrent);
		pDepthBuffer->initDepthBufferTexture(m_pCurrent);
#ifdef HAVE_OPENGLES2
		if (pDepthBuffer->m_pDepthBufferTexture->realWidth == m_pCurrent->m_pTexture->realWidth) {
#else
		if (pDepthBuffer->m_pDepthBufferTexture->realWidth >= m_pCurrent->m_pTexture->realWidth) {
#endif
			m_pCurrent->m_pDepthBuffer = pDepthBuffer;
			pDepthBuffer->setDepthAttachment(GL_DRAW_FRAMEBUFFER);
			if (video().getRender().isImageTexturesSupported() && config.frameBufferEmulation.N64DepthCompare != 0)
				pDepthBuffer->bindDepthImageTexture();
		} else
			m_pCurrent->m_pDepthBuffer = NULL;
	} else
		m_pCurrent->m_pDepthBuffer = NULL;

#ifndef HAVE_OPENGLES2
	GLuint attachments[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, attachments);
#endif
	assert(checkFBO());
}

void FrameBuffer_Init()
{
	frameBufferList().init();
	if (config.frameBufferEmulation.enable != 0) {
#ifndef HAVE_OPENGLES2
	g_fbToRDRAM.Init();
	g_dbToRDRAM.Init();
#endif
	g_RDRAMtoFB.Init();
	}
}

void FrameBuffer_Destroy()
{
	g_RDRAMtoFB.Destroy();
#ifndef HAVE_OPENGLES2
	g_dbToRDRAM.Destroy();
	g_fbToRDRAM.Destroy();
#endif
	frameBufferList().destroy();
}

#ifndef HAVE_OPENGLES2
void FrameBufferList::renderBuffer(uint32_t _address)
{
	static int32_t vStartPrev = 0;

	if (VI.width == 0 || *gfx_info.VI_WIDTH_REG == 0 || *gfx_info.VI_H_START_REG == 0) // H width is zero. Don't draw
		return;

	FrameBuffer *pBuffer = findBuffer(_address);
	if (pBuffer == NULL)
		return;

	OGLVideo & ogl = video();
	OGLRender & render = ogl.getRender();
	GLint srcY0, srcY1, dstY0, dstY1;
	GLint X0, X1, Xwidth;
	GLint srcPartHeight = 0;
	GLint dstPartHeight = 0;

	const float yScale = _FIXED2FLOAT(_SHIFTR(*gfx_info.VI_Y_SCALE_REG, 0, 12), 10);
	int32_t vEnd = _SHIFTR(*gfx_info.VI_V_START_REG, 0, 10);
	int32_t vStart = _SHIFTR(*gfx_info.VI_V_START_REG, 16, 10);
	const int32_t hEnd = _SHIFTR(*gfx_info.VI_H_START_REG, 0, 10);
	const int32_t hStart = _SHIFTR(*gfx_info.VI_H_START_REG, 16, 10);
	const int32_t vSync = (*gfx_info.VI_V_SYNC_REG) & 0x03FF;
	const bool interlaced = (*gfx_info.VI_STATUS_REG & 0x40) != 0;
	const bool isPAL = vSync > 550;
	const int32_t vShift = (isPAL ? 47 : 37);
	dstY0 = vStart - vShift;
	dstY0 >>= 1;
	dstY0 &= -(dstY0 >= 0);
	vStart >>= 1;
	vEnd >>= 1;
	const uint32_t vFullHeight = isPAL ? 288 : 240;
	const uint32_t vCurrentHeight = vEnd - vStart;
	const float dstScaleY = (float)ogl.getHeight() / float(vFullHeight);

	bool isLowerField = false;
	if (interlaced)
		isLowerField = vStart > vStartPrev;
	vStartPrev = vStart;

	srcY0 = ((_address - pBuffer->m_startAddress) << 1 >> pBuffer->m_size) / (*gfx_info.VI_WIDTH_REG);
	if (isLowerField) {
		if (srcY0 > 0)
			--srcY0;
		if (dstY0 > 0)
			--dstY0;
	}

	if (srcY0 + vCurrentHeight > vFullHeight) {
		dstPartHeight = srcY0;
		srcY0 = (GLint)(srcY0*yScale);
		srcPartHeight = srcY0;
		srcY1 = VI.real_height;
		dstY1 = dstY0 + vCurrentHeight - dstPartHeight;
	} else {
		dstY0 += srcY0;
		dstY1 = dstY0 + vCurrentHeight;
		srcY0 = (GLint)(srcY0*yScale);
		srcY1 = srcY0 + VI.real_height;
	}

	const float scaleX = _FIXED2FLOAT(_SHIFTR(*gfx_info.VI_X_SCALE_REG, 0, 12), 10);
	const int32_t h0   = (isPAL ? 128 : 108);
	const int32_t hx0  = MAX(0, hStart - h0);
	const int32_t hx1  = MAX(0, h0 + 640 - hEnd);
	X0 = (GLint)(hx0 * scaleX * ogl.getScaleX());
	Xwidth = (GLint)((MIN((float)VI.width, (hEnd - hStart)*scaleX)) * ogl.getScaleX());
	X1 = ogl.getWidth() - (GLint)(hx1 *scaleX * ogl.getScaleX());

	const float srcScaleY = ogl.getScaleY();
	const GLint hOffset = (ogl.getScreenWidth() - ogl.getWidth()) / 2;
	const GLint vOffset = (ogl.getScreenHeight() - ogl.getHeight()) / 2 + ogl.getHeightOffset();
	CachedTexture * pBufferTexture = pBuffer->m_pTexture;
	GLint srcCoord[4] = { 0, (GLint)(srcY0*srcScaleY), Xwidth, MIN((GLint)(srcY1*srcScaleY), (GLint)pBufferTexture->realHeight) };
	if (srcCoord[2] > pBufferTexture->realWidth || srcCoord[3] > pBufferTexture->realHeight) {
		removeBuffer(pBuffer->m_startAddress);
		return;
	}
	GLint dstCoord[4] = { X0 + hOffset, vOffset + (GLint)(dstY0*dstScaleY), hOffset + X1, vOffset + (GLint)(dstY1*dstScaleY) };
#ifdef GLESX
	if (render.getRenderer() == OGLRender::glrAdreno)
		dstCoord[0] += 1; // workaround for Adreno's issue with glBindFramebuffer;
#endif // GLESX

	render.updateScissor(pBuffer);
	PostProcessor::get().doGammaCorrection(pBuffer);
	// glDisable(GL_SCISSOR_TEST) does not affect glBlitFramebuffer, at least on AMD
	glScissor(0, 0, ogl.getScreenWidth(), ogl.getScreenHeight() + ogl.getHeightOffset());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	//glDrawBuffer( GL_BACK );
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	render.clearColorBuffer(clearColor);

	GLenum filter = GL_LINEAR;
	if (config.video.multisampling != 0) {
		if (X0 > 0 || dstPartHeight > 0 ||
			(srcCoord[2] - srcCoord[0]) != (dstCoord[2] - dstCoord[0]) ||
			(srcCoord[3] - srcCoord[1]) != (dstCoord[3] - dstCoord[1])) {
			pBuffer->resolveMultisampledTexture();
			glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_resolveFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		} else {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_FBO);
			filter = GL_NEAREST;
		}
	} else
		glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_FBO);

	glBlitFramebuffer(
		srcCoord[0], srcCoord[1], srcCoord[2], srcCoord[3],
		dstCoord[0], dstCoord[1], dstCoord[2], dstCoord[3],
		GL_COLOR_BUFFER_BIT, filter
	);

	if (dstPartHeight > 0) {
		const uint32_t size = *gfx_info.VI_STATUS_REG & 3;
		pBuffer = findBuffer(_address + (((*gfx_info.VI_WIDTH_REG)*VI.height)<<size>>1));
		if (pBuffer != NULL) {
			srcY0 = 0;
			srcY1 = srcPartHeight;
			dstY0 = dstY1;
			dstY1 = dstY0 + dstPartHeight;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_FBO);
			glBlitFramebuffer(
				0, (GLint)(srcY0*srcScaleY), ogl.getWidth(), MIN((GLint)(srcY1*srcScaleY), (GLint)pBuffer->m_pTexture->realHeight),
				hOffset, vOffset + (GLint)(dstY0*dstScaleY), hOffset + ogl.getWidth(), vOffset + (GLint)(dstY1*dstScaleY),
				GL_COLOR_BUFFER_BIT, filter
			);
		}
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	if (m_pCurrent != NULL)
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
	ogl.swapBuffers();
	gDP.changed |= CHANGED_SCISSOR;
}
#else

void FrameBufferList::renderBuffer(uint32_t _address)
{
	if (VI.width == 0 || *gfx_info.VI_WIDTH_REG == 0 || *gfx_info.VI_H_START_REG == 0) // H width is zero. Don't draw
		return;

	FrameBuffer *pBuffer = findBuffer(_address);
	if (pBuffer == NULL)
		return;

	OGLVideo & ogl = video();
	ogl.getRender().updateScissor(pBuffer);
	PostProcessor::get().doGammaCorrection(pBuffer);
	ogl.getRender().dropRenderState();
	gSP.changed = gDP.changed = 0;

	CombinerInfo::get().setCombine(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, 1, 0, 0, 0, TEXEL0, 0, 0, 0, 1));
	glDisable( GL_BLEND );
	glDisable(GL_DEPTH_TEST);
	glDisable( GL_CULL_FACE );
	glDisable( GL_POLYGON_OFFSET_FILL );

	const uint32_t width = pBuffer->m_width;
	const uint32_t height = pBuffer->m_height;

	pBuffer->m_pTexture->scaleS = ogl.getScaleX() / (float)pBuffer->m_pTexture->realWidth;
	pBuffer->m_pTexture->scaleT = ogl.getScaleY() / (float)pBuffer->m_pTexture->realHeight;
	pBuffer->m_pTexture->shiftScaleS = 1.0f;
	pBuffer->m_pTexture->shiftScaleT = 1.0f;
	pBuffer->m_pTexture->offsetS = 0;
	pBuffer->m_pTexture->offsetT = (float)height;
	textureCache().activateTexture(0, pBuffer->m_pTexture);
	gSP.textureTile[0]->fuls = gSP.textureTile[0]->fult = 0.0f;
	gSP.textureTile[0]->shifts = gSP.textureTile[0]->shiftt = 0;
	currentCombiner()->updateTextureInfo(true);
	CombinerInfo::get().updateParameters(OGLRender::rsTexRect);

	glScissor(0, 0, ogl.getScreenWidth(), ogl.getScreenHeight() + ogl.getHeightOffset());

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	FrameBuffer * pCurrent = m_pCurrent;
	m_pCurrent = pBuffer;
	OGLRender::TexturedRectParams params(0.0f, 0.0f, width, height, 0.0f, 0.0f, width - 1.0f, height - 1.0f, false);
	ogl.getRender().drawTexturedRect(params);
	m_pCurrent = pCurrent;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	if (m_pCurrent != NULL)
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_pCurrent->m_FBO);
	ogl.swapBuffers();

	gSP.changed |= CHANGED_VIEWPORT;
	gDP.changed |= CHANGED_COMBINE | CHANGED_SCISSOR;
}
#endif

void FrameBuffer_ActivateBufferTexture(int16_t t, FrameBuffer *pBuffer)
{
	if (pBuffer == NULL || pBuffer->m_pTexture == NULL)
		return;

	CachedTexture *pTexture = pBuffer->m_pTexture;

	pTexture->scaleS = pBuffer->m_scaleX / (float)pTexture->realWidth;
	pTexture->scaleT = pBuffer->m_scaleY / (float)pTexture->realHeight;

	if (gSP.textureTile[t]->shifts > 10)
		pTexture->shiftScaleS = (float)(1 << (16 - gSP.textureTile[t]->shifts));
	else if (gSP.textureTile[t]->shifts > 0)
		pTexture->shiftScaleS = 1.0f / (float)(1 << gSP.textureTile[t]->shifts);
	else
		pTexture->shiftScaleS = 1.0f;

	if (gSP.textureTile[t]->shiftt > 10)
		pTexture->shiftScaleT = (float)(1 << (16 - gSP.textureTile[t]->shiftt));
	else if (gSP.textureTile[t]->shiftt > 0)
		pTexture->shiftScaleT = 1.0f / (float)(1 << gSP.textureTile[t]->shiftt);
	else
		pTexture->shiftScaleT = 1.0f;

	const uint32_t shift = gSP.textureTile[t]->imageAddress - pBuffer->m_startAddress;
	const uint32_t factor = pBuffer->m_width << pBuffer->m_size >> 1;
	if (gSP.textureTile[t]->loadType == LOADTYPE_TILE)
	{
		pTexture->offsetS = (float)pBuffer->m_pLoadTile->uls;
		pTexture->offsetT = (float)(pBuffer->m_height - (pBuffer->m_pLoadTile->ult + shift/factor));
	}
	else
	{
		pTexture->offsetS = (float)((shift % factor) >> pBuffer->m_size << 1);
		pTexture->offsetT = (float)(pBuffer->m_height - shift/factor);
	}

//	frameBufferList().renderBuffer(pBuffer->m_startAddress);
	textureCache().activateTexture(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

void FrameBuffer_ActivateBufferTextureBG(int16_t t, FrameBuffer *pBuffer )
{
	if (pBuffer == NULL || pBuffer->m_pTexture == NULL)
		return;

	CachedTexture *pTexture = pBuffer->m_pTexture;
	pTexture->scaleS = video().getScaleX() / (float)pTexture->realWidth;
	pTexture->scaleT = video().getScaleY() / (float)pTexture->realHeight;

	pTexture->shiftScaleS = 1.0f;
	pTexture->shiftScaleT = 1.0f;

	pTexture->offsetS = gSP.bgImage.imageX;
	pTexture->offsetT = (float)pBuffer->m_height - gSP.bgImage.imageY;

	//	FrameBuffer_RenderBuffer(buffer->startAddress);
	textureCache().activateTexture(t, pTexture);
	gDP.changed |= CHANGED_FB_TEXTURE;
}

static
void copyWhiteToRDRAM(FrameBuffer * _pBuffer)
{
	if (_pBuffer->m_size == G_IM_SIZ_32b) {
		uint32_t *ptr_dst = (uint32_t*)(RDRAM + _pBuffer->m_startAddress);

		for (uint32_t y = 0; y < VI.height; ++y) {
			for (uint32_t x = 0; x < VI.width; ++x)
				ptr_dst[x + y*VI.width] = 0xFFFFFFFF;
		}
	}
	else {
		uint16_t *ptr_dst = (uint16_t*)(RDRAM + _pBuffer->m_startAddress);

		for (uint32_t y = 0; y < VI.height; ++y) {
			for (uint32_t x = 0; x < VI.width; ++x) {
				ptr_dst[(x + y*VI.width) ^ 1] = 0xFFFF;
			}
		}
	}
	_pBuffer->m_copiedToRdram = true;
	_pBuffer->copyRdram();

	_pBuffer->m_cleared = false;
}

#ifndef HAVE_OPENGLES2
void FrameBufferToRDRAM::Init()
{
	// generate a framebuffer
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glGenFramebuffers(1, &m_FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO);

	m_pTexture = textureCache().addFrameBufferTexture();
	m_pTexture->format = G_IM_FMT_RGBA;
	m_pTexture->clampS = 1;
	m_pTexture->clampT = 1;
	m_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pTexture->maskS = 0;
	m_pTexture->maskT = 0;
	m_pTexture->mirrorS = 0;
	m_pTexture->mirrorT = 0;
	m_pTexture->realWidth = 640;
	m_pTexture->realHeight = 580;
	m_pTexture->textureBytes = m_pTexture->realWidth * m_pTexture->realHeight * 4;
	textureCache().addFrameBufferTextureSize(m_pTexture->textureBytes);
	glBindTexture( GL_TEXTURE_2D, m_pTexture->glName );
	glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.colorInternalFormat, m_pTexture->realWidth, m_pTexture->realHeight, 0, fboFormats.colorFormat, fboFormats.colorType, NULL);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pTexture->glName, 0);
	// check if everything is OK
	assert(checkFBO());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	// Generate and initialize Pixel Buffer Objects
	glGenBuffers(3, m_PBO);
	for (uint32_t i = 0; i < 3; ++i) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBO[i]);
		glBufferData(GL_PIXEL_PACK_BUFFER, m_pTexture->textureBytes, NULL, GL_DYNAMIC_READ);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	m_curIndex = 0;
}

void FrameBufferToRDRAM::Destroy() {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	if (m_FBO != 0) {
		glDeleteFramebuffers(1, &m_FBO);
		m_FBO = 0;
	}
	if (m_pTexture != NULL) {
		textureCache().removeFrameBufferTexture(m_pTexture);
		m_pTexture = NULL;
	}
	glDeleteBuffers(3, m_PBO);
	m_PBO[0] = m_PBO[1] = m_PBO[2] = 0;
}

bool FrameBufferToRDRAM::_prepareCopy(uint32_t _address)
{
   const uint32_t curFrame = video().getBuffersSwapCount();
   FrameBuffer * pBuffer = frameBufferList().findBuffer(_address);
	if (m_frameCount == curFrame && pBuffer == m_pCurFrameBuffer)
		return true;

	if (VI.width == 0 || frameBufferList().getCurrent() == NULL)
		return false;

	m_pCurFrameBuffer = pBuffer;
	if (m_pCurFrameBuffer == NULL || m_pCurFrameBuffer->m_isOBScreen)
		return false;

	const uint32_t numPixels = m_pCurFrameBuffer->m_width * m_pCurFrameBuffer->m_height;
	if (numPixels == 0)
		return false;

	const uint32_t stride = m_pCurFrameBuffer->m_width << m_pCurFrameBuffer->m_size >> 1;
	const uint32_t height = _cutHeight(_address, m_pCurFrameBuffer->m_height, stride);
	if (height == 0)
		return false;

	if ((config.generalEmulation.hacks & hack_subscreen) != 0 && m_pCurFrameBuffer->m_width == VI.width && m_pCurFrameBuffer->m_height == VI.height) {
		copyWhiteToRDRAM(m_pCurFrameBuffer);
		return false;
	}

	_address = m_pCurFrameBuffer->m_startAddress;
	if (config.video.multisampling != 0)
   {
		m_pCurFrameBuffer->resolveMultisampledTexture();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pCurFrameBuffer->m_resolveFBO);
	} else
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pCurFrameBuffer->m_FBO);

	if (m_pCurFrameBuffer->m_scaleX > 1.0f)
   {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO);
		glScissor(0, 0, m_pCurFrameBuffer->m_pTexture->realWidth, m_pCurFrameBuffer->m_pTexture->realHeight);
		glBlitFramebuffer(
			0, 0, video().getWidth(), video().getHeight(),
			0, 0, VI.width, VI.height,
			GL_COLOR_BUFFER_BIT, GL_NEAREST
			);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBufferList().getCurrent()->m_FBO);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
	}

   m_frameCount = curFrame;

   return true;
}

template <typename TSrc, typename TDst>
void _writeToRdram(TSrc* _src, TDst* _dst, TDst(*converter)(TSrc _c), TSrc _testValue, uint32_t _xor, uint32_t _width,
      uint32_t _height, uint32_t _numPixels, uint32_t _startAddress, uint32_t _bufferAddress, uint32_t _bufferSize)
{
   uint32_t chunkStart = ((_startAddress - _bufferAddress) >> (_bufferSize - 1)) % _width;
   if (chunkStart % 2 != 0)
   {
      --chunkStart;
      --_dst;
      ++_numPixels;
   }

   uint32_t numStored = 0;
   uint32_t y = 0;
   TSrc c;
   if (chunkStart > 0)
   {
      for (uint32_t x = chunkStart; x < _width; ++x)
      {
         c = _src[x + (_height - 1)*_width];
         if (c != _testValue)
            _dst[numStored ^ _xor] = converter(c);
         ++numStored;
      }
      ++y;
      _dst += numStored;
   }

   uint32_t dsty = 0;
   for (; y < _height; ++y) {
      for (uint32_t x = 0; x < _width && numStored < _numPixels; ++x)
      {
         c = _src[x + (_height - y - 1)*_width];
         if (c != _testValue)
            _dst[(x + dsty*_width) ^ _xor] = converter(c);
         ++numStored;
      }
      ++dsty;
   }
}


void FrameBufferToRDRAM::_copy(uint32_t _startAddress, uint32_t _endAddress, bool _sync)
{ 
   const uint32_t stride = m_pCurFrameBuffer->m_width << m_pCurFrameBuffer->m_size >> 1;
	const uint32_t max_height = _cutHeight(_startAddress, m_pCurFrameBuffer->m_height, stride);

	uint32_t numPixels = (_endAddress - _startAddress) >> (m_pCurFrameBuffer->m_size - 1);
	if (numPixels / m_pCurFrameBuffer->m_width > max_height) {
		_endAddress = _startAddress + (max_height * stride);
		numPixels = (_endAddress - _startAddress) >> (m_pCurFrameBuffer->m_size - 1);
	}

	const GLsizei width  = m_pCurFrameBuffer->m_width;
	const GLint x0       = 0;
   const GLint y0       = max_height - (_endAddress - m_pCurFrameBuffer->m_startAddress) / stride;
	const GLint y1       = max_height - (_startAddress - m_pCurFrameBuffer->m_startAddress) / stride;
   const GLsizei height = MIN(max_height, 1 + y1 - y0);

	GLenum colorFormat, colorType, colorFormatBytes;
	if (m_pCurFrameBuffer->m_size > G_IM_SIZ_8b) {
		colorFormat = fboFormats.colorFormat;
		colorType = fboFormats.colorType;
		colorFormatBytes = fboFormats.colorFormatBytes;
	}
	else
   {
		colorFormat = fboFormats.monochromeFormat;
		colorType = fboFormats.monochromeType;
		colorFormatBytes = fboFormats.monochromeFormatBytes;
	}

#ifndef HAVE_OPENGLES2
	// If Sync, read pixels from the buffer, copy them to RDRAM.
	// If not Sync, read pixels from the buffer, copy pixels from the previous buffer to RDRAM.
	if (!_sync) {
		m_curIndex ^= 1;
		const uint32_t nextIndex = m_curIndex^1;
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBO[m_curIndex]);
		glReadPixels(x0, y0, width, height, colorFormat, colorType, 0);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBO[nextIndex]);
	} else {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBO[2]);
		glReadPixels(x0, y0, width, height, colorFormat, colorType, 0);
	}

	GLubyte* pixelData = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, width * height * colorFormatBytes, GL_MAP_READ_BIT);
	if (pixelData == NULL)
		return;
#else
	GLubyte* pixelData = (GLubyte* )malloc(width * height * colorFormatBytes);
	if (pixelData == NULL)
		return;
	glReadPixels(x0, y0, width, height, colorFormat, colorType, pixelData);
#endif // HAVE_OPENGLES2

	if (m_pCurFrameBuffer->m_size == G_IM_SIZ_32b)
   {
		uint32_t *ptr_src = (uint32_t*)pixelData;
		uint32_t *ptr_dst = (uint32_t*)(RDRAM + _startAddress);
		RGBA c;
      _writeToRdram<uint32_t, uint32_t>(ptr_src, ptr_dst, &RGBA16toRGBA32, 0, 0, width, height, numPixels, _startAddress, m_pCurFrameBuffer->m_startAddress, m_pCurFrameBuffer->m_size);
	}
   else if (m_pCurFrameBuffer->m_size == G_IM_SIZ_16b)
   {
		uint32_t * ptr_src = (uint32_t*)pixelData;
		uint16_t * ptr_dst = (uint16_t*)(RDRAM + _startAddress);
      _writeToRdram<uint32_t, uint16_t>(ptr_src, ptr_dst, &RGBA32toRGBA16, 0, 1, width, height, numPixels, _startAddress, m_pCurFrameBuffer->m_startAddress, m_pCurFrameBuffer->m_size);
	}
   else if (m_pCurFrameBuffer->m_size == G_IM_SIZ_8b)
   {
      uint8_t *ptr_src = (uint8_t*)pixelData;
      uint8_t *ptr_dst = RDRAM + _startAddress;
      _writeToRdram<uint8_t, uint8_t>(ptr_src, ptr_dst, &RGBA8toR8, 0, 3, width, height, numPixels, _startAddress, m_pCurFrameBuffer->m_startAddress, m_pCurFrameBuffer->m_size);
   }

	m_pCurFrameBuffer->m_copiedToRdram = true;
	m_pCurFrameBuffer->copyRdram();
	m_pCurFrameBuffer->m_cleared = false;
#ifndef HAVE_OPENGLES2
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#else
	free(pixelData);
#endif
	gDP.changed |= CHANGED_SCISSOR;
}

void FrameBufferToRDRAM::copyToRDRAM(uint32_t _address, bool _sync)
{
	if (!_prepareCopy(_address))
		return;
	_copy(m_pCurFrameBuffer->m_startAddress, m_pCurFrameBuffer->m_endAddress, _sync);
}

void FrameBufferToRDRAM::copyChunkToRDRAM(uint32_t _address)
{
	if (!_prepareCopy(_address))
		return;
	_copy(_address, _address + 0x1000, true);
}
#endif // HAVE_OPENGLES2

void FrameBuffer_CopyToRDRAM(uint32_t _address, bool _sync)
{
#ifndef HAVE_OPENGLES2
	g_fbToRDRAM.copyToRDRAM(_address, _sync);
#else
	if ((config.generalEmulation.hacks & hack_subscreen) == 0)
		return;
	if (VI.width == 0 || frameBufferList().getCurrent() == NULL)
		return;
	FrameBuffer *pBuffer = frameBufferList().findBuffer(_address);
	if (pBuffer == NULL || pBuffer->m_width < VI.width || pBuffer->m_isOBScreen)
		return;
	copyWhiteToRDRAM(pBuffer);
#endif
}

void FrameBuffer_CopyChunkToRDRAM(uint32_t _address)
{
#ifndef HAVE_OPENGLES2
   g_fbToRDRAM.copyChunkToRDRAM(_address);
#endif
}

#ifndef HAVE_OPENGLES2
void DepthBufferToRDRAM::Init()
{
	// generate a framebuffer
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glGenFramebuffers(1, &m_FBO);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO);

	m_pColorTexture = textureCache().addFrameBufferTexture();
	m_pColorTexture->format = G_IM_FMT_I;
	m_pColorTexture->clampS = 1;
	m_pColorTexture->clampT = 1;
	m_pColorTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pColorTexture->maskS = 0;
	m_pColorTexture->maskT = 0;
	m_pColorTexture->mirrorS = 0;
	m_pColorTexture->mirrorT = 0;
	m_pColorTexture->realWidth = 640;
	m_pColorTexture->realHeight = 580;
	m_pColorTexture->textureBytes = m_pColorTexture->realWidth * m_pColorTexture->realHeight;
	textureCache().addFrameBufferTextureSize(m_pColorTexture->textureBytes);

	m_pDepthTexture = textureCache().addFrameBufferTexture();
	m_pDepthTexture->format = G_IM_FMT_I;
	m_pDepthTexture->clampS = 1;
	m_pDepthTexture->clampT = 1;
	m_pDepthTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pDepthTexture->maskS = 0;
	m_pDepthTexture->maskT = 0;
	m_pDepthTexture->mirrorS = 0;
	m_pDepthTexture->mirrorT = 0;
	m_pDepthTexture->realWidth = 640;
	m_pDepthTexture->realHeight = 580;
	m_pDepthTexture->textureBytes = m_pDepthTexture->realWidth * m_pDepthTexture->realHeight * sizeof(float);
	textureCache().addFrameBufferTextureSize(m_pDepthTexture->textureBytes);

	glBindTexture( GL_TEXTURE_2D, m_pColorTexture->glName );
	glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.monochromeInternalFormat, m_pColorTexture->realWidth, m_pColorTexture->realHeight, 0, fboFormats.monochromeFormat, fboFormats.monochromeType, NULL);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

	glBindTexture( GL_TEXTURE_2D, m_pDepthTexture->glName );
	glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.depthInternalFormat, m_pDepthTexture->realWidth, m_pDepthTexture->realHeight, 0, GL_DEPTH_COMPONENT, fboFormats.depthType, NULL);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

	glBindTexture(GL_TEXTURE_2D, 0);

	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pColorTexture->glName, 0);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_pDepthTexture->glName, 0);
	// check if everything is OK
	assert(checkFBO());
	assert(!isGLError());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	// Generate and initialize Pixel Buffer Objects
	glGenBuffers(1, &m_PBO);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBO);
	glBufferData(GL_PIXEL_PACK_BUFFER, m_pDepthTexture->realWidth * m_pDepthTexture->realHeight * sizeof(float), NULL, GL_DYNAMIC_READ);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void DepthBufferToRDRAM::Destroy() {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &m_FBO);
	m_FBO = 0;
	if (m_pColorTexture != NULL) {
		textureCache().removeFrameBufferTexture(m_pColorTexture);
		m_pColorTexture = NULL;
	}
	if (m_pDepthTexture != NULL) {
		textureCache().removeFrameBufferTexture(m_pDepthTexture);
		m_pDepthTexture = NULL;
	}
	if (m_PBO != 0) {
		glDeleteBuffers(1, &m_PBO);
		m_PBO = 0;
	}
}

bool DepthBufferToRDRAM::_prepareCopy( uint32_t _address)
{
   const uint32_t curFrame = video().getBuffersSwapCount();
   if (m_frameCount == curFrame)
      return true;

	const uint32_t numPixels = VI.width * VI.height;
	if (numPixels == 0) // Incorrect buffer size. Don't copy
		return false;
	FrameBuffer *pBuffer = frameBufferList().findBuffer(_address);
	if (pBuffer == NULL || pBuffer->m_width < VI.width || pBuffer->m_pDepthBuffer == NULL || !pBuffer->m_pDepthBuffer->m_cleared)
		return false;

	m_pCurDepthBuffer = pBuffer->m_pDepthBuffer;
	const uint32_t address = m_pCurDepthBuffer->m_address;
	if (address + numPixels * 2 > RDRAMSize)
		return false;

	const uint32_t height = _cutHeight(address, MIN(VI.height, m_pCurDepthBuffer->m_lry), pBuffer->m_width * 2);
	if (height == 0)
		return false;

	if (config.video.multisampling == 0)
		glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_FBO);
	else {
		m_pCurDepthBuffer->resolveDepthBufferTexture(pBuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, pBuffer->m_resolveFBO);
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_FBO);
	glScissor(0, 0, pBuffer->m_pTexture->realWidth, pBuffer->m_pTexture->realHeight);
	glBlitFramebuffer(
		0, 0, video().getWidth(), video().getHeight(),
		0, 0, pBuffer->m_width, pBuffer->m_height,
		GL_DEPTH_BUFFER_BIT, GL_NEAREST
	);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBufferList().getCurrent()->m_FBO);
   m_frameCount = curFrame;
   return true;
}

uint16_t DepthBufferToRDRAM::_FloatToUInt16(float _z)
{
   static const uint16_t * const zLUT = depthBufferList().getZLUT();
   uint32_t idx = 0x3FFFF;
   if (_z < 1.0f)
   {
      _z *= 262144.0f;
		idx = MIN(0x3FFFFU, (uint32_t)(floorf(_z + 0.5f)));
   }

   return zLUT[idx];
}

bool DepthBufferToRDRAM::_copy(uint32_t _startAddress, uint32_t _endAddress)
{
   const uint32_t stride     = m_pCurDepthBuffer->m_width << 1;
   const uint32_t max_height =  _cutHeight(_startAddress, MIN(VI.height, m_pCurDepthBuffer->m_lry), stride);

   uint32_t numPixels        = (_endAddress - _startAddress) >> 1;
   if (numPixels / m_pCurDepthBuffer->m_width > max_height)
   {
		_endAddress = _startAddress + (max_height * stride);
		numPixels = (_endAddress - _startAddress) >> 1;
	}

   const GLsizei width = m_pCurDepthBuffer->m_width;

	const GLint x0 = 0;
   const GLint y0 = max_height - (_endAddress - m_pCurDepthBuffer->m_address) / stride;
	const GLint y1 = max_height - (_startAddress - m_pCurDepthBuffer->m_address) / stride;
   const GLsizei height = MIN(max_height, 1 + y1 - y0);

	PBOBinder binder(GL_PIXEL_PACK_BUFFER, m_PBO);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
	glReadPixels(x0, y0, width, height, fboFormats.depthFormat, fboFormats.depthType, 0);

	GLubyte* pixelData = (GLubyte*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, width * height * fboFormats.depthFormatBytes, GL_MAP_READ_BIT);
	if(pixelData == NULL)
		return false;

	float    * ptr_src = (float*)pixelData;
	uint16_t *ptr_dst  = (uint16_t*)(RDRAM + _startAddress);
   _writeToRdram<float, uint16_t>(ptr_src, ptr_dst, &DepthBufferToRDRAM::_FloatToUInt16, 2.0f, 1, width, height, numPixels, _startAddress, m_pCurDepthBuffer->m_address, G_IM_SIZ_16b);

	m_pCurDepthBuffer->m_cleared = false;
	FrameBuffer *pBuffer = frameBufferList().findBuffer(m_pCurDepthBuffer->m_address);
	if (pBuffer != NULL)
		pBuffer->m_cleared = false;

	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	gDP.changed |= CHANGED_SCISSOR;
	return true;
}

bool DepthBufferToRDRAM::copyToRDRAM( uint32_t _address)
{
   if (!_prepareCopy(_address))
      return false;

   const uint32_t endAddress = m_pCurDepthBuffer->m_address + (MIN(VI.height, m_pCurDepthBuffer->m_lry) * m_pCurDepthBuffer->m_width * 2);
   return _copy(m_pCurDepthBuffer->m_address, endAddress);
}

bool DepthBufferToRDRAM::copyChunkToRDRAM( uint32_t _address)
{
   if (!_prepareCopy(_address))
		return false;

	const uint32_t endAddress = _address + 0x1000;
	return _copy(_address, endAddress);
}
#endif // HAVE_OPENGLES2

bool FrameBuffer_CopyDepthBuffer( uint32_t address ) {
#ifndef HAVE_OPENGLES2
	FrameBuffer * pCopyBuffer = frameBufferList().getCopyBuffer();
	if (pCopyBuffer != NULL) {
		// This code is mainly to emulate Zelda MM camera.
		g_fbToRDRAM.copyToRDRAM(pCopyBuffer->m_startAddress, true);
		pCopyBuffer->m_RdramCopy.resize(0); // To disable validity check by RDRAM content. CPU may change content of the buffer for some unknown reason.
		frameBufferList().setCopyBuffer(NULL);
		return true;
	} else
		return g_dbToRDRAM.copyToRDRAM(address);
#else
	return false;
#endif
}

bool FrameBuffer_CopyDepthBufferChunk(uint32_t address)
{
#ifdef HAVE_OPENGLES2
   return false;
#else
   return g_dbToRDRAM.copyChunkToRDRAM(address);
#endif
}

void RDRAMtoFrameBuffer::Init()
{
	m_pTexture = textureCache().addFrameBufferTexture();
	m_pTexture->format = G_IM_FMT_RGBA;
	m_pTexture->clampS = 1;
	m_pTexture->clampT = 1;
	m_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pTexture->maskS = 0;
	m_pTexture->maskT = 0;
	m_pTexture->mirrorS = 0;
	m_pTexture->mirrorT = 0;
	m_pTexture->realWidth = 640;
	m_pTexture->realHeight = 580;
	m_pTexture->textureBytes = m_pTexture->realWidth * m_pTexture->realHeight * 4;
	textureCache().addFrameBufferTextureSize(m_pTexture->textureBytes);
	glBindTexture( GL_TEXTURE_2D, m_pTexture->glName );
	glTexImage2D(GL_TEXTURE_2D, 0, fboFormats.colorInternalFormat, m_pTexture->realWidth, m_pTexture->realHeight, 0, fboFormats.colorFormat, fboFormats.colorType, NULL);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glBindTexture(GL_TEXTURE_2D, 0);

	// Generate Pixel Buffer Object. Initialize it later
#ifndef HAVE_OPENGLES2
	glGenBuffers(1, &m_PBO);
#endif
}

void RDRAMtoFrameBuffer::Destroy()
{
	if (m_pTexture != NULL) {
		textureCache().removeFrameBufferTexture(m_pTexture);
		m_pTexture = NULL;
	}
#ifndef HAVE_OPENGLES2
	if (m_PBO != 0) {
		glDeleteBuffers(1, &m_PBO);
		m_PBO = 0;
	}
#endif
}

void RDRAMtoFrameBuffer::CopyFromRDRAM( uint32_t _address, bool _bUseAlpha)
{
	FrameBuffer *pBuffer = frameBufferList().findBuffer(_address);
	if (pBuffer == NULL || pBuffer->m_size < G_IM_SIZ_16b)
		return;
	if (pBuffer->m_startAddress == _address && gDP.colorImage.changed != 0)
		return;

	const bool bUseAlpha = _bUseAlpha && pBuffer->m_changed;
	const uint32_t address = pBuffer->m_startAddress;
	const uint32_t width = pBuffer->m_width;
	const uint32_t height = _cutHeight(address, pBuffer->m_startAddress == _address ? VI.real_height : pBuffer->m_height, pBuffer->m_width * 2);
	if (height == 0)
		return;

	m_pTexture->width = width;
	m_pTexture->height = height;
	const uint32_t dataSize = width*height*4;
#ifndef HAVE_OPENGLES2
	PBOBinder binder(GL_PIXEL_UNPACK_BUFFER, m_PBO);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, dataSize, NULL, GL_DYNAMIC_DRAW);
	GLubyte* ptr = (GLubyte*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, GL_MAP_WRITE_BIT);
#else
	m_PBO = (GLubyte*)malloc(dataSize);
	GLubyte* ptr = m_PBO;
	PBOBinder binder(m_PBO);
#endif // HAVE_OPENGLES2
	if (ptr == NULL)
		return;

	uint8_t * image = RDRAM + address;
	uint32_t * dst = (uint32_t*)ptr;

	uint32_t empty = 0;
	uint32_t r, g, b, a, idx;
	if (pBuffer->m_size == G_IM_SIZ_16b) {
		uint16_t * src = (uint16_t*)image;
		uint16_t col;
		const uint32_t bound = (RDRAMSize + 1 - address) >> 1;
		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{
				idx = (x + (height - y - 1)*width)^1;
				if (idx >= bound)
					break;
				col = src[idx];
				if (bUseAlpha)
					src[idx] = 0;
				empty |= col;
				r = ((col >> 11)&31)<<3;
				g = ((col >> 6)&31)<<3;
				b = ((col >> 1)&31)<<3;
				a = (col&1) > 0 && (r|g|b) > 0 ? 0xff : 0U;
				dst[x + y*width] = (a << 24) | (b << 16) | (g << 8) | r;
			}
		}
	} else {
		// 32 bit
		uint32_t * src = (uint32_t*)image;
		uint32_t col;
		const uint32_t bound = (RDRAMSize + 1 - address) >> 2;
		for (uint32_t y=0; y < height; y++)
		{
			for (uint32_t x=0; x < width; x++)
			{
				idx = x + (height - y - 1)*width;
				if (idx >= bound)
					break;
				col = src[idx];
				if (bUseAlpha)
					src[idx] = 0;
				empty |= col;
				r = (col >> 24) & 0xff;
				g = (col >> 16) & 0xff;
				b = (col >> 8) & 0xff;
				a = (r|g|b) > 0 ? col & 0xff : 0U;
				dst[x + y*width] = (a<<24)|(b<<16)|(g<<8)|r;
			}
		}
	}
#ifndef HAVE_OPENGLES2
	glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); /* release the mapped buffer */
#endif
	if (empty == 0)
		return;

	glBindTexture(GL_TEXTURE_2D, m_pTexture->glName);
#ifndef HAVE_OPENGLES2
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#else
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, m_PBO);
#endif

	m_pTexture->scaleS = 1.0f / (float)m_pTexture->realWidth;
	m_pTexture->scaleT = 1.0f / (float)m_pTexture->realHeight;
	m_pTexture->shiftScaleS = 1.0f;
	m_pTexture->shiftScaleT = 1.0f;
	m_pTexture->offsetS = 0;
	m_pTexture->offsetT = (float)m_pTexture->height;
	textureCache().activateTexture(0, m_pTexture);

	gDPTile tile0;
	tile0.fuls = tile0.fult = 0.0f;
	gDPTile * pTile0 = gSP.textureTile[0];
	gSP.textureTile[0] = &tile0;

	if (_bUseAlpha) {
		CombinerInfo::get().setCombine(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		CombinerInfo::get().setCombine(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, 1, 0, 0, 0, TEXEL0, 0, 0, 0, 1));
		glDisable(GL_BLEND);
	}
	currentCombiner()->updateFBInfo();

	glDisable(GL_DEPTH_TEST);
	const uint32_t gdpChanged = gDP.changed & CHANGED_CPU_FB_WRITE;
	gSP.changed = gDP.changed = 0;

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pBuffer->m_FBO);
	OGLRender::TexturedRectParams params(0.0f, 0.0f, (float)width, (float)height, 0.0f, 0.0f, width - 1.0f, height - 1.0f, false);
	video().getRender().drawTexturedRect(params);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBufferList().getCurrent()->m_FBO);

	gSP.textureTile[0] = pTile0;

	gDP.changed |= gdpChanged | CHANGED_RENDERMODE | CHANGED_COMBINE;
}

void FrameBuffer_CopyFromRDRAM( uint32_t address, bool bUseAlpha )
{
	g_RDRAMtoFB.CopyFromRDRAM(address, bUseAlpha);
}
