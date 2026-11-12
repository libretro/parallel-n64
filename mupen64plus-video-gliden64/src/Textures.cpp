#include <assert.h>
#include <stdint.h>
#include <memory.h>
#include <algorithm>
#include "Platform.h"
#include "Textures.h"
#include "GBI.h"
#include "RSP.h"
#include "RDP.h"
#include "gDP.h"
#include "gSP.h"
#include "N64.h"
#include "convert.h"
#include "FrameBuffer.h"
#include "Config.h"
#include "GLideNHQ/Ext_TxFilter.h"
#include "TextureFilterHandler.h"
#include "Graphics/Context.h"
#include "Graphics/Parameters.h"
#include "DisplayWindow.h"
#include "gli_textures.h"
#include "gli_fb_list.h"
#include "DepthBuffer.h"
#include "gli_texdecode.h"

using namespace std;
using namespace graphics;

static InternalColorFormatParam _texFormat(u32 _f)
{
	return _f == GLI_TEXF_RGBA8 ? internalcolorFormat::RGBA8 :
		(_f == GLI_TEXF_RGB5A1 ? internalcolorFormat::RGB5_A1 : internalcolorFormat::RGBA4);
}

static DatatypeParam _texType(u32 _t)
{
	return _t == GLI_TEXT_U8 ? datatype::UNSIGNED_BYTE :
		(_t == GLI_TEXT_5551 ? datatype::UNSIGNED_SHORT_5_5_5_1 : datatype::UNSIGNED_SHORT_4_4_4_4);
}

TextureCache & TextureCache::get() {
	static TextureCache cache;
	return cache;
}

void TextureCache::_initDummyTexture(CachedTexture * _pDummy)
{
	_pDummy->address = 0;
	_pDummy->clampS = 1;
	_pDummy->clampT = 1;
	_pDummy->clampWidth = 2;
	_pDummy->clampHeight = 2;
	_pDummy->crc = 0;
	_pDummy->format = 0;
	_pDummy->size = 0;
	_pDummy->frameBufferTexture = CachedTexture::fbNone;
	_pDummy->width = 2;
	_pDummy->height = 2;
	_pDummy->realWidth = 2;
	_pDummy->realHeight = 2;
	_pDummy->maskS = 0;
	_pDummy->maskT = 0;
	_pDummy->scaleS = 0.5f;
	_pDummy->scaleT = 0.5f;
	_pDummy->shiftScaleS = 1.0f;
	_pDummy->shiftScaleT = 1.0f;
	_pDummy->textureBytes = 2 * 2 * 4;
	_pDummy->tMem = 0;
}

/* N64 textures fit in 4 MB by a wide margin: one allocation, at load */
static const u32 DecodeScratchBytes = 4 * 1024 * 1024;

