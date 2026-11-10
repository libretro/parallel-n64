#ifdef OS_WINDOWS
# include <windows.h>
#else
# include "winlnxdefs.h"
#endif // OS_WINDOWS

#include "PluginAPI.h"
#include "DisplayWindow.h"

extern "C" {

EXPORT BOOL CALL gliden64InitiateGFX (GFX_INFO Gfx_Info)
{
	return api().InitiateGFX(Gfx_Info);
}

EXPORT void CALL gliden64MoveScreen (int xpos, int ypos)
{
	api().MoveScreen(xpos, ypos);
}

EXPORT void CALL gliden64ProcessDList(void)
{
	api().ProcessDList();
}

EXPORT void CALL gliden64ProcessRDPList(void)
{
	api().ProcessRDPList();
}

EXPORT void CALL gliden64RomClosed (void)
{
	api().RomClosed();
}

EXPORT void CALL gliden64ShowCFB (void)
{
	api().ShowCFB();
}

EXPORT void CALL gliden64UpdateScreen (void)
{
	api().UpdateScreen();
}

EXPORT void CALL gliden64ViStatusChanged (void)
{
	api().ViStatusChanged();
}

EXPORT void CALL gliden64ViWidthChanged (void)
{
	api().ViWidthChanged();
}

EXPORT void CALL gliden64ChangeWindow(void)
{
	api().ChangeWindow();
}

EXPORT void CALL gliden64FBWrite(unsigned int addr, unsigned int size)
{
	api().FBWrite(addr, size);
}

EXPORT void CALL gliden64FBRead(unsigned int addr)
{
	api().FBRead(addr);
}

EXPORT void CALL gliden64FBGetFrameBufferInfo(void *pinfo)
{
	api().FBGetFrameBufferInfo(pinfo);
}

EXPORT void CALL gliden64DestroyGfxContext(void)
{
	dwnd().destroyGfxContext();
}

EXPORT void CALL gliden64ReinitGfxContext(void)
{
	dwnd().reinitGfxContext();
}
}

extern "C" void gli_api_find_plugin_path(wchar_t * _path)
{
	api().FindPluginPath(_path);
}

extern "C" void gli_api_user_data_path(wchar_t * _path)
{
	api().GetUserDataPath(_path);
}

extern "C" void gli_api_user_cache_path(wchar_t * _path)
{
	api().GetUserCachePath(_path);
}
