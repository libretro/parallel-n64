/* parallel-RDP's primitive decoding (parallel-rdp/primitive_decode.c):
 * the RDP's triangle and rectangle commands, as the edges and
 * coefficients the renderer draws.
 *
 * A triangle command has 8 words of edges, then a shade block, a
 * texture block and a depth block as bits 2, 1 and 0 of its opcode say.
 * Y is s11.2; X and the slopes are s11.16 read with one bit less of
 * fraction. A coefficient is 16 bits of integer and 16 of fraction in
 * separate words. A block the command does not have reads as hardware
 * latches it: the shade block as the command's first two words in every
 * slot, the depth block as those two words twice. Rectangles are two
 * vertical edges at 10.2 corners, with their texture steps s5.10. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "primitive_decode.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_primitive_decode: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static uint32_t words[44];
static rdp_triangle_setup_t setup;
static rdp_attribute_setup_t attr;

/* the triangle command @opcode, every word of its blocks different */
static void triangle(unsigned opcode)
{
	unsigned i;
	for (i = 0; i < 44; i++)
		words[i] = 0x01010101u * (i + 1) + 0x00800000u * i;
	/* left major (flip), tile 5, yl -8191/4, ym 100/4, yh -1/4 */
	words[0] = (opcode << 24) | 0x800000u | (5u << 16) | 0x2001u;
	words[1] = (100u << 16) | 0x3fffu;
	words[2] = 0xf8000001u; /* xl: the lowest s11.16 and a bit, above junk */
	words[3] = 0xfffffffcu; /* dxldy: -1 */
	words[4] = 0x00010003u; /* xh */
	words[5] = 0x80000000u; /* dxhdy: negative, 0 in the bits kept */
	words[6] = 0x07fffffeu; /* xm: the highest */
	words[7] = 40;          /* dxmdy */
	memset(&setup, 0x55, sizeof(setup));
	memset(&attr, 0x55, sizeof(attr));
	rdp_decode_triangle(words, 0, &setup, &attr);
}

static int32_t high(uint32_t integer, uint32_t fraction)
{
	return (int32_t)((integer & 0xffff0000u) | (fraction >> 16));
}

static int32_t low(uint32_t integer, uint32_t fraction)
{
	return (int32_t)((integer << 16) | (fraction & 0xffffu));
}

static bool shade_is(const uint32_t *b)
{
	return attr.r == high(b[0], b[4]) && attr.g == low(b[0], b[4]) && attr.b == high(b[1], b[5])
		&& attr.a == low(b[1], b[5]) && attr.drdx == high(b[2], b[6]) && attr.dgdx == low(b[2], b[6])
		&& attr.dbdx == high(b[3], b[7]) && attr.dadx == low(b[3], b[7]) && attr.drde == high(b[8], b[12])
		&& attr.dgde == low(b[8], b[12]) && attr.dbde == high(b[9], b[13]) && attr.dade == low(b[9], b[13])
		&& attr.drdy == high(b[10], b[14]) && attr.dgdy == low(b[10], b[14]) && attr.dbdy == high(b[11], b[15])
		&& attr.dady == low(b[11], b[15]);
}

static bool tex_is(const uint32_t *b)
{
	return attr.s == high(b[0], b[4]) && attr.t == low(b[0], b[4]) && attr.w == high(b[1], b[5])
		&& attr.dsdx == high(b[2], b[6]) && attr.dtdx == low(b[2], b[6]) && attr.dwdx == high(b[3], b[7])
		&& attr.dsde == high(b[8], b[12]) && attr.dtde == low(b[8], b[12]) && attr.dwde == high(b[9], b[13])
		&& attr.dsdy == high(b[10], b[14]) && attr.dtdy == low(b[10], b[14]) && attr.dwdy == high(b[11], b[15]);
}

static bool tex_is_zero(void)
{
	return !attr.s && !attr.t && !attr.w && !attr.dsdx && !attr.dtdx && !attr.dwdx && !attr.dsde && !attr.dtde
		&& !attr.dwde && !attr.dsdy && !attr.dtdy && !attr.dwdy;
}

static bool z_is(uint32_t z, uint32_t dzdx, uint32_t dzde, uint32_t dzdy)
{
	return attr.z == (int32_t)z && attr.dzdx == (int32_t)dzdx && attr.dzde == (int32_t)dzde
		&& attr.dzdy == (int32_t)dzdy;
}

