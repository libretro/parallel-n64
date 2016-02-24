#ifndef _VU_H_
#define _VU_H_

#ifdef ARCH_MIN_SSE2
#include <emmintrin.h>
#endif

#include <stdint.h>

#define N       8
/* N:  number of processor elements in SIMD processor */

/*
 * Illegal, unaligned LWC2 operations on the RSP may write past the terminal
 * byte of a vector, while SWC2 operations may have to wrap around stores
 * from the end to the start of a vector.  Both of these risk out-of-bounds
 * memory access, but by doubling the number of bytes allocated (shift left)
 * per each vector register, we could stabilize and probably optimize this.
 */
#if 0
#define VR_STATIC_WRAPAROUND    0
#else
#define VR_STATIC_WRAPAROUND    1
#endif

/*
 * We are going to need this for vector operations doing scalar things.
 * The divides and VSAW need bit-wise information from the instruction word.
 */
extern uint32_t inst;

/*
 * When compiling without SSE2, we need to use a pointer to a destination
 * vector instead of an XMM register in the return slot of the function.
 * The vector "result" register will be emulated to serve this pointer
 * as a shared global rather than the return slot of a function call.
 */
#ifndef ARCH_MIN_SSE2
ALIGNED extern int16_t V_result[N];
#endif

/*
 * accumulator-indexing macros
 */
#define HI      00
#define MD      01
#define LO      02

#define VACC_L      (VACC[LO])
#define VACC_M      (VACC[MD])
#define VACC_H      (VACC[HI])

#define ACC_L(i)    (VACC_L)[i]
#define ACC_M(i)    (VACC_M)[i]
#define ACC_H(i)    (VACC_H)[i]

#ifdef ARCH_MIN_SSE2
typedef __m128i v16;
#else
typedef int16_t* v16;
#endif

#ifdef ARCH_MIN_SSE2
#define VECTOR_OPERATION    v16
#else
#define VECTOR_OPERATION    void
#endif
#define VECTOR_EXTERN       extern VECTOR_OPERATION

NOINLINE extern void message(const char* body);

#ifdef ARCH_MIN_SSE2

#define vector_copy(vd, vs) { \
    *(v16 *)(vd) = *(v16 *)(vs); }
#define vector_wipe(vd) { \
    *(v16 *)&(vd) = _mm_cmpgt_epi16(*(v16 *)&(vd), *(v16 *)&(vd)); }
#define vector_fill(vd) { \
    *(v16 *)&(vd) = _mm_cmpeq_epi16(*(v16 *)&(vd), *(v16 *)&(vd)); }

#define vector_and(vd, vs) { \
    *(v16 *)&(vd) = _mm_and_si128  (*(v16 *)&(vd), *(v16 *)&(vs)); }
#define vector_or(vd, vs) { \
    *(v16 *)&(vd) = _mm_or_si128   (*(v16 *)&(vd), *(v16 *)&(vs)); }
#define vector_xor(vd, vs) { \
    *(v16 *)&(vd) = _mm_xor_si128  (*(v16 *)&(vd), *(v16 *)&(vs)); }

/*
 * Every competent vector unit should have at least two vector comparison
 * operations:  EQ and LT/GT.  (MMX makes us say GT; SSE's LT is just a GT.)
 *
 * Default examples when compiling for the x86 SSE2 architecture below.
 */
#define vector_cmplt(vd, vs) { \
    *(v16 *)&(vd) = _mm_cmplt_epi16(*(v16 *)&(vd), *(v16 *)&(vs)); }
#define vector_cmpeq(vd, vs) { \
    *(v16 *)&(vd) = _mm_cmpeq_epi16(*(v16 *)&(vd), *(v16 *)&(vs)); }
#define vector_cmpgt(vd, vs) { \
    *(v16 *)&(vd) = _mm_cmpgt_epi16(*(v16 *)&(vd), *(v16 *)&(vs)); }

#else

