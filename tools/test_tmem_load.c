/* parallel-RDP's loads into TMEM (parallel-rdp/tmem_load.c): load tile,
 * load TLUT and load block, as the TMEM update shader runs them.
 *
 * - The command's operands, and when a load loads nothing.
 * - The RDRAM a load reads, to bring it up to date first: whole 8 byte
 *   words, half a byte a 4-bit texel.
 * - A tile load of more than 4 KiB (2 KiB in YUV) in parts of an even
 *   number of lines, and none at all when one line is too much.
 * - The upload: its size in 64-bit TMEM words by the tile's texel size,
 *   where it reads RDRAM and writes TMEM, and the loads not done: YUV
 *   other than 16 bit, 32-bit tiles other than RGBA, a 4-bit texture
 *   (which crashes the RDP), mismatched sizes, a TLUT of more than one
 *   line or into a 32-bit tile.
 * - Load block: a dT that never steps is a tile load; otherwise its line
 *   length bounds, from an even or uneven dT, and its stride. */
#include <stdio.h>
#include <string.h>
#include <boolean.h>

#include "tmem_load.h"

static unsigned failures;

#define CHECK(cond, what) \
	do \
	{ \
		if (!(cond)) \
		{ \
			fprintf(stderr, "test_tmem_load: FAIL (line %d): %s\n", __LINE__, what); \
			failures++; \
		} \
	} while (0)

static rdp_load_tile_info_t info;
static rdp_tile_meta_t meta;
static rdp_upload_info_t up;

/* a load of (sl, tl)..(sh, th), in 10.2 for tile and TLUT loads */
static void load(enum rdp_upload_mode mode, unsigned size, unsigned width, unsigned sl, unsigned tl, unsigned sh,
		unsigned th)
{
	uint32_t words[2];
	words[0] = (0x34u << 24) | (sl << 12) | tl;
	words[1] = (3u << 24) | (sh << 12) | th;
	memset(&info, 0x55, sizeof(info));
	rdp_decode_load(words, 0x100000, width, RDP_TEXTURE_FORMAT_RGBA, size, mode, &info);
}

static void tile(unsigned fmt, unsigned size, unsigned offset, unsigned stride)
{
	memset(&meta, 0, sizeof(meta));
	meta.fmt = (uint8_t)fmt;
	meta.size = (uint8_t)size;
	meta.offset = offset;
	meta.stride = stride;
}

static enum rdp_load_result upload(uint32_t tmem_offset)
{
	memset(&up, 0x55, sizeof(up));
	return rdp_load_upload_info(&info, &meta, tmem_offset, &up);
}

static void test_decode(void)
{
	static const uint8_t zero[4] = { 0 };
	uint32_t words[2];

	CHECK(sizeof(rdp_upload_info_t) == 64, "an upload is 64 bytes, as the shader reads it");
	words[0] = (0x34u << 24) | (0xa23u << 12) | 0x456u;
	words[1] = (7u << 24) | (0x789u << 12) | 0xabcu;
	memset(&info, 0x55, sizeof(info));
	rdp_decode_load(words, 0x1234567, 321, RDP_TEXTURE_FORMAT_IA, RDP_TEXTURE_SIZE_8, RDP_UPLOAD_TLUT, &info);
	CHECK(info.slo == 0xa23 && info.tlo == 0x456 && info.shi == 0x789 && info.thi == 0xabc && info.tex_addr == 0x1234567
			&& info.tex_width == 321 && info.fmt == RDP_TEXTURE_FORMAT_IA && info.size == RDP_TEXTURE_SIZE_8
			&& info.mode == RDP_UPLOAD_TLUT, "a load's corners, 12 bits each, and the texture image");
	CHECK(!memcmp((const uint8_t *)&info + 18, zero, 2), "its padding is zero");
}

static void test_empty(void)
{
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 31 << 2, 7 << 2);
	CHECK(!rdp_load_is_empty(&info), "a 32x8 tile load");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 8 << 2, 31 << 2, (7 << 2) | 3);
	CHECK(rdp_load_is_empty(&info), "bottom above top: nothing");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 8 << 2, 31 << 2, (8 << 2) | 0);
	CHECK(!rdp_load_is_empty(&info), "one line");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, (8 << 2) | 1, 31 << 2, (8 << 2) | 0);
	CHECK(!rdp_load_is_empty(&info), "(whole lines: T 8.25 to 8 is line 8)");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 1 << 2, 0, 0, 0);
	CHECK(rdp_load_is_empty(&info), "right one left of left: none");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 2 << 2, 0, 0, 0);
	CHECK(!rdp_load_is_empty(&info), "two left of it wraps round to 4095 texels");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 3, 0, 0, 0);
	CHECK(!rdp_load_is_empty(&info), "(whole texels: S 0.75 is texel 0)");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2047, 0);
	CHECK(!rdp_load_is_empty(&info), "a block of 2048 texels");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2048, 0);
	CHECK(rdp_load_is_empty(&info), "a block of more is nothing");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 1, 0, 0, 0);
	CHECK(rdp_load_is_empty(&info), "nor wrapping round");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 1, 0, 0, 0);
	info.shi = 0xfff;
	info.slo = 0;
	CHECK(rdp_load_is_empty(&info), "nor 4096: 0");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 5, 9 << 2, 5, 0);
	CHECK(!rdp_load_is_empty(&info), "a block ignores T");
}

