/* Copyright (c) 2020 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Utility header to smooth over the difference between
// 8/16-bit integer arithmetic vs. just 8/16-bit storage.

#ifndef SMALL_INTEGERS_H_
#define SMALL_INTEGERS_H_

// NO_STORAGE_8BIT: for devices without storageBuffer8BitAccess. Buffers are
// then never accessed as 8-bit: packed bytes are read out of 16/32-bit words,
// and mem_u8/mem_i8 stay undefined so any missed 8-bit access fails to build.
// It implies 32-bit arithmetic (SMALL_TYPES = 0).
#ifndef NO_STORAGE_8BIT
#define NO_STORAGE_8BIT 0
#endif

#extension GL_EXT_shader_16bit_storage : require
#if !NO_STORAGE_8BIT
#extension GL_EXT_shader_8bit_storage : require
#endif

#if SMALL_TYPES && NO_STORAGE_8BIT
#error "NO_STORAGE_8BIT requires SMALL_TYPES = 0"
#endif

#if NO_STORAGE_8BIT
// Bytes of a packed little-endian word; element 0 is the lowest address.
#define unpack_mem_u8x4(w) ivec4((uvec4(uint(w)) >> uvec4(0u, 8u, 16u, 24u)) & 0xffu)
#define unpack_mem_u8(w, i) int((uint(w) >> (8u * uint(i))) & 0xffu)
// Byte i of a uint array. Stores replace the lane atomically, since the other
// lanes may belong to pixels other invocations write at the same time.
#define load_u8_from_u32(arr, i) ((arr[uint(i) >> 2u] >> ((uint(i) & 3u) * 8u)) & 0xffu)
#define store_u8_to_u32(arr, i, v) do { \
	uint store_u8_shift_ = (uint(i) & 3u) * 8u; \
	atomicAnd(arr[uint(i) >> 2u], ~(0xffu << store_u8_shift_)); \
	atomicOr(arr[uint(i) >> 2u], (uint(v) & 0xffu) << store_u8_shift_); \
} while (false)
// Hidden RDRAM holds one 16-bit element per RDRAM halfword instead of one
// byte, so neighbouring pixels never share a word.
#define mem_hidden uint16_t
// 0xffff as a 16-bit store value, built from a runtime value: a converted
// constant is folded into a 16-bit OpConstant, which needs shaderInt16.
#define mem_u16_ones(runtime) uint16_t(uint(runtime) | 0xffffu)
#else
#define mem_hidden mem_u8
#define mem_u16_ones(runtime) mem_u16(0xffff)
#endif

#if SMALL_TYPES
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#define mem_u8 uint8_t
#define mem_u16 uint16_t
#define mem_u8x2 u8vec2
#define mem_u16x2 u16vec2
#define mem_u8x3 u8vec3
#define mem_u16x3 u16vec3
#define mem_u8x4 u8vec4
#define mem_u16x4 u16vec4

#define mem_i8 int8_t
#define mem_i16 int16_t
#define mem_i8x2 i8vec2
#define mem_i16x2 i16vec2
#define mem_i8x3 i8vec3
#define mem_i16x3 i16vec3
#define mem_i8x4 i8vec4
#define mem_i16x4 i16vec4

#define u8 uint8_t
#define u16 uint16_t
#define u8x2 u8vec2
#define u16x2 u16vec2
#define u8x3 u8vec3
#define u16x3 u16vec3
#define u8x4 u8vec4
#define u16x4 u16vec4

#define i8 int8_t
#define i16 int16_t
#define i8x2 i8vec2
#define i16x2 i16vec2
#define i8x3 i8vec3
#define i16x3 i16vec3
#define i8x4 i8vec4
#define i16x4 i16vec4

#define U8_C(x) uint8_t(x)
#define I8_C(x) int8_t(x)
#define U16_C(x) uint16_t(x)
#define I16_C(x) int16_t(x)

#else

#if !NO_STORAGE_8BIT
#define mem_u8 uint8_t
#define mem_u8x2 u8vec2
#define mem_u8x3 u8vec3
#define mem_u8x4 u8vec4
#define mem_i8 int8_t
#define mem_i8x2 i8vec2
#define mem_i8x3 i8vec3
#define mem_i8x4 i8vec4
#endif

#define mem_u16 uint16_t
#define mem_u16x2 u16vec2
#define mem_u16x3 u16vec3
#define mem_u16x4 u16vec4

#define mem_i16 int16_t
#define mem_i16x2 i16vec2
#define mem_i16x3 i16vec3
#define mem_i16x4 i16vec4

#define u8 int
#define u16 int
#define u8x2 ivec2
#define u16x2 ivec2
#define u8x3 ivec3
#define u16x3 ivec3
#define u8x4 ivec4
#define u16x4 ivec4

#define i8 int
#define i16 int
#define i8x2 ivec2
#define i16x2 ivec2
#define i8x3 ivec3
#define i16x3 ivec3
#define i8x4 ivec4
#define i16x4 ivec4

#define U8_C(x) int(x)
#define I8_C(x) int(x)
#define U16_C(x) int(x)
#define I16_C(x) int(x)

#endif
#endif