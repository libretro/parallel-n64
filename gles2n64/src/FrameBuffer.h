#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "Types.h"
#include "DepthBuffer.h"
#include "Textures.h"

struct FrameBuffer
{
	struct FrameBuffer *higher, *lower;

	CachedTexture *texture;

	u32 m_startAddress, m_endAddress;
	u32 m_size, m_width, m_height, m_fillcolor, m_validityChecked;
	float m_scaleX, m_scaleY;

	bool m_copiedToRdram;
	bool m_cleared;
	bool m_changed;
	bool m_cfb;
	bool m_isDepthBuffer;
	bool m_isPauseScreen;
	bool m_isOBScreen;
	bool m_needHeightCorrection;
	bool m_postProcessed;

	GLuint m_FBO;
	struct gDPTile *m_pLoadTile;
	CachedTexture *m_pTexture;
	struct DepthBuffer *m_pDepthBuffer;
	// multisampling
	CachedTexture *m_pResolveTexture;
	GLuint m_resolveFBO;
	bool m_resolved;
};

struct FrameBufferInfo
{
	struct FrameBuffer *top, *bottom, *current;
	int numBuffers;
};

extern struct FrameBufferInfo frameBuffer;

void FrameBuffer_Init(void);
void FrameBuffer_Destroy(void);
void FrameBuffer_CopyToRDRAM( u32 _address );
void FrameBuffer_CopyFromRDRAM( u32 _address, bool _bUseAlpha );
void FrameBuffer_CopyDepthBuffer( u32 _address );
void FrameBuffer_ActivateBufferTexture( s16 t, struct FrameBuffer *buffer);
void FrameBuffer_ActivateBufferTextureBG(s16 t, struct FrameBuffer *buffer);

void FrameBuffer_SaveBuffer( u32 address, u16 format, u16 size, u16 width, u16 height, bool unknown );
void FrameBuffer_RenderBuffer( u32 address );
void FrameBuffer_RestoreBuffer( u32 address, u16 size, u16 width );
void FrameBuffer_RemoveBuffer( u32 address );
struct FrameBuffer *FrameBuffer_FindBuffer( u32 address );
struct FrameBuffer *FrameBuffer_GetCurrent(void);

#endif
