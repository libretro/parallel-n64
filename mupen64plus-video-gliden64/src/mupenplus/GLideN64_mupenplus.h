#ifndef GLIDEN64_MUPENPLUS_H
#define GLIDEN64_MUPENPLUS_H

#include "m64p_config.h"
#include "m64p_vidext.h"

#define PLUGIN_VERSION              0x020000
#define VIDEO_PLUGIN_API_VERSION	0x020200
#define CONFIG_API_VERSION          0x020000
#define VIDEXT_API_VERSION          0x030000

#ifdef __cplusplus
extern "C"
#endif
void (*renderCallback)(int);

#endif /* GLIDEN64_MUPENPLUS_H */
