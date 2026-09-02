/* al_vemit.h -- the vector instruction set the RDP's per-pixel pipeline
 * needs, in a form both an x86-64 SSE2 and an AArch64 NEON backend can
 * emit, with no runtime dispatch of any kind.
 *
 * Everything here is a macro. The target is known when the core is
 * compiled, so the selection below resolves at preprocessing time and a
 * call site expands straight into the host encoder: no function
 * pointers, no backend struct, no branch. A generator written against
 * this emits the same pipeline for either architecture and pays nothing
 * for the abstraction.
 *
 * The interface is three-operand (d = a OP b) because that is the more
 * general of the two forms: AArch64 is three-operand natively, and the
 * x86 lowering inserts the register copy only when the destination is
 * neither source. That test is on generator-time constants - the
 * register numbers the generator chose - so it costs nothing at run
 * time either; it decides which bytes get written, not what the written
 * code does.
 *
 * Registers are plain small integers, 0..15, the range both hosts have.
 * A cursor `uint8_t **p` is threaded explicitly rather than kept in a
 * global, so a generator can emit into any buffer.
 */

#ifndef AL_VEMIT_H
#define AL_VEMIT_H

#include <stdint.h>

/* --------------------------------------------------------------------
 * byte writers, shared
 * ------------------------------------------------------------------ */
#define ALV_W8(p, v)  do { *(*(p))++ = (uint8_t)(v); } while (0)
#define ALV_W32(p, v) do { uint32_t v_ = (uint32_t)(v); \
    ALV_W8(p, v_); ALV_W8(p, v_ >> 8); ALV_W8(p, v_ >> 16); ALV_W8(p, v_ >> 24); } while (0)

#if defined(AL_VEMIT_FORCED)
/* backend chosen by the caller (testing) */
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
/* SSE2 covers 32-bit x86 as well; only the eight low registers and no
 * REX there, which the encoders below handle by never setting it. */
#define AL_VEMIT_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define AL_VEMIT_A64 1
#else
/* no vector backend: the caller falls back to the C span renderers */
#define AL_VEMIT_NONE 1
#endif

/* ====================================================================
 * x86-64, SSE2
 * ==================================================================== */
#if defined(AL_VEMIT_X86)

/* 66 0F <op> /r, with REX only when a register above 7 is used */
#define ALV_X_RR(p, op, d, s) do { \
    unsigned d_ = (d), s_ = (s); \
    ALV_W8(p, 0x66); \
    if ((d_ | s_) & 8) ALV_W8(p, 0x40 | ((d_ >> 3) << 2) | (s_ >> 3)); \
    ALV_W8(p, 0x0f); ALV_W8(p, (op)); \
    ALV_W8(p, 0xc0 | ((d_ & 7) << 3) | (s_ & 7)); } while (0)

/* 66 0F <op> /<ext>, imm8  -- the shift-by-immediate group */
#define ALV_X_RI(p, op, ext, d, imm) do { \
    unsigned d_ = (d); \
    ALV_W8(p, 0x66); \
    if (d_ & 8) ALV_W8(p, 0x41); \
    ALV_W8(p, 0x0f); ALV_W8(p, (op)); \
    ALV_W8(p, 0xc0 | ((ext) << 3) | (d_ & 7)); \
    ALV_W8(p, (imm)); } while (0)

#define ALV_MOV(p, d, s) do { if ((d) != (s)) ALV_X_RR(p, 0x6f, (d), (s)); } while (0)

/* d = a + b (packed 32-bit) */
#define AL_V_ADD32(p, d, a, b) do { \
    if ((d) == (b) && (d) != (a)) { ALV_X_RR(p, 0xfe, (d), (a)); } \
    else { ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xfe, (d), (b)); } } while (0)

/* d = a - b */
#define AL_V_SUB32(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xfa, (d), (b)); } while (0)

/* d = a >> imm, arithmetic */
#define AL_V_SRA32(p, d, a, imm) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RI(p, 0x72, 4, (d), (imm)); } while (0)

/* d = a >> imm, logical */
#define AL_V_SRL32(p, d, a, imm) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RI(p, 0x72, 2, (d), (imm)); } while (0)

/* d = a << imm */
#define AL_V_SLL32(p, d, a, imm) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RI(p, 0x72, 6, (d), (imm)); } while (0)

/* d = a & b, a | b, a ^ b, ~a & b */
#define AL_V_AND(p, d, a, b)  do { \
    if ((d) == (b) && (d) != (a)) { ALV_X_RR(p, 0xdb, (d), (a)); } \
    else { ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xdb, (d), (b)); } } while (0)
#define AL_V_OR(p, d, a, b)   do { \
    if ((d) == (b) && (d) != (a)) { ALV_X_RR(p, 0xeb, (d), (a)); } \
    else { ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xeb, (d), (b)); } } while (0)
#define AL_V_XOR(p, d, a, b)  do { \
    if ((d) == (b) && (d) != (a)) { ALV_X_RR(p, 0xef, (d), (a)); } \
    else { ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xef, (d), (b)); } } while (0)
