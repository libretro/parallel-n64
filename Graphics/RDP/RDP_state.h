#ifndef _RDP_STATE_H
#define _RDP_STATE_H

#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXCMD 0x100000

typedef struct
{
	uint32_t w2, w3;
	uint32_t cmd_ptr;
	uint32_t cmd_cur;
	uint32_t cmd_data[MAXCMD + 32];
} RDPInfo;

extern RDPInfo __RDP;

#ifdef __cplusplus
}
#endif

#endif
