#ifndef TEXTURES_H
#define TEXTURES_H

#include <list>
#include <vector>

#include <stddef.h>
#include <string.h>

#include "CRC.h"
#include "gli_texlru.h"
#include "gli_tex.h"
#include "convert.h"
#include "Graphics/ObjectHandle.h"
#include "Graphics/Parameter.h"

struct CachedTexture : gli_tex
{
	enum {
		fbNone = GLI_TEX_FB_NONE,
		fbOneSample = GLI_TEX_FB_ONE_SAMPLE,
		fbMultiSample = GLI_TEX_FB_MULTI_SAMPLE
	};

	CachedTexture() { memset(static_cast<gli_tex *>(this), 0, sizeof(gli_tex)); }
	CachedTexture(graphics::ObjectHandle _name) : name(_name) { memset(static_cast<gli_tex *>(this), 0, sizeof(gli_tex)); }

	graphics::ObjectHandle name;
};


struct TextureCache
{
	CachedTexture * current[2];

	void init();
	void destroy();
	CachedTexture * addFrameBufferTexture(graphics::Parameter _target);
	void removeFrameBufferTexture(CachedTexture * _pTexture);
	void activateTexture(u32 _t, CachedTexture *_pTexture);
	void activateDummy(u32 _t);
	void activateMSDummy(u32 _t);
	void update(u32 _t);

	static TextureCache & get();

private:
	TextureCache()
		: m_pool(nullptr)
		, m_pDummy(nullptr)
		, m_pMSDummy(nullptr)
		, m_hits(0)
		, m_misses(0)
		, m_curUnpackAlignment(4)
		, m_decodeScratch(nullptr)
		, m_lastHeapDecode(nullptr)
	{
		current[0] = nullptr;
		current[1] = nullptr;
		_resetFBTextureSlots();
		CRC_Init();
	}
	TextureCache(const TextureCache &) = delete;

	CachedTexture * _addTexture(u64 _crc);
	void _load(u32 _tile, CachedTexture *_pTexture);
	bool _loadHiresTexture(u32 _tile, CachedTexture *_pTexture, u64 & _ricecrc);
	void _loadBackground(CachedTexture *pTexture);
	bool _loadHiresBackground(CachedTexture *_pTexture, u64 & _ricecrc);
	void _loadDepthTexture(CachedTexture * _pTexture, u16* _pDest);
	void _updateBackground();
	void _clear();
	void _initDummyTexture(CachedTexture * _pDummy);

	/* The cached textures live in a pool sized at init; the LRU index maps
	 * keys to its slots. An evicted or dropped slot keeps its GPU texture
	 * for the next one: deleting one costs ~160us on a tiled GPU. */
	CachedTexture * m_pool;
	gli_lru m_lru;
	/* The frame buffer, depth buffer and other uncached textures: fixed
	 * slots and a stack of the free ones, so nothing is allocated as frame
	 * buffers come and go. Room for every frame buffer and depth buffer
	 * alive at once with all their textures, and the few others. */
	enum { FBTextureSlots = 4 * 128 + 5 * 64 + 64 };
	CachedTexture m_fbTextures[FBTextureSlots];
	u16 m_fbFree[FBTextureSlots];
	u32 m_fbFreeCount;
	void _resetFBTextureSlots();
	CachedTexture * m_pDummy;
	CachedTexture * m_pMSDummy;
	u32 m_hits, m_misses;
	s32 m_curUnpackAlignment;
	u8 * m_decodeScratch;
	u8 * m_lastHeapDecode;

	bool _decodedIsRead() const;
	u32 * _takeDecode(u32 _bytes, bool _lend);
	void _dropDecode(u32 * _p);
	void _deletePool();
#ifdef VC
	const size_t m_maxCacheSize = 1500;
#else
	const size_t m_maxCacheSize = 8000;
#endif
};

void getTextureShiftScale(u32 tile, const TextureCache & cache, f32 & shiftScaleS, f32 & shiftScaleT);

inline TextureCache & textureCache()
{
	return TextureCache::get();
}

#endif