static void test_range(void)
{
	uint32_t addr, size;

	/* 32x8 at (16, 4) of a 64 wide 16-bit image */
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 16 << 2, 4 << 2, (47 << 2) | 3, 11 << 2);
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 + (4 * 64 + 16) * 2 && size == (7 * 64 + 32) * 2, "a tile load reads from its first texel to its last");
	info.size = RDP_TEXTURE_SIZE_32;
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 + (4 * 64 + 16) * 4 && size == (7 * 64 + 32) * 4, "32 bit texels");
	info.size = RDP_TEXTURE_SIZE_8;
	info.shi = 46 << 2;
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 + 4 * 64 + 16 && size == ((7 * 64 + 31 + 7) & ~7u), "8 bit, in whole 8 byte words");
	info.size = RDP_TEXTURE_SIZE_4;
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 + (4 * 64 + 16) / 2 && size == (((7 * 64 + 31) / 2 + 7) & ~7u),
			"4 bit: half a byte a texel");

	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 10, 3, 109, 0x800);
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 + (10 + 3 * 64) * 2 && size == 200, "a block reads its texels from S, T on");
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255 << 2, 0);
	rdp_load_rdram_range(&info, &addr, &size);
	CHECK(addr == 0x100000 && size == 512, "a TLUT of 256 entries");
}

static void test_parts(void)
{
	/* 64 16-bit texels a line: 128 bytes; 32 lines fit */
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_16, 0, 128);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 63 << 2, 31 << 2);
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_WHOLE, "4 KiB in one");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 63 << 2, 32 << 2);
	CHECK(rdp_load_lines_per_part(&info, &meta) == 32, "more in parts of 32 lines");
	meta.stride = 136;
	CHECK(rdp_load_lines_per_part(&info, &meta) == 30, "a wider stride: 30, an even number");
	meta.stride = 128;
	meta.fmt = RDP_TEXTURE_FORMAT_YUV;
	CHECK(rdp_load_lines_per_part(&info, &meta) == 16, "YUV in 2 KiB parts");
	meta.fmt = RDP_TEXTURE_FORMAT_RGBA;
	meta.size = RDP_TEXTURE_SIZE_32;
	meta.stride = 0;
	CHECK(rdp_load_lines_per_part(&info, &meta) == 8, "a 32-bit tile of 16-bit texels covers four times as much");
	meta.size = RDP_TEXTURE_SIZE_8;
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_WHOLE, "an 8-bit one a quarter");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_16, 0, 0);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 63 << 2, 32 << 2);
	CHECK(rdp_load_lines_per_part(&info, &meta) == 32, "a stride below the line's is the line's");
	meta.stride = 2048;
	CHECK(rdp_load_lines_per_part(&info, &meta) == 2, "two lines of 2 KiB");
	meta.stride = 2056;
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_TOO_WIDE, "one line, an odd number, is too wide");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 63 << 2, 0);
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_WHOLE, "but one line alone is whole");
	/* 63 16-bit texels are 16 words: 128 bytes */
	meta.stride = 0;
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 64, 0, 0, 62 << 2, 32 << 2);
	CHECK(rdp_load_lines_per_part(&info, &meta) == 32, "a line is whole 64-bit words");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2047, 0x800);
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_WHOLE, "a block is never split");
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255 << 2, 40 << 2);
	CHECK(rdp_load_lines_per_part(&info, &meta) == RDP_LOAD_WHOLE, "nor a TLUT");
}

