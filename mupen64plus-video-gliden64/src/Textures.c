/* GLideN64's texture cache (gli_textures.h). C89.
 *
 * The C++ kept it in a singleton class found again on every draw. Here it
 * is one file's state: a pool of textures by key, made at init, and fixed
 * slots for the frame buffers', so nothing is allocated as they come and go. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "gli_textures.h"
#include "gli_tex.h"
#include "gli_texlru.h"
#include "gli_texdecode.h"
#include "gli_fb_list.h"
#include "gli_drawer.h"
#include "Graphics/gli_context.h"
#include "CRC.h"
#include "CombinerKey.h"
#include "Config.h"
#include "DepthBuffer.h"
#include "DisplayWindow.h"
#include "GBI.h"
#include "N64.h"
#include "RDP.h"
#include "RSP.h"
#include "gDP.h"
#include "gSP.h"
#include "TextureFilterHandler.h"
#include "GLideNHQ/Ext_TxFilter.h"

#ifdef VC
#define MAX_CACHE 1500
#else
#define MAX_CACHE 8000
#endif

/* Room for every frame buffer and depth buffer alive at once with all their
 * textures, and the few others */
#define FB_SLOTS (4 * 128 + 5 * 64 + 64)

/* N64 textures fit in 4 MB by a wide margin: one allocation, at load */
#define DECODE_SCRATCH_BYTES (4 * 1024 * 1024)

typedef char fb_slots_cover_buffers[FB_SLOTS >= 4 * GLI_FB_MAX + 5 * GLI_DEPTH_MAX + 16 ? 1 : -1];
typedef char fb_slots_fit_u16[FB_SLOTS <= 0x10000 ? 1 : -1];

gli_tex *gli_texture_current[2];

/* The cached textures live in a pool sized at init; the LRU index maps keys
 * to its slots. An evicted or dropped slot keeps its GPU texture for the next
 * one: deleting one costs ~160us on a tiled GPU. */
static gli_tex *pool;
static gli_lru lru;
static gli_tex fbTextures[FB_SLOTS];
static u16 fbFree[FB_SLOTS];
static u32 fbFreeCount;
static bool fbSlotsMade;
static gli_tex *dummy;
static gli_tex *msDummy;
static s32 unpackAlignment;
static u8 *decodeScratch;
static u8 *lastHeapDecode;

static u32 tex_format(u32 _f)
{
	return _f == GLI_TEXF_RGBA8 ? GLI_ICF_RGBA8 : (_f == GLI_TEXF_RGB5A1 ? GLI_ICF_RGB5_A1 : GLI_ICF_RGBA4);
}

static u32 tex_type(u32 _t)
{
	return _t == GLI_TEXT_U8 ? GLI_DT_UNSIGNED_BYTE :
		(_t == GLI_TEXT_5551 ? GLI_DT_UNSIGNED_SHORT_5_5_5_1 : GLI_DT_UNSIGNED_SHORT_4_4_4_4);
}

/* a texture of no state, of the GPU texture @_name */
static void tex_reset(gli_tex *_tex, u32 _name)
{
	memset(_tex, 0, sizeof(*_tex));
	_tex->name = _name;
}

#define init_texture(params) gli_ctx.ops->init2DTexture(gli_ctx.impl, (params))
#define set_unpack(a) gli_ctx.ops->setTextureUnpackAlignment(gli_ctx.impl, (a))
#define convert_format(f) gli_ctx.ops->convertInternalTextureFormat(gli_ctx.impl, (f))

static void reset_fb_slots(void)
{
	u32 i;
	for (i = 0; i < FB_SLOTS; ++i) {
		tex_reset(&fbTextures[i], 0);
		/* taken from the top: slot 0 first */
		fbFree[i] = (u16)(FB_SLOTS - 1 - i);
	}
	fbFreeCount = FB_SLOTS;
	fbSlotsMade = 1;
}

gli_tex *gli_textures_add_fb(u32 _target)
{
	const u32 name = gli_ctx.ops->createTexture(gli_ctx.impl, _target);
	gli_tex *tex;
	if (!fbSlotsMade)
		reset_fb_slots();
	/* the slots cover every buffer alive at once; past them is a bug, kept
	 * working from the heap */
	assert(fbFreeCount != 0);
	if (fbFreeCount == 0) {
		tex = (gli_tex *)calloc(1, sizeof(*tex));
		if (tex != NULL)
			tex->name = name;
		return tex;
	}
	tex = &fbTextures[fbFree[--fbFreeCount]];
	tex_reset(tex, name);
	return tex;
}

