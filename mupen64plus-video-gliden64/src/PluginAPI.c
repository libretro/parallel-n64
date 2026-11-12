#include <stdlib.h>
#include <string.h>
#include <compat/strl.h>
#include <libretro.h>

#include "PluginAPI.h"
#include "mupenplus/GLideN64_mupenplus.h"
#include "N64.h"
#include "GLideN64.h"
#include "GBI.h"
#include "RSP.h"
#include "RDP.h"
#include "VI.h"
#include "gDP.h"
#include "Config.h"
#include "FrameBufferInfo.h"
#include "TextureFilterHandler.h"
#include "gli_framebuffer.h"

extern retro_environment_t environ_cb;

static char pluginNameWithRevision[] = "GLideN64 rev.LINK";

void (*renderCallback)(int) = NULL;

static bool l_bRomOpen;

/* ---- paths ---- */

/* <system>/ParallelN64, UTF-8 with forward slashes, no trailing slash */
static void _getRetroArchDir(char * _strPath, size_t _size)
{
	char * p;
	size_t len;
	const char * systemDir = NULL;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemDir) || !systemDir || !*systemDir)
		systemDir = ".";
	len = strlcpy(_strPath, systemDir, _size);
	if (len > 0 && len < _size && _strPath[len - 1] != '/' && _strPath[len - 1] != '\\')
		strlcat(_strPath, "/", _size);
	strlcat(_strPath, "ParallelN64", _size);
	for (p = _strPath; *p != 0; ++p)
		if (*p == '\\')
			*p = '/';
}

void gli_api_user_data_path(char * _strPath, size_t _size)
{
	_getRetroArchDir(_strPath, _size);
}

void gli_api_user_cache_path(char * _strPath, size_t _size)
{
	_getRetroArchDir(_strPath, _size);
}

/* ---- the plugin ---- */

EXPORT int CALL gliden64InitiateGFX(GFX_INFO _gfxInfo)
{
	HEADER = _gfxInfo.HEADER;
	DMEM = _gfxInfo.DMEM;
	IMEM = _gfxInfo.IMEM;
	RDRAM = _gfxInfo.RDRAM;

	REG.MI_INTR = _gfxInfo.MI_INTR_REG;
	REG.DPC_START = _gfxInfo.DPC_START_REG;
	REG.DPC_END = _gfxInfo.DPC_END_REG;
	REG.DPC_CURRENT = _gfxInfo.DPC_CURRENT_REG;
	REG.DPC_STATUS = _gfxInfo.DPC_STATUS_REG;
	REG.DPC_CLOCK = _gfxInfo.DPC_CLOCK_REG;
	REG.DPC_BUFBUSY = _gfxInfo.DPC_BUFBUSY_REG;
	REG.DPC_PIPEBUSY = _gfxInfo.DPC_PIPEBUSY_REG;
	REG.DPC_TMEM = _gfxInfo.DPC_TMEM_REG;

	REG.VI_STATUS = _gfxInfo.VI_STATUS_REG;
	REG.VI_ORIGIN = _gfxInfo.VI_ORIGIN_REG;
	REG.VI_WIDTH = _gfxInfo.VI_WIDTH_REG;
	REG.VI_INTR = _gfxInfo.VI_INTR_REG;
	REG.VI_V_CURRENT_LINE = _gfxInfo.VI_V_CURRENT_LINE_REG;
	REG.VI_TIMING = _gfxInfo.VI_TIMING_REG;
	REG.VI_V_SYNC = _gfxInfo.VI_V_SYNC_REG;
	REG.VI_H_SYNC = _gfxInfo.VI_H_SYNC_REG;
	REG.VI_LEAP = _gfxInfo.VI_LEAP_REG;
	REG.VI_H_START = _gfxInfo.VI_H_START_REG;
	REG.VI_V_START = _gfxInfo.VI_V_START_REG;
	REG.VI_V_BURST = _gfxInfo.VI_V_BURST_REG;
	REG.VI_X_SCALE = _gfxInfo.VI_X_SCALE_REG;
	REG.VI_Y_SCALE = _gfxInfo.VI_Y_SCALE_REG;

	CheckInterrupts = _gfxInfo.CheckInterrupts;

	REG.SP_STATUS = _gfxInfo.DPC_STATUS_REG;

	return 1;
}

