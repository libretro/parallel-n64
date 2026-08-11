#include <stdint.h>

#include <encodings/crc32.h>

#include "Hash.h"

/* Texture and palette cache keying.
 *
 * This was a Jenkins one-at-a-time hash over uint32 words.  It is now
 * CRC-32/ISO-HDLC from libretro-common, which is both faster (slicing-by-8
 * against a shift-and-add chain with a serial dependency on every word) and
 * endian-independent: the old form loaded uint32s, so it produced different
 * keys on big-endian hosts for identical texture data.
 *
 * Nothing constrains the choice of function here.  These values are
 * in-memory cache keys only - they are never written to a file and never
 * compared against a stored constant, unlike GLideN64's
 * CRC_Calculate_Strict, which identifies microcodes against a fixed table
 * and therefore had to stay bit-exact.
 *
 * The count is still truncated to a multiple of 4.  The word loop it
 * replaces covered exactly floor(count/4)*4 bytes, so hashing the full
 * count would read up to three bytes the old code never touched - at an
 * RDRAM or TMEM boundary that is a read past the end of the region the
 * caller sized. */
uint32_t Hash_Calculate(uint32_t hash, const void *buffer, uint32_t count)
{
   return encoding_crc32(hash, (const uint8_t*)buffer, count & ~(uint32_t)3);
}