void TextureCache::init()
{
	m_curUnpackAlignment = 0;
	m_decodeScratch = (u8*)malloc(DecodeScratchBytes);
	if (m_pool == nullptr) {
		m_pool = new CachedTexture[m_maxCacheSize];
		gli_lru_init(&m_lru, (u32)m_maxCacheSize);
	}

	u32 dummyTexture[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	m_pDummy = addFrameBufferTexture(textureTarget::TEXTURE_2D); // we don't want to remove dummy texture
	_initDummyTexture(m_pDummy);

	Context::InitTextureParams params;
	params.handle = m_pDummy->name;
	params.mipMapLevel = 0;
	params.msaaLevel = 0;
	params.width = m_pDummy->realWidth;
	params.height = m_pDummy->realHeight;
	params.format = colorFormat::RGBA;
	params.internalFormat = gfxContext.convertInternalTextureFormat(u32(internalcolorFormat::RGBA8));
	params.dataType = datatype::UNSIGNED_BYTE;
	params.data = dummyTexture;
	gfxContext.init2DTexture(params);

	activateDummy( 0 );
	activateDummy(1);
	current[0] = current[1] = nullptr;


	m_pMSDummy = nullptr;
	if (config.video.multisampling != 0 && Context::Multisampling) {
		m_pMSDummy = addFrameBufferTexture(textureTarget::TEXTURE_2D_MULTISAMPLE); // we don't want to remove dummy texture
		_initDummyTexture(m_pMSDummy);

		Context::InitTextureParams msParams;
		msParams.handle = m_pMSDummy->name;
		msParams.mipMapLevel = 0;
		msParams.msaaLevel = config.video.multisampling;
		msParams.width = m_pMSDummy->realWidth;
		msParams.height = m_pMSDummy->realHeight;
		msParams.format = colorFormat::RGBA;
		msParams.internalFormat = gfxContext.convertInternalTextureFormat(u32(internalcolorFormat::RGBA8));
		msParams.dataType = datatype::UNSIGNED_BYTE;
		gfxContext.init2DTexture(msParams);

		activateMSDummy(0);
		activateMSDummy(1);
	}

	assert(!gfxContext.isError());
}

bool TextureCache::_decodedIsRead() const
{
	return (config.textureFilter.txHiresEnable != 0 && config.textureFilter.txDump != 0) ||
		(config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 ||
		(config.generalEmulation.hacks & hack_LoadDepthTextures) != 0;
}

/* Where to decode: lent GPU-visible memory when nothing reads it back,
 * otherwise the scratch. Only a texture past the scratch allocates. */
u32 * TextureCache::_takeDecode(u32 _bytes, bool _lend)
{
	if (_lend) {
		void * lent = gfxContext.lendTextureData(_bytes);
		if (lent != nullptr)
			return (u32*)lent;
	}
	if (_bytes <= DecodeScratchBytes && m_decodeScratch != nullptr)
		return (u32*)m_decodeScratch;
	m_lastHeapDecode = (u8*)malloc(_bytes);
	return (u32*)m_lastHeapDecode;
}

void TextureCache::_dropDecode(u32 * _p)
{
	if (_p == nullptr || (u8*)_p == m_decodeScratch)
		return;
	/* the renderer's own: nothing to give back */
	if ((u8*)_p != m_lastHeapDecode)
		return;
	free(_p);
}

void TextureCache::destroy()
{
	current[0] = current[1] = nullptr;
	free(m_decodeScratch);
	m_decodeScratch = nullptr;

	_deletePool();
	delete[] m_pool;
	m_pool = nullptr;
	gli_lru_deinit(&m_lru);

	/* the slots in use: the free stack marks the others */
	bool isFree[FBTextureSlots] = {};
	for (u32 i = 0; i < m_fbFreeCount; ++i)
		isFree[m_fbFree[i]] = true;
	for (u32 i = 0; i < FBTextureSlots; ++i)
		if (!isFree[i])
			gfxContext.deleteTexture(m_fbTextures[i].name);
	_resetFBTextureSlots();
}

void TextureCache::_resetFBTextureSlots()
{
	static_assert(FBTextureSlots >= 4 * GLI_FB_MAX + 5 * GLI_DEPTH_MAX + 16, "a frame buffer has four textures, a depth buffer five");
	static_assert(FBTextureSlots <= 0x10000, "slots are u16");
	for (u32 i = 0; i < FBTextureSlots; ++i) {
		m_fbTextures[i] = CachedTexture();
		/* taken from the top: slot 0 first */
		m_fbFree[i] = (u16)(FBTextureSlots - 1 - i);
	}
	m_fbFreeCount = FBTextureSlots;
}

/* Deletes every pooled GPU texture, cached or kept for reuse */
void TextureCache::_deletePool()
{
	if (m_pool == nullptr)
		return;
	for (size_t i = 0; i < m_maxCacheSize; ++i) {
		if (m_pool[i].name.isNotNull())
			gfxContext.deleteTexture(m_pool[i].name);
		m_pool[i] = CachedTexture();
	}
	gli_lru_reset(&m_lru);
}

CachedTexture * TextureCache::_addTexture(u64 _crc)
{
	if (m_curUnpackAlignment == 0)
		m_curUnpackAlignment = gfxContext.getTextureUnpackAlignment();
	const u32 slot = gli_lru_add(&m_lru, _crc);
	CachedTexture & tex = m_pool[slot];
	ObjectHandle name = tex.name;
	if (!name.isNotNull())
		name = gfxContext.createTexture(textureTarget::TEXTURE_2D);
	tex = CachedTexture(name);
	tex.crc = _crc;
	return &tex;
}

void TextureCache::removeFrameBufferTexture(CachedTexture * _pTexture)
{
	if (_pTexture == nullptr)
		return;
	gfxContext.deleteTexture(_pTexture->name);
	const uintptr_t offset = reinterpret_cast<uintptr_t>(_pTexture) - reinterpret_cast<uintptr_t>(m_fbTextures);
	if (offset >= sizeof(m_fbTextures)) {
		/* one past the slots, from the heap */
		delete _pTexture;
		return;
	}
	const u32 slot = (u32)(offset / sizeof(CachedTexture));
	m_fbTextures[slot] = CachedTexture();
	m_fbFree[m_fbFreeCount++] = (u16)slot;
}

CachedTexture * TextureCache::addFrameBufferTexture(graphics::Parameter _target)
{
	ObjectHandle texName(gfxContext.createTexture(_target));
	/* the slots cover every buffer alive at once; past them is a bug,
	 * kept working from the heap */
	assert(m_fbFreeCount != 0);
	if (m_fbFreeCount == 0)
		return new CachedTexture(texName);
	CachedTexture * pTexture = &m_fbTextures[m_fbFree[--m_fbFreeCount]];
	*pTexture = CachedTexture(texName);
	return pTexture;
}

inline
void _updateCachedTexture(const GHQTexInfo & _info, CachedTexture *_pTexture, f32 _scale)
{
	_pTexture->textureBytes = _info.width * _info.height;

	Parameter format(_info.format);
	if (format == internalcolorFormat::RGB8 ||
		format == internalcolorFormat::RGBA4 ||
		format == internalcolorFormat::RGB5_A1) {
		_pTexture->textureBytes <<= 1;
	}
	else {
		_pTexture->textureBytes <<= 2;
	}

	if (_pTexture->realWidth == _pTexture->width * 2)
		_pTexture->clampS = 0; // force wrap or mirror s
	if (_pTexture->realHeight == _pTexture->height * 2)
		_pTexture->clampT = 0; // force wrap or mirror t

	_pTexture->realWidth = _info.width;
	_pTexture->realHeight = _info.height;
	_pTexture->scaleS = _scale / f32(_info.width);
	_pTexture->scaleT = _scale / f32(_info.height);

	_pTexture->bHDTexture = true;
}

bool TextureCache::_loadHiresBackground(CachedTexture *_pTexture, u64 & _ricecrc)
{
	if (!gli_TFH_is_inited())
		return false;

	u8 * addr = (u8*)(RDRAM + gSP.bgImage.address);
	int tile_width = gSP.bgImage.width;
	int tile_height = gSP.bgImage.height;
	int bpl = tile_width << gSP.bgImage.size >> 1;

	u8 * paladdr = nullptr;
	u16 * palette = nullptr;
	if ((gSP.bgImage.size < G_IM_SIZ_16b) && (gDP.otherMode.textureLUT != G_TT_NONE || gSP.bgImage.format == G_IM_FMT_CI)) {
		if (gSP.bgImage.size == G_IM_SIZ_8b)
			paladdr = (u8*)(gDP.TexFilterPalette);
		else if (config.textureFilter.txHresAltCRC)
			paladdr = (u8*)(gDP.TexFilterPalette + (gSP.bgImage.palette << 5));
		else
			paladdr = (u8*)(gDP.TexFilterPalette + (gSP.bgImage.palette << 4));
		// TODO: fix palette load
		//			palette = (rdp.pal_8 + (gSP.textureTile[_t]->palette << 4));
	}

	_ricecrc = txfilter_checksum(addr, tile_width,
						tile_height, (unsigned short)(gSP.bgImage.format << 8 | gSP.bgImage.size),
						bpl, paladdr);
	GHQTexInfo ghqTexInfo;
	// TODO: fix problem with zero texture dimensions on GLideNHQ side.
	if (txfilter_hirestex(_pTexture->crc, _ricecrc, palette, &ghqTexInfo) &&
			ghqTexInfo.width != 0 && ghqTexInfo.height != 0) {
		ghqTexInfo.format = gfxContext.convertInternalTextureFormat(ghqTexInfo.format);
		Context::InitTextureParams params;
		params.handle = _pTexture->name;
		params.mipMapLevel = 0;
		params.msaaLevel = 0;
		params.width = ghqTexInfo.width;
		params.height = ghqTexInfo.height;
		params.format = ColorFormatParam(ghqTexInfo.texture_format);
		params.internalFormat = InternalColorFormatParam(ghqTexInfo.format);
		params.dataType = DatatypeParam(ghqTexInfo.pixel_type);
		params.data = ghqTexInfo.data;
		params.mutableStorage = true;
		gfxContext.init2DTexture(params);

		assert(!gfxContext.isError());
		_updateCachedTexture(ghqTexInfo, _pTexture, f32(ghqTexInfo.width) / f32(tile_width));
		return true;
	}
	return false;
}

void TextureCache::_loadBackground(CachedTexture *pTexture)
{
	u64 ricecrc = 0;
	if (_loadHiresBackground(pTexture, ricecrc))
		return;

	u32 *pDest = nullptr;

	u32 bpl;
	InternalColorFormatParam glInternalFormat;
	DatatypeParam glType;

	const gli_tex_format * loadParams =
			gli_tex_format_get(pTexture->format == 2 ? G_TT_RGBA16 : G_TT_NONE, pTexture->size, pTexture->format);
	const bool out32 = loadParams->autoFormat == GLI_TEXF_RGBA8;
	if (out32) {
		pTexture->textureBytes = (pTexture->realWidth * pTexture->realHeight) << 2;
		glInternalFormat = _texFormat(loadParams->internal32);
		glType = _texType(loadParams->type32);
	} else {
		pTexture->textureBytes = (pTexture->realWidth * pTexture->realHeight) << 1;
		glInternalFormat = _texFormat(loadParams->internal16);
		glType = _texType(loadParams->type16);
	}

	bpl = gSP.bgImage.width << gSP.bgImage.size >> 1;
	const bool lend = !_decodedIsRead();
	if (lend && pTexture->textureBytes > DecodeScratchBytes && m_decodeScratch != nullptr) {
		/* past the scratch, with nothing reading the image back: lent
		 * memory, or else strips of rows through the scratch, never a
		 * heap buffer */
		pDest = (u32*)gfxContext.lendTextureData(pTexture->textureBytes);
		if (pDest == nullptr) {
			_loadBackgroundStrips(pTexture, loadParams, out32, glInternalFormat, glType, bpl);
			return;
		}
	} else {
		pDest = _takeDecode(pTexture->textureBytes, lend);
	}
	if (pDest == nullptr)
		return;
	/* read straight out of RDRAM, only the rows shown: no unswapped copy */
	gli_tex_decode_bg(pTexture, loadParams, out32, gSP.bgImage.address, bpl, pDest);

	if ((config.generalEmulation.hacks&hack_LoadDepthTextures) != 0 && gDP.colorImage.address == gDP.depthImageAddress) {
		_loadDepthTexture(pTexture, (u16*)pDest);
		_dropDecode(pDest);
		return;
	}

	if (config.textureFilter.txHiresEnable != 0 &&
		config.textureFilter.txDump != 0) {
		txfilter_dmptx((u8*)pDest, pTexture->realWidth, pTexture->realHeight,
			pTexture->realWidth, (u16)u32(glInternalFormat),
			(unsigned short)(pTexture->format << 8 | pTexture->size),
			ricecrc);
	}

	bool bLoaded = false;
	if ((config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 &&
			config.textureFilter.txFilterIgnoreBG == 0 &&
			gli_TFH_is_inited()) {
		GHQTexInfo ghqTexInfo;
		if (txfilter_filter((u8*)pDest, pTexture->realWidth, pTexture->realHeight,
				(u16)u32(glInternalFormat), (uint64)pTexture->crc, &ghqTexInfo) != 0 &&
				ghqTexInfo.data != nullptr) {

			if (ghqTexInfo.width % 2 != 0 &&
				ghqTexInfo.format != u32(internalcolorFormat::RGBA8) &&
				m_curUnpackAlignment > 1)
				gfxContext.setTextureUnpackAlignment(2);

			ghqTexInfo.format = gfxContext.convertInternalTextureFormat(ghqTexInfo.format);
			Context::InitTextureParams params;
			params.handle = pTexture->name;
			params.mipMapLevel = 0;
			params.msaaLevel = 0;
			params.width = ghqTexInfo.width;
			params.height = ghqTexInfo.height;
			params.format = ColorFormatParam(ghqTexInfo.texture_format);
			params.internalFormat = InternalColorFormatParam(ghqTexInfo.format);
			params.dataType = DatatypeParam(ghqTexInfo.pixel_type);
			params.data = ghqTexInfo.data;
			params.mutableStorage = true;
			gfxContext.init2DTexture(params);
			_updateCachedTexture(ghqTexInfo, pTexture, f32(ghqTexInfo.width) / f32(pTexture->realWidth));
			bLoaded = true;
		}
	}
	if (!bLoaded) {
		if (pTexture->realWidth % 2 != 0 && glInternalFormat != internalcolorFormat::RGBA8)
			gfxContext.setTextureUnpackAlignment(2);
		Context::InitTextureParams params;
		params.handle = pTexture->name;
		params.mipMapLevel = 0;
		params.msaaLevel = 0;
		params.width = pTexture->realWidth;
		params.height = pTexture->realHeight;
		params.format = colorFormat::RGBA;
		params.internalFormat = gfxContext.convertInternalTextureFormat(u32(glInternalFormat));
		params.dataType = glType;
		params.data = pDest;
		params.mutableStorage = true;
		gfxContext.init2DTexture(params);
	}
	if (m_curUnpackAlignment > 1)
		gfxContext.setTextureUnpackAlignment(m_curUnpackAlignment);
	_dropDecode(pDest);
}

void TextureCache::_loadBackgroundStrips(CachedTexture *pTexture, const gli_tex_format * _loadParams, bool _out32,
	InternalColorFormatParam _internalFormat, DatatypeParam _type, u32 _bpl)
{
	const u32 pitch = (u32)pTexture->realWidth << (_out32 ? 2 : 1);
	const u32 stripRows = pitch != 0 ? DecodeScratchBytes / pitch : 0;
	if (stripRows == 0)
		return;

	if (pTexture->realWidth % 2 != 0 && _internalFormat != internalcolorFormat::RGBA8)
		gfxContext.setTextureUnpackAlignment(2);

	Context::InitTextureParams params;
	params.handle = pTexture->name;
	params.mipMapLevel = 0;
	params.msaaLevel = 0;
	params.width = pTexture->realWidth;
	params.height = pTexture->realHeight;
	params.format = colorFormat::RGBA;
	params.internalFormat = gfxContext.convertInternalTextureFormat(u32(_internalFormat));
	params.dataType = _type;
	params.data = nullptr;
	params.mutableStorage = true;
	gfxContext.init2DTexture(params);

	Context::UpdateTextureDataParams update;
	update.handle = pTexture->name;
	update.textureUnitIndex = textureIndices::Tex[0];
	update.width = pTexture->realWidth;
	update.format = colorFormat::RGBA;
	update.internalFormat = params.internalFormat;
	update.dataType = _type;
	update.data = m_decodeScratch;
	for (u32 y = 0; y < pTexture->realHeight; y += stripRows) {
		const u32 rows = std::min(stripRows, (u32)pTexture->realHeight - y);
		gli_tex_decode_bg_rows(pTexture, _loadParams, _out32, gSP.bgImage.address, _bpl, y, rows, m_decodeScratch);
		update.y = y;
		update.height = rows;
		gfxContext.update2DTexture(update);
	}

	if (m_curUnpackAlignment > 1)
		gfxContext.setTextureUnpackAlignment(m_curUnpackAlignment);
}

bool TextureCache::_loadHiresTexture(u32 _tile, CachedTexture *_pTexture, u64 & _ricecrc)
{
	if (config.textureFilter.txHiresEnable == 0 || !gli_TFH_is_inited())
		return false;

	gDPLoadTileInfo & info = gDP.loadInfo[_pTexture->tMem];

	int bpl;
	int width, height;
	u8 * addr = (u8*)(RDRAM + info.texAddress);
	if (info.loadType == LOADTYPE_TILE) {
		bpl = info.texWidth << info.size >> 1;
		addr += (info.ult * bpl) + (((info.uls << info.size) + 1) >> 1);

		width = min(info.width, info.texWidth);
		if (info.size > _pTexture->size)
			width <<= info.size - _pTexture->size;

		height = info.height;
		if ((config.generalEmulation.hacks & hack_MK64) != 0 && (height % 2) != 0)
			height--;
	} else {
		int tile_width = gDP.tiles[_tile].lrs - gDP.tiles[_tile].uls + 1;
		int tile_height = gDP.tiles[_tile].lrt - gDP.tiles[_tile].ult + 1;

		int mask_width = (gDP.tiles[_tile].masks == 0) ? (tile_width) : (1 << gDP.tiles[_tile].masks);
		int mask_height = (gDP.tiles[_tile].maskt == 0) ? (tile_height) : (1 << gDP.tiles[_tile].maskt);

		if ((gDP.tiles[_tile].clamps && tile_width <= 256))
			width = min(mask_width, tile_width);
		else
			width = mask_width;

		if ((gDP.tiles[_tile].clampt && tile_height <= 256) || (mask_height > 256))
			height = min(mask_height, tile_height);
		else
			height = mask_height;

		if (gSP.textureTile[_tile]->size == G_IM_SIZ_32b)
			bpl = gSP.textureTile[_tile]->line << 4;
		else if (info.dxt == 0)
			bpl = gSP.textureTile[_tile]->line << 3;
		else {
			u32 dxt = info.dxt;
			if (dxt > 1)
				dxt = gli_tex_reverse_dxt(dxt, info.width, _pTexture->width, _pTexture->size);
			bpl = dxt << 3;
		}
	}

	u8 * paladdr = nullptr;
	u16 * palette = nullptr;
	if ((_pTexture->size < G_IM_SIZ_16b) && (gDP.otherMode.textureLUT != G_TT_NONE || _pTexture->format == G_IM_FMT_CI)) {
		if (_pTexture->size == G_IM_SIZ_8b)
			paladdr = (u8*)(gDP.TexFilterPalette);
		else if (config.textureFilter.txHresAltCRC)
			paladdr = (u8*)(gDP.TexFilterPalette + (_pTexture->palette << 5));
		else
			paladdr = (u8*)(gDP.TexFilterPalette + (_pTexture->palette << 4));
		// TODO: fix palette load
		//			palette = (rdp.pal_8 + (gSP.textureTile[_t]->palette << 4));
	}

	_ricecrc = txfilter_checksum(addr, width, height, (unsigned short)(_pTexture->format << 8 | _pTexture->size), bpl, paladdr);
	GHQTexInfo ghqTexInfo;
	// TODO: fix problem with zero texture dimensions on GLideNHQ side.
	if (txfilter_hirestex(_pTexture->crc, _ricecrc, palette, &ghqTexInfo) &&
		ghqTexInfo.width != 0 && ghqTexInfo.height != 0) {
		ghqTexInfo.format = gfxContext.convertInternalTextureFormat(ghqTexInfo.format);
		Context::InitTextureParams params;
		params.handle = _pTexture->name;
		params.mipMapLevel = 0;
		params.msaaLevel = 0;
		params.width = ghqTexInfo.width;
		params.height = ghqTexInfo.height;
		params.internalFormat = InternalColorFormatParam(ghqTexInfo.format);
		params.format = ColorFormatParam(ghqTexInfo.texture_format);
		params.dataType = DatatypeParam(ghqTexInfo.pixel_type);
		params.data = ghqTexInfo.data;
		params.textureUnitIndex = textureIndices::Tex[_tile];
		params.mutableStorage = true;
		gfxContext.init2DTexture(params);
		assert(!gfxContext.isError());
		_updateCachedTexture(ghqTexInfo, _pTexture, f32(ghqTexInfo.width) / f32(width));
		return true;
	}

	return false;
}

void TextureCache::_loadDepthTexture(CachedTexture * _pTexture, u16* _pDest)
{
	if (!config.generalEmulation.enableFragmentDepthWrite)
		return;

	/* Decode the N64 depth words to linear before uploading them.
	 *
	 * The words are in the hardware exponent/mantissa form. Uploading them
	 * raw, divided by 65535, left the map in that non-linear space while the
	 * shader writes a linear gl_FragDepth for everything else, so the two only
	 * agreed at the extremes.
	 *
	 * Decoding here rather than converting the fragment depth through the ZLUT
	 * costs one pass per map instead of one lookup per fragment. The floats
	 * replace the words in the decode scratch, so nothing is allocated. */
	const u32 size = _pTexture->realWidth * _pTexture->realHeight;
	void * buf = _pDest;
	void * heap = nullptr;
	if ((u8*)_pDest != m_decodeScratch || size * sizeof(f32) > DecodeScratchBytes) {
		heap = malloc(size * sizeof(f32));
		if (heap == nullptr)
			return;
		memcpy(heap, _pDest, size * sizeof(u16));
		buf = heap;
	}
	const f32 * pDestFloat = gli_tex_depth_to_float(buf, size);

	Context::InitTextureParams params;
	params.handle = _pTexture->name;
	params.mipMapLevel = 0;
	params.msaaLevel = 0;
	params.width = _pTexture->realWidth;
	params.height = _pTexture->realHeight;
	params.internalFormat = internalcolorFormat::R16F;
	params.format = colorFormat::RED;
	params.dataType = datatype::FLOAT;
	params.data = pDestFloat;
	params.mutableStorage = true;
	gfxContext.init2DTexture(params);
	free(heap);
}

/*
 * Worker function for _load
*/
void TextureCache::_load(u32 _tile, CachedTexture *_pTexture)
{
	u64 ricecrc = 0;
	if (_loadHiresTexture(_tile, _pTexture, ricecrc))
		return;

	u32 *pDest;

	u16 line;
	InternalColorFormatParam glInternalFormat;
	DatatypeParam glType;
	u32 sizeShift;

	const gli_tex_format * loadParams =
			gli_tex_format_get(gDP.otherMode.textureLUT, _pTexture->size, _pTexture->format);
	const bool out32 = loadParams->autoFormat == GLI_TEXF_RGBA8;
	if (out32) {
		sizeShift = 2;
		_pTexture->textureBytes = (_pTexture->realWidth * _pTexture->realHeight) << sizeShift;
		glInternalFormat = _texFormat(loadParams->internal32);
		glType = _texType(loadParams->type32);
	} else {
		sizeShift = 1;
		_pTexture->textureBytes = (_pTexture->realWidth * _pTexture->realHeight) << sizeShift;
		glInternalFormat = _texFormat(loadParams->internal16);
		glType = _texType(loadParams->type16);
	}

	const bool lend = !_decodedIsRead();

	s32 mipLevel = 0;
	_pTexture->max_level = gli_tex_max_level(_pTexture, _tile);

	/* the level being decoded: the texture's plain state, stepped per mip level */
	gli_tex tmptex = *_pTexture;

	line = tmptex.line;

	while (true) {
		pDest = _takeDecode((tmptex.realWidth * tmptex.realHeight) << sizeShift, lend);
		if (pDest == nullptr)
			return;
		gli_tex_decode_tmem(&tmptex, loadParams, out32, gDP.otherMode.textureLUT, &line, pDest);

		if ((config.generalEmulation.hacks&hack_LoadDepthTextures) != 0 && gDP.colorImage.address == gDP.depthImageAddress) {
			_loadDepthTexture(_pTexture, (u16*)pDest);
			_dropDecode(pDest);
			return;
		}

		if (config.textureFilter.txHiresEnable != 0 &&
				config.textureFilter.txDump != 0) {
			txfilter_dmptx((u8*)pDest, tmptex.realWidth, tmptex.realHeight,
					tmptex.realWidth, (u16)u32(glInternalFormat),
					(unsigned short)(_pTexture->format << 8 | _pTexture->size),
					ricecrc);
		}

		bool bLoaded = false;
		bool needEnhance = (config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 &&
			_pTexture->max_level == 0 &&
			gli_TFH_is_inited();
		if (needEnhance && gli_tex_filter_skips())
			needEnhance = false;

		if (needEnhance) {
			GHQTexInfo ghqTexInfo;
			if (txfilter_filter((u8*)pDest, tmptex.realWidth, tmptex.realHeight,
							(u16)u32(glInternalFormat), (uint64)_pTexture->crc,
							&ghqTexInfo) != 0 && ghqTexInfo.data != nullptr) {
				if (ghqTexInfo.width % 2 != 0 &&
					ghqTexInfo.format != u32(internalcolorFormat::RGBA8) &&
					m_curUnpackAlignment > 1)
					gfxContext.setTextureUnpackAlignment(2);
				ghqTexInfo.format = gfxContext.convertInternalTextureFormat(ghqTexInfo.format);
				Context::InitTextureParams params;
				params.handle = _pTexture->name;
				params.textureUnitIndex = textureIndices::Tex[_tile];
				params.mipMapLevel = 0;
				params.msaaLevel = 0;
				params.width = ghqTexInfo.width;
				params.height = ghqTexInfo.height;
				params.internalFormat = InternalColorFormatParam(ghqTexInfo.format);
				params.format = ColorFormatParam(ghqTexInfo.texture_format);
				params.dataType = DatatypeParam(ghqTexInfo.pixel_type);
				params.data = ghqTexInfo.data;
				params.mutableStorage = true;
				gfxContext.init2DTexture(params);
				_updateCachedTexture(ghqTexInfo, _pTexture, f32(ghqTexInfo.width) / f32(tmptex.realWidth));
				bLoaded = true;
			}
		}
		if (!bLoaded) {
			if (tmptex.realWidth % 2 != 0 &&
				glInternalFormat != internalcolorFormat::RGBA8 &&
				m_curUnpackAlignment > 1)
				gfxContext.setTextureUnpackAlignment(2);
			Context::InitTextureParams params;
			params.handle = _pTexture->name;
			params.textureUnitIndex = textureIndices::Tex[_tile];
			params.mipMapLevel = mipLevel;
			params.mipMapLevels = _pTexture->max_level + 1;
			params.msaaLevel = 0;
			params.width = tmptex.realWidth;
			params.height = tmptex.realHeight;
			params.internalFormat = gfxContext.convertInternalTextureFormat(u32(glInternalFormat));
			params.format = colorFormat::RGBA;
			params.dataType = glType;
			params.data = pDest;
			params.mutableStorage = true;
			gfxContext.init2DTexture(params);
		}
		_dropDecode(pDest);
		if (mipLevel == _pTexture->max_level)
			break;
		++mipLevel;
		_pTexture->textureBytes += gli_tex_mip_step(&tmptex, mipLevel, &line, sizeShift);
	}
	if (m_curUnpackAlignment > 1)
		gfxContext.setTextureUnpackAlignment(m_curUnpackAlignment);
}

void TextureCache::activateTexture(u32 _t, CachedTexture *_pTexture)
{

	Context::TexParameters params;
	params.handle = _pTexture->name;
	if (config.video.multisampling > 0 && _pTexture->frameBufferTexture == CachedTexture::fbMultiSample) {
		params.target = textureTarget::TEXTURE_2D_MULTISAMPLE;
		params.textureUnitIndex = textureIndices::MSTex[_t];
	} else {
		params.target = textureTarget::TEXTURE_2D;
		params.textureUnitIndex = textureIndices::Tex[_t];

		const bool bUseBilinear = gDP.otherMode.textureFilter != G_TF_POINT && config.texture.bilinearMode != BILINEAR_3POINT;
		const bool bUseLOD = currentCombiner()->usesLOD();
		const s32 texLevel = bUseLOD ? _pTexture->max_level : 0;
		params.maxMipmapLevel = Parameter(texLevel);

		if (bUseLOD) {
			if (bUseBilinear) {
				// Apply standard bilinear to mipmap textures
				if (texLevel > 0)
					params.minFilter = textureParameters::FILTER_LINEAR_MIPMAP_NEAREST;
				else
					params.minFilter = textureParameters::FILTER_LINEAR;
				params.magFilter = textureParameters::FILTER_LINEAR;
			} else {
				if (texLevel > 0)
					params.minFilter = textureParameters::FILTER_NEAREST_MIPMAP_NEAREST;
				else
					params.minFilter = textureParameters::FILTER_NEAREST;
				params.magFilter = textureParameters::FILTER_NEAREST;
			}
		} else { // Don't use texture filter. Texture will be filtered by filter shader
			params.minFilter = textureParameters::FILTER_NEAREST;
			params.magFilter = textureParameters::FILTER_NEAREST;
		}

		// Set clamping modes
		params.wrapS = _pTexture->clampS ? textureParameters::WRAP_CLAMP_TO_EDGE :
			_pTexture->mirrorS ? textureParameters::WRAP_MIRRORED_REPEAT
			: textureParameters::WRAP_REPEAT;
		params.wrapT = _pTexture->clampT ? textureParameters::WRAP_CLAMP_TO_EDGE :
			_pTexture->mirrorT ? textureParameters::WRAP_MIRRORED_REPEAT
			: textureParameters::WRAP_REPEAT;

		if (dwnd().getDrawer().getDrawingState() == DrawingState::Triangle && config.texture.maxAnisotropyF > 0.0f)
			params.maxAnisotropy = Parameter(config.texture.maxAnisotropyF);
	}

	gfxContext.setTextureParameters(params);

	current[_t] = _pTexture;
}

void TextureCache::activateDummy(u32 _t)
{
	Context::TexParameters params;
	params.handle = m_pDummy->name;
	params.target = textureTarget::TEXTURE_2D;
	params.textureUnitIndex = textureIndices::Tex[_t];
	params.minFilter = textureParameters::FILTER_NEAREST;
	params.magFilter = textureParameters::FILTER_NEAREST;
	gfxContext.setTextureParameters(params);
}

void TextureCache::activateMSDummy(u32 _t)
{
	Context::TexParameters params;
	params.handle = m_pMSDummy->name;
	params.target = textureTarget::TEXTURE_2D_MULTISAMPLE;
	params.textureUnitIndex = textureIndices::MSTex[_t];
	gfxContext.setTextureParameters(params);
}

void TextureCache::_updateBackground()
{
	u32 numBytes = gSP.bgImage.width * gSP.bgImage.height << gSP.bgImage.size >> 1;
	u64 crc;

	crc = CRC_Calculate( UINT64_MAX, &RDRAM[gSP.bgImage.address], numBytes );

	if (gDP.otherMode.textureLUT != G_TT_NONE || gSP.bgImage.format == G_IM_FMT_CI) {
		if (gSP.bgImage.size == G_IM_SIZ_4b)
			crc = CRC_Calculate( crc, &gDP.paletteCRC16[gSP.bgImage.palette], sizeof(u64) );
		else if (gSP.bgImage.size == G_IM_SIZ_8b)
			crc = CRC_Calculate( crc, &gDP.paletteCRC256, sizeof(u64) );
	}

	u32 params[4] = {gSP.bgImage.width, gSP.bgImage.height, gSP.bgImage.format, gSP.bgImage.size};
	crc = CRC_Calculate(crc, params, sizeof(u32)*4);

	const u32 slot = gli_lru_find(&m_lru, crc);
	if (slot != GLI_LRU_NONE) {
		CachedTexture & currentTex = m_pool[slot];
		gli_lru_touch(&m_lru, slot);

		assert(currentTex.width == gSP.bgImage.width);
		assert(currentTex.height == gSP.bgImage.height);
		assert(currentTex.format == gSP.bgImage.format);
		assert(currentTex.size == gSP.bgImage.size);
		currentTex.clampS = gSP.bgImage.clampS;
		currentTex.clampT = gSP.bgImage.clampT;

		activateTexture(0, &currentTex);
		m_hits++;
		return;
	}

	m_misses++;

	CachedTexture * pCurrent = _addTexture(crc);

	pCurrent->address = gSP.bgImage.address;

	pCurrent->format = gSP.bgImage.format;
	pCurrent->size = gSP.bgImage.size;

	pCurrent->width = gSP.bgImage.width;
	pCurrent->height = gSP.bgImage.height;

	pCurrent->clampWidth = gSP.bgImage.width;
	pCurrent->clampHeight = gSP.bgImage.height;
	pCurrent->palette = gSP.bgImage.palette;
	pCurrent->maskS = 0;
	pCurrent->maskT = 0;
	pCurrent->mirrorS = 0;
	pCurrent->mirrorT = 0;
	pCurrent->clampS = gSP.bgImage.clampS;
	pCurrent->clampT = gSP.bgImage.clampT;
	pCurrent->line = 0;
	pCurrent->tMem = 0;
	pCurrent->frameBufferTexture = CachedTexture::fbNone;

	pCurrent->realWidth = gSP.bgImage.width;
	pCurrent->realHeight = gSP.bgImage.height;

	pCurrent->scaleS = 1.0f / (f32)(pCurrent->realWidth);
	pCurrent->scaleT = 1.0f / (f32)(pCurrent->realHeight);

	pCurrent->shiftScaleS = 1.0f;
	pCurrent->shiftScaleT = 1.0f;

	pCurrent->offsetS = 0.5f;
	pCurrent->offsetT = 0.5f;

	_loadBackground(pCurrent);
	activateTexture(0, pCurrent);

	current[0] = pCurrent;
}

void TextureCache::_clear()
{
	current[0] = current[1] = nullptr;
	_deletePool();
}

void TextureCache::update(u32 _t)
{
	const gDPTile * pTile = gSP.textureTile[_t];
	switch (pTile->textureMode) {
	case TEXTUREMODE_BGIMAGE:
		_updateBackground();
		return;
	case TEXTUREMODE_FRAMEBUFFER:
		FrameBuffer_ActivateBufferTexture( _t, pTile->frameBufferAddress );
		return;
	case TEXTUREMODE_FRAMEBUFFER_BG:
		FrameBuffer_ActivateBufferTextureBG( _t, pTile->frameBufferAddress );
		return;
	}

	if (gDP.otherMode.textureLOD == G_TL_LOD && gSP.texture.level == 0 && !currentCombiner()->usesLOD() && _t == 1) {
		current[1] = current[0];
		if (current[1] != nullptr) {
			activateTexture(1, current[1]);
			return;
		}
	}

	if (gSP.texture.tile == 7 &&
		_t == 0 &&
		gSP.textureTile[0] == gDP.loadTile &&
		gDP.loadTile->loadType == LOADTYPE_BLOCK &&
		gSP.textureTile[0]->tmem == gSP.textureTile[1]->tmem) {
		gSP.textureTile[0] = gSP.textureTile[1];
		pTile = gSP.textureTile[_t];
	}

	gli_tile_sizes sizes;
	gli_tex_tile_sizes(_t, &sizes, gDP.loadTile);
	const u64 crc = gli_tex_crc(_t, &sizes);

	if (current[_t] != nullptr && current[_t]->crc == crc) {
		activateTexture(_t, current[_t]);
		return;
	}

	const u32 slot = gli_lru_find(&m_lru, crc);
	if (slot != GLI_LRU_NONE) {
		CachedTexture & currentTex = m_pool[slot];

		if (currentTex.width == sizes.width && currentTex.height == sizes.height) {
			gli_lru_touch(&m_lru, slot);

			assert(currentTex.format == pTile->format);
			assert(currentTex.size == pTile->size);

			activateTexture(_t, &currentTex);
			m_hits++;
			return;
		}

		/* the slot keeps its GPU texture for the next one */
		gli_lru_remove(&m_lru, slot);
	}

	m_misses++;

	CachedTexture * pCurrent = _addTexture(crc);

	gli_tex_from_tile(pCurrent, pTile, &sizes);

	_load(_t, pCurrent);
	activateTexture( _t, pCurrent );

	current[_t] = pCurrent;
}

void getTextureShiftScale(u32 t, const TextureCache & cache, f32 & shiftScaleS, f32 & shiftScaleT)
{
	if (gSP.textureTile[t]->textureMode != TEXTUREMODE_NORMAL) {
		shiftScaleS = cache.current[t]->shiftScaleS;
		shiftScaleT = cache.current[t]->shiftScaleT;
		return;
	}

	if (gDP.otherMode.textureLOD == G_TL_LOD && gSP.texture.level == 0 && !currentCombiner()->usesLOD())
		t = 0;

	if (gSP.textureTile[t]->shifts > 10)
		shiftScaleS = (f32)(1 << (16 - gSP.textureTile[t]->shifts));
	else if (gSP.textureTile[t]->shifts > 0)
		shiftScaleS /= (f32)(1 << gSP.textureTile[t]->shifts);

	if (gSP.textureTile[t]->shiftt > 10)
		shiftScaleT = (f32)(1 << (16 - gSP.textureTile[t]->shiftt));
	else if (gSP.textureTile[t]->shiftt > 0)
		shiftScaleT /= (f32)(1 << gSP.textureTile[t]->shiftt);
}

bool gli_tex_integer_supported(void)
{
	return Context::IntegerTextures;
}

static CachedTexture * _createFrameBufferTexture(u16 _format, u32 _width, u32 _height, u32 _bytes)
{
	CachedTexture * pTexture = textureCache().addFrameBufferTexture(textureTarget::TEXTURE_2D);
	pTexture->format = _format;
	pTexture->clampS = 1;
	pTexture->clampT = 1;
	pTexture->frameBufferTexture = CachedTexture::fbOneSample;
	pTexture->maskS = 0;
	pTexture->maskT = 0;
	pTexture->mirrorS = 0;
	pTexture->mirrorT = 0;
	pTexture->width = _width;
	pTexture->height = _height;
	pTexture->textureBytes = pTexture->width * pTexture->height * _bytes;
	return pTexture;
}

static TextureUnitParam _lutUnit(u32 _unit)
{
	return _unit == GLI_TEX_UNIT_ZLUT ? textureIndices::ZLUTTex : textureIndices::PaletteTex;
}

CachedTexture * gli_tex_lut_create(u32 _width, u32 _height, u32 _unit, const void * _data)
{
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	CachedTexture * pTexture = _createFrameBufferTexture(G_IM_FMT_IA, _width, _height, fbTexFormats.lutFormatBytes);

	Context::InitTextureParams initParams;
	initParams.handle = pTexture->name;
	initParams.width = pTexture->width;
	initParams.height = pTexture->height;
	initParams.internalFormat = fbTexFormats.lutInternalFormat;
	initParams.format = fbTexFormats.lutFormat;
	initParams.dataType = fbTexFormats.lutType;
	initParams.data = _data;
	gfxContext.init2DTexture(initParams);

	Context::TexParameters setParams;
	setParams.handle = pTexture->name;
	setParams.target = textureTarget::TEXTURE_2D;
	setParams.textureUnitIndex = _lutUnit(_unit);
	setParams.minFilter = textureParameters::FILTER_NEAREST;
	setParams.magFilter = textureParameters::FILTER_NEAREST;
	setParams.wrapS = textureParameters::WRAP_CLAMP_TO_EDGE;
	setParams.wrapT = textureParameters::WRAP_CLAMP_TO_EDGE;
	gfxContext.setTextureParameters(setParams);
	return pTexture;
}

void gli_tex_lut_update(CachedTexture * _pTexture, u32 _unit, const void * _data)
{
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	Context::UpdateTextureDataParams params;
	params.handle = _pTexture->name;
	params.textureUnitIndex = _lutUnit(_unit);
	params.width = _pTexture->width;
	params.height = _pTexture->height;
	params.format = fbTexFormats.lutFormat;
	params.internalFormat = fbTexFormats.lutInternalFormat;
	params.dataType = fbTexFormats.lutType;
	params.data = _data;
	gfxContext.update2DTexture(params);
}

CachedTexture * gli_tex_noise_create(u32 _width, u32 _height, const void * _data)
{
	CachedTexture * pTexture = _createFrameBufferTexture(G_IM_FMT_RGBA, _width, _height, 1);
	const FramebufferTextureFormats & fbTexFormats = gfxContext.getFramebufferTextureFormats();
	{
		Context::InitTextureParams params;
		params.handle = pTexture->name;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.width = pTexture->width;
		params.height = pTexture->height;
		params.internalFormat = fbTexFormats.noiseInternalFormat;
		params.format = fbTexFormats.noiseFormat;
		params.dataType = fbTexFormats.noiseType;
		params.data = _data;
		gfxContext.init2DTexture(params);
	}
	{
		Context::TexParameters params;
		params.handle = pTexture->name;
		params.target = textureTarget::TEXTURE_2D;
		params.textureUnitIndex = textureIndices::NoiseTex;
		params.minFilter = textureParameters::FILTER_NEAREST;
		params.magFilter = textureParameters::FILTER_NEAREST;
		gfxContext.setTextureParameters(params);
	}
	return pTexture;
}

void gli_tex_noise_bind(CachedTexture * _pTexture)
{
	Context::BindTextureParameters params;
	params.texture = _pTexture->name;
	params.textureUnitIndex = textureIndices::NoiseTex;
	params.target = textureTarget::TEXTURE_2D;
	gfxContext.bindTexture(params);
}

void gli_tex_remove(CachedTexture * _pTexture)
{
	textureCache().removeFrameBufferTexture(_pTexture);
}
