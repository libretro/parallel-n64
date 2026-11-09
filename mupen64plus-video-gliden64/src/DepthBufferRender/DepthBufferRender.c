/***************************************************************** */
/* */
/* Software rendering into N64 depth buffer */
/* Idea and N64 depth value format by Orkin */
/* Polygon rasterization algorithm is taken from FATMAP2 engine by Mats Byggmastar, mri@penti.sit.fi */
/* */
/* Created by Gonetz, Dec 2004 */
/* */
/***************************************************************** */

/***************************************************************** */
/* */
/* Adopted for GLideN64 by Gonetz, Dec 2016 */
/* */
/***************************************************************** */

#include <retro_miscellaneous.h>
#include <retro_inline.h>
#include "N64.h"
#include "gDP.h"
#include "DepthBufferRender.h"

static vertexi * max_vtx;                   /* Max y vertex (ending vertex) */
static vertexi * start_vtx, *end_vtx;      /* First and last vertex in array */
static vertexi * right_vtx, *left_vtx;     /* Current right and left vertex */

static int right_height, left_height;
static int right_x, right_dxdy, left_x, left_dxdy;
static int left_z, left_dzdy;

static INLINE int imul16(int x, int y)        /* (x * y) >> 16 */
{
	return (((s64)x) * ((s64)y)) >> 16;
}

static INLINE int imul14(int x, int y)        /* (x * y) >> 14 */
{
	return (((s64)x) * ((s64)y)) >> 14;
}
static INLINE int idiv16(int x, int y)        /* (x << 16) / y */
{
	x = (((s64)x) << 16) / ((s64)y);
	return x;
}

static INLINE int iceil(int x)
{
	x += 0xffff;
	return (x >> 16);
}

static
void RightSection(void)
{
	int prestep;
	/* Walk backwards trough the vertex array */

	vertexi * v2, *v1 = right_vtx;
	if (right_vtx > start_vtx)
		v2 = right_vtx - 1;
	else
		v2 = end_vtx;         /* Wrap to end of array */
	right_vtx = v2;

	/* v1 = top vertex */
	/* v2 = bottom vertex */

	/* Calculate number of scanlines in this section */

	right_height = iceil(v2->y) - iceil(v1->y);
	if (right_height <= 0)
		return;

	/* Guard against possible div overflows */

	if (right_height > 1) {
		/* OK, no worries, we have a section that is at least */
		/* one pixel high. Calculate slope as usual. */

		int height = v2->y - v1->y;
		right_dxdy = idiv16(v2->x - v1->x, height);
	} else {
		/* Height is less or equal to one pixel. */
		/* Calculate slope = width * 1/height */
		/* using 18:14 bit precision to avoid overflows. */

		int inv_height = (0x10000 << 14) / (v2->y - v1->y);
		right_dxdy = imul14(v2->x - v1->x, inv_height);
	}

	/* Prestep initial values */

	prestep = (iceil(v1->y) << 16) - v1->y;
	right_x = v1->x + imul16(prestep, right_dxdy);
}

static
void LeftSection(void)
{
	int prestep;
	/* Walk forward trough the vertex array */

	vertexi * v2, *v1 = left_vtx;
	if (left_vtx < end_vtx)
		v2 = left_vtx + 1;
	else
		v2 = start_vtx;      /* Wrap to start of array */
	left_vtx = v2;

	/* v1 = top vertex */
	/* v2 = bottom vertex */

	/* Calculate number of scanlines in this section */

	left_height = iceil(v2->y) - iceil(v1->y);
	if (left_height <= 0)
		return;

	/* Guard against possible div overflows */

	if (left_height > 1) {
		/* OK, no worries, we have a section that is at least */
		/* one pixel high. Calculate slope as usual. */

		int height = v2->y - v1->y;
		left_dxdy = idiv16(v2->x - v1->x, height);
		left_dzdy = idiv16(v2->z - v1->z, height);
	} else {
		/* Height is less or equal to one pixel. */
		/* Calculate slope = width * 1/height */
		/* using 18:14 bit precision to avoid overflows. */

		int inv_height = (0x10000 << 14) / (v2->y - v1->y);
		left_dxdy = imul14(v2->x - v1->x, inv_height);
		left_dzdy = imul14(v2->z - v1->z, inv_height);
	}

	/* Prestep initial values */

	prestep = (iceil(v1->y) << 16) - v1->y;
	left_x = v1->x + imul16(prestep, left_dxdy);
	left_z = v1->z + imul16(prestep, left_dzdy);
}


