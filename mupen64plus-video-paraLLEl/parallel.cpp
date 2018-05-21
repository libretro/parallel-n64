#include "Gfx #1.3.h"
#include "m64p_config.h"
#include "m64p_types.h"
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "rdp.hpp"

extern "C" {

#ifdef HAVE_RDP_DUMP
#include "rdp_dump.h"
#endif

extern int retro_return(bool just_flipping);

void parallelChangeWindow(void)
{
}

void parallelReadScreen2(void *dest, int *width, int *height, int front)
{
}

void parallelDrawScreen(void)
{
}

void parallelGetDllInfo(PLUGIN_INFO *PluginInfo)
{
	PluginInfo->Version = 0x0001;
	PluginInfo->Type = 2;
	strcpy(PluginInfo->Name, "Tiny Tiger's RDP");
	PluginInfo->NormalMemory = true;
	PluginInfo->MemoryBswaped = true;
}

void parallelSetRenderingCallback(void (*callback)(int))
{
}

int parallelInitiateGFX(GFX_INFO Gfx_Info)
{
#ifdef HAVE_RDP_DUMP
	const char *env = getenv("RDP_DUMP");
	if (env)
		rdp_dump_init(env, 8 * 1024 * 1024);
#endif

	return true;
}

void parallelMoveScreen(int xpos, int ypos)
{
}

void parallelProcessDList(void)
{
}

void parallelProcessRDPList(void)
{
	RDP::process_commands();
}

void parallelRomClosed(void)
{
#ifdef HAVE_RDP_DUMP
	rdp_dump_end();
#endif
}

int parallelRomOpen(void)
{
   unsigned i;
   char name[21] = "DEFAULT";

   for (i = 0; i < 20; i++)
      name[i] = gfx_info.HEADER[(32+i)^3];
   name[20] = 0;

   while (name[strlen(name)-1] == ' ')
      name[strlen(name)-1] = 0;

   RDP::set_scissor_variables(name);

	return 1;
}

void parallelUpdateScreen(void)
{
	VI::complete_frame();
	retro_return(true);
}

void parallelShowCFB(void)
{
	parallelUpdateScreen();
}

void parallelViStatusChanged(void)
{
}

void parallelViWidthChanged(void)
{
}

void parallelFBWrite(unsigned int addr, unsigned int size)
{
}

void parallelFBRead(unsigned int addr)
{
}

void parallelFBGetFrameBufferInfo(void *pinfo)
{
}

m64p_error parallelPluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion,
                                    const char **PluginNamePtr, int *Capabilities)
{
	/* set version info */
	if (PluginType != NULL)
		*PluginType = M64PLUGIN_GFX;

	if (PluginVersion != NULL)
		*PluginVersion = 0x016304;

	if (APIVersion != NULL)
		*APIVersion = 0x020100;

	if (PluginNamePtr != NULL)
		*PluginNamePtr = "Tiny Tiger Vulkan LLE RDP Plugin";

	if (Capabilities != NULL)
		*Capabilities = 0;

	return M64ERR_SUCCESS;
}

bool parallel_init(const struct retro_hw_render_interface_vulkan *vulkan)
{
   RDP::vulkan = vulkan;
   return RDP::init();
}

void parallel_deinit()
{
   RDP::deinit();
   RDP::vulkan = nullptr;
}

unsigned parallel_frame_width()
{
   return VI::width;
}

unsigned parallel_frame_height()
{
   return VI::height;
}

bool parallel_frame_is_valid()
{
   return VI::valid;
}

void parallel_begin_frame()
{
   RDP::begin_frame();
}

void parallel_set_dithering(unsigned type)
{
   RDP::set_dithering(type);
}

}
