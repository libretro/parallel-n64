/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - fpu.c                                                   *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2010 Ari64                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef M64P_R4300_FPU_H
#define M64P_R4300_FPU_H

#include <math.h>

#include "r4300.h"

#if defined(EMSCRIPTEN)
#define CUSTOM_FESETROUND
#endif

#if defined(_MSC_VER)
#define CUSTOM_FESETROUND
#define M64P_FPU_INLINE static __inline
#include <float.h>
typedef enum { FE_TONEAREST = 0, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD } eRoundType;

static __inline double round(double x) { return floor(x + 0.5); }
static __inline float roundf(float x) { return (float) floor(x + 0.5); }
static __inline double trunc(double x) { return (double) (int32_t) x; }
static __inline float truncf(float x) { return (float) (int32_t) x; }
#define isnan _isnan
#else
#define M64P_FPU_INLINE static inline
#include <fenv.h>
#endif

#ifndef FE_TONEAREST
#define FE_TONEAREST 0x0
#endif

#ifndef FE_TOWARDZERO
#define FE_TOWARDZERO 0x1
#endif

#ifndef FE_UPWARD
#define FE_UPWARD 0x2
#endif

#ifndef FE_DOWNWARD
#define FE_DOWNWARD 0x3
#endif

#ifndef _FPSCR_RMODE_SHIFT
#define _FPSCR_RMODE_SHIFT 22
#endif

#ifdef ANDROID_OLD_GCC
typedef __uint32_t fenv_t;
typedef __uint32_t fexcept_t;
#define CUSTOM_FEGETENV
#define CUSTOM_FESETENV
#define CUSTOM_FESETROUND
#endif

#ifdef CUSTOM_FEGETENV

#ifdef ANDROID
static INLINE int32_t m64p_fegetenv(fenv_t* __envp)
{
   fenv_t _fpscr;
#if !defined(__SOFTFP__)
#if !defined(__thumb__) || defined(__thumb2__)
   __asm__ __volatile__("vmrs %0,fpscr" : "=r" (_fpscr));
#else
   /* Switching from thumb1 to arm, do vmrs, then switch back */
   __asm__ __volatile__(
         ".balign 4 \n\t"
         "mov ip, pc \n\t"
         "bx ip \n\t"
         ".arm \n\t"
         "vmrs %0, fpscr \n\t"
         "add ip, pc, #1 \n\t"
         "bx ip \n\t"
         ".thumb \n\t"
         : "=r" (_fpscr) : : "ip");
#endif
#else
   _fpscr = 0;
#endif
   *__envp = _fpscr;
   return 0;
}

#endif
#else
#define m64p_fegetenv fegetenv
#endif

#ifdef CUSTOM_FESETENV

#ifdef ANDROID
static INLINE int32_t m64p_fesetenv(const fenv_t* __envp)
{
   fenv_t _fpscr = *__envp;
#if !defined(__SOFTFP__)
#if !defined(__thumb__) || defined(__thumb2__)
   __asm__ __volatile__("vmsr fpscr,%0" : :"ri" (_fpscr));
#else
   /* Switching from thumb1 to arm, do vmsr, then switch back */
   __asm__ __volatile__(
         ".balign 4 \n\t"
         "mov ip, pc \n\t"
         "bx ip \n\t"
         ".arm \n\t"
         "vmsr fpscr, %0 \n\t"
         "add ip, pc, #1 \n\t"
         "bx ip \n\t"
         ".thumb \n\t"
         : : "ri" (_fpscr) : "ip");
#endif
#else
   _fpscr = _fpscr;
#endif
   return 0;
}
#endif

#else
#define m64p_fesetenv fesetenv
#endif

#ifdef CUSTOM_FESETROUND

#if defined(ANDROID)
static INLINE int32_t m64p_fesetround(int32_t __round)
{
   uint32_t _fpscr;
   m64p_fegetenv(&_fpscr);
   _fpscr &= ~(0x3 << _FPSCR_RMODE_SHIFT);
   _fpscr |= (__round << _FPSCR_RMODE_SHIFT);
   m64p_fesetenv(&_fpscr);
   return 0;
}
#elif defined(_MSC_VER) && defined(_M_IX86)
static INLINE void m64p_fesetround(eRoundType RoundType)
{
   static const uint32_t msRound[4] = { _RC_NEAR, _RC_CHOP, _RC_UP, _RC_DOWN };
   uint32_t oldX87, oldSSE2;

   __control87_2(msRound[RoundType], _MCW_RC, &oldX87, &oldSSE2);
}
#elif defined(EMSCRIPTEN) || (0 == 0)
static INLINE int32_t m64p_fesetround(int32_t __round)
{
   (void)__round;
   return 0;
}
#endif

