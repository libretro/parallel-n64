// #include <mupen64plus-next_common.h>
#include "GLideN64_mupenplus.h"
// #include "GLideN64_libretro.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "../Config.h"
#include "../GLideN64.h"
#include "../GBI.h"
#include "../RSP.h"
#include "../Log.h"
extern "C" {
#include "main/util.h"
#include "GLideN64.custom.ini.h"
}

Config config;

extern "C" uint32_t CoreOptionCategoriesSupported;
extern "C" uint32_t CoreOptionUpdateDisplayCbSupported;

extern "C" uint32_t bilinearMode;
extern "C" uint32_t EnableHWLighting;
extern "C" uint32_t CorrectTexrectCoords;
extern "C" uint32_t EnableInaccurateTextureCoordinates;
extern "C" uint32_t enableNativeResTexrects;
extern "C" uint32_t enableLegacyBlending;
extern "C" uint32_t EnableCopyColorToRDRAM;
extern "C" uint32_t EnableCopyDepthToRDRAM;
extern "C" uint32_t AspectRatio;
extern "C" uint32_t txFilterMode;
extern "C" uint32_t txEnhancementMode;
extern "C" uint32_t txHiresEnable;
extern "C" uint32_t txHiresFullAlphaChannel;
extern "C" uint32_t txFilterIgnoreBG;
extern "C" uint32_t EnableFXAA;
extern "C" uint32_t MultiSampling;
extern "C" uint32_t EnableFragmentDepthWrite;
extern "C" uint32_t EnableShadersStorage;
extern "C" uint32_t EnableTextureCache;
extern "C" uint32_t EnableFBEmulation;
extern "C" uint32_t EnableLODEmulation;
extern "C" uint32_t BackgroundMode; // 0 is bgOnePiece
extern "C" uint32_t EnableHiResAltCRC;
extern "C" uint32_t EnableTxCacheCompression;
extern "C" uint32_t EnableNativeResFactor;
extern "C" uint32_t EnableN64DepthCompare;
extern "C" uint32_t EnableCopyAuxToRDRAM;
extern "C" uint32_t GLideN64IniBehaviour;

extern "C" uint32_t EnableOverscan;
extern "C" uint32_t OverscanTop;
extern "C" uint32_t OverscanLeft;
extern "C" uint32_t OverscanRight;
extern "C" uint32_t OverscanBottom;

std::string replaceChars(std::string myString)
{
	for (size_t pos = myString.find(' '); pos != std::string::npos; pos = myString.find(' ', pos))
	{
		myString.replace(pos, 1, "%20");
	}
	for (size_t pos = myString.find('\''); pos != std::string::npos; pos = myString.find('\'', pos))
	{
		myString.replace(pos, 1, "%27");
	}
	return myString;
}

void LoadCustomSettings(bool internal)
{
	std::string myString = replaceChars(RSP.romname);
	bool found = false;
	char buffer[256];
	char* line;
	FILE* fPtr;
	std::transform(myString.begin(), myString.end(), myString.begin(), ::toupper);
	if (internal) {
		line = strtok(customini, "\n");
	} else {
		const char *pathname = ConfigGetSharedDataFilepath("GLideN64.custom.ini");
		if (pathname == NULL || (fPtr = fopen(pathname, "rb")) == NULL)
			return;
	}
	while (true)
	{
		if (!internal) {
			if (fgets(buffer, 255, fPtr) == NULL)
				break;
			else
				line = buffer;
		}
		ini_line l = ini_parse_line(&line);
		switch (l.type)
		{
			case INI_SECTION:
			{
				if (myString == replaceChars(l.name))
					found = true;
				else
					found = false;
			}
			case INI_PROPERTY:
			{
				if (found) {
					if (!strcmp(l.name, "video\\multisampling"))
						config.video.multisampling = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\aspect"))
						config.frameBufferEmulation.aspect = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\nativeResFactor"))
						config.frameBufferEmulation.nativeResFactor = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyToRDRAM"))
						config.frameBufferEmulation.copyToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyFromRDRAM"))
						config.frameBufferEmulation.copyFromRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyDepthToRDRAM"))
						config.frameBufferEmulation.copyDepthToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\copyAuxToRDRAM"))
						config.frameBufferEmulation.copyAuxToRDRAM = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\fbInfoDisabled"))
						config.frameBufferEmulation.fbInfoDisabled = atoi(l.value);
					else if (!strcmp(l.name, "frameBufferEmulation\\N64DepthCompare"))
					{
						// We only allow N64DepthCompare if its actually on in the settings
						// I know this is a bit counter productive
						// Maybe needs a "Auto" mode in the future, since it soon has a "fast" mode too.
						// Currently its often not supported anyway or causes crippling issues otherwise
						if(EnableN64DepthCompare)
						{
							// Set to config val, ignoring the actual pre-set, see above.
							config.frameBufferEmulation.N64DepthCompare = atoi(l.value);
						}
					}
					else if (!strcmp(l.name, "frameBufferEmulation\\bufferSwapMode"))
						config.frameBufferEmulation.bufferSwapMode = atoi(l.value);
					else if (!strcmp(l.name, "texture\\bilinearMode"))
						config.texture.bilinearMode = atoi(l.value);
					else if (!strcmp(l.name, "texture\\maxAnisotropy"))
						config.texture.maxAnisotropy = atoi(l.value);
					else if (!strcmp(l.name, "graphics2D\\enableNativeResTexrects"))
						config.graphics2D.enableNativeResTexrects = atoi(l.value);
					// Inconsistent
					else if (!strcmp(l.name, "generalEmulation\\correctTexrectCoords") || !strcmp(l.name, "graphics2D\\correctTexrectCoords"))
						config.graphics2D.correctTexrectCoords = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\enableLegacyBlending"))
						config.generalEmulation.enableLegacyBlending = atoi(l.value);
					else if (!strcmp(l.name, "generalEmulation\\enableFragmentDepthWrite"))
						config.generalEmulation.enableFragmentDepthWrite = atoi(l.value);
				}
			}
		}
		if (internal) {
			line = strtok(NULL, "\n");
			if (line == NULL)
				break;
		}
	}
}