#define vector_copy(vd, vs) { \
    (vd)[0] = (vs)[0]; \
    (vd)[1] = (vs)[1]; \
    (vd)[2] = (vs)[2]; \
    (vd)[3] = (vs)[3]; \
    (vd)[4] = (vs)[4]; \
    (vd)[5] = (vs)[5]; \
    (vd)[6] = (vs)[6]; \
    (vd)[7] = (vs)[7]; \
}
#define vector_wipe(vd) { \
    (vd)[0] =  0x0000; \
    (vd)[1] =  0x0000; \
    (vd)[2] =  0x0000; \
    (vd)[3] =  0x0000; \
    (vd)[4] =  0x0000; \
    (vd)[5] =  0x0000; \
    (vd)[6] =  0x0000; \
    (vd)[7] =  0x0000; \
}
#define vector_fill(vd) { \
    (vd)[0] = ~0x0000; \
    (vd)[1] = ~0x0000; \
    (vd)[2] = ~0x0000; \
    (vd)[3] = ~0x0000; \
    (vd)[4] = ~0x0000; \
    (vd)[5] = ~0x0000; \
    (vd)[6] = ~0x0000; \
    (vd)[7] = ~0x0000; \
}
#define vector_and(vd, vs) { \
    (vd)[0] &= (vs)[0]; \
    (vd)[1] &= (vs)[1]; \
    (vd)[2] &= (vs)[2]; \
    (vd)[3] &= (vs)[3]; \
    (vd)[4] &= (vs)[4]; \
    (vd)[5] &= (vs)[5]; \
    (vd)[6] &= (vs)[6]; \
    (vd)[7] &= (vs)[7]; \
}
#define vector_or(vd, vs) { \
    (vd)[0] |= (vs)[0]; \
    (vd)[1] |= (vs)[1]; \
    (vd)[2] |= (vs)[2]; \
    (vd)[3] |= (vs)[3]; \
    (vd)[4] |= (vs)[4]; \
    (vd)[5] |= (vs)[5]; \
    (vd)[6] |= (vs)[6]; \
    (vd)[7] |= (vs)[7]; \
}
#define vector_xor(vd, vs) { \
    (vd)[0] ^= (vs)[0]; \
    (vd)[1] ^= (vs)[1]; \
    (vd)[2] ^= (vs)[2]; \
    (vd)[3] ^= (vs)[3]; \
    (vd)[4] ^= (vs)[4]; \
    (vd)[5] ^= (vs)[5]; \
    (vd)[6] ^= (vs)[6]; \
    (vd)[7] ^= (vs)[7]; \
}

#define vector_cmplt(vd, vs) { \
    (vd)[0] = ((vd)[0] < (vs)[0]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[1] < (vs)[1]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[2] < (vs)[2]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[3] < (vs)[3]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[4] < (vs)[4]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[5] < (vs)[5]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[6] < (vs)[6]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[7] < (vs)[7]) ? ~0x0000 :  0x0000; \
}
#define vector_cmpeq(vd, vs) { \
    (vd)[0] = ((vd)[0] == (vs)[0]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[1] == (vs)[1]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[2] == (vs)[2]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[3] == (vs)[3]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[4] == (vs)[4]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[5] == (vs)[5]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[6] == (vs)[6]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[7] == (vs)[7]) ? ~0x0000 :  0x0000; \
}
#define vector_cmpgt(vd, vs) { \
    (vd)[0] = ((vd)[0] > (vs)[0]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[1] > (vs)[1]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[2] > (vs)[2]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[3] > (vs)[3]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[4] > (vs)[4]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[5] > (vs)[5]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[6] > (vs)[6]) ? ~0x0000 :  0x0000; \
    (vd)[0] = ((vd)[7] > (vs)[7]) ? ~0x0000 :  0x0000; \
}

#endif

/*
 * Many vector units have pairs of "vector condition flags" registers.
 * In SGI's vector unit implementation, these are denoted as the
 * "vector control registers" under coprocessor 2.
 *
 * VCF-0 is the carry-out flags register:  $vco.
 * VCF-1 is the compare code flags register:  $vcc.
 * VCF-2 is the compare extension flags register:  $vce.
 * There is no fourth RSP flags register.
 */
extern  VCO;
extern uint16_t VCC;
extern uint8_t VCE;

ALIGNED extern int16_t cf_ne[N];
ALIGNED extern int16_t cf_co[N];
ALIGNED extern int16_t cf_clip[N];
ALIGNED extern int16_t cf_comp[N];
ALIGNED extern int16_t cf_vce[N];

extern uint16_t get_VCO(void);
extern uint16_t get_VCC(void);
extern uint8_t get_VCE(void);

extern void set_VCO(uint16_t VCO);
extern void set_VCC(uint16_t VCC);
extern void set_VCE(uint8_t VCE);

/*
 * shuffling convenience macros for Intel SIMD
 * An 8-bit shuffle imm. of SHUFFLE(0, 1, 2, 3) should be a null operation.
 */
#define B(x)    ((x) & 3)
#define SHUFFLE(a,b,c,d)    ((B(d)<<6) | (B(c)<<4) | (B(b)<<2) | (B(a)<<0))

#endif