void gli_textures_remove_fb(gli_tex *_tex)
{
	size_t slot;
	if (_tex == NULL)
		return;
	gli_ctx.ops->deleteTexture(gli_ctx.impl, _tex->name);
	if (_tex < fbTextures || _tex >= fbTextures + FB_SLOTS) {
		/* one past the slots, from the heap */
		free(_tex);
		return;
	}
	slot = (size_t)(_tex - fbTextures);
	tex_reset(_tex, 0);
	fbFree[fbFreeCount++] = (u16)slot;
}

static void init_dummy(gli_tex *_dummy)
{
	_dummy->address = 0;
	_dummy->clampS = 1;
	_dummy->clampT = 1;
	_dummy->clampWidth = 2;
	_dummy->clampHeight = 2;
	_dummy->crc = 0;
	_dummy->format = 0;
	_dummy->size = 0;
	_dummy->frameBufferTexture = GLI_TEX_FB_NONE;
	_dummy->width = 2;
	_dummy->height = 2;
	_dummy->realWidth = 2;
	_dummy->realHeight = 2;
	_dummy->maskS = 0;
	_dummy->maskT = 0;
	_dummy->scaleS = 0.5f;
	_dummy->scaleT = 0.5f;
	_dummy->shiftScaleS = 1.0f;
	_dummy->shiftScaleT = 1.0f;
	_dummy->textureBytes = 2 * 2 * 4;
	_dummy->tMem = 0;
}

void gli_textures_init(void)
{
	static const u32 dummyTexture[16] = { 0 };
	CRC_Init();
	unpackAlignment = 0;
	decodeScratch = (u8 *)malloc(DECODE_SCRATCH_BYTES);
	if (pool == NULL) {
		pool = (gli_tex *)calloc(MAX_CACHE, sizeof(*pool));
		gli_lru_init(&lru, MAX_CACHE);
	}

	dummy = gli_textures_add_fb(GLI_TT_TEXTURE_2D); /* we don't want to remove dummy texture */
	init_dummy(dummy);
	{
		gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
		params.handle = dummy->name;
		params.width = dummy->realWidth;
		params.height = dummy->realHeight;
		params.format = GLI_CF_RGBA;
		params.internalFormat = convert_format(GLI_ICF_RGBA8);
		params.dataType = GLI_DT_UNSIGNED_BYTE;
		params.data = dummyTexture;
		init_texture(&params);
	}

	gli_textures_activate_dummy(0);
	gli_textures_activate_dummy(1);
	gli_texture_current[0] = gli_texture_current[1] = NULL;

	msDummy = NULL;
	if (config.video.multisampling != 0 && gli_ctx.multisampling) {
		gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
		msDummy = gli_textures_add_fb(GLI_TT_TEXTURE_2D_MULTISAMPLE); /* we don't want to remove dummy texture */
		init_dummy(msDummy);
		params.handle = msDummy->name;
		params.msaaLevel = config.video.multisampling;
		params.width = msDummy->realWidth;
		params.height = msDummy->realHeight;
		params.format = GLI_CF_RGBA;
		params.internalFormat = convert_format(GLI_ICF_RGBA8);
		params.dataType = GLI_DT_UNSIGNED_BYTE;
		init_texture(&params);

		gli_textures_activate_ms_dummy(0);
		gli_textures_activate_ms_dummy(1);
	}

	assert(!gli_ctx.ops->isError(gli_ctx.impl));
}

