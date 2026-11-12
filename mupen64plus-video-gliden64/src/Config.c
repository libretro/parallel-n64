#include <compat/strl.h>
#include "Config.h"
#include "PluginAPI.h"
#include "GBI.h"
#include "gli_framebuffer.h"

extern u32 screen_width;
extern u32 screen_height;

void gli_config_reset_defaults(Config * _config)
{
	_config->version = CONFIG_VERSION_CURRENT;

#if defined(PANDORA) || defined(VC)
	_config->video.fullscreen = 1;
	_config->video.fullscreenWidth = _config->video.windowedWidth = 800;
#else
	_config->video.fullscreen = 0;
	_config->video.fullscreenWidth = _config->video.windowedWidth = screen_width;
#endif
	_config->video.fullscreenHeight = _config->video.windowedHeight = screen_height;
	_config->video.fullscreenRefresh = 60;
	_config->video.fxaa = 0;
	_config->video.multisampling = 0;
	_config->video.maxMultiSampling = 0;
	_config->video.borderless = 0;
	_config->video.threadedVideo = 0;
	_config->video.verticalSync = 0;

	config.video.fullscreenHeight = screen_height;
	config.video.fullscreenWidth = screen_width;
	config.video.windowedHeight = screen_height;
	config.video.windowedWidth = screen_width;
	
	_config->texture.maxAnisotropy = 0;
	_config->texture.anisotropy = 0;
	_config->texture.bilinearMode = BILINEAR_STANDARD;
	_config->texture.enableHalosRemoval = 0;
	_config->texture.screenShotFormat = 0;

	_config->generalEmulation.enableLOD = 1;
	_config->generalEmulation.enableNoise = 1;
	_config->generalEmulation.enableDitheringPattern = 0;
	_config->generalEmulation.enableDitheringQuantization = 0;
	_config->generalEmulation.enableHiresNoiseDithering = 0;
	_config->generalEmulation.rdramImageDitheringMode = bdmDisable;
	_config->generalEmulation.enableHWLighting = 0;
	_config->generalEmulation.enableCoverage = 0;
	_config->generalEmulation.enableClipping = 1;
	_config->generalEmulation.enableCustomSettings = 1;
	_config->generalEmulation.enableShadersStorage = 1;
	_config->generalEmulation.enableLegacyBlending = 0;
	_config->generalEmulation.enableHybridFilter = 0;
	_config->generalEmulation.enableInaccurateTextureCoordinates = 0;
	_config->generalEmulation.hacks = 0;
#if defined(OS_ANDROID) || defined(OS_IOS)
	_config->generalEmulation.enableFragmentDepthWrite = 0;
	_config->generalEmulation.forcePolygonOffset = 0;
	_config->generalEmulation.polygonOffsetFactor = 0.0f;
	_config->generalEmulation.polygonOffsetUnits = 0.0f;
#else
	_config->generalEmulation.enableFragmentDepthWrite = 1;
#endif

	_config->graphics2D.correctTexrectCoords = tcDisable;
	_config->graphics2D.enableNativeResTexrects = 0;
	_config->graphics2D.bgMode = bgStripped;
	_config->graphics2D.enableTexCoordBounds = 0;

	_config->frameBufferEmulation.enable = 0;
	_config->frameBufferEmulation.copyDepthToRDRAM = cdSoftwareRender;
	_config->frameBufferEmulation.copyFromRDRAM = 0;
	_config->frameBufferEmulation.copyAuxToRDRAM = 0;
	_config->frameBufferEmulation.copyToRDRAM = ctDoubleBuffer;
	_config->frameBufferEmulation.copyDepthToMainDepthBuffer = 0;
	_config->frameBufferEmulation.N64DepthCompare = 0;
	_config->frameBufferEmulation.forceDepthBufferClear = 0;
	_config->frameBufferEmulation.aspect = a43;
	_config->frameBufferEmulation.bufferSwapMode = bsOnVerticalInterrupt;
	_config->frameBufferEmulation.nativeResFactor = 0;
	_config->frameBufferEmulation.fbInfoReadColorChunk = 0;
	_config->frameBufferEmulation.fbInfoReadDepthChunk = 1;
	_config->frameBufferEmulation.fbInfoDisabled = 1;
	_config->frameBufferEmulation.enableOverscan = 0;

	_config->textureFilter.txFilterMode = 0;
	_config->textureFilter.txEnhancementMode = 0;
	_config->textureFilter.txDeposterize = 0;
	_config->textureFilter.txFilterIgnoreBG = 0;
	_config->textureFilter.txCacheSize = 100 * gc_uMegabyte;

	_config->textureFilter.txHiresEnable = 0;
	_config->textureFilter.txHiresFullAlphaChannel = 1;
	_config->textureFilter.txHresAltCRC = 0;
	_config->textureFilter.txDump = 0;
	_config->textureFilter.txStrongCRC = 0;

	_config->textureFilter.txForce16bpp = 0;
	_config->textureFilter.txCacheCompression = 1;
	_config->textureFilter.txSaveCache = 1;

	_config->textureFilter.txEnhancedTextureFileStorage = 0;
	_config->textureFilter.txHiresTextureFileStorage = 0;
	_config->textureFilter.txNoTextureFileStorage = 0;
	_config->textureFilter.txHiresVramLimit = 0;

	gli_api_user_data_path(_config->textureFilter.txPath, sizeof(_config->textureFilter.txPath));
	strlcat(_config->textureFilter.txPath, "/hires_texture", sizeof(_config->textureFilter.txPath));
	gli_api_user_cache_path(_config->textureFilter.txCachePath, sizeof(_config->textureFilter.txCachePath));
	strlcat(_config->textureFilter.txCachePath, "/cache", sizeof(_config->textureFilter.txCachePath));
	gli_api_user_cache_path(_config->textureFilter.txDumpPath, sizeof(_config->textureFilter.txDumpPath));
	strlcat(_config->textureFilter.txDumpPath, "/texture_dump", sizeof(_config->textureFilter.txDumpPath));

	_config->gammaCorrection.force = 0;
	_config->gammaCorrection.level = 2.0f;

	_config->stereo.mode = stereoOff;
	_config->stereo.separation = 0.03f;
	_config->stereo.depthBoost = 0.0f;
	_config->stereo.immersive = 0;
	_config->stereo.immersiveDistance = 0.0f;
	_config->stereo.immersiveWorldScale = 1.0f;
	_config->stereo.immersiveHudScale = 1.0f;

}

bool gli_isHWLightingAllowed(void)
{
	if (config.generalEmulation.enableHWLighting == 0)
		return false;
	return gli_GBI_hwlSupported;
}
