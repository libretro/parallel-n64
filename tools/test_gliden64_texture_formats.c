/* test_gliden64_texture_formats -- the host format GLideN64 picks for a
 * texture holds every bit the N64 texels have.
 *
 * A 16-bit host format is taken only where it is exact: RGBA4444 for
 * texels of four bits a channel (I4, IA 4+4), RGBA5551 for 5551 texels and
 * palettes. Anything else is RGBA8888. IA 3+1 texels went into RGBA4444:
 * three bits of intensity do not fit four evenly, and the shades came out
 * up to 7/255 off what the RDP makes of them (the bits repeated to 8).
 * Their decoder must give exactly that.
 *
 * Depth maps (hack_LoadDepthTextures, Resident Evil 2) are R16F: their
 * words must become the halves nearest their linear depth, two bytes a
 * texel. Floats went into the Vulkan map raw, and its characters came out
 * as stripes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mupen64plus-video-gliden64/src/Types.h"
#include "../mupen64plus-video-gliden64/src/gli_texdecode.h"

u64 TMEM[512];
u8 *RDRAM;
u32 RDRAMSize;

static int failures;

/* An IEEE half, finite, as a double */
static double half_value(u32 h)
{
	u32 e = (h >> 10) & 0x1f;
	double v = (double)(h & 0x3ff);
	int scale;
	if (e != 0)
		v += 1024.0;
	else
		e = 1;
	/* v * 2^(e - 25) */
	for (scale = (int)e - 25; scale < 0; ++scale)
		v *= 0.5;
	for (; scale > 0; --scale)
		v *= 2.0;
	if (h & 0x8000)
		v = -v;
	return v;
}

/* The linear depth of an N64 depth word */
static double depth_value(u32 w)
{
	static const u32 shift[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };
	static const u32 add[8] = { 0x00000, 0x20000, 0x30000, 0x38000, 0x3c000, 0x3e000, 0x3f000, 0x3f800 };
	const u32 e = (w >> 13) & 7;
	return (double)((((w >> 2) & 0x7ff) << shift[e]) + add[e]) / 262143.0;
}

static void check_depth(void)
{
	static u16 words[65536];
	u32 w, prev = 0;
	for (w = 0; w < 65536; ++w)
		words[w] = (u16)w;
	if (gli_tex_depth_to_half(words, 65536) != words) {
		fprintf(stderr, "depth: not decoded in place\n");
		failures++;
	}
	for (w = 0; w < 65536; ++w) {
		const u32 h = words[w];
		const double x = depth_value(w), d = half_value(h) - x;
		const double below = h > 0 ? x - half_value(h - 1) : 1.0;
		const double above = half_value(h + 1) - x;
		const double ad = d < 0 ? -d : d;
		/* the nearest half; at a tie, the even one */
		if (h > 0x3c00 || ad > below || ad > above ||
				((ad == below || ad == above) && (h & 1))) {
			fprintf(stderr, "depth word %04x: half %04x (%.9g) for %.9g\n",
					(unsigned)w, (unsigned)h, half_value(h), x);
			failures++;
			return;
		}
		/* nearer is never deeper */
		if (h < prev) {
			fprintf(stderr, "depth word %04x: half %04x below %04x\n", (unsigned)w, (unsigned)h, (unsigned)prev);
			failures++;
			return;
		}
		prev = h;
	}
	if (words[0] != 0 || words[0xfffc] != 0x3c00 || words[0x2000] != 0x3800) {
		fprintf(stderr, "depth: 0, far and the second exponent are %04x %04x %04x\n",
				(unsigned)words[0], (unsigned)words[0xfffc], (unsigned)words[0x2000]);
		failures++;
	}
}

/* size 0..3 (4, 8, 16, 32 bits), format 0..4 (RGBA, YUV, CI, IA, I) */
static int exact_in_16(u32 tlut, u32 size, u32 format, u32 host)
{
	if (tlut == 0) {
		if (host == GLI_TEXF_RGBA4)
			return (size == 0 && format != 3) || (size == 1 && format == 3);
		return size == 2 && format == 0; /* RGBA 5551 */
	}
	/* a palette of 5551 entries; an IA palette is 8+8 */
	return host == GLI_TEXF_RGB5A1 && tlut != 3 && size <= 2 && (size != 2 || format == 0 || format == 3);
}

int main(void)
{
	u32 tlut, size, format, v;
	const gli_tex_format *ia31;

	for (tlut = 0; tlut < 4; ++tlut)
		for (size = 0; size < 4; ++size)
			for (format = 0; format < 5; ++format) {
				const gli_tex_format *f = gli_tex_format_get(tlut, size, format);
				u32 probe[4] = { 0, 0, 0, 0 };
				u8 texels[16];
				memset(texels, 0xA5, sizeof(texels));
				/* a format with no decoder writes nothing */
				f->row32(probe, texels, 0, 0, 1, 0xFFFF, 0xFFFF, 0);
				if (probe[0] == 0 && f->autoFormat != GLI_TEXF_RGBA8) {
					u16 probe16[4] = { 0, 0, 0, 0 };
					f->row16(probe16, texels, 0, 0, 1, 0xFFFF, 0xFFFF, 0);
					if (probe16[0] == 0)
						continue;
				}
				if (f->autoFormat != GLI_TEXF_RGBA8 && !exact_in_16(tlut, size, format, f->autoFormat)) {
					fprintf(stderr, "LUT %u, size %u, format %u: host format %u loses bits\n",
							tlut, size, format, (unsigned)f->autoFormat);
					failures++;
				}
			}

	/* IA 3+1: intensity i becomes i repeated to eight bits, alpha 0 or 255 */
	ia31 = gli_tex_format_get(0, 0, 3);
	if (ia31->autoFormat != GLI_TEXF_RGBA8) {
		fprintf(stderr, "IA 3+1 is not RGBA8888\n");
		failures++;
	}
	for (v = 0; v < 16; ++v) {
		u8 texels[8];
		u32 out[2] = { 0, 0 };
		const u32 i = v >> 1;
		const u32 want_i = (i << 5) | (i << 2) | (i >> 1);
		const u32 want_a = (v & 1) ? 255u : 0u;
		const u8 *px = (const u8 *)out;
		memset(texels, (int)((v << 4) | v), sizeof(texels));
		ia31->row32(out, texels, 0, 0, 1, 0xFFFF, 0xFFFF, 0);
		if (px[0] != want_i || px[1] != want_i || px[2] != want_i || px[3] != want_a) {
			fprintf(stderr, "IA 3+1 texel %u: %u,%u,%u,%u, wanted %u and alpha %u\n",
					v, px[0], px[1], px[2], px[3], want_i, want_a);
			failures++;
		}
	}

	check_depth();

	if (failures) {
		fprintf(stderr, "test_gliden64_texture_formats: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_texture_formats: ok\n");
	return 0;
}
