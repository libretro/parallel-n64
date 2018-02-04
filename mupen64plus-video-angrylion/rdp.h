#ifndef _RDP_H_
#define _RDP_H_

#include "z64.h"


#define OPTS_ENABLED

#ifdef ARCH_MIN_SSE2
#define USE_SSE_SUPPORT
#endif

#ifdef USE_SSE_SUPPORT
#include <emmintrin.h>

#define SIGND(m)    _mm_srai_epi32(m, 31)
#define XMM_ZERO    _mm_setzero_si128()
#define setzero_si128(buf) _mm_store_si128((__m128i *)buf, XMM_ZERO)

#define mm_neg_epi32(m)     _mm_sub_epi32(XMM_ZERO, m)

#define mm_sign_epi32_nz(m1, m2) \
_mm_sub_epi32(_mm_xor_si128(m1, SIGND(m2)), SIGND(m2))

#define mm_unpacklo_epi64_hz(m, n) \
_mm_unpacklo_epi64(_mm_unpacklo_epi32(m, n), _mm_unpackhi_epi32(m, n))

INLINE extern __m128i mm_mullo_epi32_seh(__m128i dest, __m128i src);

#else
#define setzero_si128(buf) \
    { ((int64_t *)buf)[0] = ((int64_t *)buf)[1] = 0x0000000000000000; }
#endif

#define setzero_si64(buffer) { \
    *(int64_t *)(buffer) = 0x0000000000000000; \
}

#if defined(_AMD64_) || defined(_IA64_) || defined(__x86_64__)
#define BUFFERFIFO(word, base, offset) { \
    *(int64_t *)&cmd_data[word] = *(int64_t *)((base) + 8*(offset)); \
}
#else
/*
 * compatibility fallback to prevent unspecified behavior from the ANSI C
 * specifications (reading unions per 32 bits when 64 bits were just written)
 */
#define BUFFERFIFO(word, base, offset) { \
    cmd_data[word].W32[0] = *(int32_t *)((base) + 8*(offset) + 0); \
    cmd_data[word].W32[1] = *(int32_t *)((base) + 8*(offset) + 4); \
}
#endif

#ifdef USE_SSE_SUPPORT
typedef __m128i     v8;
typedef __m128i     v16;
typedef __m128i     v32;
#else
typedef char*       v8;
typedef short*      v16;
typedef int*        v32;
#endif

#define SIGN16(x)       (int16_t)(x)
#define SIGN8(x)        (int8_t)(x)

#if (~0 >> 1 < 0)
#define SRA(exp, sa)    ((signed)(exp) >> (sa))
#else
#define SRA(exp, sa)    (SE((exp) >> (sa), (sa) ^ 31))
#endif

/*
 * Virtual register sign-extensions and clamps using b-bit immediates:
 */
#define CLAMP(i, b)     ((i) & ~(~0x00000000 << (b)))
#define SB(i, b)        ((i) &  ( 0x00000001 << ((b) - 1)))
#define SE(i, b)        ((i) | -SB((i), (b)))

/*
 * Forces to clamp immediate bit width AND sign-extend virtual register:
 */
#if (0)
#define SIGN(i, b)      SE(CLAMP((i), (b)), (b))
#else
#define SIGN(i, b)      SRA((i) << (32 - (b)), (32 - (b)))
#endif
#define SIGNF(x, numb)    ((x) | -((x) & (1 << (numb - 1))))

/* thread-local storage */
#if 1
#define TLS 
#else
#ifdef HAVE_PARALLEL
#if defined(_MSC_VER)
    #define TLS __declspec(thread)
#elif defined(__GNUC__)
    #define TLS __thread
#else
    #define TLS _Thread_local
#endif
#else
#define TLS 
#endif
#endif

#define GET_LOW_RGBA16_TMEM(x)      replicated_rgba[((x) & 0x003F) >>  1]
#define GET_MED_RGBA16_TMEM(x)      replicated_rgba[((x) & 0x07FF) >>  6]
#define GET_HI_RGBA16_TMEM(x)       replicated_rgba[(uint16_t)(x) >> 11]

#define f_BYTE_H(B) (!!(B&0xFF)*8 | !!(B&0xF0)*4 | !!(B&0xCC)*2 | !!(B&0xAA))
#define f_BYTE_L(B) (!!(B&0xFF)*0 | !!(B&0xF0)*4 | !!(B&0xCC)*2 | !!(B&0xAA))

