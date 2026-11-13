/* parallel-RDP's VI register decoding (parallel-rdp/vi_registers.c).
 *
 * From the VI registers a scanout works out which output lines show
 * something, where on each line the picture starts and ends, the
 * 8-pixel guard band it blanks at each edge the picture does not run
 * off, the step through the framebuffer, and how much of the
 * framebuffer that reads. A picture that starts left of, or above, the
 * visible area is cut there and its start moved on to match. PAL has
 * its own offsets and more lines. Lines nothing is shown on are all
 * zero. The RDRAM range a scanout reads follows from the same numbers,
 * with room for the antialiasing and divot filters around it. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "vi_registers.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_vi_registers: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static uint32_t regs[RDP_VI_REGISTER_COUNT];
static rdp_vi_horizontal_info_t lines[VI_MAX_OUTPUT_SCANLINES + 1];
static rdp_vi_decoded_t d;

static uint32_t pair(uint32_t hi, uint32_t lo)
{
	return (hi << 16) | lo;
}

/* a 320x240 NTSC picture filling the visible area */
static void ntsc_320x240(void)
{
	memset(regs, 0, sizeof(regs));
	regs[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_RGBA5551_BIT | VI_CONTROL_AA_MODE_RESAMP_REPLICATE_BIT;
	regs[RDP_VI_REGISTER_ORIGIN] = 0x100000;
	regs[RDP_VI_REGISTER_WIDTH] = 320;
	regs[RDP_VI_REGISTER_V_SYNC] = VI_V_SYNC_NTSC;
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC, VI_H_OFFSET_NTSC + 640);
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC, VI_V_OFFSET_NTSC + 480);
	regs[RDP_VI_REGISTER_X_SCALE] = pair(0, 512);
	regs[RDP_VI_REGISTER_Y_SCALE] = pair(0, 1024);
}

static bool all_zero(const rdp_vi_horizontal_info_t *line)
{
	static const rdp_vi_horizontal_info_t zero;
	return !memcmp(line, &zero, sizeof(zero));
}

static void decode(void)
{
	memset(lines, 0x55, sizeof(lines));
	memset(&d, 0x55, sizeof(d));
	rdp_vi_decode_registers(regs, &d, lines);
	CHECK(lines[VI_MAX_OUTPUT_SCANLINES].h_start == 0x55555555, "nothing is written past the lines");
}

static void test_full_frame(void)
{
	unsigned offset, length;

	ntsc_320x240();
	decode();
	CHECK(d.status == regs[RDP_VI_REGISTER_CONTROL] && d.vi_width == 320 && d.vi_offset == 0x100000 && !d.is_pal
			&& d.v_current_line == 0 && d.init_y_add == 1024, "the registers are taken apart");
	CHECK(d.v_start == 0 && d.v_res == 240, "a full NTSC frame shows 240 lines from the top");
	CHECK(d.h_start == 0 && d.h_res == 640, "640 pixels wide from the left");
	CHECK(d.h_start_clamp == 8 && d.h_res_clamp == 625, "less the guard band: 8 on the left, 7 on the right");
	CHECK(d.max_x == 320 && d.max_y == 240, "reading 320x240 of the framebuffer");
	CHECK(d.min_x == 4 && d.max_x_clamp == 317, "of which the columns right of the guard band are shown");
	CHECK(lines[0].h_start == 0 && lines[0].h_start_clamp == 8 && lines[0].h_end_clamp == 633
			&& lines[0].x_start == 0 && lines[0].x_add == 512 && lines[0].y_start == 0 && lines[0].y_add == 1024
			&& lines[0].y_base == 0, "each line gets the same");
	CHECK(!memcmp(&lines[0], &lines[239], sizeof(lines[0])) && all_zero(&lines[240]) && all_zero(&lines[287]),
			"down to the last shown; the rest are zero");

	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0x100000 - (2 + 2 * 320) * 2 && length == ((240 + 5) * 320 + 320 + 6) * 2,
			"the scanout reads the picture and a border for the filters");
	regs[RDP_VI_REGISTER_CONTROL] |= VI_CONTROL_DIVOT_ENABLE_BIT;
	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0x100000 - (3 + 2 * 320) * 2 && length == ((240 + 5) * 320 + 320 + 8) * 2,
			"one more column each side for the divot filter");
	regs[RDP_VI_REGISTER_CONTROL] = VI_CONTROL_TYPE_RGBA8888_BIT;
	regs[RDP_VI_REGISTER_ORIGIN] = 0x100003;
	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0x100000 - (2 + 2 * 320) * 4 && length == ((240 + 5) * 320 + 320 + 6) * 4,
			"32-bit pixels: four bytes each, from an aligned origin");

	/* the fetch bug is only emulated when two lines can read one row */
	ntsc_320x240();
	decode();
	CHECK(!rdp_vi_need_fetch_bug_emulation(&d, 1), "a step of one row a line needs no fetch bug emulation");
	regs[RDP_VI_REGISTER_Y_SCALE] = pair(0, 1023);
	decode();
	CHECK(rdp_vi_need_fetch_bug_emulation(&d, 1) && !rdp_vi_need_fetch_bug_emulation(&d, 2),
			"a smaller step does, at native resolution");
}

