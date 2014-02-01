#ifndef DEPTHBUFFER_H
#define DEPTHBUFFER_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __LIBRETRO__ // Prefix symbol
#define depthBuffer gln64depthBuffer
#endif

typedef struct DepthBuffer
{
    struct DepthBuffer *higher, *lower;

    u32 address, cleared;
} DepthBuffer;

typedef struct
{
    DepthBuffer *top, *bottom, *current;
    int numBuffers;
} DepthBufferInfo;

extern DepthBufferInfo depthBuffer;

void DepthBuffer_Init(void);
void DepthBuffer_Destroy(void);
void DepthBuffer_SetBuffer( u32 address );
void DepthBuffer_RemoveBuffer( u32 address );
DepthBuffer *DepthBuffer_FindBuffer( u32 address );

#ifdef __cplusplus
}
#endif

#endif

