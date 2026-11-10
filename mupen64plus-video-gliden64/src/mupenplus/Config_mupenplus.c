#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <compat/strl.h>
#include <streams/file_stream.h>

#include "GLideN64.custom.ini.h"

#include "../Types.h"
#include "../Config.h"
#include "../RSP.h"

Config config;

/* From the core's m64p_config.h and main/util.h, which are not C89 */
const char *ConfigGetSharedDataFilepath(const char *filename);
typedef enum { INI_BLANK, INI_COMMENT, INI_SECTION, INI_PROPERTY, INI_TRASH } ini_line_type;
typedef struct _ini_line { ini_line_type type; char *name; char *value; } ini_line;
ini_line ini_parse_line(char **lineptr);

extern u32 bilinearMode;
extern u32 EnableHWLighting;
extern u32 CorrectTexrectCoords;
extern u32 enableNativeResTexrects;
extern u32 enableLegacyBlending;
extern u32 EnableCopyColorToRDRAM;
extern u32 EnableCopyColorFromRDRAM;
extern u32 EnableCopyDepthToRDRAM;
extern u32 AspectRatio;
extern u32 txFilterMode;
extern u32 txEnhancementMode;
extern u32 txHiresEnable;
extern u32 txHiresFullAlphaChannel;
extern u32 txFilterIgnoreBG;
extern u32 EnableFXAA;
extern u32 MultiSampling;
extern u32 EnableFragmentDepthWrite;
extern u32 EnableShadersStorage;
extern u32 EnableTextureCache;
extern u32 EnableFBEmulation;
extern u32 EnableLODEmulation;
extern u32 BackgroundMode; /* 0 is bgOnePiece */
extern u32 EnableHiResAltCRC;
extern u32 EnableTxCacheCompression;
extern u32 EnableNativeResFactor;
extern u32 EnableN64DepthCompare;
extern u32 EnableCopyAuxToRDRAM;
extern u32 GLideN64IniBehaviour;

extern u32 EnableOverscan;
extern u32 OverscanTop;
extern u32 OverscanLeft;
extern u32 OverscanRight;
extern u32 OverscanBottom;

/* VR */
extern u32 VRMode; /* 0 off, 1 stereoscopic, 2 immersive */
extern float VRSeparation;
extern float VRDepthBoost;
extern float VRImmersiveDistance;
extern float VRImmersiveWorldScale;
extern float VRImmersiveHudScale;

#define INI_LINE 255

/* Section names match with spaces and quotes escaped as %20 and %27 */
static int section_matches(const char *_name, const char *_escaped)
{
	for (; *_name != '\0'; ++_name) {
		const char *rep = *_name == ' ' ? "%20" : (*_name == '\'' ? "%27" : NULL);
		if (rep != NULL) {
			if (strncmp(_escaped, rep, 3) != 0)
				return 0;
			_escaped += 3;
		} else if (*_escaped++ != *_name)
			return 0;
	}
	return *_escaped == '\0';
}

static void apply_property(const char *_name, const char *_value)
{
	if (!strcmp(_name, "video\\multisampling"))
		config.video.multisampling = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\aspect"))
		config.frameBufferEmulation.aspect = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\nativeResFactor"))
		config.frameBufferEmulation.nativeResFactor = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\copyToRDRAM"))
		config.frameBufferEmulation.copyToRDRAM = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\copyFromRDRAM"))
		config.frameBufferEmulation.copyFromRDRAM = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\copyDepthToRDRAM"))
		config.frameBufferEmulation.copyDepthToRDRAM = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\copyAuxToRDRAM"))
		config.frameBufferEmulation.copyAuxToRDRAM = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\fbInfoDisabled"))
		config.frameBufferEmulation.fbInfoDisabled = atoi(_value);
	else if (!strcmp(_name, "frameBufferEmulation\\N64DepthCompare")) {
		/* Only when the user has N64 depth compare on */
		if (EnableN64DepthCompare)
			config.frameBufferEmulation.N64DepthCompare = atoi(_value);
	}
	else if (!strcmp(_name, "frameBufferEmulation\\bufferSwapMode"))
		config.frameBufferEmulation.bufferSwapMode = atoi(_value);
	else if (!strcmp(_name, "texture\\bilinearMode"))
		config.texture.bilinearMode = atoi(_value);
	else if (!strcmp(_name, "texture\\maxAnisotropy"))
		config.texture.maxAnisotropy = atoi(_value);
	else if (!strcmp(_name, "graphics2D\\enableNativeResTexrects"))
		config.graphics2D.enableNativeResTexrects = atoi(_value);
	/* Inconsistent */
	else if (!strcmp(_name, "generalEmulation\\correctTexrectCoords") || !strcmp(_name, "graphics2D\\correctTexrectCoords"))
		config.graphics2D.correctTexrectCoords = atoi(_value);
	else if (!strcmp(_name, "generalEmulation\\enableLegacyBlending"))
		config.generalEmulation.enableLegacyBlending = atoi(_value);
	else if (!strcmp(_name, "generalEmulation\\enableFragmentDepthWrite"))
		config.generalEmulation.enableFragmentDepthWrite = atoi(_value);
}

