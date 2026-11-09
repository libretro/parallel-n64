#include <stdlib.h>
#include "Types.h"
#include "Config.h"
#include "gli_framebuffer.h"
#include "gli_textures.h"
#include "NoiseTexture.h"

#define NOISE_TEX_SIZE (NOISE_TEX_WIDTH * NOISE_TEX_HEIGHT)

static struct CachedTexture * l_pTexture;
static u32 l_DList;
static u32 l_offsetX, l_offsetY;

/* Picks the noise offset each frame; drawing thread only. */
static u32 Rand(void)
{
	static u32 state = 0x9E3779B9U;
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

/* xorshift64*, eight bytes per step, the same every run */
static void _fillTextureData(u8 * _data)
{
	u64 x = 0x9E3779B97F4A7C15ULL;
	u32 t = 0;
	while (t < NOISE_TEX_SIZE) {
		u64 r;
		u32 k;
		x ^= x >> 12;
		x ^= x << 25;
		x ^= x >> 27;
		r = x * 0x2545F4914F6CDD1DULL;
		for (k = 0; k < 8 && t < NOISE_TEX_SIZE; ++k, ++t) {
			_data[t] = (u8)r;
			r >>= 8;
		}
	}
}

/* One texture, read at a random offset that changes every frame. This used
 * to bind one of 30 textures each frame instead, which looks the same but
 * makes drivers that cache per-draw texture state (freedreno's a6xx
 * tex_cache, keyed by every bound view) keep 30 times as many entries; that
 * cache is never trimmed and is scanned linearly whenever a sampler view
 * dies, which made long sessions stutter.
 */
void gli_noise_init(void)
{
	u8 * data;

	if (config.generalEmulation.enableNoise == 0)
		return;

	/* the fill is cheap, so the data lives only for the upload */
	data = (u8*)malloc(NOISE_TEX_SIZE);
	if (data == NULL)
		return;
	_fillTextureData(data);
	l_pTexture = gli_tex_noise_create(NOISE_TEX_WIDTH, NOISE_TEX_HEIGHT, data);
	free(data);
}

void gli_noise_destroy(void)
{
	gli_tex_remove(l_pTexture);
	l_pTexture = NULL;
}

void gli_noise_update(void)
{
	u32 rand_value;

	if (l_DList == gli_window_swap_count() || config.generalEmulation.enableNoise == 0)
		return;

	if (l_pTexture == NULL)
		return;
	rand_value = Rand();
	l_offsetX = (rand_value & 0xFFFF) % NOISE_TEX_WIDTH;
	l_offsetY = (rand_value >> 16) % NOISE_TEX_HEIGHT;
	gli_tex_noise_bind(l_pTexture);
	l_DList = gli_window_swap_count();
}

u32 gli_noise_offset_x(void)
{
	return l_offsetX;
}

u32 gli_noise_offset_y(void)
{
	return l_offsetY;
}
