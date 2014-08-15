/*
 * (C) Gražvydas "notaz" Ignotas, 2014
 *
 * This work is licensed under the terms of GNU GPL version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "arm_features.h"
#include "RenderBase_neon.h"

.syntax unified
.text
.align 3

/*
 * ProcessVertexData register map:
 *
 *  q | d | c code
 * ...      
 * 12  24   gRSPworldProject _11,_12,_13,_14
 *     25   
 * 13  26   gRSPworldProject _21,_22,_23,_24
 *     27   
 * 14  28   gRSPworldProject _31,_32,_33,_34
 *     29   
 * 15  30   gRSPworldProject _41,_42,_43,_44
 *     31   
 *
 * r4 vtx[], 16 bytes:
 * short y, x, flag, z, tv, tu;
 * / uint8 a, b, g, r;
 * \ char  a, z, y, x;
 *
 *  outputs:
 * r0        - XVECTOR4 *g_vtxTransformed
 * r1        - XVECTOR4 *g_vecProjected
 * r2        - uint32   *g_dwVtxDifColor
 * r3        - VECTOR2  *g_fVtxTxtCoords
 *     sp+00 - float    *g_fFogCoord
 * r6  sp+04 - uint32   *g_clipFlag2
 *  inputs:
 * r11 sp+08 - uint32      dwNum
 * r10 sp+0c - int         neon_flags
 * r4  sp+10 - FiddledVtx  vtx[], (r4 [0], r5 [1])
 * r7  sp+14 - Light      *gRSPlights
 *     sp+18 - float      *fRSPAmbientLightRGBA
 *     sp+1c - XMATRIX    *gRSPworldProject
 *     sp+20 - XMATRIX    *gRSPmodelViewTop
 *     sp+24 - uint32      gRSPnumLights
 *     sp+28 - float       gRSPfFogMin
 *     sp+2c - uint32      primitiveColor
 *     sp+30 - uint32      primitiveColor
 */