/* list of command IDs */
#define CMD_ID_NO_OP                           0x00
#define CMD_ID_FILL_TRIANGLE                   0x08
#define CMD_ID_FILL_ZBUFFER_TRIANGLE           0x09
#define CMD_ID_TEXTURE_TRIANGLE                0x0a
#define CMD_ID_TEXTURE_ZBUFFER_TRIANGLE        0x0b
#define CMD_ID_SHADE_TRIANGLE                  0x0c
#define CMD_ID_SHADE_ZBUFFER_TRIANGLE          0x0d
#define CMD_ID_SHADE_TEXTURE_TRIANGLE          0x0e
#define CMD_ID_SHADE_TEXTURE_Z_BUFFER_TRIANGLE 0x0f
#define CMD_ID_TEXTURE_RECTANGLE               0x24
#define CMD_ID_TEXTURE_RECTANGLE_FLIP          0x25
#define CMD_ID_SYNC_LOAD                       0x26
#define CMD_ID_SYNC_PIPE                       0x27
#define CMD_ID_SYNC_TILE                       0x28
#define CMD_ID_SYNC_FULL                       0x29
#define CMD_ID_SET_KEY_GB                      0x2a
#define CMD_ID_SET_KEY_R                       0x2b
#define CMD_ID_SET_CONVERT                     0x2c
#define CMD_ID_SET_SCISSOR                     0x2d
#define CMD_ID_SET_PRIM_DEPTH                  0x2e
#define CMD_ID_SET_OTHER_MODES                 0x2f
#define CMD_ID_LOAD_TLUT                       0x30
#define CMD_ID_SET_TILE_SIZE                   0x32
#define CMD_ID_LOAD_BLOCK                      0x33
#define CMD_ID_LOAD_TILE                       0x34
#define CMD_ID_SET_TILE                        0x35
#define CMD_ID_FILL_RECTANGLE                  0x36
#define CMD_ID_SET_FILL_COLOR                  0x37
#define CMD_ID_SET_FOG_COLOR                   0x38
#define CMD_ID_SET_BLEND_COLOR                 0x39
#define CMD_ID_SET_PRIM_COLOR                  0x3a
#define CMD_ID_SET_ENV_COLOR                   0x3b
#define CMD_ID_SET_COMBINE                     0x3c
#define CMD_ID_SET_TEXTURE_IMAGE               0x3d
#define CMD_ID_SET_MASK_IMAGE                  0x3e
#define CMD_ID_SET_COLOR_IMAGE                 0x3f

#define CMD_MAX_INTS 44
#define CMD_MAX_SIZE (CMD_MAX_INTS * sizeof(int32_t))		  #define CMD_MAX_SIZE (CMD_MAX_INTS * sizeof(int32_t))
#define CMD_ID(cmd) ((*(cmd) >> 24) & 0x3f)

typedef union {
    int64_t W;
    int64_t SW;
    uint64_t UW;
    int32_t W32[2];
    int32_t SW32[2];
    uint32_t UW32[2];
    int16_t W16[4];
    int16_t SW16[4];
    uint16_t UW16[4];
    unsigned char B[8];
    signed char SB[8];
} DP_FIFO;

typedef struct {
    int32_t xl, yl, xh, yh;
} RECTANGLE;

enum {
   TEXTURE_FLIP_NO = 0,
   TEXTURE_FLIP_YES
};

enum {
    PIXEL_SIZE_4BIT = 0,
    PIXEL_SIZE_8BIT,
    PIXEL_SIZE_16BIT,
    PIXEL_SIZE_32BIT
};

enum {
    CYCLE_TYPE_1 = 0,
    CYCLE_TYPE_2,
    CYCLE_TYPE_COPY,
    CYCLE_TYPE_FILL
};

enum {
    FORMAT_RGBA = 0,
    FORMAT_YUV,
    FORMAT_CI,
    FORMAT_IA,
    FORMAT_I
};

enum {
    TEXEL_RGBA4 = 0,
    TEXEL_RGBA8,
    TEXEL_RGBA16,
    TEXEL_RGBA32,
    TEXEL_YUV4,
    TEXEL_YUV8,
    TEXEL_YUV16,
    TEXEL_YUV32,
    TEXEL_CI4,
    TEXEL_CI8,
    TEXEL_CI16,
    TEXEL_CI32,
    TEXEL_IA4,
    TEXEL_IA8,
    TEXEL_IA16,
    TEXEL_IA32,
    TEXEL_I4,
    TEXEL_I8,
    TEXEL_I16,
    TEXEL_I32
};

enum {
    NO = 0,
    YES
};
enum {
    SHADE_NO = 0,
    SHADE_YES = YES
};
enum {
    TEXTURE_NO = 0,
    TEXTURE_YES = YES
};
enum {
    ZBUFFER_NO = 0,
    ZBUFFER_YES = YES
};

extern void process_RDP_list(void);

int32_t irand(void);

extern uint32_t internal_vi_v_current_line;

#endif
