/* test_rdp_coverage -- the RDP's coverage written for the renderers that keep
 * none (Graphics/rdp_coverage.h).
 *
 * Ocarina of Time, pausing, draws its frame buffer's coverage with the
 * blender (G_RM_VISCVG), copies it into an 8-bit image with a texture
 * rectangle, and filters on the CPU each pixel that image calls partly
 * covered. This holds the modes that draw coverage told from those that do
 * not, the coverage rectangle to the blend color and full coverage in each
 * pixel size, the copy to the texel's red at an even address and green at an
 * odd one, clamped to the tile, and the copy told from other rectangles.
 *
 *   ./test_rdp_coverage
 */
#include <stdio.h>
#include <string.h>

#include "rdp_coverage.h"

static int failures;

static void check(int _ok, const char *_what)
{
	if (!_ok) {
		printf("FAIL: %s\n", _what);
		failures++;
	}
}

#define RDRAM_SIZE 0x1000
static uint8_t rdram[RDRAM_SIZE];

/* a byte as the CPU reads it: RDRAM words are in host order */
static uint8_t rd8(uint32_t _a)
{
	return rdram[_a ^ 3];
}

static uint16_t rd16(uint32_t _a)
{
	return (uint16_t)((rd8(_a) << 8) | rd8(_a + 1));
}

static uint32_t rd32(uint32_t _a)
{
	return ((uint32_t)rd16(_a) << 16) | rd16(_a + 2);
}

/* G_RM_VISCVG in one cycle, as Ocarina of Time sets it */
#define VISCVG 0x0fa54044u

static void test_modes(void)
{
	check(rdpcvg_draws_coverage(VISCVG), "G_RM_VISCVG draws coverage");
	check(!rdpcvg_draws_coverage(VISCVG & ~0x4000u), "not without forced blending");
	check(!rdpcvg_draws_coverage(VISCVG & ~0x40u), "not without reading memory");
	check(!rdpcvg_draws_coverage((VISCVG & ~(3u << 26)) | (2u << 26)), "not with the pixel's color as A");
	check(!rdpcvg_draws_coverage((VISCVG & ~(3u << 22)) | (1u << 22)), "not with memory color as M");
	check(!rdpcvg_draws_coverage((VISCVG & ~(3u << 18)) | (3u << 18)), "not with B other than memory alpha");
	/* G_RM_OPA_SURF, the most common mode */
	check(!rdpcvg_draws_coverage(0x0f0a4000u), "an opaque surface draws none");
	check(RDPCVG_MAY_DRAW_COVERAGE(VISCVG), "the inline test lets G_RM_VISCVG through");
	check(!RDPCVG_MAY_DRAW_COVERAGE(VISCVG & ~0x4000u) && !RDPCVG_MAY_DRAW_COVERAGE(VISCVG & ~0x40u) &&
		!RDPCVG_MAY_DRAW_COVERAGE(0x0f0a4000u), "and stops modes without forced blending or memory reads");
}

