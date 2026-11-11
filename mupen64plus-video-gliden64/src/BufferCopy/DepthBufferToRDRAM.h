#ifndef DepthBufferToRDRAM_H
#define DepthBufferToRDRAM_H

#include <memory>
#include <Graphics/ObjectHandle.h>
#include "DeferredReadback.h"
#include "../gli_rdram_write.h"

namespace graphics {
	class PixelReadBuffer;
}

struct CachedTexture;
struct FrameBuffer;

/* A full copy at the default Async or TripleBuffer colour copy setting is
 * left in flight and written to RDRAM once the GPU has finished it, at a
 * later copy, like the colour copies; a chunk copy, which the CPU is
 * waiting on, writes out what is in flight and then its own at once */
class DepthBufferToRDRAM : private DeferredReadback<gli_rdram_write>::Ops
{
public:
	void init();
	void destroy();

	bool copyToRDRAM(u32 _address);
	bool copyChunkToRDRAM(u32 _startAddress);

	static DepthBufferToRDRAM & get();

private:
	DepthBufferToRDRAM();
	~DepthBufferToRDRAM();

	bool _prepareCopy(u32& _startAddress, bool _copyChunk);
	bool _copy(u32 _startAddress, u32 _endAddress, bool _sync);

	/* DeferredReadback::Ops */
	bool ready(u32 _slot, bool _wait) override;
	void write(u32 _slot, const gli_rdram_write & _w) override;

	enum { MaxSlots = 3 };

	// Convert pixel from video memory to N64 depth buffer format.

	graphics::ObjectHandle m_FBO;
	/* one read-back buffer per read in flight, all allocated at init */
	std::unique_ptr<graphics::PixelReadBuffer> m_pbuf[MaxSlots];
	u32 m_numSlots;
	bool m_written;
	DeferredReadback<gli_rdram_write, MaxSlots> m_readback;
	u32 m_frameCount;
	CachedTexture * m_pColorTexture;
	CachedTexture * m_pDepthTexture;
	FrameBuffer * m_pCurFrameBuffer;
};

#endif // DepthBufferToRDRAM_H
