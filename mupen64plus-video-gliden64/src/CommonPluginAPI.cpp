#include <stdint.h>
#include <boolean.h>
#include "PluginAPI.h"

extern "C" {

bool gln64InitiateGFX (GFX_INFO Gfx_Info)
{
	return api().InitiateGFX(Gfx_Info);
}

void gln64MoveScreen (int xpos, int ypos)
{
	api().MoveScreen(xpos, ypos);
}

void gln64ProcessDList(void)
{
	api().ProcessDList();
}

void gln64ProcessRDPList(void)
{
	api().ProcessRDPList();
}

void gln64RomClosed (void)
{
	api().RomClosed();
}

void gln64ShowCFB (void)
{
	api().ShowCFB();
}

void gln64UpdateScreen (void)
{
	api().UpdateScreen();
}

void gln64ViStatusChanged (void)
{
	api().ViStatusChanged();
}

void gln64ViWidthChanged (void)
{
	api().ViWidthChanged();
}

void gln64ChangeWindow(void)
{
	api().ChangeWindow();
}

}
