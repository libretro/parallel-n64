#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "GLideNHQ/Ext_TxFilter.h"
#include "osal/osal_files.h"
#include "RSP.h"
#include "Config.h"
#include "gli_framebuffer.h"
#include "TextureFilterHandler.h"

static u32 l_inited;
static u32 l_options;

static const u32 textureFilters[] = {
	NO_FILTER, /* "None" */
	SMOOTH_FILTER_1, /* "Smooth filtering 1" */
	SMOOTH_FILTER_2, /* "Smooth filtering 2" */
	SMOOTH_FILTER_3, /* "Smooth filtering 3" */
	SMOOTH_FILTER_4, /* "Smooth filtering 4" */
	SHARP_FILTER_1, /* "Sharp filtering 1" */
	SHARP_FILTER_2, /* "Sharp filtering 2" */
};

static const u32 textureEnhancements[] = {
	NO_ENHANCEMENT, /* "None" */
	NO_ENHANCEMENT, /* "Store" */
	X2_ENHANCEMENT, /* "X2" */
	X2SAI_ENHANCEMENT, /* "X2SAI" */
	HQ2X_ENHANCEMENT, /* "HQ2X" */
	HQ2XS_ENHANCEMENT, /* "HQ2XS" */
	LQ2X_ENHANCEMENT, /* "LQ2X" */
	LQ2XS_ENHANCEMENT, /* "LQ2XS" */
	HQ4X_ENHANCEMENT, /* "HQ4X" */
	BRZ2X_ENHANCEMENT, /* "2XBRZ" */
	BRZ3X_ENHANCEMENT, /* "3XBRZ" */
	BRZ4X_ENHANCEMENT, /* "4XBRZ" */
	BRZ5X_ENHANCEMENT, /* "5XBRZ" */
	BRZ6X_ENHANCEMENT  /* "6XBRZ" */
};

static u32 _getConfigOptions(void)
{
	u32 options = textureFilters[config.textureFilter.txFilterMode] | textureEnhancements[config.textureFilter.txEnhancementMode];
	if (config.textureFilter.txHiresEnable)
		options |= RICE_HIRESTEXTURES;
	if (config.textureFilter.txForce16bpp)
		options |= FORCE16BPP_TEX | FORCE16BPP_HIRESTEX;
	if (config.textureFilter.txCacheCompression)
		options |= GZ_TEXCACHE | GZ_HIRESTEXCACHE;
	if (config.textureFilter.txSaveCache)
		options |= (DUMP_TEXCACHE | DUMP_HIRESTEXCACHE);
	if (config.textureFilter.txHiresFullAlphaChannel)
		options |= LET_TEXARTISTS_FLY;
	if (config.textureFilter.txDump)
		options |= DUMP_TEX;
	if (config.textureFilter.txDeposterize)
		options |= DEPOSTERIZE;
	return options;
}

/* The configured path, or the default folder under _base */
static const wchar_t * _path(const wchar_t * _configured, wchar_t * _buf, bool _data, const wchar_t * _folder)
{
	if (wcslen(_configured) != 0 && osal_is_absolute_path(_configured) != 0)
		return _configured;
	if (_data)
		gli_api_user_data_path(_buf);
	else
		gli_api_user_cache_path(_buf);
	wcscat(_buf, _folder);
	return _buf;
}

void gli_TFH_init(void)
{
	s32 maxTextureSize;
	wchar_t wRomName[32];
	wchar_t txPath[PLUGIN_PATH_SIZE + 16];
	wchar_t txCachePath[PLUGIN_PATH_SIZE + 16];
	wchar_t txDumpPath[PLUGIN_PATH_SIZE + 16];
	const wchar_t * pTexPackPath;
	const wchar_t * pTexCachePath;
	const wchar_t * pTexDumpPath;

	if (gli_TFH_is_inited())
		return;

	l_inited = config.textureFilter.txFilterMode | config.textureFilter.txEnhancementMode | config.textureFilter.txHiresEnable;
	if (l_inited == 0)
		return;

	l_options = _getConfigOptions();

	maxTextureSize = gli_gfx_max_texture_size();
	mbstowcs(wRomName, RSP.romname, 32);

	pTexPackPath = _path(config.textureFilter.txPath, txPath, true, L"/hires_texture");
	pTexCachePath = _path(config.textureFilter.txCachePath, txCachePath, false, L"/cache");
	pTexDumpPath = _path(config.textureFilter.txDumpPath, txDumpPath, false, L"/texture_dump");

	l_inited = txfilter_init(maxTextureSize, /* max texture width supported by hardware */
		maxTextureSize, /* max texture height supported by hardware */
		32, /* max texture bpp supported by hardware */
		l_options,
		config.textureFilter.txCacheSize, /* cache texture to system memory */
		pTexCachePath, /* path to store cache files */
		pTexDumpPath, /* path to folder with dumped textures */
		pTexPackPath, /* path to texture packs folder */
		wRomName, /* name of ROM. must be no longer than 256 characters */
		NULL); /* no progress display: the frontend owns the screen */
}

void gli_TFH_shutdown(void)
{
	if (gli_TFH_is_inited()) {
		txfilter_shutdown();
		l_inited = l_options = 0;
	}
}

void gli_TFH_dumpcache(void)
{
	if (gli_TFH_is_inited())
		txfilter_dumpcache();
}

bool gli_TFH_is_inited(void)
{
	return l_inited != 0;
}

bool gli_TFH_options_changed(void)
{
	return _getConfigOptions() != l_options;
}