static void test_fill(void)
{
	memset(rdram, 0x55, sizeof(rdram));
	/* 16-bit, 8 wide, from (1,1) to but not through (3,2) */
	rdpcvg_fill(rdram, RDRAM_SIZE, 0x100, 8, 2, 1, 1, 3, 2, 0xffffff);
	check(rd16(0x100 + (8 + 1) * 2) == 0xffff && rd16(0x100 + (8 + 2) * 2) == 0xffff,
		"16-bit white with full coverage is 0xffff");
	check(rd16(0x100 + (8 + 0) * 2) == 0x5555 && rd16(0x100 + (8 + 3) * 2) == 0x5555 &&
		rd16(0x100 + 1 * 2) == 0x5555 && rd16(0x100 + (16 + 1) * 2) == 0x5555,
		"nothing outside the rectangle");
	rdpcvg_fill(rdram, RDRAM_SIZE, 0x200, 8, 2, 0, 0, 1, 1, 0x804020);
	check(rd16(0x200) == ((0x10 << 11) | (0x08 << 6) | (0x04 << 1) | 1), "16-bit channels are their top five bits");

	rdpcvg_fill(rdram, RDRAM_SIZE, 0x300, 4, 3, 0, 0, 2, 1, 0x123456);
	check(rd32(0x300) == 0x123456e0u && rd32(0x304) == 0x123456e0u && rd32(0x308) == 0x55555555u,
		"32-bit: the color and the top three coverage bits of alpha");

	rdpcvg_fill(rdram, RDRAM_SIZE, 0x400, 4, 1, 0, 0, 4, 1, 0xa0b0c0);
	check(rd8(0x400) == 0xa0 && rd8(0x401) == 0xb0 && rd8(0x402) == 0xa0 && rd8(0x403) == 0xb0,
		"8-bit: red at an even address, green at an odd one");

	memset(rdram, 0x55, sizeof(rdram));
	rdpcvg_fill(rdram, RDRAM_SIZE, 0x100, 4, 2, -2, -1, 6, 1, 0xffffff);
	check(rd16(0x100) == 0xffff && rd16(0x106) == 0xffff && rd16(0x108) == 0x5555,
		"clipped to the image's first row and width");
	rdpcvg_fill(rdram, RDRAM_SIZE, RDRAM_SIZE - 2, 4, 2, 0, 0, 4, 4, 0xffffff);
	check(rd16(RDRAM_SIZE - 2) == 0xffff, "the last word of RDRAM written, none past it");
}

/* a 4x2 tile of texels (s, t) -> 0xSTxx */
static uint16_t texel(const void *_ctx, uint32_t _s, uint32_t _t)
{
	const uint16_t *tile = (const uint16_t *)_ctx;
	return tile[_t * 4 + _s];
}

static void test_texrect(void)
{
	uint16_t tile[8];
	uint32_t i;
	for (i = 0; i < 8; ++i)
		tile[i] = (uint16_t)(((i & 3) << 12) | ((i >> 2) << 8) | 0x10 | i);

	memset(rdram, 0x55, sizeof(rdram));
	/* IA: red and green are both the intensity */
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x100, 8, 0, 0, 4, 2, 0, 0, 4, 2, 1, texel, tile);
	check(rd8(0x100) == 0x00 && rd8(0x101) == 0x10 && rd8(0x103) == 0x30 && rd8(0x108) == 0x01 && rd8(0x10b) == 0x31,
		"IA: the intensity, one texel a pixel, a row a line");
	check(rd8(0x104) == 0x55 && rd8(0x110) == 0x55, "nothing past the rectangle");

	/* clamped to the tile: s from 2 for 4 pixels gives 2, 3, 3, 3 */
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x200, 8, 0, 0, 4, 3, 2, 1, 4, 2, 1, texel, tile);
	check(rd8(0x200) == 0x21 && rd8(0x201) == 0x31 && rd8(0x203) == 0x31 && rd8(0x210) == 0x21,
		"clamped to the tile's width and height");

	/* starting left of the image: the texels move along */
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x300, 8, -1, 0, 2, 1, 0, 0, 4, 2, 1, texel, tile);
	check(rd8(0x300) == 0x10 && rd8(0x301) == 0x20 && rd8(0x302) == 0x55, "clipped on the left, texels from the next");

	/* RGBA: red and green widened from five bits */
	tile[0] = (uint16_t)((0x1f << 11) | (0x10 << 6));
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x400, 8, 0, 0, 2, 1, 0, 0, 1, 1, 0, texel, tile);
	check(rd8(0x400) == 0xff && rd8(0x401) == 0x84, "RGBA: red at an even address, green at an odd one, widened");

	memset(rdram, 0x55, sizeof(rdram));
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x100, 8, 0, 0, 4, 2, 0, 0, 0, 2, 1, texel, tile);
	check(rd8(0x100) == 0x55, "an empty tile copies nothing");
}

/* RDRAM as the CPU writes it: each 32-bit word in host order */
static void wr16(uint32_t _a, uint16_t _v)
{
	rdram[_a ^ 3] = (uint8_t)(_v >> 8);
	rdram[(_a + 1) ^ 3] = (uint8_t)_v;
}

