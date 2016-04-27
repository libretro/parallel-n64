#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <retro_miscellaneous.h>

#include "../Config.h"
#include "../OpenGL.h"
#include "../GBI.h"
#include "../RSP.h"
#include "../Log.h"

#include "m64p_config.h"

static
const uint32_t uMegabyte = 1024U*1024U;

static
bool Config_SetDefault()
{
	//config.resetToDefaults();
   config.video.fullscreen = true;
   config.video.windowedWidth = 640;
   config.video.windowedHeight = 480;
   config.video.verticalSync = false;

   config.video.multisampling                   = 0; /* 0=off, 2,4,8,16=quality */
   config.frameBufferEmulation.bufferSwapMode   = 0; /* 0=On VI update call, 1=on VI origin change, 2 =On buffer update */
	config.frameBufferEmulation.aspect           = 0; /*Screen aspect ratio (0=stretch, 1=force 4:3, 2=force 16:9, 3=adjust) */

   /* Texture Settings */
	config.texture.bilinearMode                  = 1; /* Bilinear filtering mode (0=N64 3point, 1=standard) */

	/* Emulation Settings */
	config.generalEmulation.enableNoise          = 1; /* Enable color noise emulation. */
	config.generalEmulation.enableLOD            = 1; /* Enable LOD emulation. */
	config.generalEmulation.enableHWLighting     = 0; /* Enable hardware per-pixel lighting. */

	/* Frame Buffer Settings */
	config.frameBufferEmulation.enable           = 0; /* Enable frame and|or depth buffer emulation. */
	config.frameBufferEmulation.copyAuxToRDRAM   = 0; /* Copy auxiliary buffers to RDRAM */
	config.frameBufferEmulation.copyDepthToRDRAM = 0; /* Enable depth buffer copy to RDRAM. */
	config.frameBufferEmulation.copyToRDRAM      = 1; /* Enable color buffer copy to RDRAM (0=do not copy, 1=copy in sync mode, 2=copy in async mode) */
	config.frameBufferEmulation.copyFromRDRAM    = 1; /* Enable color buffer copy from RDRAM. */
	config.frameBufferEmulation.N64DepthCompare  = 0; /* Enable N64 depth compare instead of OpenGL standard one. Experimental. */

#if 0
	//#Texture Settings
	res = ConfigSetDefaultBool(g_configVideoGliden64, "MaxAnisotropy", config.texture.maxAnisotropy, "Max level of Anisotropic Filtering, 0 for off");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultInt(g_configVideoGliden64, "CacheSize", config.texture.maxBytes / uMegabyte, "Size of texture cache in megabytes. Good value is VRAM*3/4");
	assert(res == M64ERR_SUCCESS);

	//#Emulation Settings
	res = ConfigSetDefaultBool(g_configVideoGliden64, "EnableShadersStorage", config.generalEmulation.enableShadersStorage, "Use persistent storage for compiled shaders.");
	assert(res == M64ERR_SUCCESS);
#ifdef ANDROID
	res = ConfigSetDefaultBool(g_configVideoGliden64, "ForcePolygonOffset", config.generalEmulation.forcePolygonOffset, "If true, use polygon offset values specified below");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultFloat(g_configVideoGliden64, "PolygonOffsetFactor", config.generalEmulation.polygonOffsetFactor, "Specifies a scale factor that is used to create a variable depth offset for each polygon");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultFloat(g_configVideoGliden64, "PolygonOffsetUnits", config.generalEmulation.polygonOffsetUnits, "Is multiplied by an implementation-specific value to create a constant depth offset");
	assert(res == M64ERR_SUCCESS);
#endif

	//#Texture filter settings
	res = ConfigSetDefaultInt(g_configVideoGliden64, "txFilterMode", config.textureFilter.txFilterMode, "Texture filter (0=none, 1=Smooth filtering 1, 2=Smooth filtering 2, 3=Smooth filtering 3, 4=Smooth filtering 4, 5=Sharp filtering 1, 6=Sharp filtering 2)");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultInt(g_configVideoGliden64, "txEnhancementMode", config.textureFilter.txEnhancementMode, "Texture Enhancement (0=none, 1=store as is, 2=X2, 3=X2SAI, 4=HQ2X, 5=HQ2XS, 6=LQ2X, 7=LQ2XS, 8=HQ4X, 9=2xBRZ, 10=3xBRZ, 11=4xBRZ, 12=5xBRZ), 13=6xBRZ");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txFilterIgnoreBG", config.textureFilter.txFilterIgnoreBG, "Don't filter background textures.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultInt(g_configVideoGliden64, "txCacheSize", config.textureFilter.txCacheSize/uMegabyte, "Size of filtered textures cache in megabytes.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txHiresEnable", config.textureFilter.txHiresEnable, "Use high-resolution texture packs if available.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txHiresFullAlphaChannel", config.textureFilter.txHiresFullAlphaChannel, "Allow to use alpha channel of high-res texture fully.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txHresAltCRC", config.textureFilter.txHresAltCRC, "Use alternative method of paletted textures CRC calculation.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txDump", config.textureFilter.txDump, "Enable dump of loaded N64 textures.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txCacheCompression", config.textureFilter.txCacheCompression, "Zip textures cache.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txForce16bpp", config.textureFilter.txForce16bpp, "Force use 16bit texture formats for HD textures.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultBool(g_configVideoGliden64, "txSaveCache", config.textureFilter.txSaveCache, "Save texture cache to hard disk.");
	assert(res == M64ERR_SUCCESS);

	// Convert to multibyte
	char txPath[PATH_MAX_LENGTH * 2];
	wcstombs(txPath, config.textureFilter.txPath, PATH_MAX_LENGTH * 2);
	res = ConfigSetDefaultString(g_configVideoGliden64, "txPath", txPath, "Path to folder with hi-res texture packs.");
	assert(res == M64ERR_SUCCESS);

	//#Gamma correction settings
	res = ConfigSetDefaultBool(g_configVideoGliden64, "ForceGammaCorrection", config.gammaCorrection.force, "Force gamma correction.");
	assert(res == M64ERR_SUCCESS);
	res = ConfigSetDefaultFloat(g_configVideoGliden64, "GammaCorrectionLevel", config.gammaCorrection.level, "Gamma correction level.");
	assert(res == M64ERR_SUCCESS);
#endif

	return M64ERR_SUCCESS;
}

void Config_LoadConfig()
{
	const uint32_t hacks = config.generalEmulation.hacks;

	if (!Config_SetDefault()) {
		config.generalEmulation.hacks = hacks;
		return;
	}

	config.generalEmulation.hacks = hacks;
}
