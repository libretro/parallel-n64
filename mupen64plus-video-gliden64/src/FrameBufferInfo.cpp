#include "FrameBufferInfoAPI.h"
#include "FrameBufferInfo.h"
#include "OpenGL.h"
#include "FrameBuffer.h"
#include "DepthBuffer.h"
#include "RSP.h"
#include "VI.h"

void FrameBufferWrite(uint32_t addr, uint32_t size)
{
	//LOG("FBWrite addr=%08lx size=%u\n", addr, size);
}

void FrameBufferWriteList(FrameBufferModifyEntry *plist, uint32_t size)
{
#if 0
	LOG("FBWList size=%u\n", size);
	for (uint32_t i = 0; i < size; ++i)
		LOG(" plist[%u] addr=%08lx val=%08lx size=%u\n", i, plist[i].addr, plist[i].val, plist[i].size);
#endif
}

void FrameBufferRead(uint32_t addr)
{
   const uint32_t address = RSP_SegmentToPhysical(addr);
	FrameBuffer * pBuffer  = frameBufferList().findBuffer(address);

	if (!pBuffer)
		return;
	if (pBuffer->m_isDepthBuffer)
		FrameBuffer_CopyDepthBufferChunk(address);
	else
		FrameBuffer_CopyChunkToRDRAM(address);
}

void FrameBufferGetInfo(void *pinfo)
{
	uint32_t idx = 0;
	DepthBuffer * pDepthBuffer = depthBufferList().getCurrent();
	FrameBufferInfo * pFBInfo  = (FrameBufferInfo*)pinfo;

	memset(pFBInfo, 0, sizeof(FrameBufferInfo)* 6);

	if (pDepthBuffer != NULL)
   {
		pFBInfo[idx].addr   = pDepthBuffer->m_address;
		pFBInfo[idx].width  = pDepthBuffer->m_width;
		pFBInfo[idx].height = VI.real_height;
		pFBInfo[idx++].size = 2;
	}
	frameBufferList().fillBufferInfo(&pFBInfo[idx], 6 - idx);
}
