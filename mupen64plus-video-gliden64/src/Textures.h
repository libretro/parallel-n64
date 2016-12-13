#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>

#include <map>

#include "CRC.h"
#include "convert.h"

extern const GLuint g_noiseTexIndex;
extern const GLuint g_MSTex0Index;

typedef uint32_t (*GetTexelFunc)( uint64_t *src, uint16_t x, uint16_t i, uint8_t palette );

struct CachedTexture
{
	CachedTexture(GLuint _glName) : glName(_glName), max_level(0), frameBufferTexture(fbNone) {}

	GLuint	glName;
	uint32_t		crc;
//	float	fulS, fulT;
//	WORD	ulS, ulT, lrS, lrT;
	float	offsetS, offsetT;
	uint8_t		maskS, maskT;
	uint8_t		clampS, clampT;
	uint8_t		mirrorS, mirrorT;
	uint16_t		line;
	uint16_t		size;
	uint16_t		format;
	uint32_t		tMem;
	uint32_t		palette;
	uint16_t		width, height;			  // N64 width and height
	uint16_t		clampWidth, clampHeight;  // Size to clamp to
	uint16_t		realWidth, realHeight;	  // Actual texture size
	float		scaleS, scaleT;			  // Scale to map to 0.0-1.0
	float		shiftScaleS, shiftScaleT; // Scale to shift
	uint32_t		textureBytes;

	uint32_t		lastDList;
	uint32_t		address;
	uint8_t		max_level;
	enum {
		fbNone = 0,
		fbOneSample = 1,
		fbMultiSample = 2
	} frameBufferTexture;
};


struct TextureCache
{
	CachedTexture * current[2];

	void init();
	void destroy();
	CachedTexture * addFrameBufferTexture();
	void addFrameBufferTextureSize(uint32_t _size) {m_cachedBytes += _size;}
	void removeFrameBufferTexture(CachedTexture * _pTexture);
	void activateTexture(uint32_t _t, CachedTexture *_pTexture);
	void activateDummy(uint32_t _t);
	void activateMSDummy(uint32_t _t);
	void update(uint32_t _t);

	static TextureCache & get();

private:
	TextureCache() : m_pDummy(NULL), m_hits(0), m_misses(0), m_maxBytes(0), m_cachedBytes(0), m_curUnpackAlignment(4), m_toggleDumpTex(false)
	{
		current[0] = NULL;
		current[1] = NULL;
		CRC_BuildTable();
	}
	TextureCache(const TextureCache &);

	void _checkCacheSize();
	CachedTexture * _addTexture(uint32_t _crc32);
	void _load(uint32_t _tile, CachedTexture *_pTexture);
	bool _loadHiresTexture(uint32_t _tile, CachedTexture *_pTexture, uint64_t & _ricecrc);
	void _loadBackground(CachedTexture *pTexture);
	bool _loadHiresBackground(CachedTexture *_pTexture);
	void _updateBackground();
	void _clear();
	void _initDummyTexture(CachedTexture * _pDummy);
	void _getTextureDestData(CachedTexture& tmptex, uint32_t* pDest, GLuint glInternalFormat, GetTexelFunc GetTexel, uint16_t* pLine);

	typedef std::list<CachedTexture> Textures;
	typedef std::map<uint32_t, Textures::iterator> Texture_Locations;
	typedef std::map<uint32_t, CachedTexture> FBTextures;
	Textures m_textures;
	Texture_Locations m_lruTextureLocations;
	FBTextures m_fbTextures;
	CachedTexture * m_pDummy;
	CachedTexture * m_pMSDummy;
	uint32_t m_hits, m_misses;
	uint32_t m_maxBytes;
	uint32_t m_cachedBytes;
	GLint m_curUnpackAlignment;
	bool m_toggleDumpTex;
};

void getTextureShiftScale(uint32_t tile, const TextureCache & cache, float & shiftScaleS, float & shiftScaleT);

inline TextureCache & textureCache()
{
	return TextureCache::get();
}

inline uint32_t pow2( uint32_t dim )
{
	uint32_t i = 1;

	while (i < dim) i <<= 1;

	return i;
}

inline uint32_t powof( uint32_t dim )
{
	uint32_t num = 1;
	uint32_t i = 0;

	while (num < dim)
	{
		num <<= 1;
		i++;
	}

	return i;
}
#endif