static void LoadCustomSettings(int _internal)
{
	char rom[sizeof(RSP.romname) * 3];
	char buffer[INI_LINE];
	size_t next = 0;
	RFILE *file = NULL;
	int found = 0;
	size_t i, n = 0;

	for (i = 0; i < sizeof(RSP.romname) && RSP.romname[i] != '\0'; ++i) {
		const char c = RSP.romname[i];
		if (c == ' ' || c == '\'') {
			memcpy(rom + n, c == ' ' ? "%20" : "%27", 3);
			n += 3;
		} else
			rom[n++] = (char)toupper((unsigned char)c);
	}
	rom[n] = '\0';

	if (!_internal) {
		const char *pathname = ConfigGetSharedDataFilepath("GLideN64.custom.ini");
		if (pathname == NULL)
			return;
		file = filestream_open(pathname, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		if (file == NULL)
			return;
	}

	for (;;) {
		char *line = buffer;
		ini_line l;
		if (!_internal) {
			if (filestream_gets(file, buffer, INI_LINE) == NULL)
				break;
		} else {
			/* the parser writes into the line, so it gets a copy */
			if (next >= sizeof(customini) / sizeof(customini[0]))
				break;
			strlcpy(buffer, customini[next++], sizeof(buffer));
		}
		l = ini_parse_line(&line);
		if (l.type == INI_SECTION)
			found = section_matches(l.name, rom);
		else if (l.type == INI_PROPERTY && found)
			apply_property(l.name, l.value);
	}

	if (file != NULL)
		filestream_close(file);
}

/* The VR depth settings that may change while a game runs */
void gln64_vr_update_config(void)
{
	config.stereo.separation = VRSeparation;
	config.stereo.depthBoost = VRDepthBoost;
	config.stereo.immersiveDistance = VRImmersiveDistance;
	config.stereo.immersiveWorldScale = VRImmersiveWorldScale;
	config.stereo.immersiveHudScale = VRImmersiveHudScale;
}

void Config_LoadConfig(void)
{
	u32 hacks = config.generalEmulation.hacks;

	gli_config_reset_defaults(&config);

	/* Early */
	if (GLideN64IniBehaviour == 1) {
		LoadCustomSettings(1);
		LoadCustomSettings(0);
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
	config.frameBufferEmulation.copyFromRDRAM = EnableCopyColorFromRDRAM;

	config.frameBufferEmulation.bufferSwapMode = bsOnVerticalInterrupt;

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

	/* Overscan, the same for NTSC and PAL */
	config.frameBufferEmulation.enableOverscan = EnableOverscan;
	config.frameBufferEmulation.overscanNTSC.left = OverscanLeft;
	config.frameBufferEmulation.overscanNTSC.right = OverscanRight;
	config.frameBufferEmulation.overscanNTSC.top = OverscanTop;
	config.frameBufferEmulation.overscanNTSC.bottom = OverscanBottom;
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

	/* Late */
	if (GLideN64IniBehaviour == 0) {
		LoadCustomSettings(1);
		LoadCustomSettings(0);
	}

	/* VR: both eyes side by side, each presented into its half of the window */
	config.stereo.mode = VRMode != 0 ? stereoBothEyes : stereoOff;
	config.stereo.immersive = VRMode == 2 ? 1 : 0;
	gln64_vr_update_config();
	if (VRMode != 0)
		config.frameBufferEmulation.enable = 1;
	/* A head tracked picture covers exactly the field of view it was drawn for */
	if (VRMode == 2) {
		config.frameBufferEmulation.aspect = aStretch;
		config.frameBufferEmulation.enableOverscan = 0;
	}
}