static void test_tile_upload(void)
{
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_16, 0x100, 64);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 320, 3 << 2, 2 << 2, 33 << 2, 9 << 2);
	CHECK(upload(0x10) == RDP_LOAD_OK, "a 31x8 16-bit tile load");
	CHECK(up.width == 32 && up.height == 8 && up.vram_effective_width == 32, "31 texels are 32: four to a 64-bit word");
	CHECK(up.vram_addr == 0x100000 + (2 * 320 + 3) * 2 && up.vram_width == 320 && up.vram_size == RDP_TEXTURE_SIZE_16,
			"read from (3, 2) of the image");
	CHECK(up.tmem_offset == 0x110 && up.tmem_stride_words == 32 && up.tmem_size == RDP_TEXTURE_SIZE_16
			&& up.tmem_fmt == RDP_TEXTURE_FORMAT_RGBA && up.mode == RDP_UPLOAD_TILE && up.dxt == 0
			&& up.inv_tmem_stride_words == 1.0f / 32.0f && up.padding == 0,
			"written to the tile, a part's offset past it, at its stride in 16-bit words");
	CHECK(upload(0xf10) == RDP_LOAD_OK && up.tmem_offset == 0x010, "TMEM wraps at 4 KiB");

	tile(RDP_TEXTURE_FORMAT_CI, RDP_TEXTURE_SIZE_8, 0, 64);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_8, 320, 0, 0, 20 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 12 && up.vram_effective_width == 24 && up.vram_size == 1,
			"21 8-bit texels are 24: 12 16-bit words");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_8, 320, 0, 0, 8 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 8 && up.vram_effective_width == 16, "9 are 16: eight to a word");
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_8, 320, 1 << 2, 0, 0, 0);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "nothing to load is not loaded");
	tile(RDP_TEXTURE_FORMAT_CI, RDP_TEXTURE_SIZE_4, 0, 64);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_8, 320, 0, 0, 20 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 8, "21 into a 4-bit tile are 32: 8 16-bit words");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_32, 0, 64);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_32, 320, 0, 0, 20 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 22 && up.vram_effective_width == 22, "21 32-bit texels are 22");
	tile(RDP_TEXTURE_FORMAT_YUV, RDP_TEXTURE_SIZE_16, 0, 64);
	load(RDP_UPLOAD_TILE, RDP_TEXTURE_SIZE_16, 320, 0, 0, 20 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 12 && up.vram_effective_width == 24, "YUV counts as 32 bit");

	/* ---- not done ---- */
	tile(RDP_TEXTURE_FORMAT_YUV, RDP_TEXTURE_SIZE_32, 0, 64);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "YUV into a 32-bit tile");
	tile(RDP_TEXTURE_FORMAT_YUV, RDP_TEXTURE_SIZE_16, 0, 64);
	info.size = RDP_TEXTURE_SIZE_8;
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "YUV from an 8-bit image");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_16, 0, 64);
	info.size = RDP_TEXTURE_SIZE_4;
	CHECK(upload(0) == RDP_LOAD_CRASHES, "a 4-bit image crashes the RDP");
	tile(RDP_TEXTURE_FORMAT_IA, RDP_TEXTURE_SIZE_32, 0, 64);
	info.size = RDP_TEXTURE_SIZE_32;
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "a 32-bit tile other than RGBA");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_8, 0, 64);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "a 32-bit image into an 8-bit tile");
	meta.size = RDP_TEXTURE_SIZE_4;
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "or a 4-bit one");
	info.size = RDP_TEXTURE_SIZE_16;
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "a 16-bit image into a 4-bit tile");
	meta.size = RDP_TEXTURE_SIZE_8;
	CHECK(upload(0) == RDP_LOAD_OK, "into an 8-bit one is done");
	info.size = RDP_TEXTURE_SIZE_8;
	meta.size = RDP_TEXTURE_SIZE_32;
	CHECK(upload(0) == RDP_LOAD_OK, "and an 8-bit image into a 32-bit tile");
}

static void test_tlut(void)
{
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_4, 0x800, 0);
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 64 && up.height == 1 && up.vram_effective_width == 256
			&& up.mode == RDP_UPLOAD_TLUT && up.tmem_offset == 0x800, "a TLUT of 256 16-bit entries: one at a time");
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_16, 64, 0, 0, 14 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_OK && up.vram_effective_width == 15, "15 entries are 15");
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255 << 2, 1 << 2);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "two lines are not loaded");
	meta.size = RDP_TEXTURE_SIZE_32;
	load(RDP_UPLOAD_TLUT, RDP_TEXTURE_SIZE_32, 64, 0, 0, 255 << 2, 0);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "nor into a 32-bit tile");
	meta.size = RDP_TEXTURE_SIZE_8;
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 128 && up.vram_effective_width == 256,
			"a 32-bit image into an 8-bit tile is a TLUT's to do");
}