extern "C" void Config_LoadConfig()
{
	u32 hacks = config.generalEmulation.hacks;
	
	config.resetToDefaults();

	// Early
	if(GLideN64IniBehaviour == 1)
	{
		LoadCustomSettings(true);
		LoadCustomSettings(false);
	}

	config.frameBufferEmulation.aspect = AspectRatio;
	config.frameBufferEmulation.enable = EnableFBEmulation;
	config.frameBufferEmulation.N64DepthCompare = EnableN64DepthCompare;

	config.texture.bilinearMode = bilinearMode;
	config.generalEmulation.enableHWLighting = EnableHWLighting;
	config.generalEmulation.enableLegacyBlending = enableLegacyBlending;
	config.generalEmulation.enableLOD = EnableLODEmulation;
	
	config.frameBufferEmulation.copyDepthToRDRAM = EnableCopyDepthToRDRAM;
	config.frameBufferEmulation.copyToRDRAM = EnableCopyColorToRDRAM;

	// TODO: Make this a Core options or maybe only default to bsOnVerticalInterrupt on Android with Thr Renderer
	config.frameBufferEmulation.bufferSwapMode = Config::bsOnVerticalInterrupt;

#ifdef HAVE_OPENGLES2
	config.generalEmulation.enableFragmentDepthWrite = 0;
#else
	config.generalEmulation.enableFragmentDepthWrite = EnableFragmentDepthWrite;
#endif
#ifdef VC
	config.generalEmulation.enableShadersStorage = 0;
#else
	config.generalEmulation.enableShadersStorage = EnableShadersStorage;
#endif

	config.frameBufferEmulation.copyAuxToRDRAM = EnableCopyAuxToRDRAM;
	config.textureFilter.txSaveCache = EnableTextureCache;
	
	config.textureFilter.txFilterMode = txFilterMode;
	config.textureFilter.txEnhancementMode = txEnhancementMode;
	config.textureFilter.txFilterIgnoreBG = txFilterIgnoreBG;
	config.textureFilter.txHiresEnable = txHiresEnable;
	config.textureFilter.txCacheCompression = EnableTxCacheCompression;
	config.textureFilter.txHiresFullAlphaChannel = txHiresFullAlphaChannel;
	config.video.fxaa = EnableFXAA;
	config.video.multisampling = MultiSampling;
	
	// Overscan
	config.frameBufferEmulation.enableOverscan = EnableOverscan;
	// NTSC
	config.frameBufferEmulation.overscanNTSC.left = OverscanLeft;
	config.frameBufferEmulation.overscanNTSC.right = OverscanRight;
	config.frameBufferEmulation.overscanNTSC.top = OverscanTop;
	config.frameBufferEmulation.overscanNTSC.bottom = OverscanBottom;
	// PAL
	config.frameBufferEmulation.overscanPAL.left = OverscanLeft;
	config.frameBufferEmulation.overscanPAL.right = OverscanRight;
	config.frameBufferEmulation.overscanPAL.top = OverscanTop;
	config.frameBufferEmulation.overscanPAL.bottom = OverscanBottom;

	config.graphics2D.correctTexrectCoords = CorrectTexrectCoords;
	config.graphics2D.enableNativeResTexrects = enableNativeResTexrects;

	config.graphics2D.bgMode = BackgroundMode;

	config.textureFilter.txHresAltCRC = EnableHiResAltCRC;
	config.frameBufferEmulation.nativeResFactor = EnableNativeResFactor;

	config.generalEmulation.hacks = hacks;

	// Late
	if(GLideN64IniBehaviour == 0)
	{
		LoadCustomSettings(true);
		LoadCustomSettings(false);
	}
}