static bool decoded_is_read(void)
{
	return (config.textureFilter.txHiresEnable != 0 && config.textureFilter.txDump != 0) ||
		(config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 ||
		(config.generalEmulation.hacks & hack_LoadDepthTextures) != 0;
}

/* Where to decode: lent GPU-visible memory when nothing reads it back,
 * otherwise the scratch. Only a texture past the scratch allocates. */
static u32 *take_decode(u32 _bytes, bool _lend)
{
	if (_lend) {
		void *lent = gli_ctx.ops->lendTextureData(gli_ctx.impl, _bytes);
		if (lent != NULL)
			return (u32 *)lent;
	}
	if (_bytes <= DECODE_SCRATCH_BYTES && decodeScratch != NULL)
		return (u32 *)decodeScratch;
	lastHeapDecode = (u8 *)malloc(_bytes);
	return (u32 *)lastHeapDecode;
}

static void drop_decode(u32 *_p)
{
	if (_p == NULL || (u8 *)_p == decodeScratch)
		return;
	/* the renderer's own: nothing to give back */
	if ((u8 *)_p != lastHeapDecode)
		return;
	free(_p);
}

/* Deletes every pooled GPU texture, cached or kept for reuse */
static void delete_pool(void)
{
	u32 i;
	if (pool == NULL)
		return;
	for (i = 0; i < MAX_CACHE; ++i) {
		if (pool[i].name != 0)
			gli_ctx.ops->deleteTexture(gli_ctx.impl, pool[i].name);
		tex_reset(&pool[i], 0);
	}
	gli_lru_reset(&lru);
}

void gli_textures_destroy(void)
{
	static bool isFree[FB_SLOTS];
	u32 i;
	gli_texture_current[0] = gli_texture_current[1] = NULL;
	free(decodeScratch);
	decodeScratch = NULL;

	delete_pool();
	free(pool);
	pool = NULL;
	gli_lru_deinit(&lru);

	if (!fbSlotsMade)
		return;
	/* the slots in use: the free stack marks the others */
	memset(isFree, 0, sizeof(isFree));
	for (i = 0; i < fbFreeCount; ++i)
		isFree[fbFree[i]] = 1;
	for (i = 0; i < FB_SLOTS; ++i)
		if (!isFree[i])
			gli_ctx.ops->deleteTexture(gli_ctx.impl, fbTextures[i].name);
	reset_fb_slots();
}

static gli_tex *add_texture(u64 _crc)
{
	u32 slot, name;
	gli_tex *tex;
	if (unpackAlignment == 0)
		unpackAlignment = gli_ctx.ops->getTextureUnpackAlignment(gli_ctx.impl);
	slot = gli_lru_add(&lru, _crc);
	tex = &pool[slot];
	name = tex->name;
	if (name == 0)
		name = gli_ctx.ops->createTexture(gli_ctx.impl, GLI_TT_TEXTURE_2D);
	tex_reset(tex, name);
	tex->crc = _crc;
	return tex;
}

static void update_cached(const GHQTexInfo *_info, gli_tex *_tex, f32 _scale)
{
	_tex->textureBytes = _info->width * _info->height;

	if (_info->format == GLI_ICF_RGB8 || _info->format == GLI_ICF_RGBA4 || _info->format == GLI_ICF_RGB5_A1)
		_tex->textureBytes <<= 1;
	else
		_tex->textureBytes <<= 2;

	if (_tex->realWidth == _tex->width * 2)
		_tex->clampS = 0; /* force wrap or mirror s */
	if (_tex->realHeight == _tex->height * 2)
		_tex->clampT = 0; /* force wrap or mirror t */

	_tex->realWidth = _info->width;
	_tex->realHeight = _info->height;
	_tex->scaleS = _scale / (f32)_info->width;
	_tex->scaleT = _scale / (f32)_info->height;

	_tex->bHDTexture = 1;
}

/* a replacement or filtered texture GLideNHQ made, as @_tex's level 0 */
static void init_from_info(gli_tex *_tex, GHQTexInfo *_info, u32 _unit)
{
	gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	_info->format = convert_format(_info->format);
	params.handle = _tex->name;
	if (_unit != GLI_PARAM_INVALID)
		params.textureUnitIndex = _unit;
	params.width = _info->width;
	params.height = _info->height;
	params.format = _info->texture_format;
	params.internalFormat = _info->format;
	params.dataType = _info->pixel_type;
	params.data = _info->data;
	params.mutableStorage = 1;
	init_texture(&params);
}

/* the palette GLideNHQ hashes a texture of @_size and @_format with */
static u8 *filter_palette(u32 _size, u32 _format, u32 _palette)
{
	if (_size < G_IM_SIZ_16b && (gDP.otherMode.textureLUT != G_TT_NONE || _format == G_IM_FMT_CI)) {
		if (_size == G_IM_SIZ_8b)
			return (u8 *)(gDP.TexFilterPalette);
		if (config.textureFilter.txHresAltCRC)
			return (u8 *)(gDP.TexFilterPalette + (_palette << 5));
		return (u8 *)(gDP.TexFilterPalette + (_palette << 4));
	}
	/* TODO: fix palette load: no palette is handed to GLideNHQ */
	return NULL;
}

static bool load_hires_background(gli_tex *_tex, u64 *_ricecrc)
{
	u8 *addr;
	int tile_width, tile_height, bpl;
	GHQTexInfo info;
	if (!gli_TFH_is_inited())
		return 0;

	addr = (u8 *)(RDRAM + gSP.bgImage.address);
	tile_width = gSP.bgImage.width;
	tile_height = gSP.bgImage.height;
	bpl = tile_width << gSP.bgImage.size >> 1;

	*_ricecrc = txfilter_checksum(addr, tile_width, tile_height,
		(unsigned short)(gSP.bgImage.format << 8 | gSP.bgImage.size), bpl,
		filter_palette(gSP.bgImage.size, gSP.bgImage.format, gSP.bgImage.palette));
	memset(&info, 0, sizeof(info));
	/* TODO: fix problem with zero texture dimensions on GLideNHQ side. */
	if (txfilter_hirestex(_tex->crc, *_ricecrc, NULL, &info) && info.width != 0 && info.height != 0) {
		init_from_info(_tex, &info, GLI_PARAM_INVALID);
		assert(!gli_ctx.ops->isError(gli_ctx.impl));
		update_cached(&info, _tex, (f32)info.width / (f32)tile_width);
		return 1;
	}
	return 0;
}

static void load_depth_texture(gli_tex *_tex, u16 *_dest)
{
	gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
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
	 * costs one pass per map instead of one lookup per fragment. Each word
	 * becomes the half float the R16F map holds, in its place, so nothing is
	 * allocated or copied, and the upload is the texels as they are: floats
	 * went into the Vulkan map two bytes a texel, as stripes. */
	gli_tex_depth_to_half(_dest, (u32)_tex->realWidth * _tex->realHeight);

	params.handle = _tex->name;
	params.width = _tex->realWidth;
	params.height = _tex->realHeight;
	params.internalFormat = GLI_ICF_R16F;
	params.format = GLI_CF_RED;
	params.dataType = GLI_DT_HALF_FLOAT;
	params.data = _dest;
	params.mutableStorage = 1;
	init_texture(&params);
}

/* the decoded image, unfiltered, as level @_level of @_tex */
static void init_decoded(const gli_tex *_tex, u32 _unit, u32 _level, u32 _levels,
	u32 _width, u32 _height, u32 _internal, u32 _type, const void *_data)
{
	gli_init_texture_params params = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	params.handle = _tex->name;
	if (_unit != GLI_PARAM_INVALID)
		params.textureUnitIndex = _unit;
	params.mipMapLevel = _level;
	params.mipMapLevels = _levels;
	params.width = _width;
	params.height = _height;
	params.format = GLI_CF_RGBA;
	params.internalFormat = convert_format(_internal);
	params.dataType = _type;
	params.data = _data;
	params.mutableStorage = 1;
	init_texture(&params);
}

static void load_background_strips(gli_tex *_tex, const gli_tex_format *_load, bool _out32,
	u32 _internal, u32 _type, u32 _bpl)
{
	const u32 pitch = (u32)_tex->realWidth << (_out32 ? 2 : 1);
	const u32 stripRows = pitch != 0 ? DECODE_SCRATCH_BYTES / pitch : 0;
	gli_update_texture_params update = GLI_UPDATE_TEXTURE_PARAMS_DEFAULT;
	u32 y;
	if (stripRows == 0)
		return;

	if (_tex->realWidth % 2 != 0 && _internal != GLI_ICF_RGBA8)
		set_unpack(2);

	init_decoded(_tex, GLI_PARAM_INVALID, 0, 1, _tex->realWidth, _tex->realHeight, _internal, _type, NULL);

	update.handle = _tex->name;
	update.textureUnitIndex = GLI_TU_TEX0;
	update.width = _tex->realWidth;
	update.format = GLI_CF_RGBA;
	update.internalFormat = convert_format(_internal);
	update.dataType = _type;
	update.data = decodeScratch;
	for (y = 0; y < _tex->realHeight; y += stripRows) {
		const u32 left = (u32)_tex->realHeight - y;
		const u32 rows = stripRows < left ? stripRows : left;
		gli_tex_decode_bg_rows(_tex, _load, _out32, gSP.bgImage.address, _bpl, y, rows, decodeScratch);
		update.y = y;
		update.height = rows;
		gli_ctx.ops->update2DTexture(gli_ctx.impl, &update);
	}

	if (unpackAlignment > 1)
		set_unpack(unpackAlignment);
}

static void load_background(gli_tex *_tex)
{
	u64 ricecrc = 0;
	u32 *dest;
	u32 bpl, internal, type;
	const gli_tex_format *load;
	bool out32, lend, loaded = 0;

	if (load_hires_background(_tex, &ricecrc))
		return;

	load = gli_tex_format_get(_tex->format == 2 ? G_TT_RGBA16 : G_TT_NONE, _tex->size, _tex->format);
	out32 = load->autoFormat == GLI_TEXF_RGBA8;
	if (out32) {
		_tex->textureBytes = ((u32)_tex->realWidth * _tex->realHeight) << 2;
		internal = tex_format(load->internal32);
		type = tex_type(load->type32);
	} else {
		_tex->textureBytes = ((u32)_tex->realWidth * _tex->realHeight) << 1;
		internal = tex_format(load->internal16);
		type = tex_type(load->type16);
	}

	bpl = gSP.bgImage.width << gSP.bgImage.size >> 1;
	lend = !decoded_is_read();
	if (lend && _tex->textureBytes > DECODE_SCRATCH_BYTES && decodeScratch != NULL) {
		/* past the scratch, with nothing reading the image back: lent
		 * memory, or else strips of rows through the scratch, never a heap
		 * buffer */
		dest = (u32 *)gli_ctx.ops->lendTextureData(gli_ctx.impl, _tex->textureBytes);
		if (dest == NULL) {
			load_background_strips(_tex, load, out32, internal, type, bpl);
			return;
		}
	} else {
		dest = take_decode(_tex->textureBytes, lend);
	}
	if (dest == NULL)
		return;
	/* read straight out of RDRAM, only the rows shown: no unswapped copy */
	gli_tex_decode_bg(_tex, load, out32, gSP.bgImage.address, bpl, dest);

	if ((config.generalEmulation.hacks & hack_LoadDepthTextures) != 0 && gDP.colorImage.address == gDP.depthImageAddress) {
		load_depth_texture(_tex, (u16 *)dest);
		drop_decode(dest);
		return;
	}

	if (config.textureFilter.txHiresEnable != 0 && config.textureFilter.txDump != 0)
		txfilter_dmptx((u8 *)dest, _tex->realWidth, _tex->realHeight, _tex->realWidth, (u16)internal,
			(unsigned short)(_tex->format << 8 | _tex->size), ricecrc);

	if ((config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 &&
			config.textureFilter.txFilterIgnoreBG == 0 && gli_TFH_is_inited()) {
		GHQTexInfo info;
		memset(&info, 0, sizeof(info));
		if (txfilter_filter((u8 *)dest, _tex->realWidth, _tex->realHeight, (u16)internal,
				(uint64)_tex->crc, &info) != 0 && info.data != NULL) {
			if (info.width % 2 != 0 && info.format != GLI_ICF_RGBA8 && unpackAlignment > 1)
				set_unpack(2);
			init_from_info(_tex, &info, GLI_PARAM_INVALID);
			update_cached(&info, _tex, (f32)info.width / (f32)_tex->realWidth);
			loaded = 1;
		}
	}
	if (!loaded) {
		if (_tex->realWidth % 2 != 0 && internal != GLI_ICF_RGBA8)
			set_unpack(2);
		init_decoded(_tex, GLI_PARAM_INVALID, 0, 1, _tex->realWidth, _tex->realHeight, internal, type, dest);
	}
	if (unpackAlignment > 1)
		set_unpack(unpackAlignment);
	drop_decode(dest);
}

static bool load_hires_texture(u32 _tile, gli_tex *_tex, u64 *_ricecrc)
{
	gDPLoadTileInfo *info;
	int bpl, width, height;
	u8 *addr;
	GHQTexInfo ghq;
	if (config.textureFilter.txHiresEnable == 0 || !gli_TFH_is_inited())
		return 0;

	info = &gDP.loadInfo[_tex->tMem];
	addr = (u8 *)(RDRAM + info->texAddress);
	if (info->loadType == LOADTYPE_TILE) {
		bpl = info->texWidth << info->size >> 1;
		addr += (info->ult * bpl) + (((info->uls << info->size) + 1) >> 1);

		width = info->width < info->texWidth ? info->width : info->texWidth;
		if (info->size > _tex->size)
			width <<= info->size - _tex->size;

		height = info->height;
		if ((config.generalEmulation.hacks & hack_MK64) != 0 && (height % 2) != 0)
			height--;
	} else {
		const gDPTile *tile = &gDP.tiles[_tile];
		const int tile_width = tile->lrs - tile->uls + 1;
		const int tile_height = tile->lrt - tile->ult + 1;
		const int mask_width = (tile->masks == 0) ? (tile_width) : (1 << tile->masks);
		const int mask_height = (tile->maskt == 0) ? (tile_height) : (1 << tile->maskt);

		if (tile->clamps && tile_width <= 256)
			width = mask_width < tile_width ? mask_width : tile_width;
		else
			width = mask_width;

		if ((tile->clampt && tile_height <= 256) || (mask_height > 256))
			height = mask_height < tile_height ? mask_height : tile_height;
		else
			height = mask_height;

		if (gSP.textureTile[_tile]->size == G_IM_SIZ_32b)
			bpl = gSP.textureTile[_tile]->line << 4;
		else if (info->dxt == 0)
			bpl = gSP.textureTile[_tile]->line << 3;
		else {
			u32 dxt = info->dxt;
			if (dxt > 1)
				dxt = gli_tex_reverse_dxt(dxt, info->width, _tex->width, _tex->size);
			bpl = dxt << 3;
		}
	}

	*_ricecrc = txfilter_checksum(addr, width, height, (unsigned short)(_tex->format << 8 | _tex->size), bpl,
		filter_palette(_tex->size, _tex->format, _tex->palette));
	memset(&ghq, 0, sizeof(ghq));
	/* TODO: fix problem with zero texture dimensions on GLideNHQ side. */
	if (txfilter_hirestex(_tex->crc, *_ricecrc, NULL, &ghq) && ghq.width != 0 && ghq.height != 0) {
		init_from_info(_tex, &ghq, GLI_TU_TEX0 + _tile);
		assert(!gli_ctx.ops->isError(gli_ctx.impl));
		update_cached(&ghq, _tex, (f32)ghq.width / (f32)width);
		return 1;
	}
	return 0;
}

static void load(u32 _tile, gli_tex *_tex)
{
	u64 ricecrc = 0;
	u32 *dest;
	u16 line;
	u32 internal, type, sizeShift;
	const gli_tex_format *fmt;
	bool out32, lend;
	u32 mipLevel = 0;
	/* the level being decoded: the texture's plain state, stepped per mip level */
	gli_tex level;

	if (load_hires_texture(_tile, _tex, &ricecrc))
		return;

	fmt = gli_tex_format_get(gDP.otherMode.textureLUT, _tex->size, _tex->format);
	out32 = fmt->autoFormat == GLI_TEXF_RGBA8;
	if (out32) {
		sizeShift = 2;
		internal = tex_format(fmt->internal32);
		type = tex_type(fmt->type32);
	} else {
		sizeShift = 1;
		internal = tex_format(fmt->internal16);
		type = tex_type(fmt->type16);
	}
	_tex->textureBytes = ((u32)_tex->realWidth * _tex->realHeight) << sizeShift;

	lend = !decoded_is_read();
	_tex->max_level = gli_tex_max_level(_tex, _tile);
	level = *_tex;
	line = level.line;

	for (;;) {
		bool loaded = 0, enhance;
		dest = take_decode(((u32)level.realWidth * level.realHeight) << sizeShift, lend);
		if (dest == NULL)
			return;
		gli_tex_decode_tmem(&level, fmt, out32, gDP.otherMode.textureLUT, &line, dest);

		if ((config.generalEmulation.hacks & hack_LoadDepthTextures) != 0 && gDP.colorImage.address == gDP.depthImageAddress) {
			load_depth_texture(_tex, (u16 *)dest);
			drop_decode(dest);
			return;
		}

		if (config.textureFilter.txHiresEnable != 0 && config.textureFilter.txDump != 0)
			txfilter_dmptx((u8 *)dest, level.realWidth, level.realHeight, level.realWidth, (u16)internal,
				(unsigned short)(_tex->format << 8 | _tex->size), ricecrc);

		enhance = (config.textureFilter.txEnhancementMode | config.textureFilter.txFilterMode) != 0 &&
			_tex->max_level == 0 && gli_TFH_is_inited();
		if (enhance && gli_tex_filter_skips())
			enhance = 0;

		if (enhance) {
			GHQTexInfo info;
			memset(&info, 0, sizeof(info));
			if (txfilter_filter((u8 *)dest, level.realWidth, level.realHeight, (u16)internal,
					(uint64)_tex->crc, &info) != 0 && info.data != NULL) {
				if (info.width % 2 != 0 && info.format != GLI_ICF_RGBA8 && unpackAlignment > 1)
					set_unpack(2);
				init_from_info(_tex, &info, GLI_TU_TEX0 + _tile);
				update_cached(&info, _tex, (f32)info.width / (f32)level.realWidth);
				loaded = 1;
			}
		}
		if (!loaded) {
			if (level.realWidth % 2 != 0 && internal != GLI_ICF_RGBA8 && unpackAlignment > 1)
				set_unpack(2);
			init_decoded(_tex, GLI_TU_TEX0 + _tile, mipLevel, (u32)_tex->max_level + 1,
				level.realWidth, level.realHeight, internal, type, dest);
		}
		drop_decode(dest);
		if (mipLevel == _tex->max_level)
			break;
		++mipLevel;
		_tex->textureBytes += gli_tex_mip_step(&level, mipLevel, &line, sizeShift);
	}
	if (unpackAlignment > 1)
		set_unpack(unpackAlignment);
}

void gli_textures_activate(u32 _t, gli_tex *_tex)
{
	gli_tex_parameters params = GLI_TEX_PARAMETERS_DEFAULT;
	params.handle = _tex->name;
	if (config.video.multisampling > 0 && _tex->frameBufferTexture == GLI_TEX_FB_MULTI_SAMPLE) {
		params.target = GLI_TT_TEXTURE_2D_MULTISAMPLE;
		params.textureUnitIndex = GLI_TU_MSTEX0 + _t;
	} else {
		static const u32 filters[] = {
			GLI_TP_FILTER_NEAREST, GLI_TP_FILTER_LINEAR,
			GLI_TP_FILTER_NEAREST_MIPMAP_NEAREST, GLI_TP_FILTER_LINEAR_MIPMAP_NEAREST };
		static const u32 wraps[] = { GLI_TP_WRAP_REPEAT, GLI_TP_WRAP_MIRRORED_REPEAT, GLI_TP_WRAP_CLAMP_TO_EDGE };
		gli_tex_sampler sampler;
		params.target = GLI_TT_TEXTURE_2D;
		params.textureUnitIndex = GLI_TU_TEX0 + _t;
		gli_tex_sampler_get(_tex, gli_combiner_uses_lod(), &sampler);
		params.maxMipmapLevel = (u32)sampler.maxLevel;
		params.minFilter = filters[sampler.minFilter];
		params.magFilter = filters[sampler.magFilter];
		params.wrapS = wraps[sampler.wrapS];
		params.wrapT = wraps[sampler.wrapT];

		if (config.texture.maxAnisotropyF > 0.0f && gli_drawer_is_triangle())
			params.maxAnisotropy = config.texture.maxAnisotropyF;
	}

	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &params);

	gli_texture_current[_t] = _tex;
}

void gli_textures_activate_dummy(u32 _t)
{
	gli_tex_parameters params = GLI_TEX_PARAMETERS_DEFAULT;
	params.handle = dummy->name;
	params.target = GLI_TT_TEXTURE_2D;
	params.textureUnitIndex = GLI_TU_TEX0 + _t;
	params.minFilter = GLI_TP_FILTER_NEAREST;
	params.magFilter = GLI_TP_FILTER_NEAREST;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &params);
}

