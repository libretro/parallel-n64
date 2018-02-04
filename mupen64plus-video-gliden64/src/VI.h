#ifndef VI_H
#define VI_H

#include <stdint.h>

struct VIInfo
{
	uint32_t width, widthPrev, height, real_height;
	float rwidth, rheight;
	uint32_t lastOrigin;
	bool interlaced;
	bool PAL;

	VIInfo() :
		width(0), widthPrev(0), height(0), real_height(0),
		lastOrigin(-1), interlaced(false), PAL(false)
	{}
};

extern VIInfo VI;

void VI_UpdateSize();
void VI_UpdateScreen();

#endif

