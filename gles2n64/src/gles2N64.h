#ifndef GLN64_H
#define GLN64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "m64p_config.h"
#include "stdio.h"

//#define DEBUG

#define PLUGIN_NAME     "gles2n64"
#define PLUGIN_VERSION  0x000005
#define PLUGIN_API_VERSION 0x020200

#ifndef __LIBRETRO__ // Built in
extern ptr_ConfigGetSharedDataFilepath ConfigGetSharedDataFilepath;
#endif

#ifdef __LIBRETRO__ // Avoid symbol clash
#define renderCallback gln64RenderCallback
#endif

extern void (*CheckInterrupts)( void );
extern void (*renderCallback)();

#ifdef __cplusplus
}
#endif

#endif