FUNCTION(pv_neon):
    ldr         r12, [sp, #0x10]
    pld         [r12]

    push        {r4-r11,lr}
    vpush       {q4-q7}

    mov         r4, r12               @ vtx
    ldr         r12, [sp, #0x64+0x1c]
    vld1.32     {q12,q13}, [r12, :128]! @ load gRSPworldProject
    vld1.32     {q14,q15}, [r12, :128]
    ldr         r6, [sp, #0x64+0x04]  @ g_clipFlag2
    add         r5, r4, #16           @ vtx + 1
    ldr         r11, [sp, #0x64+0x08] @ dwNum
    ldr         r10, [sp, #0x64+0x0c] @ neon_flags

0:
    vld1.16     d12, [r4]!            @ vtx[0] .z .flag .x .y (reg)
    vmovl.s16   q6, d12
    vld1.16     d14, [r5]!            @ vtx[1] .z .flag .x .y
    vmovl.s16   q7, d14
    vcvt.f32.s32 q6, q6               @ q6 = vtx_raw0
    vcvt.f32.s32 q7, q7               @ q7 = vtx_raw1
    vdup.32     q0, d12[1]            @ vtx_raw0.x (dup)
    vdup.32     q1, d12[0]            @ vtx_raw0.y (dup)
    vdup.32     q2, d13[1]            @ vtx_raw0.z (dup)
    vdup.32     q3, d14[1]            @ vtx_raw1.x (dup)
    vdup.32     q4, d14[0]            @ vtx_raw1.y (dup)
    vdup.32     q5, d15[1]            @ vtx_raw1.z (dup)
    /* note: order of operations matters greatly,
     * may cause like 20 fraction bits to differ! */
    vmul.f32    q0, q0, q12
    vmul.f32    q3, q3, q12
    vmla.f32    q0, q1, q13
    vmla.f32    q3, q4, q13
    vmul.f32    q2, q2, q14           @ yes, mul+add is
    vmul.f32    q5, q5, q14           @ faster than mla
    vadd.f32    q0, q2
    vadd.f32    q3, q5
    vadd.f32    q0, q15               @ q0 = g_vtxTransformed[i]
    vadd.f32    q3, q15               @ q3 = g_vtxTransformed[i + 1]

                                      vld1.16     d16[1], [r4]! @ [0].v
    vmov        d2, d1
                                      vld1.16     d16[0], [r4]! @ [0].u
    vsri.64     d2, d7, #32
                                      vld1.16     d18[1], [r5]! @ [0].v
#if 1
    vrecpe.f32  d4, d2                @ inv [0][1] .w
                                      vld1.16     d18[0], [r5]! @ [0].u
    vrecps.f32  d5, d2, d4            @ step
                                      vmovl.s16   q8, d16
    /* g_vtxTransformed[0] */         vst1.32     {q0}, [r0, :128]!
                                      vmovl.s16   q9, d18
                                      vcvt.f32.s32 d16, d16
                                      vcvt.f32.s32 d18, d18
    vmul.f32    d4, d5, d4            @ better inv
                                      bic         r9, r5, #63
                                      pld         [r9, #64]
    vrecps.f32  d5, d2, d4            @ step
                                      cmp         r11, #1
    /* u,v g_fVtxTxtCoords[0] */      vst1.32     {d16}, [r3]!
                                      beq         99f
    /* g_vtxTransformed[1] */         vst1.32     {q3}, [r0, :128]!
    /* ... [1] */                     vst1.32     {d18}, [r3]!
                                      99:
                                      vmov.f32    d20, #1.0
                                      vmov.f32    d21, #-1.0
    vmul.f32    d4, d5, d4            @ better inv [0][1] .w
 #if 0
    vrecps.f32  d5, d2, d4            @ step
    vmul.f32    d4, d5, d4            @ better inv
 #endif
#else
    mov         r12, #0x3f800000      @ 1.0f
    vmov.f32    s6, r12
    vdiv.f32    s8, s6, s4
    vdiv.f32    s9, s6, s5
 #error incomplete
#endif

                                      mov         r8, #X_CLIP_MAX
                                      mov         r9, #Y_CLIP_MAX
                                      vmov        d22, r8, r9
    vmul.f32    q0, q0, d4[1]         @ .x .y .z .w *= [0] .w
    vmul.f32    q1, q3, d4[0]
    vshr.u64    d5, d4, #32           @ [0] .w
                                      mov         r8, #X_CLIP_MIN
                                      mov         r9, #Y_CLIP_MIN
                                      vmov        d23, r8, r9
    vsli.64     d3, d4, #32           @ insert [1] .w
    vsli.64     d1, d5, #32
                                      vsli.u64    d5, d4, #32 @ [0] [1] .w
                                      vcgt.f32    d6, d0, d20 @ .xy > 1.0?
                                      vcgt.f32    d7, d21, d0
                                      vcgt.f32    d4, d5, #0  @ .w > 0?
    vst1.32     {q0}, [r1]!           @ g_vecProjected[0]
                                      vcgt.f32    d8, d2, d20
                                      vcgt.f32    d9, d21, d2
    vld1.32     d0[0], [r4]!          @ mem: [0] .azyx
                                      vand        q3, q11
                                      vand        q4, q11
    cmp         r11, #1
    beq         99f
    vst1.32     {q1}, [r1]!           @ g_vecProjected[1]
99:
                                      vorr        d6, d6, d7
                                      vorr        d7, d8, d9
    vld1.32     d0[1], [r5]!          @ mem: [1] .azyx
                                      vpadd.u32   d6, d7
    vrev32.8    d0, d0                @ make 0xaazzyyxx [1][0]
    vsli.u64    d1, d3, #32           @ d3 = [1] [0] .z
    vmovl.s8    q4, d0
                                      vand        d6, d4
    vmovl.s16   q1, d8
    vmovl.s16   q2, d9
                                      vst1.32     {d6}, [r6]! @ g_clipFlag2

    tst         r10, #PV_NEON_ENABLE_LIGHT
    beq         pv_neon_no_light
@ pv_neon_light:
    @ live NEON registers:
    @ d1    = [1][0] .z (must preserve)
    @ q1,q2 = azyx [1][0]
    @ q12+  = gRSPworldProject
    ldr         r12, [sp, #0x64+0x20]
    vcvt.f32.s32 q1, q1
    vcvt.f32.s32 q2, q2
    vld1.32     {q8,q9}, [r12, :128]! @ load gRSPmodelViewTop
    vld1.32     {q10},   [r12, :128]

    vdup.32     q5, d4[0]             @ [1] .x (dup)
    vdup.32     q6, d4[1]             @ [1] .y (dup)
    vdup.32     q7, d5[0]             @ [1] .z (dup)
    vdup.32     q2, d2[0]             @ [0] .x (dup)
    vdup.32     q3, d2[1]             @ [0] .y (dup)
    vdup.32     q4, d3[0]             @ [0] .z (dup)
    vmul.f32    q2, q2, q8
    vmul.f32    q5, q5, q8
    vmla.f32    q2, q3, q9
    vmla.f32    q5, q6, q9
    vmul.f32    q4, q4, q10
    vmul.f32    q7, q7, q10
    vadd.f32    q4, q2                @ q4 = temp[0] .xyz0
    vadd.f32    q5, q7                @ q5 = temp[1] .xyz0
    vmul.f32    q2, q4, q4            @ temp .xyz0 ^2
    vmul.f32    q3, q5, q5
    vpadd.f32   d2, d4, d5
    vpadd.f32   d3, d6, d7
    movw        r8, #0x0000ffff
    movt        r8, #0x7f7f           @ max normal float, ~3.4e+38
    vdup.32     d4, r8
    vpadd.f32   d2, d2, d3            @ d2 = [1][0] x^2 + y^2 + z^2
    vcgt.f32    d5, d2, #0
    vbif        d2, d4, d5            @ if (d2 == 0) d2 = MAXFLOAT

    vrsqrte.f32 d3, d2                @ ~ 1/sqrt(d2), d2 = [1][0] .sqrsum
    vmul.f32    d4, d3, d2
    ldr         r9, [sp, #0x64+0x18]  @ &fRSPAmbientLightRGBA
    ldr         r7, [sp, #0x64+0x14]  @ gRSPlights
    ldr         r8, [sp, #0x64+0x24]  @ gRSPnumLights
    vrsqrts.f32 d4, d3, d4            @ step
                                      vld1.32     {q6}, [r9] @ rgb
                                      vld1.32     {q7}, [r9] @ rgb
    vmul.f32    d3, d3, d4            @ 1/sqrt(d2)
#if 0 /* not necessary? */
    vmul.f32    d4, d3, d2
    vrsqrts.f32 d4, d3, d4            @ step
    vmul.f32    d3, d3, d4            @ 1/sqrt(d2)
#endif
    vmul.f32    q2, q4, d3[0]         @ q2 = normal[0] .xyz
    vmul.f32    q3, q5, d3[1]         @ q3 = normal[1] .xyz

1:
    vld1.32     {q8}, [r7]
    vmul.f32    q4, q8, q2            @ gRSPlights[l] * normal
    vmul.f32    q5, q8, q3
    vpadd.f32   d8, d8, d9
    vpadd.f32   d10, d10, d11
    vpadd.f32   d8, d8, d10           @ d8 = [1][0] fCosT
    vcgt.f32    d9, d8, #0            @ if (!(fCosT > 0))
    vand        d8, d9                @   fCosT = 0
    add         r9, r7, #OFFSETOF_Light_fr
    vld1.32     {q8}, [r9]            @ .fr .fg .fb
    vdup.32     q5, d8[1]             @ [1] fCosT (dup)
    vdup.32     q4, d8[0]             @
    vmla.f32    q7, q8, q5            @ .rgb += frgb * fCosT
    vmla.f32    q6, q8, q4
    add         r7, #SIZEOF_Light
    subs        r8, #1
    bgt         1b

    movt        r8, #0x437f           @ float 255
    vdup.32     q8, r8
    vcgt.f32    q4, q6, q8            @ if (.rgb > 255)
    vcgt.f32    q5, q7, q8
    vbit        q6, q8, q4            @   .rgb = 255
    vbit        q7, q8, q5
    vcvt.u32.f32 q6, q6
    vcvt.u32.f32 q7, q7
    ldrb        r8, [r4, #-4]         @ .a from vtx
    ldrb        r9, [r5, #-4]
    vext.32     q4, q6, q6, #3        @ reg: .abgr -> .bgra
    vext.32     q5, q7, q7, #3
    vmov.32     d8[0], r8             @ use .a from input
    vmov.32     d10[0], r9
    vmovn.u32   d8, q4
    vmovn.u32   d10, q5
    vmovn.u16   d0, q4
    vmovn.u16   d2, q5
    vsli.u64    d0, d2, #32
    vrev32.8    d0, d0                @ 0xbbggrraa -> 0xaarrggbb
    b           pv_neon_fog_alpha

pv_neon_no_light:
    tst         r10, #PV_NEON_ENABLE_SHADE
    vldr        d0, [sp, #0x64+0x2c]  @ primitiveColor [0] [1]
    beq         pv_neon_fog_alpha
    @ easier to do with ARM
    ldr         r8, [r4, #-4]
    ldr         r9, [r5, #-4]
    ror         r8, #8                @ mem: .argb -> .rgba
    ror         r9, #8                @ reg: 0xbbggrraa -> ..
    vmov        d0, r8, r9

pv_neon_fog_alpha:
    tst         r10, #PV_NEON_FOG_ALPHA
    beq         pv_neon_next
    vmov.f32    d20, #1.0
    vcgt.f32    d2, d1, d20           @ [0] [1] .z > 1.0?
    vcgt.f32    d3, d1, #0            @ > 0?
    movw        r8, #0
    movt        r8, #0x4f7f           @ r8 = (float)(255<<24)
    vbit        d1, d20, d2           @ make 1.0 if needed
    vand        d1, d3
    vdup.32     d4, r8
    vmul.f32    d1, d1, d4
    vcvt.u32.f32 d1, d1
    vmov.u32    d5, #0xff000000
    vbit        d0, d1, d5

pv_neon_next:
    subs        r11, #2
    vst1.32     {d0}, [r2]!           @ g_dwVtxDifColor
    add         r4, #16
    add         r5, #16
    bgt         0b
    nop

    vpop        {q4-q7}
    pop         {r4-r11,pc}
    .size       pv_neon, .-pv_neon
 

@ (float *d, const float *m1, const float *m2, const float *s)
FUNCTION(multiply_subtract2):
    vld1.32     {d1}, [r1]
    vld1.32     {d2}, [r2]
    vmul.f32    d0, d1, d2
    vld1.32     {d3}, [r3]
    vsub.f32    d0, d3
    vst1.32     {d0}, [r0]
    bx          lr
    .size       multiply_subtract2, .-multiply_subtract2


@ (const XVECTOR4 *v0, const XVECTOR4 *v1, const XVECTOR4 *v2)
FUNCTION(tv_direction):
    vld1.32     {q0}, [r0]
    vld1.32     {q2}, [r2]
    vld1.32     {q1}, [r1]
    vsub.f32    d6, d4, d0     @ d6 = V2,V1
    vsub.f32    d7, d4, d2     @ d7 = W2,W1
    vmul.f32    d1, d5         @ d1 = v0.w * v2.w
    vrev64.32   d7, d7
    vmul.f32    d6, d7         @ d6 = V2*W1,V1*W2
    vmul.f32    d1, d3         @ d1 *= v1.w
    vshr.u64    d7, d6, #32
    vsub.f32    d6, d7         @ d6[0] = V1*W2 - V2*W1
    vshr.u64    d1, d1, #32
    vmul.f32    d0, d1, d6
    vmov.32     r0, d0[0]
    bx          lr


@ vim:filetype=armasm:expandtab
