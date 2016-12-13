/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* Bog-standard windowed SINC implementation. */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef __SSE__
#include <xmmintrin.h>
#endif

#include <retro_inline.h>
#include <filters.h>
#include <memalign.h>

#include "../audio_resampler_driver.h"

/* Rough SNR values for upsampling:
 * LOWEST: 40 dB
 * LOWER: 55 dB
 * NORMAL: 70 dB
 * HIGHER: 110 dB
 * HIGHEST: 140 dB
 */

/* TODO, make all this more configurable. */
#if defined(SINC_LOWEST_QUALITY)
#define SINC_WINDOW_LANCZOS
#define CUTOFF 0.98
#define PHASE_BITS 12
#define SINC_COEFF_LERP 0
#define SUBPHASE_BITS 10
#define SIDELOBES 2
#define ENABLE_AVX 0
#elif defined(SINC_LOWER_QUALITY)
#define SINC_WINDOW_LANCZOS
#define CUTOFF 0.98
#define PHASE_BITS 12
#define SUBPHASE_BITS 10
#define SINC_COEFF_LERP 0
#define SIDELOBES 4
#define ENABLE_AVX 0
#elif defined(SINC_HIGHER_QUALITY)
#define SINC_WINDOW_KAISER
#define SINC_WINDOW_KAISER_BETA 10.5
#define CUTOFF 0.90
#define PHASE_BITS 10
#define SUBPHASE_BITS 14
#define SINC_COEFF_LERP 1
#define SIDELOBES 32
#define ENABLE_AVX 1
#elif defined(SINC_HIGHEST_QUALITY)
#define SINC_WINDOW_KAISER
#define SINC_WINDOW_KAISER_BETA 14.5
#define CUTOFF 0.962
#define PHASE_BITS 10
#define SUBPHASE_BITS 14
#define SINC_COEFF_LERP 1
#define SIDELOBES 128
#define ENABLE_AVX 1
#else
#define SINC_WINDOW_KAISER
#define SINC_WINDOW_KAISER_BETA 5.5
#define CUTOFF 0.825
#define PHASE_BITS 8
#define SUBPHASE_BITS 16
#define SINC_COEFF_LERP 1
#define SIDELOBES 8
#define ENABLE_AVX 0
#endif

/* For the little amount of taps we're using,
 * SSE1 is faster than AVX for some reason.
 * AVX code is kept here though as by increasing number
 * of sinc taps, the AVX code is clearly faster than SSE1.
 */

#if defined(__AVX__) && ENABLE_AVX
#include <immintrin.h>
#endif

#define PHASES (1 << (PHASE_BITS + SUBPHASE_BITS))

#define TAPS (SIDELOBES * 2)
#define SUBPHASE_MASK ((1 << SUBPHASE_BITS) - 1)
#define SUBPHASE_MOD (1.0f / (1 << SUBPHASE_BITS))

typedef struct rarch_sinc_resampler
{
   float *phase_table;
   float *buffer_l;
   float *buffer_r;

   unsigned taps;

   unsigned ptr;
   uint32_t time;

   /* A buffer for phase_table, buffer_l and buffer_r 
    * are created in a single calloc().
    * Ensure that we get as good cache locality as we can hope for. */
   float *main_buffer;
} rarch_sinc_resampler_t;

#if defined(SINC_WINDOW_LANCZOS)
#define window_function(idx)  (lanzcos_window_function(idx))
#elif defined(SINC_WINDOW_KAISER)
#define window_function(idx)  (kaiser_window_function(idx, SINC_WINDOW_KAISER_BETA))
#else
#error "No SINC window function defined."
#endif

