/* test_gliden64_fill_rdram -- GLideN64's fills of a frame buffer into RDRAM
 * against its asynchronous colour copies.
 *
 * Perfect Dark's pause screen blurs the frame just drawn: the CPU reads it
 * back from RDRAM. An asynchronous copy of that frame lands a frame later,
 * so the game reads the buffer as it was before the frame was drawn. The
 * frame began with a fill, and that fill went into RDRAM at once: the game
 * blurred the cleared screen, and the pause screen faded to black.
 *
 * Now a fill leaves RDRAM alone where the buffer's last asynchronous copy
 * is there and nothing has been drawn since. The copy of what is drawn
 * now replaces it whole, so until then a reader gets the last picture.
 * Nothing waits for the GPU and nothing more is written; the fill is no
 * longer written for nothing.
 *
 * The frame is run through gli_fb_rdram_fill_buffer for each copy mode and
 * buffer kind, checking what RDRAM holds, whether the clear is noted, and
 * that the validity check still holds the buffer as RDRAM has it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/N64.h"
#include "../mupen64plus-video-gliden64/src/VI.h"
#include "../mupen64plus-video-gliden64/src/gDP.h"
#include "../mupen64plus-video-gliden64/src/gSP.h"
#include "../mupen64plus-video-gliden64/src/Config.h"
#include "../mupen64plus-video-gliden64/src/gli_fb_rdram.h"

#define RAM_BYTES (1u << 20)
#define FB_ADDR   0x10000u
#define FB_W      320u
#define FB_H      240u
#define FB_BYTES  (FB_W * FB_H * 2u)
#define FILL      0x00010001u /* black, coverage bit set */

u8 *RDRAM;
u32 RDRAMSize;
VIInfo VI;
gDPInfo gDP;
gSPInfo gSP;
Config gliden64Config;

static u8 snapshot[FB_BYTES];
static int failures;

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s: %s\n", name, what); ++failures; } } while (0)

static u32 picture(u32 i) { return 0x8421u * (i + 1u) | 0x00010001u; }

/* the buffer as its last copy left it: the picture, noted as copied */
static void land_copy(gli_fb *fb)
{
	u32 *p = (u32 *)(RDRAM + FB_ADDR);
	u32 i;
	for (i = 0; i < FB_BYTES / 4; ++i)
		p[i] = picture(i);
	fb->m_copiedToRdram = 1;
	fb->m_cleared = 0;
	gli_fb_save_rdram(fb);
}

static int holds_picture(void)
{
	const u32 *p = (const u32 *)(RDRAM + FB_ADDR);
	u32 i;
	for (i = 0; i < FB_BYTES / 4; ++i)
		if (p[i] != picture(i))
			return 0;
	return 1;
}

static int holds_fill(void)
{
	const u32 *p = (const u32 *)(RDRAM + FB_ADDR);
	u32 i;
	for (i = 0; i < FB_BYTES / 4; ++i)
		if (p[i] != FILL)
			return 0;
	return 1;
}

/* One frame: the last copy landed, then the frame starts with a full
 * fill. @expect_kept: RDRAM keeps the picture. */
static void frame(const char *name, u32 mode, u32 width, int depth, int drawn_since, int expect_kept)
{
	gli_fb fb;
	int filled;

	memset(&fb, 0, sizeof(fb));
	fb.m_startAddress = FB_ADDR;
	fb.m_endAddress = FB_ADDR + FB_BYTES - 1;
	fb.m_width = width;
	fb.m_height = FB_H;
	fb.m_size = G_IM_SIZ_16b;
	fb.m_isDepthBuffer = (u8)depth;
	fb.m_RdramCopy = snapshot;
	fb.m_RdramCopyCapacity = sizeof(snapshot);

	config.frameBufferEmulation.copyToRDRAM = mode;
	VI.width = FB_W;
	gDP.colorImage.address = FB_ADDR;
	gDP.colorImage.width = width;
	gDP.colorImage.size = G_IM_SIZ_16b;
	gDP.scissor.ulx = 0.0f;
	gDP.scissor.uly = 0.0f;
	gDP.scissor.lrx = (f32)width;
	gDP.scissor.lry = (f32)FB_H;
	gDP.fillColor.color = FILL;

	land_copy(&fb);
	if (drawn_since)
		fb.m_copiedToRdram = 0;

	filled = gli_fb_rdram_fill_buffer(&fb, 0, 0, (s32)width, FB_H);

	if (expect_kept) {
		CHECK(!filled, "filled");
		CHECK(holds_picture(), "RDRAM lost the last picture");
		CHECK(!fb.m_cleared, "clear noted");
	} else {
		CHECK(filled, "not filled");
		CHECK(width != FB_W || holds_fill(), "RDRAM not filled");
		CHECK(fb.m_cleared, "clear not noted");
	}
	/* either way RDRAM is what the buffer expects there */
	CHECK(gli_fb_is_valid(&fb, 1, 0), "invalid after the fill");
}

int main(void)
{
	RDRAM = (u8 *)calloc(1, RAM_BYTES);
	RDRAMSize = RAM_BYTES - 1;

	/* asynchronous copies, double and triple buffered: the picture stays */
	frame("async", ctDoubleBuffer, FB_W, 0, 0, 1);
	frame("triple", ctTripleBuffer, FB_W, 0, 0, 1);
	/* synchronous or no copies: the fill goes in as the RDP writes it */
	frame("sync", ctSync, FB_W, 0, 0, 0);
	frame("off", ctDisable, FB_W, 0, 0, 0);
	/* something was drawn since the copy: it is not the last picture */
	frame("async, drawn since", ctDoubleBuffer, FB_W, 0, 1, 0);
	/* the copy at full sync is of the screen's buffers only */
	frame("async, auxiliary", ctDoubleBuffer, FB_W / 2, 0, 0, 0);
	frame("async, depth", ctDoubleBuffer, FB_W, 1, 0, 0);

	free(RDRAM);
	if (failures != 0) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("test_gliden64_fill_rdram: ok\n");
	return 0;
}