#define AL_V_ANDN(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xdf, (d), (b)); } while (0)

/* d = (a > b) ? ~0 : 0, and (a == b) */
#define AL_V_CMPGT32(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0x66, (d), (b)); } while (0)
#define AL_V_CMPEQ32(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0x76, (d), (b)); } while (0)

/* d = (m & a) | (~m & b) -- three ops on SSE2, no blend needed */
#define AL_V_SELECT(p, d, m, a, b, tmp) do { \
    ALV_MOV(p, (tmp), (m)); ALV_X_RR(p, 0xdf, (tmp), (b)); \
    ALV_MOV(p, (d), (m)); ALV_X_RR(p, 0xdb, (d), (a)); \
    ALV_X_RR(p, 0xeb, (d), (tmp)); } while (0)

/* load/store 16 bytes, [base + disp] */
#define ALV_X_MEM(p, pfx, op, r, base, disp) do { \
    unsigned r_ = (r), b_ = (base); int32_t dp_ = (disp); \
    ALV_W8(p, (pfx)); \
    if ((r_ | b_) & 8) ALV_W8(p, 0x40 | ((r_ >> 3) << 2) | (b_ >> 3)); \
    ALV_W8(p, 0x0f); ALV_W8(p, (op)); \
    if (dp_ == 0 && (b_ & 7) != 5) { ALV_W8(p, 0x00 | ((r_ & 7) << 3) | (b_ & 7)); } \
    else if (dp_ >= -128 && dp_ <= 127) { ALV_W8(p, 0x40 | ((r_ & 7) << 3) | (b_ & 7)); ALV_W8(p, dp_); } \
    else { ALV_W8(p, 0x80 | ((r_ & 7) << 3) | (b_ & 7)); ALV_W32(p, dp_); } \
    } while (0)

#define AL_V_LOAD(p, d, base, disp)  ALV_X_MEM(p, 0xf3, 0x6f, (d), (base), (disp))  /* movdqu */
#define AL_V_STORE(p, s, base, disp) ALV_X_MEM(p, 0xf3, 0x7f, (s), (base), (disp))


/* d = a * b, low 16 bits of each 16-bit lane (combiner products) */
#define AL_V_MULLO16(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xd5, (d), (b)); } while (0)

/* d = a * b as signed 16x16 -> 32 per lane, the odd halves of both
 * operands being zero. The combiner's (a-b)*c needs eighteen bits of
 * product, which the 16-bit multiply truncates, so this is the form
 * that keeps it exact.
 *
 * The instruction sums the products of both halves of each lane, so the
 * caller must clear the upper half of one operand or the sign words of
 * two negative operands contribute a stray plus one. Both of the
 * combiner's operands fit in signed sixteen bits, so clearing the upper
 * half of one of them leaves the product exact. */
#define AL_V_MADD16(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0xf5, (d), (b)); } while (0)

/* d = saturating pack of a,b from 32-bit to 16-bit signed lanes */
#define AL_V_PACKSS32(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_X_RR(p, 0x6b, (d), (b)); } while (0)

/* d = unsigned min / max, 16-bit lanes (the RDP's clamps) */
#define AL_V_MINU16(p, d, a, b) do { \
    ALV_MOV(p, (d), (a)); ALV_W8(p, 0x66); ALV_W8(p, 0x0f); ALV_W8(p, 0x38); ALV_W8(p, 0x3a); \
    ALV_W8(p, 0xc0 | (((d) & 7) << 3) | ((b) & 7)); } while (0)

/* move a 32-bit lane mask to a GPR: the "any pixel survived" test */
#define AL_V_MOVMSK(p, gpr, s) do { \
    unsigned g_ = (gpr), s_ = (s); \
    ALV_W8(p, 0x66); \
    if ((g_ | s_) & 8) ALV_W8(p, 0x40 | ((g_ >> 3) << 2) | (s_ >> 3)); \
    ALV_W8(p, 0x0f); ALV_W8(p, 0xd7); \
    ALV_W8(p, 0xc0 | ((g_ & 7) << 3) | (s_ & 7)); } while (0)

/* broadcast a 32-bit immediate already in memory at [base+disp] */
#define AL_V_LOADDUP32(p, d, base, disp) do { \
    ALV_X_MEM(p, 0xf3, 0x7e, (d), (base), (disp)); /* movq */ \
    ALV_X_RI(p, 0x70, 0, (d), 0x00); } while (0)

#define AL_V_RET(p) ALV_W8(p, 0xc3)

/* ====================================================================
 * AArch64, NEON
 * ==================================================================== */
#elif defined(AL_VEMIT_A64)

#define ALV_A_W(p, w) ALV_W32(p, w)

/* three-register 4S form: op Vd.4S, Vn.4S, Vm.4S */
#define ALV_A_3(p, base, d, n, m) \
    ALV_A_W(p, (base) | (((m) & 31) << 16) | (((n) & 31) << 5) | ((d) & 31))

