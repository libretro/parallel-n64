#pragma once
#include <vector>
#include <Types.h>
#include <Textures.h>

namespace graphics {

class ColorBufferReader
{
public:
	ColorBufferReader(CachedTexture * _pTexture);
	virtual ~ColorBufferReader() = default;

	/* The rows read back, _stride pixels apart, valid until cleanUp().
	 * _height comes back as the rows the buffer holds. Integer formats
	 * are the reader's own memory, read where they lie. */
	virtual const u8 * readPixels(s32 _x0, s32 _y0, u32 _width, u32 & _height, u32 _size, bool _sync, u32 & _stride);
	virtual void cleanUp() = 0;

	/* Readers with a ring of pixel buffers read into numbered slots and
	 * leave the wait to the caller: readToSlot starts a read of _height
	 * rows, slotReady polls it (or waits for it), slotPixels gives its
	 * _height rows of _width pixels, _stride apart, valid until cleanUp(). */
	virtual u32 numSlots() const { return 0; }
	virtual void readToSlot(u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size) {}
	virtual bool slotReady(u32 _slot, bool _wait) { return true; }
	const u8 * slotPixels(u32 _slot, u32 _width, u32 & _height, u32 _size, u32 & _stride);

protected:
	struct ReadColorBufferParams {
		s32 x0;
		s32 y0;
		u32 width;
		u32 height;
		bool sync;
		ColorFormatParam colorFormat;
		DatatypeParam colorType;
		u32 colorFormatBytes;
	};

	CachedTexture * m_pTexture;
	std::vector<u8> m_pixelData;

	void _slotFormat(u32 _size, ColorFormatParam & _format, DatatypeParam & _type, u32 & _bytes) const;

private:
	const u8* _convertFloatTextureBuffer(const u8* _gpuData, u32 _width, u32 & _height, u32 _heightOffset, u32 _stride);
	virtual const u8 * _readPixels(const ReadColorBufferParams& _params, u32& _heightOffset, u32& _stride) = 0;
	virtual const u8 * _mapSlot(u32 _slot, u32 _height, u32 _bytes, u32 & _stride) { return nullptr; }
};

}
