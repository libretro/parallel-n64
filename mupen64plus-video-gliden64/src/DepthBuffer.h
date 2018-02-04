#ifndef DEPTHBUFFER_H
#define DEPTHBUFFER_H

#include <stdint.h>

#include "Textures.h"

struct FrameBuffer;

struct DepthBuffer
{
	DepthBuffer();
	DepthBuffer(DepthBuffer && _other);
	~DepthBuffer();
	void initDepthImageTexture(FrameBuffer * _pBuffer);
	void initDepthBufferTexture(FrameBuffer * _pBuffer);
	CachedTexture * resolveDepthBufferTexture(FrameBuffer * _pBuffer);

	void setDepthAttachment(GLenum _target);
	void activateDepthBufferTexture(FrameBuffer * _pBuffer);

	void bindDepthImageTexture();

	uint32_t m_address, m_width;
	uint32_t m_uly, m_lry; // Top and bottom bounds of fillrect command.
	GLuint m_FBO;
	CachedTexture *m_pDepthImageTexture;
	CachedTexture *m_pDepthBufferTexture;
	bool m_cleared;
	// multisampling
	CachedTexture *m_pResolveDepthBufferTexture;
	bool m_resolved;

private:
	void _initDepthBufferTexture(FrameBuffer * _pBuffer, CachedTexture *_pTexture, bool _multisample);
};

class DepthBufferList
{
public:
	void init();
	void destroy();
	void saveBuffer(uint32_t _address);
	void removeBuffer(uint32_t _address);
	void clearBuffer(uint32_t _uly, uint32_t _lry);
	void setNotCleared();
	DepthBuffer *findBuffer(uint32_t _address);
	DepthBuffer * getCurrent() const {return m_pCurrent;}

	static DepthBufferList & get();

	const uint16_t * const getZLUT() const {return m_pzLUT;}

private:
	DepthBufferList();
	DepthBufferList(const DepthBufferList &);
	~DepthBufferList();

	typedef std::list<DepthBuffer> DepthBuffers;
	DepthBuffers m_list;
	DepthBuffer *m_pCurrent;
	uint16_t * m_pzLUT;
};

inline
DepthBufferList & depthBufferList()
{
	return DepthBufferList::get();
}

extern const GLuint ZlutImageUnit;
extern const GLuint TlutImageUnit;
extern const GLuint depthImageUnit;

void DepthBuffer_Init();
void DepthBuffer_Destroy();

#endif