#else
#define m64p_fesetround fesetround
#endif

M64P_FPU_INLINE void set_rounding(void)
{
   switch(rounding_mode)
   {
      case 0x33F:
         m64p_fesetround(FE_TONEAREST);
         break;
      case 0xF3F:
         m64p_fesetround(FE_TOWARDZERO);
         break;
      case 0xB3F:
         m64p_fesetround(FE_UPWARD);
         break;
      case 0x73F:
         m64p_fesetround(FE_DOWNWARD);
         break;
   }
}

M64P_FPU_INLINE void cvt_s_w(int32_t *source,float *dest)
{
   set_rounding();
   *dest = (float) *source;
}
M64P_FPU_INLINE void cvt_d_w(int32_t *source,double *dest)
{
   *dest = (double) *source;
}
M64P_FPU_INLINE void cvt_s_l(int64_t *source,float *dest)
{
   set_rounding();
   *dest = (float) *source;
}
M64P_FPU_INLINE void cvt_d_l(int64_t *source,double *dest)
{
   set_rounding();
   *dest = (double) *source;
}
M64P_FPU_INLINE void cvt_d_s(float *source,double *dest)
{
   *dest = (double) *source;
}
M64P_FPU_INLINE void cvt_s_d(double *source,float *dest)
{
   set_rounding();
   *dest = (float) *source;
}

M64P_FPU_INLINE void round_l_s(float *source,int64_t *dest)
{
   *dest = (int64_t) roundf(*source);
}
M64P_FPU_INLINE void round_w_s(float *source, int32_t *dest)
{
   *dest = (int32_t) roundf(*source);
}
M64P_FPU_INLINE void trunc_l_s(float *source,int64_t *dest)
{
   *dest = (int64_t) truncf(*source);
}
M64P_FPU_INLINE void trunc_w_s(float *source,int32_t *dest)
{
   *dest = (int32_t) truncf(*source);
}
M64P_FPU_INLINE void ceil_l_s(float *source,int64_t *dest)
{
   *dest = (int64_t) ceilf(*source);
}
M64P_FPU_INLINE void ceil_w_s(float *source,int32_t *dest)
{
   *dest = (int32_t) ceilf(*source);
}
M64P_FPU_INLINE void floor_l_s(float *source,int64_t *dest)
{
   *dest = (int64_t) floorf(*source);
}
M64P_FPU_INLINE void floor_w_s(float *source,int32_t *dest)
{
   *dest = (int32_t) floorf(*source);
}

M64P_FPU_INLINE void round_l_d(double *source,int64_t *dest)
{
   *dest = (int64_t) round(*source);
}
M64P_FPU_INLINE void round_w_d(double *source, int32_t *dest)
{
   *dest = (int32_t) round(*source);
}
M64P_FPU_INLINE void trunc_l_d(double *source,int64_t *dest)
{
   *dest = (int64_t) trunc(*source);
}
M64P_FPU_INLINE void trunc_w_d(double *source, int32_t *dest)
{
   *dest = (int32_t) trunc(*source);
}
M64P_FPU_INLINE void ceil_l_d(double *source,int64_t *dest)
{
   *dest = (int64_t) ceil(*source);
}
M64P_FPU_INLINE void ceil_w_d(double *source,int32_t *dest)
{
   *dest = (int32_t) ceil(*source);
}
M64P_FPU_INLINE void floor_l_d(double *source,int64_t *dest)
{
   *dest = (int64_t) floor(*source);
}
M64P_FPU_INLINE void floor_w_d(double *source,int32_t *dest)
{
   *dest = (int32_t) floor(*source);
}

