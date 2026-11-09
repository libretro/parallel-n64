#ifndef _FRAME_BUFFER_INFO_H_
#define _FRAME_BUFFER_INFO_H_

#include <boolean.h>
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One entry of the core's FBGetFrameBufferInfo array */
typedef struct FBInfoEntry
{
	unsigned int addr;
	unsigned int size;
	unsigned int width;
	unsigned int height;
} FBInfoEntry;

void gli_fbinfo_reset(void);
void gli_fbinfo_write(u32 addr, u32 size);
void gli_fbinfo_read(u32 addr);
void gli_fbinfo_get_info(void *pinfo);
bool gli_FBInfo_supported(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRAME_BUFFER_INFO_H_ */
