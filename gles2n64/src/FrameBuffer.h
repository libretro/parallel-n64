#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "Types.h"
#include "DepthBuffer.h"
#include "Textures.h"

struct FrameBuffer
{
	struct FrameBuffer *higher, *lower;

	CachedTexture *texture;

	u32 startAddress, endAddress;
	u32 size, width, height, fillcolor, validityChecked;
   bool changed;
	float scaleX, scaleY;

	bool m_copiedToRdram;
	bool m_cleared;
	bool m_changed;
	bool m_cfb;
	bool m_isDepthBuffer;
	bool m_isPauseScreen;
	bool m_isOBScreen;
	bool m_needHeightCorrection;
	bool m_postProcessed;

	GLuint FBO;
	struct gDPTile *pLoadTile;
	CachedTexture *pTexture;
	struct DepthBuffer *pDepthBuffer;
	// multisampling
	CachedTexture *pResolveTexture;
	GLuint resolveFBO;
	bool resolved;
};

struct FrameBufferInfo
{
	struct FrameBuffer *top, *bottom, *current;
	int numBuffers;
};

extern struct FrameBufferInfo frameBuffer;

void FrameBuffer_Init(void);
void FrameBuffer_Destroy(void);
void FrameBuffer_SaveBuffer( u32 address, u16 format, u16 size, u16 width, u16 height, bool unknown );
void FrameBuffer_RenderBuffer( u32 address );
void FrameBuffer_RestoreBuffer( u32 address, u16 size, u16 width );
void FrameBuffer_RemoveBuffer( u32 address );
struct FrameBuffer *FrameBuffer_FindBuffer( u32 address );
struct FrameBuffer *FrameBuffer_GetCurrent(void);
void FrameBuffer_ActivateBufferTexture( s16 t, struct FrameBuffer *buffer);
void FrameBuffer_ActivateBufferTextureBG(s16 t, struct FrameBuffer *buffer);
void FrameBuffer_CopyFromRDRAM( u32 _address, bool _bUseAlpha );
void FrameBuffer_CopyToRDRAM( u32 _address );
void FrameBuffer_CopyDepthBuffer( u32 _address );

#endif
