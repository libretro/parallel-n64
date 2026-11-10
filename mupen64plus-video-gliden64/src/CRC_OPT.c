#include <encodings/crc32.h>
#include <string.h>
#include <encodings/xxh3.h>
#include <retro_endianness.h>
#include <retro_inline.h>
#include "CRC.h"

/* The 256-entry CRC table this file used to carry is gone: encoding_crc32()
 * in libretro-common computes the same CRC-32/ISO-HDLC with a slicing-by-8
 * kernel off its own static tables.  Dropping it also removes the signed
 * left shift of 1 into bit 31 that the table build performed, which is
 * undefined behaviour and was reported by UBSan. */
void CRC_Init(void)
{
}

/* Byte-wise CRC-32/ISO-HDLC.  Expressed in terms of encoding_crc32(),
 * whose slicing-by-8 kernel replaces the local 256-entry table.
 *
 * encoding_crc32() applies zlib's conventions - it complements the seed on
 * the way in and the result on the way out - so undoing both reproduces the
 * bare table loop this used to run, bit for bit, for every seed.  The one
 * live caller (GBI.cpp) passes 0xFFFFFFFF, for which this reduces exactly to
 * encoding_crc32(0, buffer, count); microcode detection compares the result
 * against a table of known constants, so it has to stay bit-exact. */
u32 CRC_Calculate_Strict( u32 crc, const void * buffer, u32 count )
{
	return ~encoding_crc32(~crc, (const u8*)buffer, count) ^ crc;
}

u64 CRC_Calculate( u64 crc, const void * buffer, u32 count )
{
	return xxh3_64(buffer, count, crc);
}

/* A palette entry sits every 8 bytes of TMEM. Entries are packed four
 * to a word in registers and hashed in one call per 16, so the hash's
 * 8-byte loads forward from 8-byte stores. */
static INLINE u64 palette_entry(const u8 *p, u32 i)
{
	u16 e;
	memcpy(&e, p + 8 * i, 2);
#if RETRO_IS_BIG_ENDIAN
	return (u64)e << (48 - 16 * (i & 3));
#else
	return (u64)e << (16 * (i & 3));
#endif
}

static INLINE u64 palette_word(const u8 *p)
{
	return palette_entry(p, 0) | palette_entry(p, 1)
		| palette_entry(p, 2) | palette_entry(p, 3);
}

u64 CRC_CalculatePalette(u64 crc, const void * buffer, u32 count )
{
	const u8 *p = (const u8*)buffer;
	u64 w[4];
	for (; count >= 16; count -= 16, p += 128) {
		w[0] = palette_word(p);
		w[1] = palette_word(p + 32);
		w[2] = palette_word(p + 64);
		w[3] = palette_word(p + 96);
		crc = xxh3_64(w, 32, crc);
	}
	if (count) {
		u32 i;
		w[0] = w[1] = w[2] = w[3] = 0;
		for (i = 0; i < count; ++i)
			w[i >> 2] |= palette_entry(p, i);
		crc = xxh3_64(w, count * 2, crc);
	}
	return crc;
}
