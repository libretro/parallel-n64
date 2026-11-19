/* test_gliden64_std_hash: std::hash<std::string> in C (glsl_StdHash.h),
 * which names the shader storage's files, against each C++ library's own
 * for strings of 0 to 130 bytes: every length of every branch, CityHash's
 * 64 byte loop twice. The library the build picks for the host where it is
 * known: libstdc++ on Linux and MinGW, MSVC's on MSVC. C89. */
#include <stdio.h>
#include <string.h>
#include "Graphics/OpenGLContext/GLSL/glsl_StdHash.h"
#include "test_gliden64_std_hash.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static unsigned char s_bytes[256];

static u32 variant(int _v, int _len)
{
	switch (_v) {
	case 0: return (u32)gli_std_hash_libstdcxx64(s_bytes, (u64)_len);
	case 1: return gli_std_hash_murmur2(s_bytes, (u32)_len, 0xc70f6907U);
	case 2: return (u32)gli_std_hash_libcxx64(s_bytes, (u64)_len);
	case 3: return gli_std_hash_murmur2(s_bytes, (u32)_len, 0);
	case 4: return GLI_FNV64_LOW(s_bytes, (size_t)_len);
	default: return GLI_FNV32(s_bytes, (size_t)_len);
	}
}

int main(void)
{
	unsigned x = 12345;
	int i, v, len;
	char name[132];
	for (i = 0; i < 256; ++i) {
		x = x * 1103515245u + 12345u;
		s_bytes[i] = (unsigned char)(x >> 16);
		if (s_bytes[i] == 0)
			s_bytes[i] = 1;
	}
	for (v = 0; v < 6; ++v)
		for (len = 0; len <= 130; ++len)
			if (variant(v, len) != s_want[v][len]) {
				printf("FAIL variant %d length %d: %08x, not %08x\n", v, len, (unsigned)variant(v, len),
					(unsigned)s_want[v][len]);
				++fails;
			}

	/* the host's own: what the files of before were named with */
	for (len = 0; len <= 130; ++len) {
		int want = -1;
		memcpy(name, s_bytes, (size_t)len);
		name[len] = '\0';
#if defined(_MSC_VER)
		want = sizeof(size_t) == 8 ? 4 : 5;
#elif (defined(__linux__) && !defined(__ANDROID__)) || defined(__MINGW32__)
		want = sizeof(size_t) == 8 ? 0 : 1;
#endif
		if (want >= 0)
			CHECK(gli_std_hash_string(name) == s_want[want][len]);
	}
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__MINGW32__)
	if (sizeof(size_t) == 8)
		CHECK(gli_std_hash_string("SUPER MARIO 64") == 0x356c268eU);
#endif

	if (fails) {
		printf("test_gliden64_std_hash: %d failures\n", fails);
		return 1;
	}
	printf("test_gliden64_std_hash: ok\n");
	return 0;
}
