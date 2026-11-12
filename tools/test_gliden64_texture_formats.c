/* test_gliden64_texture_formats -- the host format GLideN64 picks for a
 * texture holds every bit the N64 texels have.
 *
 * A 16-bit host format is taken only where it is exact: RGBA4444 for
 * texels of four bits a channel (I4, IA 4+4), RGBA5551 for 5551 texels and
 * palettes. Anything else is RGBA8888. IA 3+1 texels went into RGBA4444:
 * three bits of intensity do not fit four evenly, and the shades came out
 * up to 7/255 off what the RDP makes of them (the bits repeated to 8).
 * Their decoder must give exactly that.
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

	if (failures) {
		fprintf(stderr, "test_gliden64_texture_formats: %d failures\n", failures);
		return 1;
	}
	printf("test_gliden64_texture_formats: ok\n");
	return 0;
}
