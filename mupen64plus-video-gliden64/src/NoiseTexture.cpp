#include <algorithm>
#include <stdint.h>
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include "FrameBuffer.h"
#include "Config.h"
#include "GBI.h"
#include "VI.h"
#include "Textures.h"
#include "NoiseTexture.h"
#include "DisplayWindow.h"

using namespace graphics;

NoiseTexture g_noiseTexture;

NoiseTexture::NoiseTexture()
	: m_pTexture(nullptr)
	, m_DList(0)
	, m_offsetX(0)
	, m_offsetY(0)
{
}

// Picks the noise offset each frame; drawing thread only.
static
u32 Rand()
{
	static uint32_t state = 0x9E3779B9U;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

void NoiseTexture::_fillTextureData()
{
	// One thread and a local xorshift64* generator, eight bytes per step.
	// The old fill split the textures across one thread per CPU, but
	// every thread called the C library's random generator, whose shared
	// state is locked in glibc and unsynchronised elsewhere (and each
	// thread's reseed stomped the others'). This is ~27x faster than that
	// was even on one CPU, needs no threads, and is the same every run.
	uint64_t x = 0x9E3779B97F4A7C15ULL;
	m_texData.resize(NOISE_TEX_WIDTH * NOISE_TEX_HEIGHT);
	const size_t sz = m_texData.size();
	size_t t = 0;
	while (t < sz) {
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		uint64_t r = x * 0x2545F4914F6CDD1DULL;
		for (u32 k = 0; k < 8 && t < sz; ++k, ++t) {
			m_texData[t] = u8(r);
			r >>= 8;
		}
	}
}


// One texture, read at a random offset that changes every frame. This used
// to bind one of 30 textures each frame instead, which looks the same but
// makes drivers that cache per-draw texture state (freedreno's a6xx
// tex_cache, keyed by every bound view) keep 30 times as many entries; that
// cache is never trimmed and is scanned linearly whenever a sampler view
// dies, which made long sessions stutter.
void NoiseTexture::init()
{
	if (config.generalEmulation.enableNoise == 0)
		return;

	if (m_texData.empty())
		_fillTextureData();

	m_pTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	m_pTexture->format = G_IM_FMT_RGBA;
	m_pTexture->clampS = 1;
	m_pTexture->clampT = 1;
	m_pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	m_pTexture->maskS = 0;
	m_pTexture->maskT = 0;
	m_pTexture->mirrorS = 0;
	m_pTexture->mirrorT = 0;
	m_pTexture->width = NOISE_TEX_WIDTH;
	m_pTexture->height = NOISE_TEX_HEIGHT;
	m_pTexture->textureBytes = m_pTexture->width * m_pTexture->height;

	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	{
		Context::InitTextureParams params;
		params.handle = m_pTexture->name;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.width = m_pTexture->width;
		params.height = m_pTexture->height;
		params.internalFormat = fbTexFormats.noiseInternalFormat;
		params.format = fbTexFormats.noiseFormat;
		params.dataType = fbTexFormats.noiseType;
		params.data = m_texData.data();
		gfxContext.init2DTexture(params);
	}
	{
		Context::TexParameters params;
		params.handle = m_pTexture->name;
		params.target = textureTarget::TEXTURE_2D;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.minFilter = textureParameters::FILTER_NEAREST;
		params.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(params);
	}
}

void NoiseTexture::destroy()
{
	textureCache().removeFrameBufferTexture(m_pTexture);
	m_pTexture = nullptr;
}

void NoiseTexture::update()
{
	if (m_DList == dwnd().getBuffersSwapCount() || config.generalEmulation.enableNoise == 0)
		return;

	if (m_pTexture == nullptr)
		return;
	const u32 rand_value = Rand();
	m_offsetX = (rand_value & 0xFFFF) % NOISE_TEX_WIDTH;
	m_offsetY = (rand_value >> 16) % NOISE_TEX_HEIGHT;
	{
		Context::BindTextureParameters params;
		params.texture = m_pTexture->name;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.target = textureTarget::TEXTURE_2D;
		gfxContext.bindTexture(params);
	}
	m_DList = dwnd().getBuffersSwapCount();
}