M64P_FPU_INLINE void cvt_w_s(float *source,int32_t *dest)
{
   switch(FCR31&3)
   {
      case 0: round_w_s(source,dest);return;
      case 1: trunc_w_s(source,dest);return;
      case 2: ceil_w_s(source,dest);return;
      case 3: floor_w_s(source,dest);return;
   }
}
M64P_FPU_INLINE void cvt_w_d(double *source,int32_t *dest)
{
   switch(FCR31&3)
   {
      case 0: round_w_d(source,dest);return;
      case 1: trunc_w_d(source,dest);return;
      case 2: ceil_w_d(source,dest);return;
      case 3: floor_w_d(source,dest);return;
   }
}
M64P_FPU_INLINE void cvt_l_s(float *source,int64_t *dest)
{
   switch(FCR31&3)
   {
      case 0: round_l_s(source,dest);return;
      case 1: trunc_l_s(source,dest);return;
      case 2: ceil_l_s(source,dest);return;
      case 3: floor_l_s(source,dest);return;
   }
}
M64P_FPU_INLINE void cvt_l_d(double *source,int64_t *dest)
{
   switch(FCR31&3)
   {
      case 0: round_l_d(source,dest);return;
      case 1: trunc_l_d(source,dest);return;
      case 2: ceil_l_d(source,dest);return;
      case 3: floor_l_d(source,dest);return;
   }
}

M64P_FPU_INLINE void c_f_s(void)
{
   FCR31 &= ~0x800000;
}
M64P_FPU_INLINE void c_un_s(float *source,float *target)
{
   FCR31=(isnan(*source) || isnan(*target)) ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_eq_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ueq_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_olt_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ult_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_ole_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ule_s(float *source,float *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_sf_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31&=~0x800000;
}
M64P_FPU_INLINE void c_ngle_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31&=~0x800000;
}

M64P_FPU_INLINE void c_seq_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ngl_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_lt_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_nge_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_le_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ngt_s(float *source,float *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_f_d(void)
{
   FCR31 &= ~0x800000;
}
M64P_FPU_INLINE void c_un_d(double *source,double *target)
{
   FCR31=(isnan(*source) || isnan(*target)) ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_eq_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ueq_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_olt_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ult_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_ole_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31&=~0x800000;return;}
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ule_d(double *source,double *target)
{
   if (isnan(*source) || isnan(*target)) {FCR31|=0x800000;return;}
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_sf_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31&=~0x800000;
}
M64P_FPU_INLINE void c_ngle_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31&=~0x800000;
}

M64P_FPU_INLINE void c_seq_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ngl_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source==*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_lt_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_nge_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<*target ? FCR31|0x800000 : FCR31&~0x800000;
}

M64P_FPU_INLINE void c_le_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}
M64P_FPU_INLINE void c_ngt_d(double *source,double *target)
{
   //if (isnan(*source) || isnan(*target)) // FIXME - exception
   FCR31 = *source<=*target ? FCR31|0x800000 : FCR31&~0x800000;
}


M64P_FPU_INLINE void add_s(float *source1,float *source2,float *target)
{
   set_rounding();
   *target=(*source1)+(*source2);
}
M64P_FPU_INLINE void sub_s(float *source1,float *source2,float *target)
{
   set_rounding();
   *target=(*source1)-(*source2);
}
M64P_FPU_INLINE void mul_s(float *source1,float *source2,float *target)
{
   set_rounding();
   *target=(*source1)*(*source2);
}
M64P_FPU_INLINE void div_s(float *source1,float *source2,float *target)
{
   set_rounding();
   *target=(*source1)/(*source2);
}
M64P_FPU_INLINE void sqrt_s(float *source,float *target)
{
   set_rounding();
   *target=sqrtf(*source);
}
M64P_FPU_INLINE void abs_s(float *source,float *target)
{
   *target=fabsf(*source);
}
M64P_FPU_INLINE void mov_s(float *source,float *target)
{
   *target=*source;
}
M64P_FPU_INLINE void neg_s(float *source,float *target)
{
   *target=-(*source);
}
M64P_FPU_INLINE void add_d(double *source1,double *source2,double *target)
{
   set_rounding();
   *target=(*source1)+(*source2);
}
M64P_FPU_INLINE void sub_d(double *source1,double *source2,double *target)
{
   set_rounding();
   *target=(*source1)-(*source2);
}
M64P_FPU_INLINE void mul_d(double *source1,double *source2,double *target)
{
   set_rounding();
   *target=(*source1)*(*source2);
}
M64P_FPU_INLINE void div_d(double *source1,double *source2,double *target)
{
   set_rounding();
   *target=(*source1)/(*source2);
}
M64P_FPU_INLINE void sqrt_d(double *source,double *target)
{
   set_rounding();
   *target=sqrt(*source);
}
M64P_FPU_INLINE void abs_d(double *source,double *target)
{
   *target=fabs(*source);
}
M64P_FPU_INLINE void mov_d(double *source,double *target)
{
   *target=*source;
}
M64P_FPU_INLINE void neg_d(double *source,double *target)
{
   *target=-(*source);
}

#endif /* M64P_R4300_FPU_H */