static void test_block(void)
{
	/* dT never steps: a tile load of one line */
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_16, 0x40, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 6, 2, 6 + 15, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.mode == RDP_UPLOAD_TILE && up.width == 16 && up.height == 1
			&& up.tmem_stride_words == 0 && up.vram_width == 64 && up.dxt == 0,
			"a block whose T does not step is a tile load");
	CHECK(up.vram_addr == 0x100000 + (2 * 64 + 6) * 2 && up.tmem_offset == 0x40, "from S, T");

	/* 64 texels a line: dT 2048/16, even */
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.mode == RDP_UPLOAD_BLOCK && up.width == 256 && up.height == 1
			&& up.vram_width == 256 && up.vram_effective_width == 256, "a block of 256 that steps T");
	CHECK(up.dxt == 0x80 << 5 && up.tmem_stride_words == 8 && up.min_t_mod == 1.0f / 24.0f
			&& up.max_t_mod == 1.0f / 24.0f, "16 words a line, 8 more of stride: T is a word / 24");
	meta.stride = 0;
	CHECK(upload(0) == RDP_LOAD_OK && up.tmem_stride_words == 0 && up.min_t_mod == 1.0f / 16.0f
			&& up.max_t_mod == 1.0f / 16.0f, "no stride: a word / 16");

	/* dT 0x2ab: lines of 2 or 3 words, the error 1 a line; over the 21
	 * lines of 256 texels it stays under a step: always 3, and 8 of stride */
	meta.stride = 64;
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255, 0x2ab);
	CHECK(upload(0) == RDP_LOAD_OK && up.min_t_mod == 1.0f / 11.0f && up.max_t_mod == 1.0f / 11.0f,
			"an uneven dT whose error stays under a step is taken as even");
	/* dT 0x3ff: 2 or 3 words, the error 1021 a line: it adds up */
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2047, 0x3ff);
	CHECK(upload(0) == RDP_LOAD_OK && up.min_t_mod == 1.0f / 11.0f && up.max_t_mod == 1.0f / 10.0f,
			"one where it adds up keeps both bounds");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2047, 0x155);
	CHECK(upload(0) == RDP_LOAD_OK && up.min_t_mod == 1.0f / 15.0f && up.max_t_mod == 1.0f / 14.0f,
			"lines of 6 or 7 words");
	/* dT 0x801: a line of 1 word, just under */
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 7, 0x801);
	CHECK(upload(0) == RDP_LOAD_OK && up.mode == RDP_UPLOAD_BLOCK && up.min_t_mod == 1.0f / 9.0f
			&& up.max_t_mod == 1.0f / 9.0f, "a dT past one line a word steps from the second word");

	/* sizes */
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_32, 0, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_32, 64, 0, 0, 255, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.tmem_stride_words == 16 && up.min_t_mod == 1.0f / 32.0f
			&& up.width == 256, "a 32-bit tile goes round twice a word: twice the stride");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_32, 64, 0, 0, 2047, 0x7ff);
	CHECK(upload(0) == RDP_LOAD_OK, "uneven, the image the same size");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_32, 0, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 2047, 0x7ff);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "uneven into a 32-bit tile from another size is not loaded");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.dxt == (0x80 << 5) >> 1 && up.min_t_mod == 1.0f / 48.0f,
			"a smaller image into a larger tile steps slower and has twice the words a line");
	tile(RDP_TEXTURE_FORMAT_RGBA, RDP_TEXTURE_SIZE_8, 0, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255, 0x80);
	CHECK(upload(0) == RDP_LOAD_SKIPPED, "a larger image into a smaller tile is not loaded");
	tile(RDP_TEXTURE_FORMAT_YUV, RDP_TEXTURE_SIZE_16, 0, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_16, 64, 0, 0, 255, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.tmem_stride_words == 16, "YUV goes round twice too");
	tile(RDP_TEXTURE_FORMAT_CI, RDP_TEXTURE_SIZE_8, 0, 64);
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_8, 64, 0, 0, 511, 0x80);
	CHECK(upload(0) == RDP_LOAD_OK && up.width == 256 && up.vram_effective_width == 512 && up.dxt == 0x80 << 5
			&& up.min_t_mod == 1.0f / 24.0f, "8-bit: 8 texels a word");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_8, 64, 0, 0, 7, 0x800);
	CHECK(upload(0) == RDP_LOAD_OK && up.mode == RDP_UPLOAD_TILE, "one word: T never steps");
	load(RDP_UPLOAD_BLOCK, RDP_TEXTURE_SIZE_8, 64, 0, 0, 8, 0x800);
	CHECK(upload(0) == RDP_LOAD_OK && up.mode == RDP_UPLOAD_BLOCK, "two words: it does");
}

int main(void)
{
	test_decode();
	test_empty();
	test_range();
	test_parts();
	test_tile_upload();
	test_tlut();
	test_block();

	if (failures)
	{
		fprintf(stderr, "test_tmem_load: %u failure(s)\n", failures);
		return 1;
	}
	printf("test_tmem_load: ok\n");
	return 0;
}
