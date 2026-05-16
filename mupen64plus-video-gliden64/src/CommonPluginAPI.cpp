#ifdef OS_WINDOWS
# include <windows.h>
#else
# include "winlnxdefs.h"
#endif // OS_WINDOWS

#include "PluginAPI.h"

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

#ifndef MUPENPLUSAPI
EXPORT void CALL gliden64FBWList(FrameBufferModifyEntry *plist, unsigned int size)
{
	api().FBWList(plist, size);
}
#endif
}
