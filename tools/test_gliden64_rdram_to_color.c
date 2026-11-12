/* test_gliden64_rdram_to_color -- GLideN64's RDRAM to colour buffer copy.
 *
 * A frame buffer the CPU wrote is 5 bits a channel; the colour buffer it
 * is copied into is 8. Each 5-bit value must become the 8-bit value
 * nearest to it, the top bits repeated in the low ones, so that black
 * stays black and white becomes white: shifted left by three, white came
 * out 248 and every channel up to 7 short. Taking the top five bits back
 * must give the value that went in. Both the whole image and the marked
 * pixels are copied that way; 32-bit pixels pass through as they are.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/gli_rdram_to_color.h"

u8 *RDRAM;
u32 RDRAMSize;

#define W 32
#define H 3

static int failures;

static void check16(const char *what, const u32 *out, int fullAlpha)
{
	u32 v;
	for (v = 0; v < 32; ++v) {
		const u32 want = (v << 3) | (v >> 2);
		u32 c;
		/* row 0 red, row 1 green, row 2 blue; alpha set on odd values */
		for (c = 0; c < 3; ++c) {
			const u32 px = out[c * W + v];
			const u32 got = (px >> (8 * c)) & 0xff;
			const u32 alpha = px >> 24;
			if (got != want || (got >> 3) != v) {
				fprintf(stderr, "%s: channel %u value %u became %u, wanted %u\n", what, c, v, got, want);
				failures++;
			}
			if (alpha != ((fullAlpha || (v & 1)) ? 0xffu : 0u)) {
				fprintf(stderr, "%s: channel %u value %u alpha %u\n", what, c, v, alpha);
				failures++;
			}
		}
	}
}

int main(void)
{
	static u16 image[W * H];
	static u32 image32[4];
	static u32 out[W * H];
	static gli_r2c_marks marks;
	u32 v, c;

	RDRAM = (u8 *)image;
	RDRAMSize = sizeof(image) - 1;
	for (c = 0; c < 3; ++c)
		for (v = 0; v < 32; ++v)
			/* pixels are stored word-swapped in pairs */
			image[(c * W + v) ^ 1] = (u16)((v << (11 - 5 * c)) | (v & 1));

	gli_r2c_copy_image(0, 2, out, W, H, 0);
	check16("image", out, 0);
	gli_r2c_copy_image(0, 2, out, W, H, 1);
	check16("image, opaque", out, 1);

	gli_r2c_marks_init(&marks);
	for (v = 0; v < W * H; ++v)
		gli_r2c_mark(&marks, v * 2);
	gli_r2c_copy_marked(&marks, 0, 2, out, W, H, 0);
	check16("marked", out, 0);

	/* 32-bit: every bit kept */
	RDRAM = (u8 *)image32;
	RDRAMSize = sizeof(image32) - 1;
	image32[0] = 0x12345678; image32[1] = 0xffffffff; image32[2] = 0x00000000; image32[3] = 0x01fe80a5;
	gli_r2c_copy_image(0, 3, out, 4, 1, 0);
	for (v = 0; v < 4; ++v) {
		const u32 s = image32[v];
		const u32 want = ((s & 0xff) << 24) | (((s >> 8) & 0xff) << 16) | (((s >> 16) & 0xff) << 8) | (s >> 24);
		if (out[v] != want) {
			fprintf(stderr, "32-bit: %08x became %08x, wanted %08x\n", s, out[v], want);
			failures++;
		}
	}

	if (failures) {
		fprintf(stderr, "test_gliden64_rdram_to_color: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_rdram_to_color: ok\n");
	return 0;
}
