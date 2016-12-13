#include <boolean.h>

#include <algorithm>
#include "../PluginAPI.h"
#include "../OpenGL.h"
#include "../RSP.h"

int PluginAPI::InitiateGFX(const GFX_INFO & _gfxInfo)
{
	_initiateGFX(_gfxInfo);

	return true;
}

void PluginAPI::GetUserDataPath(wchar_t * _strPath)
{
}

void PluginAPI::GetUserCachePath(wchar_t * _strPath)
{
}

void PluginAPI::FindPluginPath(wchar_t * _strPath)
{
}
