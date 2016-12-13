#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#include <list>
#include <vector>

#include "m64p_plugin.h"
#include "Textures.h"

struct gDPTile;
struct DepthBuffer;

const int fingerprint[4] = { 2, 6, 4, 3 };

struct FrameBuffer
{
	FrameBuffer();
	FrameBuffer(FrameBuffer && _other);
	~FrameBuffer();
	void init(uint32_t _address, uint32_t _endAddress, uint16_t _format, uint16_t _size, uint16_t _width, uint16_t _height, bool _cfb);
	void reinit(uint16_t _height);
	void resolveMultisampledTexture();
	CachedTexture * getTexture();
	void copyRdram();
	bool isValid() const;
	bool _isMarioTennisScoreboard() const;

	uint32_t m_startAddress, m_endAddress;
	uint32_t m_size, m_width, m_height, m_fillcolor, m_validityChecked;
	float m_scaleX, m_scaleY;
	bool m_copiedToRdram;
	bool m_fingerprint;
	bool m_cleared;
	bool m_changed;
	bool m_cfb;
	bool m_isDepthBuffer;
	bool m_isPauseScreen;
	bool m_isOBScreen;
	bool m_needHeightCorrection;
	uint32_t m_postProcessed;

	GLuint m_FBO;
	gDPTile *m_pLoadTile;
	CachedTexture *m_pTexture;
	DepthBuffer *m_pDepthBuffer;
	// multisampling
	CachedTexture *m_pResolveTexture;
	GLuint m_resolveFBO;
	bool m_resolved;

	std::vector<uint8_t> m_RdramCopy;

private:
	void _initTexture(uint16_t _format, uint16_t _size, CachedTexture *_pTexture);
	void _setAndAttachTexture(uint16_t _size, CachedTexture *_pTexture);
};

class FrameBufferList
{
public:
	void init();
	void destroy();
	void saveBuffer(uint32_t _address, uint16_t _format, uint16_t _size, uint16_t _width, uint16_t _height, bool _cfb);
	void removeAux();
	void copyAux();
	void removeBuffer(uint32_t _address);
	void removeBuffers(uint32_t _width);
	void attachDepthBuffer();
	FrameBuffer * findBuffer(uint32_t _startAddress);
	FrameBuffer * findTmpBuffer(uint32_t _address);
	FrameBuffer * getCurrent() const {return m_pCurrent;}
	void renderBuffer(uint32_t _address);
	void setBufferChanged();
	void correctHeight();
	void clearBuffersChanged();

	FrameBuffer * getCopyBuffer() const { return m_pCopy; }
	void setCopyBuffer(FrameBuffer * _pBuffer) { m_pCopy = _pBuffer; }

   void fillBufferInfo(FrameBufferInfo * _pinfo, uint32_t _size);

	static FrameBufferList & get();

private:
	FrameBufferList() : m_pCurrent(NULL), m_pCopy(NULL) {}
	FrameBufferList(const FrameBufferList &);

	FrameBuffer * _findBuffer(uint32_t _startAddress, uint32_t _endAddress, uint32_t _width);

	typedef std::list<FrameBuffer> FrameBuffers;
	FrameBuffers m_list;
	FrameBuffer * m_pCurrent;
	FrameBuffer * m_pCopy;
};

struct PBOBinder {
#ifndef HAVE_OPENGLES2
	PBOBinder(GLenum _target, GLuint _PBO) : m_target(_target)
	{
		glBindBuffer(m_target, _PBO);
	}
	~PBOBinder() {
		glBindBuffer(m_target, 0);
	}
	GLenum m_target;
#else
	PBOBinder(GLubyte* _ptr) : ptr(_ptr) {}
	~PBOBinder() { free(ptr); }
	GLubyte* ptr;
#endif
};

inline
FrameBufferList & frameBufferList()
{
	return FrameBufferList::get();
}

void FrameBuffer_Init();
void FrameBuffer_Destroy();
void FrameBuffer_CopyToRDRAM( uint32_t _address , bool _sync );
void FrameBuffer_CopyChunkToRDRAM(uint32_t _address);
void FrameBuffer_CopyFromRDRAM( uint32_t address, bool bUseAlpha );
bool FrameBuffer_CopyDepthBuffer( uint32_t address );
bool FrameBuffer_CopyDepthBufferChunk(uint32_t address);
void FrameBuffer_ActivateBufferTexture(int16_t t, FrameBuffer *pBuffer);
void FrameBuffer_ActivateBufferTextureBG(int16_t t, FrameBuffer *pBuffer);

#endif