void gli_textures_activate_ms_dummy(u32 _t)
{
	gli_tex_parameters params = GLI_TEX_PARAMETERS_DEFAULT;
	params.handle = msDummy->name;
	params.target = GLI_TT_TEXTURE_2D_MULTISAMPLE;
	params.textureUnitIndex = GLI_TU_MSTEX0 + _t;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &params);
}

static void update_background(void)
{
	const u64 crc = gli_tex_bg_crc();
	const u32 slot = gli_lru_find(&lru, crc);
	gli_tex *tex;
	if (slot != GLI_LRU_NONE) {
		tex = &pool[slot];
		gli_lru_touch(&lru, slot);

		assert(tex->width == gSP.bgImage.width);
		assert(tex->height == gSP.bgImage.height);
		assert(tex->format == gSP.bgImage.format);
		assert(tex->size == gSP.bgImage.size);
		tex->clampS = gSP.bgImage.clampS;
		tex->clampT = gSP.bgImage.clampT;

		gli_textures_activate(0, tex);
		return;
	}

	tex = add_texture(crc);
	gli_tex_from_bg(tex);

	load_background(tex);
	gli_textures_activate(0, tex);
}

void gli_textures_update(u32 _t)
{
	const gDPTile *tile = gSP.textureTile[_t];
	gli_tile_sizes sizes;
	gli_tex *tex;
	u64 crc;
	u32 slot;
	switch (tile->textureMode) {
	case TEXTUREMODE_BGIMAGE:
		update_background();
		return;
	case TEXTUREMODE_FRAMEBUFFER:
		gli_fb_activate_texture(_t, tile->frameBufferAddress);
		return;
	case TEXTUREMODE_FRAMEBUFFER_BG:
		gli_fb_activate_texture_bg(_t, tile->frameBufferAddress);
		return;
	}

	if (gDP.otherMode.textureLOD == G_TL_LOD && gSP.texture.level == 0 && _t == 1 && !gli_combiner_uses_lod()) {
		gli_texture_current[1] = gli_texture_current[0];
		if (gli_texture_current[1] != NULL) {
			gli_textures_activate(1, gli_texture_current[1]);
			return;
		}
	}

	if (gSP.texture.tile == 7 &&
		_t == 0 &&
		gSP.textureTile[0] == gDP.loadTile &&
		gDP.loadTile->loadType == LOADTYPE_BLOCK &&
		gSP.textureTile[0]->tmem == gSP.textureTile[1]->tmem) {
		gSP.textureTile[0] = gSP.textureTile[1];
		tile = gSP.textureTile[_t];
	}

	gli_tex_tile_sizes(_t, &sizes, gDP.loadTile);
	crc = gli_tex_crc(_t, &sizes);

	if (gli_texture_current[_t] != NULL && gli_texture_current[_t]->crc == crc) {
		gli_textures_activate(_t, gli_texture_current[_t]);
		return;
	}

	slot = gli_lru_find(&lru, crc);
	if (slot != GLI_LRU_NONE) {
		tex = &pool[slot];
		if (tex->width == sizes.width && tex->height == sizes.height) {
			gli_lru_touch(&lru, slot);

			assert(tex->format == tile->format);
			assert(tex->size == tile->size);

			gli_textures_activate(_t, tex);
			return;
		}

		/* the slot keeps its GPU texture for the next one */
		gli_lru_remove(&lru, slot);
	}

	tex = add_texture(crc);
	gli_tex_from_tile(tex, tile, &sizes);

	load(_t, tex);
	gli_textures_activate(_t, tex);
}

