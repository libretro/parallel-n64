#pragma once

#include <Graphics/ColorBufferReader.h>
#include "opengl_CachedFunctions.h"
#include <Graphics/Parameter.h>

namespace opengl {

	class ColorBufferReaderWithBufferStorage :
		public graphics::ColorBufferReader
	{
	public:
		ColorBufferReaderWithBufferStorage(CachedTexture * _pTexture,
			CachedBindBuffer * _bindBuffer);
		virtual ~ColorBufferReaderWithBufferStorage();

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

		static const int _maxPBO = 3;
		u32 m_numPBO;
		GLuint m_PBO[_maxPBO];
		void* m_PBOData[_maxPBO];
		GLsync m_fence[_maxPBO];
	};

}
