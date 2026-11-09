#include <string.h>
#include "Config.h"
#include "gSP.h"
#include "RSP.h"
#include "VI.h"
#include "gli_framebuffer.h"
#include "FrameBufferInfo.h"

#define FBINFO_BUFFERS 6

static const struct FrameBuffer * l_writeBuffers[FBINFO_BUFFERS];
static const struct FrameBuffer * l_readBuffers[FBINFO_BUFFERS];
static bool l_supported;

static void _clearBuffers(void)
{
	u32 i;
	for (i = 0; i < FBINFO_BUFFERS; ++i) {
		l_writeBuffers[i] = NULL;
		l_readBuffers[i] = NULL;
	}
}

void gli_fbinfo_reset(void)
{
	l_supported = false;
	_clearBuffers();
}

/* true if found; else *_idx is the first free slot, or
 * FBINFO_BUFFERS when full */
static bool _findBuffer(const struct FrameBuffer * const * _buffers, const struct FrameBuffer * _buf, u32 * _idx)
{
	u32 i = 0;
	while (i < FBINFO_BUFFERS && _buffers[i] != NULL) {
		if (_buffers[i++] == _buf) {
			*_idx = i;
			return true;
		}
	}
	*_idx = i;
	return false;
}

void gli_fbinfo_write(u32 addr, u32 size)
{
	const u32 address = RSP_SegmentToPhysical(addr);
	const struct FrameBuffer * writeBuffer = gli_fb_find(address);
	u32 idx;
	if (writeBuffer == NULL)
		return;
	if (!_findBuffer(l_writeBuffers, writeBuffer, &idx) && idx < FBINFO_BUFFERS)
		l_writeBuffers[idx] = writeBuffer;
	gli_fb_add_address(address, size);
}

void gli_fbinfo_read(u32 addr)
{
	const u32 address = RSP_SegmentToPhysical(addr);
	struct FrameBuffer * pBuffer = gli_fb_find(address);
	bool found;
	u32 idx;

	if (pBuffer == NULL || _findBuffer(l_writeBuffers, pBuffer, &idx))
		return;

	found = _findBuffer(l_readBuffers, pBuffer, &idx);
	if (gli_fb_is_depth(pBuffer)) {
		if (config.frameBufferEmulation.fbInfoReadDepthChunk != 0)
			gli_fb_copy_depth_chunk(address);
		else if (!found)
			gli_fb_copy_depth(address);
	} else {
		if (config.frameBufferEmulation.fbInfoReadColorChunk != 0)
			gli_fb_copy_chunk(address);
		else if (!found)
			gli_fb_copy_to_rdram(address, true);
	}

	if (!found && idx < FBINFO_BUFFERS)
		l_readBuffers[idx] = pBuffer;
}

void gli_fbinfo_get_info(void *pinfo)
{
	FBInfoEntry * pFBInfo = (FBInfoEntry*)pinfo;
	u32 idx = 0;
	u32 depthAddress, depthWidth;

	memset(pFBInfo, 0, sizeof(FBInfoEntry) * 6);

	if (config.frameBufferEmulation.fbInfoDisabled != 0)
		return;

	if (gli_depth_current(&depthAddress, &depthWidth)) {
		pFBInfo[idx].addr = depthAddress;
		pFBInfo[idx].width = depthWidth;
		pFBInfo[idx].height = VI.real_height;
		pFBInfo[idx++].size = 2;
	}
	gli_fb_fill_info(&pFBInfo[idx], 6 - idx);

	_clearBuffers();
	l_supported = true;
}

bool gli_FBInfo_supported(void)
{
	return l_supported;
}
