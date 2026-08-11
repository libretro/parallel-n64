#include <encodings/crc32.h>
#include "CRC.h"
#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

/* The 256-entry CRC table this file used to carry is gone: encoding_crc32()
 * in libretro-common computes the same CRC-32/ISO-HDLC with a slicing-by-8
 * kernel off its own static tables.  Dropping it also removes the signed
 * left shift of 1 into bit 31 that the table build performed, which is
 * undefined behaviour and was reported by UBSan. */
void CRC_Init()
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
	return XXH3_64bits_withSeed(buffer, count, crc);
}

u64 CRC_CalculatePalette(u64 crc, const void * buffer, u32 count )
{
	u8 *p = (u8*) buffer;
	while (count--) {
		crc = XXH3_64bits_withSeed(p, 2, crc);
		p += 8;
	}
	return crc;
}