static void test_rdram_tile(void)
{
	rdpcvg_rdram_tile src;
	memset(rdram, 0, sizeof(rdram));
	/* a 16-bit image 8 wide at 0x100; the tile from its texel (2, 1) */
	wr16(0x100 + (1 * 8 + 2) * 2, 0x1234);
	wr16(0x100 + (1 * 8 + 3) * 2, 0x5678);
	wr16(0x100 + (2 * 8 + 2) * 2, 0x9abc);
	src.rdram = rdram;
	src.size = RDRAM_SIZE;
	src.bpl = 16;
	src.address = 0x100 + (1 * 8 + 2) * 2;
	check(rdpcvg_rdram_texel16(&src, 0, 0) == 0x1234, "the tile's corner texel, as the RDP reads it");
	check(rdpcvg_rdram_texel16(&src, 1, 0) == 0x5678, "the next texel along");
	check(rdpcvg_rdram_texel16(&src, 0, 1) == 0x9abc, "the next row, a row's bytes on");
	src.address = RDRAM_SIZE - 2;
	wr16(RDRAM_SIZE - 2, 0x4321);
	check(rdpcvg_rdram_texel16(&src, 0, 0) == 0x4321, "the last texel of RDRAM");
	check(rdpcvg_rdram_texel16(&src, 1, 0) == 0 && rdpcvg_rdram_texel16(&src, 0, 1) == 0, "none past it");
	src.address = RDRAM_SIZE - 1;
	check(rdpcvg_rdram_texel16(&src, 0, 0) == 0, "nor one half in it");

	/* the copy from it: IA, red and green the intensity */
	memset(rdram + 0x800, 0x55, 16);
	src.address = 0x100 + (1 * 8 + 2) * 2;
	rdpcvg_texrect_8(rdram, RDRAM_SIZE, 0x800, 8, 0, 0, 2, 1, 0, 0, 2, 2, 1, rdpcvg_rdram_texel16, &src);
	check(rd8(0x800) == 0x12 && rd8(0x801) == 0x56 && rd8(0x802) == 0x55, "copied from where the texels were loaded");
}

static void test_copy(void)
{
	/* Ocarina of Time's copy: 1 cycle, unblended, TEXEL0 */
	const uint32_t l = 0x0f0a0004u;
	check(rdpcvg_is_texel_copy(0, l, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "the coverage copy");
	check(rdpcvg_is_texel_copy(0, l, 16, 1, 1, 2, 0, 1.0, 1.0, 0), "from RGBA too, by zero of any code past 15");
	check(!rdpcvg_is_texel_copy(1, l, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not in two cycles");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 2, 2, 3, 1.0, 1.0, 0), "not into a 16-bit image");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 1, 1, 3, 1.0, 1.0, 0), "not from an 8-bit tile");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 1, 2, 2, 1.0, 1.0, 0), "not from a CI tile");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 1, 2, 3, 0.5, 1.0, 0), "not scaled across");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 1, 2, 3, 1.0, 2.0, 0), "not scaled down");
	check(!rdpcvg_is_texel_copy(0, l, 31, 1, 1, 2, 3, 1.0, 1.0, 1), "not flipped");
	check(!rdpcvg_is_texel_copy(0, l | 0x4000u, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not blended");
	check(!rdpcvg_is_texel_copy(0, l | 0x40u, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not reading memory");
	check(!rdpcvg_is_texel_copy(0, l | 0x1u, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not alpha compared");
	check(!rdpcvg_is_texel_copy(0, l | 0x10u, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not depth compared");
	check(!rdpcvg_is_texel_copy(0, l | 0x80u, 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not color on coverage");
	check(!rdpcvg_is_texel_copy(0, l | (1u << 30), 31, 1, 1, 2, 3, 1.0, 1.0, 0), "not with P other than the pixel's color");
	check(!rdpcvg_is_texel_copy(0, l, 15, 1, 1, 2, 3, 1.0, 1.0, 0), "not multiplied by something");
	check(!rdpcvg_is_texel_copy(0, l, 31, 4, 1, 2, 3, 1.0, 1.0, 0), "not adding shade");
}

int main(void)
{
	test_modes();
	test_fill();
	test_texrect();
	test_rdram_tile();
	test_copy();
	if (failures) {
		printf("%d failed\n", failures);
		return 1;
	}
	printf("rdp coverage: ok\n");
	return 0;
}