static void init_sinc_table(rarch_sinc_resampler_t *resamp, double cutoff,
      float *phase_table, int phases, int taps, bool calculate_delta)
{
   int i, j;
   double    window_mod = window_function(0.0); /* Need to normalize w(0) to 1.0. */
   int           stride = calculate_delta ? 2 : 1;
   double     sidelobes = taps / 2.0;

   for (i = 0; i < phases; i++)
   {
      for (j = 0; j < taps; j++)
      {
         double sinc_phase;
         float val;
         int               n = j * phases + i;
         double window_phase = (double)n / (phases * taps); /* [0, 1). */
         window_phase = 2.0 * window_phase - 1.0; /* [-1, 1) */
         sinc_phase = sidelobes * window_phase;

         val = cutoff * sinc(M_PI * sinc_phase * cutoff) * 
            window_function(window_phase) / window_mod;
         phase_table[i * stride * taps + j] = val;
      }
   }

   if (calculate_delta)
   {
      int phase;
      int p;

      for (p = 0; p < phases - 1; p++)
      {
         for (j = 0; j < taps; j++)
         {
            float delta = phase_table[(p + 1) * stride * taps + j] - 
               phase_table[p * stride * taps + j];
            phase_table[(p * stride + 1) * taps + j] = delta;
         }
      }

      phase = phases - 1;
      for (j = 0; j < taps; j++)
      {
         float val, delta;
         double sinc_phase;
         int n = j * phases + (phase + 1);
         double window_phase = (double)n / (phases * taps); /* (0, 1]. */
         window_phase = 2.0 * window_phase - 1.0; /* (-1, 1] */
         sinc_phase = sidelobes * window_phase;

         val = cutoff * sinc(M_PI * sinc_phase * cutoff) * 
            window_function(window_phase) / window_mod;
         delta = (val - phase_table[phase * stride * taps + j]);
         phase_table[(phase * stride + 1) * taps + j] = delta;
      }
   }
}

#if !(defined(__AVX__) && ENABLE_AVX) && !defined(__SSE__)
static INLINE void process_sinc_C(rarch_sinc_resampler_t *resamp,
      float *out_buffer)
{
   unsigned i;
   float sum_l              = 0.0f;
   float sum_r              = 0.0f;
   const float *buffer_l    = resamp->buffer_l + resamp->ptr;
   const float *buffer_r    = resamp->buffer_r + resamp->ptr;
   unsigned taps            = resamp->taps;
   unsigned phase           = resamp->time >> SUBPHASE_BITS;
#if SINC_COEFF_LERP
   const float *phase_table = resamp->phase_table + phase * taps * 2;
   const float *delta_table = phase_table + taps;
   float delta              = (float)
      (resamp->time & SUBPHASE_MASK) * SUBPHASE_MOD;
#else
   const float *phase_table = resamp->phase_table + phase * taps;
#endif

   for (i = 0; i < taps; i++)
   {
#if SINC_COEFF_LERP
      float sinc_val = phase_table[i] + delta_table[i] * delta;
#else
      float sinc_val = phase_table[i];
#endif
      sum_l         += buffer_l[i] * sinc_val;
      sum_r         += buffer_r[i] * sinc_val;
   }

   out_buffer[0] = sum_l;
   out_buffer[1] = sum_r;
}
#endif