void gli_textures_current(const gli_tex *_cur[2])
{
	_cur[0] = gli_texture_current[0];
	_cur[1] = gli_texture_current[1];
}

bool gli_tex_integer_supported(void)
{
	return gli_ctx.integerTextures;
}

static gli_tex *create_fb_texture(u16 _format, u32 _width, u32 _height, u32 _bytes)
{
	gli_tex *tex = gli_textures_add_fb(GLI_TT_TEXTURE_2D);
	tex->format = _format;
	tex->clampS = 1;
	tex->clampT = 1;
	tex->frameBufferTexture = GLI_TEX_FB_ONE_SAMPLE;
	tex->maskS = 0;
	tex->maskT = 0;
	tex->mirrorS = 0;
	tex->mirrorT = 0;
	tex->width = (u16)_width;
	tex->height = (u16)_height;
	tex->textureBytes = (u32)tex->width * tex->height * _bytes;
	return tex;
}

static u32 lut_unit(u32 _unit)
{
	return _unit == GLI_TEX_UNIT_ZLUT ? GLI_TU_ZLUTTex : GLI_TU_PaletteTex;
}

gli_tex *gli_tex_lut_create(u32 _width, u32 _height, u32 _unit, const void *_data)
{
	const gli_fb_formats *f = &gli_ctx.formats;
	gli_tex *tex = create_fb_texture(G_IM_FMT_IA, _width, _height, f->lutFormatBytes);
	gli_init_texture_params init = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters set = GLI_TEX_PARAMETERS_DEFAULT;

	init.handle = tex->name;
	init.width = tex->width;
	init.height = tex->height;
	init.internalFormat = f->lutInternalFormat;
	init.format = f->lutFormat;
	init.dataType = f->lutType;
	init.data = _data;
	init_texture(&init);

	set.handle = tex->name;
	set.target = GLI_TT_TEXTURE_2D;
	set.textureUnitIndex = lut_unit(_unit);
	set.minFilter = GLI_TP_FILTER_NEAREST;
	set.magFilter = GLI_TP_FILTER_NEAREST;
	set.wrapS = GLI_TP_WRAP_CLAMP_TO_EDGE;
	set.wrapT = GLI_TP_WRAP_CLAMP_TO_EDGE;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &set);
	return tex;
}