/* shift by immediate, 4S: sshr/ushr/shl */
#define ALV_A_SHR(p, base, d, n, imm) \
    ALV_A_W(p, (base) | ((64 - (imm)) << 16) | (((n) & 31) << 5) | ((d) & 31))
#define ALV_A_SHL(p, base, d, n, imm) \
    ALV_A_W(p, (base) | ((32 + (imm)) << 16) | (((n) & 31) << 5) | ((d) & 31))

/* native three-operand: no copies, the destination is free */
#define AL_V_ADD32(p, d, a, b)   ALV_A_3(p, 0x4ea08400u, (d), (a), (b)) /* add  Vd.4S,Vn.4S,Vm.4S */
#define AL_V_SUB32(p, d, a, b)   ALV_A_3(p, 0x6ea08400u, (d), (a), (b)) /* sub  */
#define AL_V_AND(p, d, a, b)     ALV_A_3(p, 0x4e201c00u, (d), (a), (b)) /* and  Vd.16B */
#define AL_V_OR(p, d, a, b)      ALV_A_3(p, 0x4ea01c00u, (d), (a), (b)) /* orr  */
#define AL_V_XOR(p, d, a, b)     ALV_A_3(p, 0x6e201c00u, (d), (a), (b)) /* eor  */
#define AL_V_ANDN(p, d, a, b)    ALV_A_3(p, 0x4e601c00u, (d), (b), (a)) /* bic Vd,Vm,Vn: b & ~a */
#define AL_V_CMPGT32(p, d, a, b) ALV_A_3(p, 0x4ea03400u, (d), (a), (b)) /* cmgt Vd.4S,Vn.4S,Vm.4S */
#define AL_V_CMPEQ32(p, d, a, b) ALV_A_3(p, 0x6ea08c00u, (d), (a), (b)) /* cmeq */

#define AL_V_SRA32(p, d, a, imm) ALV_A_SHR(p, 0x4f000400u, (d), (a), (imm)) /* sshr Vd.4S,Vn.4S,#imm */
#define AL_V_SRL32(p, d, a, imm) ALV_A_SHR(p, 0x6f000400u, (d), (a), (imm)) /* ushr */
#define AL_V_SLL32(p, d, a, imm) ALV_A_SHL(p, 0x4f005400u, (d), (a), (imm)) /* shl  */

/* bit-select: bsl overwrites its mask operand, so the mask is copied
 * into the destination first, exactly as the x86 lowering copies. */
#define AL_V_SELECT(p, d, m, a, b, tmp) do { \
    ALV_A_3(p, 0x4ea01c00u, (d), (m), (m)); /* mov Vd,Vm (orr d,m,m) */ \
    ALV_A_3(p, 0x6e601c00u, (d), (a), (b)); /* bsl Vd.16B,Vn,Vm */ \
    (void)(tmp); } while (0)

/* ldr/str q, [Xbase, #imm12*16] */
#define AL_V_LOAD(p, d, base, disp) \
    ALV_A_W(p, 0x3dc00000u | ((((disp) >> 4) & 0xfff) << 10) | (((base) & 31) << 5) | ((d) & 31))
#define AL_V_STORE(p, s, base, disp) \
    ALV_A_W(p, 0x3d800000u | ((((disp) >> 4) & 0xfff) << 10) | (((base) & 31) << 5) | ((s) & 31))


#define AL_V_MULLO16(p, d, a, b)  ALV_A_3(p, 0x4e609c00u, (d), (a), (b)) /* mul Vd.8H,Vn.8H,Vm.8H */
/* NEON multiplies 32-bit lanes directly, so the widening the x86 side
 * needs is not required here: one instruction, same result. */
#define AL_V_MADD16(p, d, a, b)   ALV_A_3(p, 0x4ea09c00u, (d), (a), (b)) /* mul Vd.4S,Vn.4S,Vm.4S */
#define AL_V_PACKSS32(p, d, a, b) do { \
    ALV_A_W(p, 0x0e614800u | (((a) & 31) << 5) | ((d) & 31)); /* sqxtn  Vd.4H, Vn.4S */ \
    ALV_A_W(p, 0x4e614800u | (((b) & 31) << 5) | ((d) & 31)); /* sqxtn2 Vd.8H, Vm.4S */ } while (0)
#define AL_V_MINU16(p, d, a, b)   ALV_A_3(p, 0x6e606c00u, (d), (a), (b)) /* umin Vd.8H */
#define AL_V_MOVMSK(p, gpr, s)    ALV_A_W(p, 0x0e043c00u | (((s) & 31) << 5) | ((gpr) & 31)) /* umov w,Vn.s[0] */
#define AL_V_LOADDUP32(p, d, base, disp) \
    ALV_A_W(p, 0x4d40c800u | (((base) & 31) << 5) | ((d) & 31))  /* ld1r {Vd.4S},[Xn] */

#define AL_V_RET(p) ALV_A_W(p, 0xd65f03c0u)

#endif
#endif /* AL_VEMIT_H */