EXPORT void CALL gliden64MoveScreen(int xpos, int ypos)
{
	(void)xpos;
	(void)ypos;
}

EXPORT void CALL gliden64ProcessDList(void)
{
	gli_RSP_ProcessDList();
}

EXPORT void CALL gliden64ProcessRDPList(void)
{
	gli_RDP_ProcessRDPList();
}

EXPORT void CALL gliden64RomClosed(void)
{
	if (!l_bRomOpen)
		return;

	l_bRomOpen = false;

	gli_TFH_dumpcache();
	gli_window_stop();
	gli_GBI_destroy();
	gli_RSP_StereoDestroy();
}

EXPORT int CALL gliden64RomOpen(void)
{
	RDRAMSize = 8 * 1024 * 1024 - 1;
	if (l_bRomOpen)
		return 1;

	gli_RSP_Init();
	gli_GBI_init();
	Config_LoadConfig();
	gli_window_start();
	l_bRomOpen = true;
	return 1;
}

EXPORT void CALL gliden64ShowCFB(void)
{
	gDP.changed |= CHANGED_CPU_FB_WRITE;
}

EXPORT void CALL gliden64UpdateScreen(void)
{
	gli_VI_UpdateScreen();
}

EXPORT void CALL gliden64ViStatusChanged(void)
{
}

EXPORT void CALL gliden64ViWidthChanged(void)
{
}

EXPORT void CALL gliden64ChangeWindow(void)
{
	gli_window_set_toggle_fullscreen();
	if (!l_bRomOpen)
		gli_window_close();
}

EXPORT void CALL gliden64FBWrite(unsigned int addr, unsigned int size)
{
	gli_fbinfo_write(addr, size);
}

EXPORT void CALL gliden64FBRead(unsigned int addr)
{
	gli_fbinfo_read(addr);
}

EXPORT void CALL gliden64FBGetFrameBufferInfo(void *pinfo)
{
	gli_fbinfo_get_info(pinfo);
}

EXPORT void CALL gliden64DestroyGfxContext(void)
{
	gli_window_destroy_gfx_context();
}

EXPORT void CALL gliden64ReinitGfxContext(void)
{
	gli_window_reinit_gfx_context();
}

EXPORT m64p_error CALL gliden64PluginGetVersion(m64p_plugin_type * _PluginType,
	int * _PluginVersion, int * _APIVersion, const char ** _PluginNamePtr, int * _Capabilities)
{
	if (_PluginType != NULL)
		*_PluginType = M64PLUGIN_GFX;
	if (_PluginVersion != NULL)
		*_PluginVersion = PLUGIN_VERSION;
	if (_APIVersion != NULL)
		*_APIVersion = VIDEO_PLUGIN_API_VERSION;
	if (_PluginNamePtr != NULL)
		*_PluginNamePtr = pluginNameWithRevision;
	if (_Capabilities != NULL)
		*_Capabilities = 0;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL gliden64PluginStartup(m64p_dynlib_handle CoreLibHandle,
	void *Context, void (*DebugCallback)(void *, int, const char *))
{
	(void)CoreLibHandle;
	(void)Context;
	(void)DebugCallback;
	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL gliden64PluginShutdown(void)
{
	gliden64RomClosed();
	return M64ERR_SUCCESS;
}

EXPORT void CALL gliden64ReadScreen2(void *dest, int *width, int *height, int front)
{
	gli_window_read_screen2(dest, width, height, front);
}

EXPORT void CALL gliden64SetRenderingCallback(void (*callback)(int))
{
	renderCallback = callback;
}

EXPORT void CALL gliden64ResizeVideoOutput(int width, int height)
{
	gli_window_set_size((u32)width, (u32)height);
}

void retroChangeWindow(void)
{
	gli_window_set_toggle_fullscreen();
	gli_window_change();
}