#if defined(__AVX__) && ENABLE_AVX
#define process_sinc_func process_sinc
static void process_sinc(rarch_sinc_resampler_t *resamp, float *out_buffer)
{
   unsigned i;
   __m256 sum_l             = _mm256_setzero_ps();
   __m256 sum_r             = _mm256_setzero_ps();

   const float *buffer_l    = resamp->buffer_l + resamp->ptr;
   const float *buffer_r    = resamp->buffer_r + resamp->ptr;

   unsigned taps            = resamp->taps;
   unsigned phase           = resamp->time >> SUBPHASE_BITS;
#if SINC_COEFF_LERP
   const float *phase_table = resamp->phase_table + phase * taps * 2;
   const float *delta_table = phase_table + taps;
   __m256 delta             = _mm256_set1_ps((float)
         (resamp->time & SUBPHASE_MASK) * SUBPHASE_MOD);
#else
   const float *phase_table = resamp->phase_table + phase * taps;
#endif

   for (i = 0; i < taps; i += 8)
   {
      __m256 buf_l  = _mm256_loadu_ps(buffer_l + i);
      __m256 buf_r  = _mm256_loadu_ps(buffer_r + i);

#if SINC_COEFF_LERP
      __m256 deltas = _mm256_load_ps(delta_table + i);
      __m256 sinc   = _mm256_add_ps(_mm256_load_ps(phase_table + i),
            _mm256_mul_ps(deltas, delta));
#else
      __m256 sinc   = _mm256_load_ps(phase_table + i);
#endif
      sum_l         = _mm256_add_ps(sum_l, _mm256_mul_ps(buf_l, sinc));
      sum_r         = _mm256_add_ps(sum_r, _mm256_mul_ps(buf_r, sinc));
   }

   /* hadd on AVX is weird, and acts on low-lanes 
    * and high-lanes separately. */
   __m256 res_l = _mm256_hadd_ps(sum_l, sum_l);
   __m256 res_r = _mm256_hadd_ps(sum_r, sum_r);
   res_l        = _mm256_hadd_ps(res_l, res_l);
   res_r        = _mm256_hadd_ps(res_r, res_r);
   res_l        = _mm256_add_ps(_mm256_permute2f128_ps(res_l, res_l, 1), res_l);
   res_r        = _mm256_add_ps(_mm256_permute2f128_ps(res_r, res_r, 1), res_r);

   /* This is optimized to mov %xmmN, [mem].
    * There doesn't seem to be any _mm256_store_ss intrinsic. */
   _mm_store_ss(out_buffer + 0, _mm256_extractf128_ps(res_l, 0));
   _mm_store_ss(out_buffer + 1, _mm256_extractf128_ps(res_r, 0));
}
#elif defined(__SSE__)
#define process_sinc_func process_sinc
static void process_sinc(rarch_sinc_resampler_t *resamp, float *out_buffer)
{
   unsigned i;
   __m128 sum;
   __m128 sum_l             = _mm_setzero_ps();
   __m128 sum_r             = _mm_setzero_ps();

   const float *buffer_l    = resamp->buffer_l + resamp->ptr;
   const float *buffer_r    = resamp->buffer_r + resamp->ptr;

   unsigned taps            = resamp->taps;
   unsigned phase           = resamp->time >> SUBPHASE_BITS;
#if SINC_COEFF_LERP
   const float *phase_table = resamp->phase_table + phase * taps * 2;
   const float *delta_table = phase_table + taps;
   __m128 delta             = _mm_set1_ps((float)
         (resamp->time & SUBPHASE_MASK) * SUBPHASE_MOD);
#else
   const float *phase_table = resamp->phase_table + phase * taps;
#endif

   for (i = 0; i < taps; i += 4)
   {
      __m128 buf_l = _mm_loadu_ps(buffer_l + i);
      __m128 buf_r = _mm_loadu_ps(buffer_r + i);

#if SINC_COEFF_LERP
      __m128 deltas = _mm_load_ps(delta_table + i);
      __m128 _sinc  = _mm_add_ps(_mm_load_ps(phase_table + i),
            _mm_mul_ps(deltas, delta));
#else
      __m128 _sinc = _mm_load_ps(phase_table + i);
#endif
      sum_l        = _mm_add_ps(sum_l, _mm_mul_ps(buf_l, _sinc));
      sum_r        = _mm_add_ps(sum_r, _mm_mul_ps(buf_r, _sinc));
   }

   /* Them annoying shuffles.
    * sum_l = { l3, l2, l1, l0 }
    * sum_r = { r3, r2, r1, r0 }
    */

   sum = _mm_add_ps(_mm_shuffle_ps(sum_l, sum_r,
            _MM_SHUFFLE(1, 0, 1, 0)),
         _mm_shuffle_ps(sum_l, sum_r, _MM_SHUFFLE(3, 2, 3, 2)));

   /* sum   = { r1, r0, l1, l0 } + { r3, r2, l3, l2 }
    * sum   = { R1, R0, L1, L0 }
    */

   sum = _mm_add_ps(_mm_shuffle_ps(sum, sum, _MM_SHUFFLE(3, 3, 1, 1)), sum);

   /* sum   = {R1, R1, L1, L1 } + { R1, R0, L1, L0 }
    * sum   = { X,  R,  X,  L } 
    */

   /* Store L */
   _mm_store_ss(out_buffer + 0, sum);

   /* movehl { X, R, X, L } == { X, R, X, R } */
   _mm_store_ss(out_buffer + 1, _mm_movehl_ps(sum, sum));
}
#elif defined(__ARM_NEON__) && !defined(VITA)

#if SINC_COEFF_LERP
#error "NEON asm does not support SINC lerp."
#endif

/* Need to make this function pointer as Android doesn't 
 * have built-in targets for NEON and plain ARMv7a.
 */
static void (*process_sinc_func)(rarch_sinc_resampler_t *resamp,
      float *out_buffer);

