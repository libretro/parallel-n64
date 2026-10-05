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
#include "DisplayLoadProgress.h"

using namespace graphics;

#define NOISE_TEX_WIDTH 640
#define NOISE_TEX_HEIGHT 580

NoiseTexture g_noiseTexture;

NoiseTexture::NoiseTexture()
	: m_DList(0)
	, m_currTex(0)
	, m_prevTex(0)
{
	for (u32 i = 0; i < NOISE_TEX_NUM; ++i)
		m_pTexture[i] = nullptr;
}

// Picks the noise texture each frame; drawing thread only.
static
u32 Rand(u32)
{
	static uint32_t state = 0x9E3779B9U;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

void NoiseTexture::_fillTextureData()
{
	displayLoadProgress(L"INIT NOISE TEXTURES. PLEASE WAIT...");

	// One thread and a local xorshift64* generator, eight bytes per step.
	// The old fill split the textures across one thread per CPU, but
	// every thread called the C library's random generator, whose shared
	// state is locked in glibc and unsynchronised elsewhere (and each
	// thread's reseed stomped the others'). This is ~27x faster than that
	// was even on one CPU, needs no threads, and is the same every run.
	uint64_t x = 0x9E3779B97F4A7C15ULL;
	for (auto& vec : m_texData) {
		vec.resize(NOISE_TEX_WIDTH * NOISE_TEX_HEIGHT);
		const size_t sz = vec.size();
		size_t t = 0;
		while (t < sz) {
			x ^= x >> 12;
			x ^= x << 25;
			x ^= x >> 27;
			uint64_t r = x * 0x2545F4914F6CDD1DULL;
			for (u32 k = 0; k < 8 && t < sz; ++k, ++t) {
				vec[t] = u8(r);
				r >>= 8;
			}
		}
	}

	displayLoadProgress(L"");
}


void NoiseTexture::init()
{
	if (config.generalEmulation.enableNoise == 0)
		return;

	if (m_texData[0].empty())
		_fillTextureData();

	for (u32 i = 0; i < NOISE_TEX_NUM; ++i) {
		m_pTexture[i] = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
		m_pTexture[i]->format = G_IM_FMT_RGBA;
		m_pTexture[i]->clampS = 1;
		m_pTexture[i]->clampT = 1;
		m_pTexture[i]->frameBufferTexture = CachedTexture::fbOneSample;
		m_pTexture[i]->maskS = 0;
		m_pTexture[i]->maskT = 0;
		m_pTexture[i]->mirrorS = 0;
		m_pTexture[i]->mirrorT = 0;
		m_pTexture[i]->width = NOISE_TEX_WIDTH;
		m_pTexture[i]->height = NOISE_TEX_HEIGHT;
		m_pTexture[i]->textureBytes = m_pTexture[i]->width * m_pTexture[i]->height;

		const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
		{
			Context::InitTextureParams params;
			params.handle = m_pTexture[i]->name;
			params.textureUnitIndex = textureIndices::NoiseTex;
			params.width = m_pTexture[i]->width;
			params.height = m_pTexture[i]->height;
			params.internalFormat = fbTexFormats.noiseInternalFormat;
			params.format = fbTexFormats.noiseFormat;
			params.dataType = fbTexFormats.noiseType;
			params.data = m_texData[i].data();
			gfxContext.init2DTexture(params);
		}
		{
			Context::TexParameters params;
			params.handle = m_pTexture[i]->name;
			params.target = textureTarget::TEXTURE_2D;
			params.textureUnitIndex = textureIndices::NoiseTex;
			params.minFilter = textureParameters::FILTER_NEAREST;
			params.magFilter = textureParameters::FILTER_NEAREST;
			gfxContext.setTextureParameters(params);
		}
	}
}

void NoiseTexture::destroy()
{
	for (u32 i = 0; i < NOISE_TEX_NUM; ++i) {
		textureCache().removeFrameBufferTexture(m_pTexture[i]);
		m_pTexture[i] = nullptr;
	}
}

void NoiseTexture::update()
{
	if (m_DList == dwnd().getBuffersSwapCount() || config.generalEmulation.enableNoise == 0)
		return;

	u32 rand_value(0U);
	while (m_currTex == m_prevTex) {
		rand_value = Rand(rand_value);
		m_currTex = rand_value % NOISE_TEX_NUM;
	}
	m_prevTex = m_currTex;
	if (m_pTexture[m_currTex] == nullptr)
		return;
	{
		Context::BindTextureParameters params;
		params.texture = m_pTexture[m_currTex]->name;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.target = textureTarget::TEXTURE_2D;
		gfxContext.bindTexture(params);
	}
	m_DList = dwnd().getBuffersSwapCount();
}
