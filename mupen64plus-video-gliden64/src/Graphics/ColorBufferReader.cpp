
#include "ColorBufferReader.h"
#include "FramebufferTextureFormats.h"
#include "Context.h"
#include "Parameters.h"
#include <algorithm>
#include <cstring>

namespace graphics {

	ColorBufferReader::ColorBufferReader(CachedTexture * _pTexture)
		: m_pTexture(_pTexture)
	{
		m_pixelData.resize(m_pTexture->textureBytes);
	}

	const u8* ColorBufferReader::_convertFloatTextureBuffer(const u8* _gpuData, u32 _width, u32 & _height,
		u32 _heightOffset, u32 _stride)
	{
		u8* pixelDataAlloc = m_pixelData.data();
		const float* pixelData = reinterpret_cast<const float*>(_gpuData);
		const u32 colorsPerPixel = 4;
		const u32 widthPixels = _width * colorsPerPixel;
		const u32 stridePixels = _stride * colorsPerPixel;

		if (_height * widthPixels  > m_pixelData.size())
			_height = static_cast<u32>(m_pixelData.size()) / widthPixels;

		for (u32 heightIndex = 0; heightIndex < _height; ++heightIndex) {
			for (u32 widthIndex = 0; widthIndex < widthPixels; ++widthIndex) {
				u8& dest = *(pixelDataAlloc + heightIndex*widthPixels + widthIndex);
				const float& src = *(pixelData + (heightIndex+_heightOffset)*stridePixels + widthIndex);
				dest = static_cast<u8>(src*255.0);
			}
		}

		return pixelDataAlloc;
	}


	const u8 * ColorBufferReader::readPixels(s32 _x0, s32 _y0, u32 _width, u32 & _height, u32 _size, bool _sync,
		u32 & _stride)
	{
		const FramebufferTextureFormats & fbTexFormat = gfxContext.getFramebufferTextureFormats();

		ReadColorBufferParams params;
		params.x0 = _x0;
		params.y0 = _y0;
		params.width = _width;
		params.height = _height;
		params.sync = _sync;

		if (_size > G_IM_SIZ_8b) {
			params.colorFormat = fbTexFormat.colorFormat;
			params.colorType = fbTexFormat.colorType;
			params.colorFormatBytes = fbTexFormat.colorFormatBytes;
		} else {
			params.colorFormat = fbTexFormat.monochromeFormat;
			params.colorType = fbTexFormat.monochromeType;
			params.colorFormatBytes = fbTexFormat.monochromeFormatBytes;
		}

		u32 heightOffset = 0;
		u32 stride = 0;
		const u8* pixelData = _readPixels(params, heightOffset, stride);

		if (pixelData == nullptr)
			return nullptr;

		if(params.colorType == datatype::FLOAT && _size > G_IM_SIZ_8b) {
			_stride = _width;
			return _convertFloatTextureBuffer(pixelData, params.width, _height, heightOffset, stride);
		}

		/* no compacting copy: the consumer steps by the stride */
		const u32 strideBytes = stride * params.colorFormatBytes;
		const u32 rows = strideBytes != 0 ? m_pTexture->textureBytes / strideBytes : 0;
		_height = rows > heightOffset ? std::min(_height, rows - heightOffset) : 0;
		_stride = stride;
		return pixelData + heightOffset * strideBytes;
	}

}