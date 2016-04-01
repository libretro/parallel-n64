#ifndef _GLIDEN64_FRAME_BUFFER_INFO_H_
#define _GLIDEN64_FRAME_BUFFER_INFO_H_

#include <stdint.h>

#include "m64p_plugin.h"
#include "PluginAPI.h"

void FrameBufferWrite(uint32_t addr, uint32_t size);

void FrameBufferWriteList(FrameBufferModifyEntry *plist, uint32_t size);

void FrameBufferRead(uint32_t addr);

void FrameBufferGetInfo(void* data);

#endif