static void test_edges(void)
{
	static const uint8_t zero[sizeof(rdp_triangle_setup_t)] = { 0 };
	rdp_triangle_setup_t padded;

	CHECK(sizeof(rdp_triangle_setup_t) == 32 && sizeof(rdp_attribute_setup_t) == 128,
			"the setups are the sizes the shaders read");

	triangle(0x08);
	CHECK(setup.yl == -8191 && setup.ym == 100 && setup.yh == -1, "Y is 14 bits, signed");
	CHECK(setup.xl == -67108864 && setup.xh == 32769 && setup.xm == 67108863,
			"X is 28 bits, signed, without its lowest bit");
	CHECK(setup.dxldy == -1 && setup.dxhdy == 0 && setup.dxmdy == 5,
			"a slope is 28 bits from bit 2 up, signed, without its lowest bit");
	CHECK(setup.tile == 5, "the tile");
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DO_OFFSET_BIT),
			"left major, with the offset when the major edge's slope has the same sign");

	/* nothing of the setup is left as it was, padding included */
	memset(&padded, 0, sizeof(padded));
	padded.xh = setup.xh; padded.xm = setup.xm; padded.xl = setup.xl;
	padded.yh = setup.yh; padded.ym = setup.ym; padded.yl = setup.yl;
	padded.dxhdy = setup.dxhdy; padded.dxmdy = setup.dxmdy; padded.dxldy = setup.dxldy;
	padded.flags = setup.flags; padded.tile = setup.tile;
	CHECK(!memcmp(&padded, &setup, sizeof(setup)) && memcmp(&setup, zero, sizeof(setup)),
			"the whole setup is written: it is compared and hashed by its bytes");

	words[0] &= ~0x800000u;
	rdp_decode_triangle(words, 0, &setup, &attr);
	CHECK(setup.flags == 0, "right major with a negative slope: no flip, no offset");
	words[5] = 0x7ffffffcu;
	rdp_decode_triangle(words, 0, &setup, &attr);
	CHECK(setup.flags == TRIANGLE_SETUP_DO_OFFSET_BIT && setup.dxhdy == -1, "right major with a positive one: the offset");
	words[0] |= 0x800000u;
	rdp_decode_triangle(words, 0, &setup, &attr);
	CHECK(setup.flags == TRIANGLE_SETUP_FLIP_BIT, "left major with a positive one: the flip alone");

	rdp_decode_triangle(words, RDP_DECODE_COPY_CYCLE, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_SKIP_XFRAC_BIT), "in copy cycle X fractions are skipped");
	rdp_decode_triangle(words, RDP_DECODE_FILL_CYCLE | RDP_DECODE_NATIVE_TEX_RECT, &setup, &attr);
	CHECK(setup.flags == TRIANGLE_SETUP_FLIP_BIT, "the fill cycle and the rectangle quirk are not a triangle's");
	rdp_decode_triangle(words, RDP_DECODE_NATIVE_TEXTURE_LOD, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_NATIVE_LOD_BIT), "the LOD quirk is");

	/* the tile field is 6 bits wide; the two above it are not the tile */
	words[0] |= 0x3f0000u;
	rdp_decode_triangle(words, 0, &setup, &attr);
	CHECK(setup.tile == 63, "the tile field's six bits");
}

static void test_blocks(void)
{
	uint32_t header[16];
	unsigned i, opcode;

	for (opcode = 0x08; opcode <= 0x0f; opcode++)
	{
		const bool shade = (opcode & 4) != 0, tex = (opcode & 2) != 0, z = (opcode & 1) != 0;
		const uint32_t *block = words + 8;

		triangle(opcode);
		for (i = 0; i < 16; i += 2)
		{
			header[i] = words[0];
			header[i + 1] = words[1];
		}
		CHECK(rdp_triangle_words(opcode) == 8u + (shade ? 16 : 0) + (tex ? 16 : 0) + (z ? 4 : 0),
				"a triangle is its edges and the blocks its opcode names");
		CHECK(setup.yl == -8191 && setup.xh == 32769, "the edges come first");
		if (shade)
		{
			CHECK(shade_is(block), "the shade block follows the edges");
			block += 16;
		}
		else
			CHECK(shade_is(header), "without a shade block, shade is the command's first two words in every slot");
		if (tex)
		{
			CHECK(tex_is(block), "the texture block follows it");
			block += 16;
		}
		else
			CHECK(tex_is_zero(), "without a texture block the texture coefficients are zero");
		if (z)
			CHECK(z_is(block[0], block[1], block[2], block[3]), "the depth block is last");
		else
			CHECK(z_is(words[0], words[1], words[0], words[1]),
					"without a depth block, depth is the command's first two words twice");
	}

	/* the halves of a coefficient */
	triangle(0x0f);
	words[8] = 0x12345678u;
	words[12] = 0x9abcdef0u;
	words[24] = 0xfedcba98u;
	words[28] = 0x76543210u;
	words[40] = 0x80000001u;
	rdp_decode_triangle(words, 0, &setup, &attr);
	CHECK(attr.r == 0x12349abc && attr.g == 0x5678def0, "red is the high halves of two words, green the low halves");
	CHECK(attr.s == (int32_t)0xfedc7654u && attr.t == (int32_t)0xba983210u, "and so are s and t");
	CHECK(attr.z == (int32_t)0x80000001u, "depth is one word");
}

