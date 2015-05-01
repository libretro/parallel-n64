#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "Types.h"
#include "Textures.h"

struct FrameBuffer
{
	struct FrameBuffer *higher, *lower;

	CachedTexture *texture;

	u32 startAddress, endAddress;
	u32 size, width, height, changed;
	float scaleX, scaleY;
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
void FrameBuffer_ActivateBufferTexture( s16 t, struct FrameBuffer *buffer);
void FrameBuffer_ActivateBufferTextureBG(s16 t, struct FrameBuffer *buffer);
void FrameBuffer_CopyFromRDRAM( u32 _address, bool _bUseAlpha );

#endif
