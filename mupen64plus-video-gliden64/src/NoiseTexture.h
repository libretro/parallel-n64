#pragma once
#include <vector>
#include "Types.h"

#define NOISE_TEX_WIDTH 640
#define NOISE_TEX_HEIGHT 580

struct CachedTexture;

class NoiseTexture
{
public:
	NoiseTexture();

	void init();
	void destroy();
	void update();
	u32 getOffsetX() const { return m_offsetX; }
	u32 getOffsetY() const { return m_offsetY; }

private:
	void _fillTextureData();

	CachedTexture * m_pTexture;
	u32 m_DList;
	u32 m_offsetX, m_offsetY;
	std::vector<u8> m_texData;
};

extern NoiseTexture g_noiseTexture;