static void test_rectangles(void)
{
	uint32_t r[4];

	/* (xl, yl) = (0x234, 0x561), (xh, yh) = (0x012, 0x040), with junk above */
	r[0] = (0xf6u << 24) | (0x234u << 12) | 0x561u;
	r[1] = (0xfdu << 24) | (0x012u << 12) | 0x040u;
	r[2] = (0x8001u << 16) | 0x7fffu;
	r[3] = (0xfc00u << 16) | 0x0400u;

	memset(&setup, 0x55, sizeof(setup));
	rdp_decode_fill_rectangle(r, 0, &setup);
	CHECK(setup.xh == 0x012 << 13 && setup.xl == 0x234 << 13 && setup.xm == 0x234 << 13 && setup.yh == 0x040
			&& setup.ym == 0x561 && setup.yl == 0x561 && !setup.dxhdy && !setup.dxmdy && !setup.dxldy && !setup.tile,
			"a rectangle is two vertical edges at its corners");
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"a fill rectangle is not upscaled");
	rdp_decode_fill_rectangle(r, RDP_DECODE_COPY_CYCLE, &setup);
	CHECK(setup.yl == 0x563 && setup.ym == 0x563 && setup.yh == 0x040
			&& setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"in copy cycle the last line is drawn whole");
	rdp_decode_fill_rectangle(r, RDP_DECODE_FILL_CYCLE, &setup);
	CHECK(setup.yl == 0x563, "and in fill cycle");
	rdp_decode_fill_rectangle(r, RDP_DECODE_NATIVE_TEXTURE_LOD | RDP_DECODE_NATIVE_TEX_RECT, &setup);
	CHECK(setup.yl == 0x561 && setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"the quirks are not a fill rectangle's");

	/* ---- textured ---- */
	memset(&setup, 0x55, sizeof(setup));
	memset(&attr, 0x55, sizeof(attr));
	rdp_decode_texture_rectangle(r, false, 0, &setup, &attr);
	CHECK(setup.xh == 0x012 << 13 && setup.xl == 0x234 << 13 && setup.xm == 0x234 << 13 && setup.yh == 0x040
			&& setup.ym == 0x561 && setup.yl == 0x561 && setup.tile == 5 && setup.flags == TRIANGLE_SETUP_FLIP_BIT,
			"a texture rectangle has a tile, and is upscaled");
	CHECK(attr.s == (int32_t)0x80010000u && attr.t == 0x7fff0000, "s and t are 16 bits of integer");
	CHECK(attr.dsdx == -1024 * 2048 && attr.dtde == 1024 * 2048 && attr.dtdy == 1024 * 2048 && !attr.dtdx
			&& !attr.dsde && !attr.dsdy, "s steps along X and t along Y, s5.10");
	CHECK(!attr.r && !attr.a && !attr.drdx && !attr.z && !attr.w && !attr.dzdx && !attr.dwdx && !attr.dwde
			&& !attr.dzdy, "and nothing else is set");

	rdp_decode_texture_rectangle(r, true, 0, &setup, &attr);
	CHECK(attr.s == (int32_t)0x80010000u && attr.t == 0x7fff0000 && attr.dtdx == 1024 * 2048
			&& attr.dsde == -1024 * 2048 && attr.dsdy == -1024 * 2048 && !attr.dsdx && !attr.dtde && !attr.dtdy,
			"flipped, t steps along X and s along Y");
	CHECK(setup.tile == 5 && setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"a flipped one is not upscaled");

	rdp_decode_texture_rectangle(r, false, RDP_DECODE_NATIVE_TEX_RECT, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT),
			"nor is any with the rectangle quirk");
	rdp_decode_texture_rectangle(r, false, RDP_DECODE_NATIVE_TEXTURE_LOD, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_NATIVE_LOD_BIT), "the LOD quirk");
	rdp_decode_texture_rectangle(r, false, RDP_DECODE_COPY_CYCLE, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_SKIP_XFRAC_BIT) && setup.yl == 0x563,
			"in copy cycle X fractions are skipped and the last line is whole");
	rdp_decode_texture_rectangle(r, true, RDP_DECODE_FILL_CYCLE, &setup, &attr);
	CHECK(setup.flags == (TRIANGLE_SETUP_FLIP_BIT | TRIANGLE_SETUP_DISABLE_UPSCALING_BIT) && setup.yl == 0x563,
			"in fill cycle only the last line");

	/* the largest steps */
	r[3] = (0x7fffu << 16) | 0x8000u;
	rdp_decode_texture_rectangle(r, false, 0, &setup, &attr);
	CHECK(attr.dsdx == 32767 * 2048 && attr.dtdy == -32768 * 2048, "the largest steps");
}

int main(void)
{
	test_edges();
	test_blocks();
	test_rectangles();

	if (failures)
	{
		fprintf(stderr, "test_primitive_decode: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_primitive_decode: ok\n");
	return 0;
}
