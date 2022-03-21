#include "PluginAPI.h"
#include "Types.h"
#include "mupenplus/GLideN64_mupenplus.h"
#include "N64.h"

extern "C" {

EXPORT int CALL gliden64RomOpen(void)
{
	// wtf gliden64
	RDRAMSize = 8 * 1024 * 1024 - 1;
	api().RomOpen();
	return 1;
}

EXPORT m64p_error CALL gliden64PluginGetVersion(
	m64p_plugin_type * _PluginType,
	int * _PluginVersion,
	int * _APIVersion,
	const char ** _PluginNamePtr,
	int * _Capabilities
)
{
	return api().PluginGetVersion(_PluginType, _PluginVersion, _APIVersion, _PluginNamePtr, _Capabilities);
}

EXPORT m64p_error CALL gliden64PluginStartup(
	m64p_dynlib_handle CoreLibHandle,
	void *Context,
	void (*DebugCallback)(void *, int, const char *)
)
{
	return api().PluginStartup(CoreLibHandle);
}

EXPORT m64p_error CALL gliden64PluginShutdown(void)
{
	return api().PluginShutdown();
}

EXPORT void CALL gliden64ReadScreen2(void *dest, int *width, int *height, int front)
{
	api().ReadScreen2(dest, width, height, front);
}

EXPORT void CALL gliden64SetRenderingCallback(void (*callback)())
{
	api().SetRenderingCallback(callback);
}

EXPORT void CALL gliden64ResizeVideoOutput(int width, int height)
{
	api().ResizeVideoOutput(width, height);
}

} // extern "C"
