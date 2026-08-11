#include <string.h>
#include <encodings/crc32.h>
#include "CRC.h"

/* The 256-entry CRC table this file used to carry is gone: encoding_crc32()
 * in libretro-common computes the same CRC-32/ISO-HDLC with a slicing-by-8
 * kernel off its own static tables.  Dropping it also removes the signed
 * left shift of 1 into bit 31 that the table build performed, which is
 * undefined behaviour and was reported by UBSan.
 *
 * encoding_crc32() applies zlib's conventions - it complements the seed on
 * the way in and the result on the way out - so undoing both reproduces the
 * bare table loop these functions used to run, bit for bit, for every seed. */
static INLINE u32 crc32_raw(u32 crc, const u8 *buffer, u32 count)
{
	return ~encoding_crc32(~crc, buffer, count);
}

void CRC_Init()
{
}

u64 CRC_Calculate( u64 crc, const void * buffer, u32 count )
{
	const u32 orig = static_cast<u32>(crc);

	return crc32_raw(orig, reinterpret_cast<const u8*>(buffer), count) ^ orig;
}

u32 CRC_Calculate_Strict( u32 crc, const void * buffer, u32 count )
{
	return static_cast<u32>(CRC_Calculate(crc, buffer, count));
}

/* Palette entries are 16 bits stored every 8 bytes, so the CRC covers 2 of
 * every 8 source bytes.  Gathering each run of pairs into a contiguous
 * staging buffer lets one encoding_crc32() call cover 256 entries at a time
 * rather than paying a call per entry; the byte sequence fed to the CRC is
 * unchanged, so the result is identical. */
u64 CRC_CalculatePalette(u64 crc, const void * buffer, u32 count )
{
	const u32 orig = static_cast<u32>(crc);
	u32 crc32      = orig;
	const u8 *p    = reinterpret_cast<const u8*>(buffer);
	u8 staging[512];

	while (count > 0) {
		const u32 n = (count > 256) ? 256 : count;
		u32 i;

		for (i = 0; i < n; ++i, p += 8) {
			staging[i * 2 + 0] = p[0];
			staging[i * 2 + 1] = p[1];
		}

		crc32  = crc32_raw(crc32, staging, n * 2);
		count -= n;
	}

	return crc32 ^ orig;
}