/* Assumes that taps >= 8, and that taps is a multiple of 8. */
void process_sinc_neon_asm(float *out, const float *left, 
      const float *right, const float *coeff, unsigned taps);

static void process_sinc_neon(rarch_sinc_resampler_t *resamp,
      float *out_buffer)
{
   const float *buffer_l    = resamp->buffer_l + resamp->ptr;
   const float *buffer_r    = resamp->buffer_r + resamp->ptr;

   unsigned phase           = resamp->time >> SUBPHASE_BITS;
   unsigned taps            = resamp->taps;
   const float *phase_table = resamp->phase_table + phase * taps;

   process_sinc_neon_asm(out_buffer, buffer_l, buffer_r, phase_table, taps);
}
#else /* Plain ol' C99 */
#define process_sinc_func process_sinc_C
#endif

static void resampler_sinc_process(void *re_, struct resampler_data *data)
{
   rarch_sinc_resampler_t *re = (rarch_sinc_resampler_t*)re_;

   uint32_t ratio        = PHASES / data->ratio;
   const float *input    = data->data_in;
   float *output         = data->data_out;
   size_t frames         = data->input_frames;
   size_t out_frames     = 0;

   while (frames)
   {
      while (frames && re->time >= PHASES)
      {
         /* Push in reverse to make filter more obvious. */
         if (!re->ptr)
            re->ptr = re->taps;
         re->ptr--;

         re->buffer_l[re->ptr + re->taps] = re->buffer_l[re->ptr] = *input++;
         re->buffer_r[re->ptr + re->taps] = re->buffer_r[re->ptr] = *input++;

         re->time -= PHASES;
         frames--;
      }

      while (re->time < PHASES)
      {
         process_sinc_func(re, output);
         output += 2;
         out_frames++;
         re->time += ratio;
      }
   }

   data->output_frames = out_frames;
}

static void resampler_sinc_free(void *re)
{
   rarch_sinc_resampler_t *resampler = (rarch_sinc_resampler_t*)re;
   if (resampler)
      memalign_free(resampler->main_buffer);
   free(resampler);
}

static void *resampler_sinc_new(const struct resampler_config *config,
      double bandwidth_mod, resampler_simd_mask_t mask)
{
   size_t phase_elems, elems;
   double cutoff;
   rarch_sinc_resampler_t *re = (rarch_sinc_resampler_t*)
      calloc(1, sizeof(*re));

   if (!re)
      return NULL;

   (void)config;

   re->taps = TAPS;
   cutoff   = CUTOFF;

   /* Downsampling, must lower cutoff, and extend number of 
    * taps accordingly to keep same stopband attenuation. */
   if (bandwidth_mod < 1.0)
   {
      cutoff *= bandwidth_mod;
      re->taps = (unsigned)ceil(re->taps / bandwidth_mod);
   }

   /* Be SIMD-friendly. */
#if (defined(__AVX__) && ENABLE_AVX) || (defined(__ARM_NEON__)&& !defined(VITA))
   re->taps = (re->taps + 7) & ~7;
#else
   re->taps = (re->taps + 3) & ~3;
#endif

   phase_elems = (1 << PHASE_BITS) * re->taps;
#if SINC_COEFF_LERP
   phase_elems *= 2;
#endif
   elems = phase_elems + 4 * re->taps;

   re->main_buffer = (float*)memalign_alloc(128, sizeof(float) * elems);
   if (!re->main_buffer)
      goto error;

   re->phase_table = re->main_buffer;
   re->buffer_l = re->main_buffer + phase_elems;
   re->buffer_r = re->buffer_l + 2 * re->taps;

   init_sinc_table(re, cutoff, re->phase_table,
         1 << PHASE_BITS, re->taps, SINC_COEFF_LERP);

#if defined(__ARM_NEON__) && !defined(VITA)
   process_sinc_func = mask & RESAMPLER_SIMD_NEON 
      ? process_sinc_neon : process_sinc_C;
#endif

   return re;

error:
   resampler_sinc_free(re);
   return NULL;
}

rarch_resampler_t sinc_resampler = {
   resampler_sinc_new,
   resampler_sinc_process,
   resampler_sinc_free,
   RESAMPLER_API_VERSION,
   "sinc",
   "sinc"
};
