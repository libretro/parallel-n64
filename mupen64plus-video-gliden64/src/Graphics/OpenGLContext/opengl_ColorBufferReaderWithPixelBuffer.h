#pragma once
#include <Graphics/ColorBufferReader.h>
#include "opengl_CachedFunctions.h"

namespace opengl {

class ColorBufferReaderWithPixelBuffer :
		public graphics::ColorBufferReader
{
public:
	ColorBufferReaderWithPixelBuffer(CachedTexture * _pTexture,
			CachedBindBuffer * _bindBuffer);
	~ColorBufferReaderWithPixelBuffer();

	const u8 * _readPixels(const ReadColorBufferParams& _params, u32& _heightOffset, u32& _stride) override;
	void cleanUp() override;

	u32 numSlots() const override { return m_numPBO; }
	void readToSlot(u32 _slot, s32 _x0, s32 _y0, u32 _height, u32 _size) override;
	bool slotReady(u32 _slot, bool _wait) override;

private:
	const u8 * _mapSlot(u32 _slot, u32 _height, u32 _bytes, u32 & _stride) override;
	void _initBuffers();
	void _destroyBuffers();

	CachedBindBuffer * m_bindBuffer;

	u32 m_numPBO;
	static const int _maxPBO = 3;
	GLuint m_PBO[_maxPBO];
	GLsync m_fence[_maxPBO];
	bool m_mapped;
};

}
