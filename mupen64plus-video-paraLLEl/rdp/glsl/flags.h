#ifndef FLAGS_H_
#define FLAGS_H_

#define TEST_FLAG(flags, bit) ((int(flags) & (1 << bit)) != 0)

#define PRIMITIVE_IS_FLIP(flags)                TEST_FLAG(flags,  0)
#define PRIMITIVE_DO_OFFSET(flags)              TEST_FLAG(flags,  1)
#define PRIMITIVE_SAMPLE_TEX(flags)             TEST_FLAG(flags,  2)
#define PRIMITIVE_FORCE_BLEND(flags)            TEST_FLAG(flags,  3)
#define PRIMITIVE_AA_ENABLE(flags)              TEST_FLAG(flags,  4)
#define PRIMITIVE_SHADE(flags)                  TEST_FLAG(flags,  5)
#define PRIMITIVE_INTERPOLATE_Z(flags)          TEST_FLAG(flags,  6)
#define PRIMITIVE_Z_UPDATE(flags)               TEST_FLAG(flags,  7)
#define PRIMITIVE_Z_COMPARE(flags)              TEST_FLAG(flags,  8)
#define PRIMITIVE_PERSPECTIVE(flags)            TEST_FLAG(flags,  9)
#define PRIMITIVE_BILERP0(flags)                TEST_FLAG(flags, 10)
#define PRIMITIVE_BILERP1(flags)                TEST_FLAG(flags, 11)
#define PRIMITIVE_ALPHATEST(flags)              TEST_FLAG(flags, 12)
#define PRIMITIVE_COLOR_ON_CVG(flags)           TEST_FLAG(flags, 13)
#define PRIMITIVE_ALPHA_TO_CVG(flags)           TEST_FLAG(flags, 14)
#define PRIMITIVE_CVG_TO_ALPHA(flags)           TEST_FLAG(flags, 15)
#define PRIMITIVE_ALPHA_NOISE(flags)            TEST_FLAG(flags, 16)
#define PRIMITIVE_SAMPLE_TEX_PIPELINED(flags)   TEST_FLAG(flags, 17)
#define PRIMITIVE_SAMPLE_TEX_LOD(flags)         TEST_FLAG(flags, 18)

#define PRIMITIVE_INTERPOLATE_VARYINGS(flags) ((flags & (4u | 32u | 64u)) != 0u)

#define PRIMITIVE_CVG_MODE(flags) (((flags) >> 20u) & 0x3u)
#define PRIMITIVE_TILE(flags) (((flags) >> 22u) & 0x7u)
#define PRIMITIVE_LEVELS(flags) ((((flags) >> 25u) & 0x7u) + 1u)
#define PRIMITIVE_Z_MODE(flags) (((flags) >> 28u) & 0x3u)

#define PRIMITIVE_CYCLE_TYPE_IS_COPY(flags) ((flags & 0xc0000000u) == 0x80000000u)
#define PRIMITIVE_CYCLE_TYPE(flags) (((flags) >> 30u) & 0xfu)

#define RGB_DITHER_SEL(blendword) (((blendword) >> 18u) & 0x3u)
#define ALPHA_DITHER_SEL(blendword) (((blendword) >> 16u) & 0x3u)
#define FULL_DITHER_SEL(blendword) (((blendword) >> 16u) & 0xfu)

#define CYCLE_TYPE_1 0u
#define CYCLE_TYPE_2 1u
#define CYCLE_TYPE_COPY 2u
#define CYCLE_TYPE_FILL 3u

#define ZMODE_OPAQUE 0u
#define ZMODE_INTERPENETRATING 1u
#define ZMODE_TRANSPARENT 2u
#define ZMODE_DECAL 3u

#define CVG_MODE_CLAMP 0u
#define CVG_MODE_WRAP 1u
#define CVG_MODE_ZAP 2u
#define CVG_MODE_SAVE 3u

#define LOD_INFO_PRIMITIVE_DETAIL(flags) TEST_FLAG(flags, 16)
#define LOD_INFO_PRIMITIVE_SHARPEN(flags) TEST_FLAG(flags, 17)
#define LOD_INFO_PRIMITIVE(flags) ((flags) & 0xffffu)
#define LOD_INFO_PRIMITIVE_MIN_LOD(flags) int((flags) >> 18u)

#endif
