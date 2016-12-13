#include <stdint.h>
#include <string.h>

#include "PluginAPI.h"

#include "m64p_types.h"
#include "m64p_plugin.h"

extern "C" {

EXPORT int CALL gln64RomOpen(void)
{
	api().RomOpen();
	return 1;
}

EXPORT m64p_error CALL gln64PluginGetVersion(
	m64p_plugin_type * _PluginType,
	int * _PluginVersion,
	int * _APIVersion,
	const char ** _PluginNamePtr,
	int * _Capabilities
)
{
	//return api().PluginGetVersion(_PluginType, _PluginVersion, _APIVersion, _PluginNamePtr, _Capabilities);
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL gln64PluginStartup(
	m64p_dynlib_handle CoreLibHandle,
	void *Context,
	void (*DebugCallback)(void *, int, const char *)
)
{
	//return api().PluginStartup(CoreLibHandle);
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL gln64PluginShutdown(void)
{
	//return api().PluginShutdown();
   return M64ERR_SUCCESS;
}

EXPORT void CALL gln64ReadScreen2(void *dest, int *width, int *height, int front)
{
	//api().ReadScreen2(dest, width, height, front);
}

EXPORT void CALL gln64SetRenderingCallback(void (*callback)(int))
{
	//api().SetRenderingCallback(callback);
}

EXPORT void CALL gln64FBRead(uint32_t addr)
{
	api().FBRead(addr);
}

EXPORT void CALL gln64FBWrite(uint32_t addr, uint32_t size)
{
	api().FBWrite(addr, size);
}

EXPORT void CALL gln64FBGetFrameBufferInfo(void *p)
{
	api().FBGetFrameBufferInfo(p);
}

EXPORT void CALL gln64ResizeVideoOutput(int Width, int Height)
{
	//api().ResizeVideoOutput(Width, Height);
}

} // extern "C"