void gli_Rasterize(vertexi * vtx, int vertices, int dzdx, const u16 * zLUT, u32 depthBufferWidth)
{
	vertexi * min_vtx;
	int min_y;
	int max_y;
	int n;
	u16 * destptr;
	int y1;
	int shift;
	start_vtx = vtx;        /* First vertex in array */

	/* Search trough the vtx array to find min y, max y */
	/* and the location of these structures. */

	min_vtx = vtx;
	max_vtx = vtx;

	min_y = vtx->y;
	max_y = vtx->y;

	vtx++;

	for (n = 1; n < vertices; n++) {
		if (vtx->y < min_y) {
			min_y = vtx->y;
			min_vtx = vtx;
		} else if (vtx->y > max_y) {
			max_y = vtx->y;
			max_vtx = vtx;
		}
		vtx++;
	}

	/* OK, now we know where in the array we should start and */
	/* where to end while scanning the edges of the polygon */

	left_vtx = min_vtx;    /* Left side starting vertex */
	right_vtx = min_vtx;    /* Right side starting vertex */
	end_vtx = vtx - 1;      /* Last vertex in array */

	/* Search for the first usable right section */

	do {
		if (right_vtx == max_vtx)
			return;
		RightSection();
	} while (right_height <= 0);

	/* Search for the first usable left section */

	do {
		if (left_vtx == max_vtx)
			return;
		LeftSection();
	} while (left_height <= 0);

	destptr = (u16*)(RDRAM + gDP.depthImageAddress);
	y1 = iceil(min_y);
	if (y1 >= (int)gDP.scissor.lry)
		return;

	for (;;) {
		int width;
		int x1 = iceil(left_x);
		if (x1 < (int)gDP.scissor.ulx)
			x1 = (int)gDP.scissor.ulx;
		width = iceil(right_x) - x1;
		if (x1 + width >= (int)gDP.scissor.lrx)
			width = (int)(gDP.scissor.lrx - x1 - 1);

		if (width > 0 && y1 >= (int)gDP.scissor.uly) {
			int trueZ;
			int idx;
			u16 encodedZ;
			int x;

			/* Prestep initial z */

			int prestep = (x1 << 16) - left_x;
			int z = left_z + imul16(prestep, dzdx);

			shift = x1 + y1*depthBufferWidth;
			/*draw to depth buffer */
			for (x = 0; x < width; x++)	{
				trueZ = z / 8192;
				if (trueZ < 0)
					trueZ = 0;
				encodedZ = zLUT[trueZ];
				idx = (shift + x) ^ 1;
				if (encodedZ < destptr[idx])
					destptr[idx] = encodedZ;
				z = MIN(z + dzdx, 0x7fffffff);
			}
		}

		/*destptr += rdp.zi_width; */
		y1++;
		if (y1 >= (int)gDP.scissor.lry)
			return;

		/* Scan the right side */

		if (--right_height <= 0) {               /* End of this section? */
			do {
				if (right_vtx == max_vtx)
					return;
				RightSection();
			} while (right_height <= 0);
		} else
			right_x += right_dxdy;

		/* Scan the left side */

		if (--left_height <= 0) {                /* End of this section? */
			do {
				if (left_vtx == max_vtx)
					return;
				LeftSection();
			} while (left_height <= 0);
		} else {
			left_x += left_dxdy;
			left_z += left_dzdy;
		}
	}
}
