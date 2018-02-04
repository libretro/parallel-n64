#ifndef CONFIG_H
#define CONFIG_H

#include <string>

#define CONFIG_VERSION_ONE 1U
#define CONFIG_VERSION_TWO 2U
#define CONFIG_VERSION_THREE 3U
#define CONFIG_VERSION_FOUR 4U		// Remove ValidityCheckMethod setting
#define CONFIG_VERSION_FIVE 5U		// Add shader storage option
#define CONFIG_VERSION_SIX 6U		// Change gamma correction options
#define CONFIG_VERSION_CURRENT CONFIG_VERSION_SIX

#define BILINEAR_3POINT   0
#define BILINEAR_STANDARD 1

const uint32_t gc_uMegabyte = 1024U * 1024U;

struct gliden64_config
{
	uint32_t version;

	std::string translationFile;

	struct
	{
		uint32_t fullscreen;
		uint32_t windowedWidth, windowedHeight;
		uint32_t fullscreenWidth, fullscreenHeight, fullscreenRefresh;
		uint32_t multisampling;
		uint32_t verticalSync;
	} video;

	struct
	{
		uint32_t maxAnisotropy;
		float maxAnisotropyF;
		uint32_t bilinearMode;
		uint32_t maxBytes;
		uint32_t screenShotFormat;
	} texture;

	struct {
		uint32_t enableNoise;
		uint32_t enableLOD;
		uint32_t enableHWLighting;
		uint32_t enableCustomSettings;
		uint32_t enableShadersStorage;
		uint32_t hacks;
#ifdef ANDROID
		uint32_t forcePolygonOffset;
		float polygonOffsetFactor;
		float polygonOffsetUnits;
#endif
	} generalEmulation;

	enum Aspect {
		aStretch = 0,
		a43 = 1,
		a169 = 2,
		aAdjust = 3,
		aTotal = 4
	};

	enum CopyToRDRAM {
		ctDisable = 0,
		ctSync,
		ctAsync
	};

	enum BufferSwapMode {
		bsOnVerticalInterrupt = 0,
		bsOnVIOriginChange,
		bsOnColorImageChange
	};

	struct {
		uint32_t enable;
		uint32_t copyAuxToRDRAM;
		uint32_t copyToRDRAM;
		uint32_t copyDepthToRDRAM;
		uint32_t copyFromRDRAM;
		uint32_t N64DepthCompare;
		uint32_t aspect; // 0: stretch ; 1: 4/3 ; 2: 16/9; 3: adjust
		uint32_t bufferSwapMode; // 0: on VI update call; 1: on VI origin change; 2: on main frame buffer update
	} frameBufferEmulation;

	struct
	{
		uint32_t txFilterMode;				// Texture filtering mode, eg Sharpen
		uint32_t txEnhancementMode;			// Texture enhancement mode, eg 2xSAI
		uint32_t txFilterIgnoreBG;			// Do not apply filtering to backgrounds textures
		uint32_t txCacheSize;				// Cache size in Mbytes

		uint32_t txHiresEnable;				// Use high-resolution texture packs
		uint32_t txHiresFullAlphaChannel;	// Use alpha channel fully
		uint32_t txHresAltCRC;				// Use alternative method of paletted textures CRC calculation
		uint32_t txDump;						// Dump textures

		uint32_t txForce16bpp;				// Force use 16bit color textures
		uint32_t txCacheCompression;			// Zip textures cache
		uint32_t txSaveCache;				// Save texture cache to hard disk

		wchar_t txPath[256];
	} textureFilter;

	struct {
		uint32_t force;
		float level;
	} gammaCorrection;

	void resetToDefaults();
};

#define hack_Ogre64					(1<<0)  //Ogre Battle 64 background copy
#define hack_noDepthFrameBuffers	(1<<1)  //Do not use depth buffers as texture
#define hack_blurPauseScreen		(1<<2)  //Game copies frame buffer to depth buffer area, CPU blurs it. That image is used as background for pause screen.
#define hack_scoreboard				(1<<3)  //Copy data from RDRAM to auxilary frame buffer. Scoreboard in Mario Tennis.
#define hack_scoreboardJ			(1<<4)  //Copy data from RDRAM to auxilary frame buffer. Scoreboard in Mario Tennis (J).
#define hack_pilotWings				(1<<5)  //Special blend mode for PilotWings.
#define hack_subscreen				(1<<6)  //Fix subscreen delay in Zelda OOT and Doubutsu no Mori
#define hack_blastCorps				(1<<8)  //Blast Corps black polygons
#define hack_ignoreVIHeightChange	(1<<9)  //Do not reset FBO when VI height is changed. Space Invaders need it.
#define hack_skipVIChangeCheck		(1<<11) //Don't reset FBO when VI parameters changed. Zelda MM
#define hack_ZeldaCamera			(1<<12) //Special hack to detect and process Zelda MM camera.

extern gliden64_config config;

void Config_LoadConfig();
#ifndef MUPENPLUSAPI
void Config_DoConfig(/*HWND hParent*/);
#endif

#endif // CONFIG_H
