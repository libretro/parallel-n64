#ifndef ColorBufferToRDRAM_H
#define ColorBufferToRDRAM_H

#include <memory>
#include <array>
#include <vector>
#include <Graphics/ObjectHandle.h>
#include "DeferredReadback.h"

namespace graphics {
	class ColorBufferReader;
}

struct CachedTexture;
struct FrameBuffer;

/* What a colour copy to RDRAM was read for. */
struct ColorCopyGeometry
{
	u32 startAddress;
	u32 numPixels;
	u32 bufferAddress;
	u32 width;
	u32 height;
	u32 size;
};

class ColorBufferToRDRAM : private DeferredReadback<ColorCopyGeometry>::Ops
{
public:
	void init();
	void destroy();

	void copyToRDRAM(u32 _address, bool _sync);
	void copyChunkToRDRAM(u32 _startAddress);

	static ColorBufferToRDRAM & get();

private:
	ColorBufferToRDRAM();
	ColorBufferToRDRAM(const ColorBufferToRDRAM &) = delete;
	virtual ~ColorBufferToRDRAM();

	CachedTexture * m_pTexture;

	void _initFBTexture(void);

	void _destroyFBTexure(void);

	bool _prepareCopy(u32& _startAddress);

	void _copy(u32 _startAddress, u32 _endAddress, bool _sync);
	void _write(const u8 * _pixels, u32 _stride, const ColorCopyGeometry & _g);

	bool ready(u32 _slot, bool _wait) override;
	void write(u32 _slot, const ColorCopyGeometry & _g) override;

	u32 _getRealWidth(u32 _viWidth);

	// Convert pixel from video memory to N64 buffer format.

	graphics::ObjectHandle m_FBO;
	FrameBuffer * m_pCurFrameBuffer;
	u32 m_frameCount;
	u32 m_startAddress;

	u32 m_lastBufferWidth;

	std::array<u32, 3> m_allowedRealWidths;
	std::unique_ptr<graphics::ColorBufferReader> m_bufferReader;
	DeferredReadback<ColorCopyGeometry> m_readback;
};

void copyWhiteToRDRAM(FrameBuffer * _pBuffer);

#endif // ColorBufferToRDRAM
