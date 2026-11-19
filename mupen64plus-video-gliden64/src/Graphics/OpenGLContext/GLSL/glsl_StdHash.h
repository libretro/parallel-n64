#ifndef GLI_GLSL_STD_HASH_H
#define GLI_GLSL_STD_HASH_H

/* std::hash<std::string> as each C++ library has it, which named the
 * shader storage's files: the files of before are found by the same name.
 * Only the low 32 bits were ever used. C89. */

#include <stddef.h>
#include <string.h>
#include <retro_inline.h>
#include <Types.h>

/* little endian loads, as the libraries' unaligned loads on the targets
 * that build this */
static INLINE u64 gli_std_hash_load64(const unsigned char *_p)
{
	u64 v = 0;
	int i;
	for (i = 7; i >= 0; --i)
		v = (v << 8) | _p[i];
	return v;
}

static INLINE u32 gli_std_hash_load32(const unsigned char *_p)
{
	return (u32)_p[0] | ((u32)_p[1] << 8) | ((u32)_p[2] << 16) | ((u32)_p[3] << 24);
}

/* MurmurHash2: libstdc++ with a 32 bit size_t (its seed 0xc70f6907), and
 * libc++ (seed 0) */
static INLINE u32 gli_std_hash_murmur2(const unsigned char *_s, u32 _len, u32 _seed)
{
	const u32 m = 0x5bd1e995U;
	u32 h = _seed ^ _len;
	for (; _len >= 4; _s += 4, _len -= 4) {
		u32 k = gli_std_hash_load32(_s);
		k *= m;
		k ^= k >> 24;
		k *= m;
		h *= m;
		h ^= k;
	}
	switch (_len) {
	case 3: h ^= (u32)_s[2] << 16; /* fall through */
	case 2: h ^= (u32)_s[1] << 8; /* fall through */
	case 1: h ^= _s[0]; h *= m;
	}
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;
	return h;
}

/* libstdc++'s _Hash_bytes with a 64 bit size_t */
static INLINE u64 gli_std_hash_libstdcxx64(const unsigned char *_s, u64 _len)
{
	const u64 mul = ((u64)0xc6a4a793U << 32) + 0x5bd1e995U;
	const u64 aligned = _len & ~(u64)7;
	u64 hash = (u64)0xc70f6907U ^ (_len * mul);
	u64 i;
	for (i = 0; i != aligned; i += 8) {
		u64 data = gli_std_hash_load64(_s + i) * mul;
		data = (data ^ (data >> 47)) * mul;
		hash ^= data;
		hash *= mul;
	}
	if ((_len & 7) != 0) {
		u64 data = 0;
		int n = (int)(_len & 7) - 1;
		do
			data = (data << 8) + _s[aligned + n];
		while (--n >= 0);
		hash ^= data;
		hash *= mul;
	}
	hash = (hash ^ (hash >> 47)) * mul;
	return hash ^ (hash >> 47);
}

/* libc++'s CityHash64 */
#define GLI_CITY_K0 (((u64)0xc3a5c85cU << 32) | 0x97cb3127U)
#define GLI_CITY_K1 (((u64)0xb492b66fU << 32) | 0xbe98f273U)
#define GLI_CITY_K2 (((u64)0x9ae16a3bU << 32) | 0x2f90404fU)
#define GLI_CITY_K3 (((u64)0xc949d7c7U << 32) | 0x509e6557U)

static INLINE u64 gli_city_rotate(u64 _v, int _shift)
{
	return _shift == 0 ? _v : ((_v >> _shift) | (_v << (64 - _shift)));
}

static INLINE u64 gli_city_shift_mix(u64 _v)
{
	return _v ^ (_v >> 47);
}

static INLINE u64 gli_city_len16(u64 _u, u64 _v)
{
	const u64 mul = ((u64)0x9ddfea08U << 32) | 0xeb382d69U;
	u64 a = (_u ^ _v) * mul;
	u64 b;
	a ^= a >> 47;
	b = (_v ^ a) * mul;
	b ^= b >> 47;
	return b * mul;
}

/* the pair a 32 byte block hashes to with two seeds */
static INLINE void gli_city_weak32(const unsigned char *_s, u64 _a, u64 _b, u64 *_first, u64 *_second)
{
	const u64 w = gli_std_hash_load64(_s), x = gli_std_hash_load64(_s + 8);
	const u64 y = gli_std_hash_load64(_s + 16), z = gli_std_hash_load64(_s + 24);
	u64 c;
	_a += w;
	_b = gli_city_rotate(_b + _a + z, 21);
	c = _a;
	_a += x;
	_a += y;
	_b += gli_city_rotate(_a, 44);
	*_first = _a + z;
	*_second = _b + c;
}