void gli_tex_lut_update(gli_tex *_tex, u32 _unit, const void *_data)
{
	const gli_fb_formats *f = &gli_ctx.formats;
	gli_update_texture_params params = GLI_UPDATE_TEXTURE_PARAMS_DEFAULT;
	params.handle = _tex->name;
	params.textureUnitIndex = lut_unit(_unit);
	params.width = _tex->width;
	params.height = _tex->height;
	params.format = f->lutFormat;
	params.internalFormat = f->lutInternalFormat;
	params.dataType = f->lutType;
	params.data = _data;
	gli_ctx.ops->update2DTexture(gli_ctx.impl, &params);
}

gli_tex *gli_tex_noise_create(u32 _width, u32 _height, const void *_data)
{
	const gli_fb_formats *f = &gli_ctx.formats;
	gli_tex *tex = create_fb_texture(G_IM_FMT_RGBA, _width, _height, 1);
	gli_init_texture_params init = GLI_INIT_TEXTURE_PARAMS_DEFAULT;
	gli_tex_parameters set = GLI_TEX_PARAMETERS_DEFAULT;

	init.handle = tex->name;
	init.textureUnitIndex = GLI_TU_NoiseTex;
	init.width = tex->width;
	init.height = tex->height;
	init.internalFormat = f->noiseInternalFormat;
	init.format = f->noiseFormat;
	init.dataType = f->noiseType;
	init.data = _data;
	init_texture(&init);

	set.handle = tex->name;
	set.target = GLI_TT_TEXTURE_2D;
	set.textureUnitIndex = GLI_TU_NoiseTex;
	set.minFilter = GLI_TP_FILTER_NEAREST;
	set.magFilter = GLI_TP_FILTER_NEAREST;
	gli_ctx.ops->setTextureParameters(gli_ctx.impl, &set);
	return tex;
}

void gli_tex_noise_bind(gli_tex *_tex)
{
	gli_bind_texture_params params = GLI_BIND_TEXTURE_PARAMS_DEFAULT;
	params.texture = _tex->name;
	params.textureUnitIndex = GLI_TU_NoiseTex;
	params.target = GLI_TT_TEXTURE_2D;
	gli_ctx.ops->bindTexture(gli_ctx.impl, &params);
}

void gli_tex_remove(gli_tex *_tex)
{
	gli_textures_remove_fb(_tex);
}
