#include <encodings/crc32.h>
#include "CRC.h"
#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"
#include <arm_neon.h>

/* The 256-entry CRC table this file used to carry is gone: encoding_crc32()
 * in libretro-common computes the same CRC-32/ISO-HDLC with a slicing-by-8
 * kernel off its own static tables.  Dropping it also removes the signed
 * left shift of 1 into bit 31 that the table build performed, which is
 * undefined behaviour and was reported by UBSan. */
void CRC_Init() {
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

#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4	668265263U
#define PRIME32_5	374761393U

#if defined(_MSC_VER)
#  define XXH_rotl32(x,r) _rotl(x,r)
#else
#  define XXH_rotl32(x, r) ((x << r) | (x >> (32 - r)))
#endif

u32 ReliableHash32NEON(const void *input, size_t len, u32 seed) {
	if (((uintptr_t) input & 3) != 0) {
		// Cannot handle misaligned data. Fall back to XXH32.
		return XXH32(input, len, seed);
	}

	const u8 *p = (const u8 *) input;
	const u8 *const bEnd = p + len;
	u32 h32;


	if (len >= 16) {
		const unsigned char *const limit = bEnd - 16;
		u32 v1 = seed + PRIME32_1 + PRIME32_2;
		u32 v2 = seed + PRIME32_2;
		u32 v3 = seed + 0;
		u32 v4 = seed - PRIME32_1;

		uint32x4_t prime32_1q = vdupq_n_u32(PRIME32_1);
		uint32x4_t prime32_2q = vdupq_n_u32(PRIME32_2);
		uint32x4_t vq = vcombine_u32(vcreate_u32(v1 | ((u64) v2 << 32)),
									 vcreate_u32(v3 | ((u64) v4 << 32)));

		do {
			__builtin_prefetch(p + 0xc0, 0, 0);
			vq = vmlaq_u32(vq, vld1q_u32((const u32 *) p), prime32_2q);
			vq = vorrq_u32(vshlq_n_u32(vq, 13), vshrq_n_u32(vq, 32 - 13));
			p += 16;
			vq = vmulq_u32(vq, prime32_1q);
		} while (p <= limit);

		v1 = vgetq_lane_u32(vq, 0);
		v2 = vgetq_lane_u32(vq, 1);
		v3 = vgetq_lane_u32(vq, 2);
		v4 = vgetq_lane_u32(vq, 3);

		h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
	} else {
		h32 = seed + PRIME32_5;
	}

	h32 += (u32) len;

	while (p <= bEnd - 4) {
		h32 += *(const u32 *) p * PRIME32_3;
		h32 = XXH_rotl32(h32, 17) * PRIME32_4;
		p += 4;
	}

	while (p < bEnd) {
		h32 += (*p) * PRIME32_5;
		h32 = XXH_rotl32(h32, 11) * PRIME32_1;
		p++;
	}

	h32 ^= h32 >> 15;
	h32 *= PRIME32_2;
	h32 ^= h32 >> 13;
	h32 *= PRIME32_3;
	h32 ^= h32 >> 16;

	return h32;
}

u32 CRC_Calculate(u32 crc, const void *buffer, u32 count) {
	return ReliableHash32NEON(buffer, count, crc);
}

u32 CRC_CalculatePalette(u32 crc, const void *buffer, u32 count) {
	u8 *p = (u8 *) buffer;
	while (count--) {
		crc = ReliableHash32NEON(p, 2, crc);
		p += 8;
	}
	return crc;
}