static INLINE u64 gli_std_hash_libcxx64(const unsigned char *_s, u64 _len)
{
	u64 x, y, z, vf, vs, wf, ws;
	if (_len <= 16) {
		if (_len > 8) {
			const u64 a = gli_std_hash_load64(_s), b = gli_std_hash_load64(_s + _len - 8);
			return gli_city_len16(a, gli_city_rotate(b + _len, (int)_len)) ^ b;
		}
		if (_len >= 4) {
			const u32 a = gli_std_hash_load32(_s), b = gli_std_hash_load32(_s + _len - 4);
			/* the shift in 32 bits, as libc++'s ABI has it */
			return gli_city_len16(_len + (u32)(a << 3), b);
		}
		if (_len > 0) {
			const u32 y32 = (u32)_s[0] + ((u32)_s[_len >> 1] << 8);
			const u32 z32 = (u32)_len + ((u32)_s[_len - 1] << 2);
			return gli_city_shift_mix(y32 * GLI_CITY_K2 ^ z32 * GLI_CITY_K3) * GLI_CITY_K2;
		}
		return GLI_CITY_K2;
	}
	if (_len <= 32) {
		const u64 a = gli_std_hash_load64(_s) * GLI_CITY_K1, b = gli_std_hash_load64(_s + 8);
		const u64 c = gli_std_hash_load64(_s + _len - 8) * GLI_CITY_K2;
		const u64 d = gli_std_hash_load64(_s + _len - 16) * GLI_CITY_K0;
		return gli_city_len16(gli_city_rotate(a - b, 43) + gli_city_rotate(c, 30) + d,
			a + gli_city_rotate(b ^ GLI_CITY_K3, 20) - c + _len);
	}
	if (_len <= 64) {
		u64 a, b, c, r;
		z = gli_std_hash_load64(_s + 24);
		a = gli_std_hash_load64(_s) + (_len + gli_std_hash_load64(_s + _len - 16)) * GLI_CITY_K0;
		b = gli_city_rotate(a + z, 52);
		c = gli_city_rotate(a, 37);
		a += gli_std_hash_load64(_s + 8);
		c += gli_city_rotate(a, 7);
		a += gli_std_hash_load64(_s + 16);
		vf = a + z;
		vs = b + gli_city_rotate(a, 31) + c;
		a = gli_std_hash_load64(_s + 16) + gli_std_hash_load64(_s + _len - 32);
		z += gli_std_hash_load64(_s + _len - 8);
		b = gli_city_rotate(a + z, 52);
		c = gli_city_rotate(a, 37);
		a += gli_std_hash_load64(_s + _len - 24);
		c += gli_city_rotate(a, 7);
		a += gli_std_hash_load64(_s + _len - 16);
		wf = a + z;
		ws = b + gli_city_rotate(a, 31) + c;
		r = gli_city_shift_mix((vf + ws) * GLI_CITY_K2 + (wf + vs) * GLI_CITY_K0);
		return gli_city_shift_mix(r * GLI_CITY_K0 + vs) * GLI_CITY_K2;
	}
	x = gli_std_hash_load64(_s + _len - 40);
	y = gli_std_hash_load64(_s + _len - 16) + gli_std_hash_load64(_s + _len - 56);
	z = gli_city_len16(gli_std_hash_load64(_s + _len - 48) + _len, gli_std_hash_load64(_s + _len - 24));
	gli_city_weak32(_s + _len - 64, _len, z, &vf, &vs);
	gli_city_weak32(_s + _len - 32, y + GLI_CITY_K1, x, &wf, &ws);
	x = x * GLI_CITY_K1 + gli_std_hash_load64(_s);
	_len = (_len - 1) & ~(u64)63;
	do {
		u64 t;
		x = gli_city_rotate(x + y + vf + gli_std_hash_load64(_s + 8), 37) * GLI_CITY_K1;
		y = gli_city_rotate(y + vs + gli_std_hash_load64(_s + 48), 42) * GLI_CITY_K1;
		x ^= ws;
		y += vf + gli_std_hash_load64(_s + 40);
		z = gli_city_rotate(z + wf, 33) * GLI_CITY_K1;
		gli_city_weak32(_s, vs * GLI_CITY_K1, x + wf, &vf, &vs);
		gli_city_weak32(_s + 32, z + ws, y + gli_std_hash_load64(_s + 16), &wf, &ws);
		t = z;
		z = x;
		x = t;
		_s += 64;
		_len -= 64;
	} while (_len != 0);
	return gli_city_len16(gli_city_len16(vf, wf) + gli_city_shift_mix(y) * GLI_CITY_K1 + z,
		gli_city_len16(vs, ws) + x);
}

/* the MSVC library's FNV-1a. The 64 bit one's low 32 bits, all that is
 * used, come from the low halves of its offset and prime alone. */
static INLINE u32 gli_std_hash_fnv1a(const unsigned char *_s, size_t _len, u32 _offset, u32 _prime)
{
	u32 h = _offset;
	size_t i;
	for (i = 0; i < _len; ++i) {
		h ^= _s[i];
		h *= _prime;
	}
	return h;
}
#define GLI_FNV64_LOW(_s, _len) gli_std_hash_fnv1a(_s, _len, 0x84222325U, 0x1b3U)
#define GLI_FNV32(_s, _len) gli_std_hash_fnv1a(_s, _len, 2166136261U, 16777619U)

/* the library this builds with: MSVC's, libc++ where it is the platform's
 * (Apple, Android, the BSDs that use clang, emscripten), libstdc++ elsewhere */
static INLINE u32 gli_std_hash_string(const char *_s)
{
	const unsigned char *s = (const unsigned char *)_s;
	const size_t len = strlen(_s);
#if defined(_MSC_VER)
#if defined(_WIN64)
	return GLI_FNV64_LOW(s, len);
#else
	return GLI_FNV32(s, len);
#endif
#elif defined(__APPLE__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
	if (sizeof(size_t) == 8)
		return (u32)gli_std_hash_libcxx64(s, len);
	return gli_std_hash_murmur2(s, (u32)len, 0);
#else
	if (sizeof(size_t) == 8)
		return (u32)gli_std_hash_libstdcxx64(s, len);
	return gli_std_hash_murmur2(s, (u32)len, 0xc70f6907U);
#endif
}

#endif