static void test_windowed(void)
{
	/* 40 output pixels in from each side, 20 lines down, 200 lines */
	ntsc_320x240();
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 40, VI_H_OFFSET_NTSC + 600);
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC + 40, VI_V_OFFSET_NTSC + 440);
	regs[RDP_VI_REGISTER_X_SCALE] = pair(100, 512);
	regs[RDP_VI_REGISTER_Y_SCALE] = pair(2000, 1024);
	decode();
	CHECK(d.v_start == 20 && d.v_res == 200, "a window starts and ends where the registers say");
	CHECK(d.h_start == 40 && d.h_res == 560 && d.h_start_clamp == 48 && d.h_res_clamp == 545, "across as well");
	CHECK(d.max_x == (100 + 560 * 512) >> 10 && d.max_y == 201,
			"reading as far as the step takes it from its start");
	CHECK(d.min_x == (100 + 8 * 512) >> 10 && d.max_x_clamp == (100 + 553 * 512 + 1023) >> 10,
			"shown between the guard bands");
	CHECK(all_zero(&lines[19]) && lines[20].h_start == 40 && lines[20].x_start == 100 && lines[20].y_start == 2000
			&& lines[219].h_end_clamp == 593 && all_zero(&lines[220]), "only its lines are filled");
}

static void test_cut_off(void)
{
	/* starts 20 pixels left of and 10 lines above the visible area,
	 * and runs 30 pixels past the right */
	ntsc_320x240();
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC - 20, VI_H_OFFSET_NTSC + 670);
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC - 20, VI_V_OFFSET_NTSC + 480);
	regs[RDP_VI_REGISTER_X_SCALE] = pair(0, 512);
	decode();
	CHECK(d.h_start == 0 && d.h_res == 640, "a picture wider than the screen is cut to it");
	CHECK(lines[0].x_start == 20 * 512, "its start moved on by what was cut off the left");
	CHECK(d.h_start_clamp == 0 && d.h_res_clamp == 640 && lines[0].h_start_clamp == 0 && lines[0].h_end_clamp == 640,
			"with no guard band on an edge it runs off");
	CHECK(d.v_start == 0 && lines[0].y_start == 10 * 1024, "one that starts above the screen starts lower in the picture");
	CHECK(d.v_res == 250 - 10 || d.v_res == 250, "(its line count is not adjusted)");
	CHECK(d.v_res == 250 && d.v_start + d.v_res <= VI_MAX_OUTPUT_SCANLINES && !all_zero(&lines[249]) && all_zero(&lines[250]),
			"and stays within the lines there are");

	/* far too tall: clamped */
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_NTSC, 0x3ff);
	decode();
	CHECK(d.v_start == 0 && d.v_res == 240 && all_zero(&lines[240]), "an end past the last NTSC line is cut to it");
	regs[RDP_VI_REGISTER_V_START] = pair(0x3ff, 0x3ff);
	decode();
	CHECK(d.v_start == (VI_V_END_NTSC - VI_V_OFFSET_NTSC) / 2, "(a start past the last line is cut to it as well)");
	CHECK(d.v_res <= 0 && all_zero(&lines[0]) && all_zero(&lines[287]) && d.h_res == 640 && d.max_x == 330,
			"a start past it too: no lines, but the horizontal extent is still known");
}

static void test_pal(void)
{
	unsigned offset, length;

	ntsc_320x240();
	regs[RDP_VI_REGISTER_V_SYNC] = VI_V_SYNC_PAL;
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_PAL, VI_H_OFFSET_PAL + 640);
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_PAL, VI_V_OFFSET_PAL + 576);
	regs[RDP_VI_REGISTER_V_CURRENT_LINE] = 1;
	decode();
	CHECK(d.is_pal && d.v_current_line == 1 && d.v_start == 0 && d.v_res == 288 && d.h_start == 0 && d.h_res == 640,
			"PAL has its own offsets and 288 lines a field");
	CHECK(!all_zero(&lines[287]), "all of them filled");
	CHECK(d.h_start_clamp == 8 && d.h_res_clamp == 625, "with the guard band at both edges");
	regs[RDP_VI_REGISTER_V_START] = pair(0, 0x3ff);
	decode();
	CHECK(d.v_start == 0 && d.v_res == VI_MAX_OUTPUT_SCANLINES && !all_zero(&lines[287]),
			"more lines than a PAL field has are cut to it");
	regs[RDP_VI_REGISTER_V_START] = pair(VI_V_OFFSET_PAL, VI_V_OFFSET_PAL + 576);
	regs[RDP_VI_REGISTER_V_SYNC] = VI_V_SYNC_NTSC + 25;
	decode();
	CHECK(!d.is_pal, "NTSC up to 25 lines over its count");
	regs[RDP_VI_REGISTER_V_SYNC] = VI_V_SYNC_NTSC + 26;
	decode();
	CHECK(d.is_pal, "PAL beyond");

	/* nothing to read */
	ntsc_320x240();
	regs[RDP_VI_REGISTER_ORIGIN] = 0;
	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0 && length == 0, "no origin: nothing is read");
	ntsc_320x240();
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 640, VI_H_OFFSET_NTSC + 700);
	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0 && length == 0, "a picture that starts right of the screen: nothing");
	ntsc_320x240();
	regs[RDP_VI_REGISTER_H_START] = pair(VI_H_OFFSET_NTSC + 300, VI_H_OFFSET_NTSC + 300);
	rdp_vi_scanout_memory_range(regs, &offset, &length);
	CHECK(offset == 0 && length == 0, "one with no width: nothing");

	/* without lines to fill */
	ntsc_320x240();
	memset(&d, 0x55, sizeof(d));
	rdp_vi_decode_registers(regs, &d, NULL);
	CHECK(d.v_res == 240 && d.h_res == 640 && d.max_x == 320, "the lines are optional");
}

int main(void)
{
	test_full_frame();
	test_windowed();
	test_cut_off();
	test_pal();

	if (failures)
	{
		fprintf(stderr, "test_vi_registers: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_vi_registers: ok\n");
	return 0;
}
