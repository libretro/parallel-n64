/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - audio_backend_libretro.c                                *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2014 Bobby Smiles                                       *
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

/* libretro audio backend for parallel-n64.
 *
 * Design:
 *
 *   The N64 AI hardware always emits 16-bit stereo samples regardless of
 *   the BITRATE register (which controls the serial-DAC transfer rate,
 *   not the sample depth). The libretro frontend's audio mixer accepts
 *   variable-rate input via retro_get_system_av_info / RETRO_ENVIRONMENT
 *   _SET_SYSTEM_AV_INFO and resamples to the host audio device at high
 *   quality. There is therefore no reason to resample inside the core;
 *   we declare the actual N64 sample rate to the frontend and pass the
 *   samples through with a single byteswap-on-copy step into a per-
 *   retro_run accumulator. The accumulator is drained exactly once per
 *   retro_run iteration (immediately after emu_step_render in the
 *   libretro entry point), so each video frame is paired with exactly
 *   one audio batch -- the determinism contract a well-behaved libretro
 *   core promises its frontend.
 *
 * Memory:
 *
 *   The accumulator is a single statically-sized int16_t array; no heap
 *   allocations, no float scratch buffers, no resampler state. The
 *   capacity is derived from the libretro lifecycle: worst case is PAL
 *   at 50 Hz video with the N64 game producing audio at 48 kHz (the
 *   highest rate the AI controller's BITRATE divider permits, well
 *   above any actual N64 game's choice). 48000 / 50 = 960 stereo
 *   frames per video frame. Beyond the single-frame working set, the
 *   per-run delivery smoother (see flush_audio_libretro below) carries a
 *   few VI fields of audio between flushes to even out games with a
 *   bursty mixer cadence, so the accumulator must also hold that carry:
 *   up to the high-water mark (4 fields) plus one run's push before the
 *   once-per-run drain. Sizing the buffer at 8192 stereo frames (~8.5
 *   fields even at the 960-frame worst case, 32 KB total) keeps that
 *   working set well clear of the capacity, so the emergency mid-frame
 *   drain in push_audio_samples_via_libretro is never reached by any
 *   real game. */

#include "../../api/m64p_types.h"
#include <libretro.h>
#include "../../device/rcp/ai/ai_controller.h"
#include "../../main/main.h"
#include "../../device/device.h"
#include "../../main/rom.h"
#include "../plugin.h"
#include "../../device/rcp/ri/ri_controller.h"
#include "../../device/rcp/vi/vi_controller.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern retro_audio_sample_batch_t audio_batch_cb;
extern retro_environment_t        environ_cb;

#define AUDIO_ACC_FRAMES   8192u

static int16_t  audio_acc[AUDIO_ACC_FRAMES * 2];
static size_t   audio_acc_frames;        /* stereo frames currently held */
static unsigned current_sample_rate;     /* 0 until first set_audio_format */

/* Per-retro_run audio-delivery smoother state (see flush_audio_libretro). */
#define SMOOTH_RATE_WINDOW  512u   /* cap on the running-mean window     */
#define SMOOTH_SETPOINT_F   3u     /* carry setpoint, in fields (= rate)  */
#define SMOOTH_HIWATER_F    4u     /* hard-drain threshold, in fields     */

static unsigned long smooth_pushed_this_run; /* frames pushed since last flush */
static unsigned long smooth_rate_q16;        /* running mean of frames/run, Q16 */
static unsigned long smooth_runs;            /* retro_run count (window ramp)   */

void init_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   smooth_pushed_this_run = 0;
   smooth_rate_q16 = 0;
   smooth_runs = 0;
}

void deinit_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   smooth_pushed_this_run = 0;
   smooth_rate_q16 = 0;
   smooth_runs = 0;
}

unsigned get_audio_sample_rate_libretro(void)
{
   /* 32040 Hz is by far the most common N64 game rate and a reasonable
    * fallback when retro_get_system_av_info is queried before the game
    * has had a chance to issue its first AI register write. */
   return current_sample_rate ? current_sample_rate : 32040u;
}

/* Emit the first n stereo frames of the accumulator to the frontend in a
 * single audio_batch_cb call (looped only to absorb a partial-consume by
 * the frontend) and shift any remainder down to the front of audio_acc so
 * it carries into the next run. n is clamped to what is actually held. */
static void emit_frames(size_t n)
{
   const int16_t *out;
   size_t         remaining;
   size_t         consumed;
   size_t         left;

   if (n == 0)
      return;
   if (n > audio_acc_frames)
      n = audio_acc_frames;

   out       = audio_acc;
   remaining = n;

   while (remaining)
   {
      size_t ret = audio_batch_cb(out, remaining);
      if (ret == 0)
         break;                          /* frontend backpressure; keep the
                                          * remainder rather than stall the
                                          * emulator */
      remaining -= ret;
      out       += ret * 2;
   }

   consumed = n - remaining;
   left     = audio_acc_frames - consumed;
   if (left != 0 && consumed != 0)
      memmove(audio_acc, audio_acc + consumed * 2,
              left * 2 * sizeof(int16_t));
   audio_acc_frames = left;
}

/* Per-retro_run audio-delivery smoother.
 *
 * Most games push roughly one VI field of audio per retro_run, so the
 * accumulator drains as a steady ~1-field batch every frame. Some games
 * service their audio mixer on an irregular cadence: Doom 64 runs it on
 * alternate VI fields (pushing ~2 fields one run, nothing the next);
 * Perfect Dark runs it every retrace but with a variable per-tick sample
 * count and occasional catch-up frames, so its per-run push is a skewed
 * mix of 0 / 1-tick / 2-tick bursts. Either way the unsmoothed drain
 * emits a bursty pattern. A frontend with elastic dynamic-rate control
 * absorbs that, but the exact-content-framerate + scanline-sync path
 * cannot -- the ripple manifests as audio crackle and the video throttles
 * to a lower equilibrium (e.g. ~50 fps on a 60 Hz title).
 *
 * We even out delivery with a small controller. Each retro_run it emits a
 * target close to the game's true mean production rate and carries the
 * remainder in audio_acc, holding a few fields of cushion so a zero-push
 * run never starves the output:
 *
 *   rate   : running mean of frames pushed per run (Q16). A true mean
 *            (window ramps to 512, then fixed) -- NOT a fast EMA, which
 *            would settle biased high on a skewed burst distribution and
 *            then over-emit during transitions, draining the cushion and
 *            starving real audio (observed as a frame-rate dip whenever
 *            the soundscape changed).
 *   emit   = rate + ((carry - SETPOINT*rate) >> 3)  (proportional pull of
 *            the carry toward the cushion setpoint; gain 1/8).
 *   clamps : 0 <= emit <= 1.5*rate normally; if the carry exceeds HIWATER
 *            fields (a sustained production spike) drain down to two fields
 *            this run so the carry stays well under the buffer; never emit
 *            more than is held (no fabrication).
 *
 * Total sample count and ordering are preserved exactly; only the per-run
 * partition changes. The rate is derived from observed production, not the
 * declared timing.fps (a fixed constant that does not match every title),
 * so it cannot be skewed by that constant. Games that already deliver one
 * field per run see emit == their per-run push, making the controller a
 * cadence no-op for them (a constant few-field phase offset established at
 * boot, negligible against the frontend's own audio buffering). */
void flush_audio_libretro(void)
{
   unsigned long w;
   unsigned long rate;
   long          num;
   long          err;
   long          emit;
   long          cap;
   size_t        held;

   /* Fold the run that just finished into the running-mean rate (Q16).
    * w ramps 1..SMOOTH_RATE_WINDOW so this is a true cumulative average
    * over a bounded window rather than a fast EMA. */
   w = ++smooth_runs;
   if (w > SMOOTH_RATE_WINDOW)
      w = SMOOTH_RATE_WINDOW;
   num = (long)(smooth_pushed_this_run << 16) - (long)smooth_rate_q16;
   smooth_rate_q16 = (unsigned long)((long)smooth_rate_q16 + num / (long)w);
   smooth_pushed_this_run = 0;

   held = audio_acc_frames;
   if (held == 0)
      return;

   rate = smooth_rate_q16 >> 16;
   if (rate == 0)
   {
      /* no production history yet (silent boot): pass straight through */
      emit_frames(held);
      return;
   }

   /* proportional pull of the carry toward the SETPOINT-field cushion */
   err  = (long)held - (long)(SMOOTH_SETPOINT_F * rate);
   emit = (long)rate + (err >> 3);

   /* normal bounds: never negative, never more than 1.5 fields per run */
   cap = (long)((rate * 3u) / 2u);
   if (emit < 0)
      emit = 0;
   if (emit > cap)
      emit = cap;

   /* sustained production spike: hard-drain down to two fields so the
    * carry can never approach the accumulator capacity */
   if (held > SMOOTH_HIWATER_F * rate)
      emit = (long)held - (long)(2u * rate);

   /* never emit more than is actually held */
   if (emit > (long)held)
      emit = (long)held;

   emit_frames((size_t)emit);
}

/* bits is ignored: the AI controller always produces 16-bit stereo.
 * frequency is the N64 game's chosen sample rate; we forward it to the
 * frontend via RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO when it changes. */
void set_audio_format_via_libretro(void *user_data,
      unsigned int frequency, unsigned int bits)
{
   (void)user_data;
   (void)bits;

   if (frequency == 0 || frequency == current_sample_rate)
      return;

   current_sample_rate = frequency;

   /* the production-rate estimate is in absolute frames/run, which scales
    * with the sample rate; reset it so the smoother re-converges to the
    * new title's cadence instead of dragging the old rate along */
   smooth_rate_q16        = 0;
   smooth_runs            = 0;
   smooth_pushed_this_run = 0;

   if (environ_cb)
   {
      struct retro_system_av_info info;
      extern void retro_get_system_av_info(struct retro_system_av_info *info);
      retro_get_system_av_info(&info);
      info.timing.sample_rate = (double)frequency;
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
   }
}

/* Append size bytes of N64 AI audio (BE s16 stereo, (L, R) interleaved
 * per the AI controller's storage convention) to the per-frame
 * accumulator. Performs a single byteswap-on-copy that materialises
 * host-native (L, R) interleaved int16 in the destination, with
 * channel selection determined by the host endianness and the address
 * swizzle macros (S8) that map N64 logical byte addresses to physical
 * byte addresses in our RDRAM buffer.
 *
 *   On LE host (S8=3): a 32-bit BE stereo sample (L_hi L_lo R_hi R_lo)
 *   written by the N64 CPU through the swizzle lands in physical
 *   dram[] as bytes (R_lo R_hi L_lo L_hi); we rebuild L from p[3..2]
 *   and R from p[1..0] in host LE order.
 *
 *   On BE host (S8=0): no swizzle; p[0..3] already in (L_hi L_lo R_hi
 *   R_lo) logical order; rebuild L from p[0..1] and R from p[2..3] in
 *   host BE order.
 *
 * If the new push would overflow the remaining accumulator capacity we
 * flush what we have first. If a single push exceeds the full
 * accumulator capacity (only possible with a pathologically large AI
 * DMA buffer that no real game uses) we feed it through the
 * accumulator in chunks, each chunk causing one extra audio_batch_cb
 * call; the determinism contract degrades to "one batch per frame plus
 * one batch per AUDIO_ACC_FRAMES-sized chunk", still bounded and still
 * deterministic given identical inputs. */
void push_audio_samples_via_libretro(void *user_data,
      const void *buffer, size_t size)
{
   const uint8_t *src;
   size_t         frames;
   size_t         off;

   (void)user_data;

   if (buffer == NULL || size < 4)
      return;

   src    = (const uint8_t*)buffer;
   frames = size >> 2;                   /* 4 bytes per stereo frame */

   if (audio_acc_frames + frames > AUDIO_ACC_FRAMES)
      emit_frames(audio_acc_frames);     /* emergency full drain; the
                                          * smoother runs once per run from
                                          * flush_audio_libretro, not here */

   off = 0;
   while (off < frames)
   {
      size_t   chunk = frames - off;
      size_t   i;
      int16_t *dst;

      if (chunk > AUDIO_ACC_FRAMES - audio_acc_frames)
         chunk = AUDIO_ACC_FRAMES - audio_acc_frames;

      dst = audio_acc + audio_acc_frames * 2;

      for (i = 0; i < chunk; ++i)
      {
         const uint8_t *p = src + (off + i) * 4;
#ifdef MSB_FIRST
         dst[i*2 + 0] = (int16_t)((p[0] << 8) | p[1]);   /* L */
         dst[i*2 + 1] = (int16_t)((p[2] << 8) | p[3]);   /* R */
#else
         dst[i*2 + 0] = (int16_t)((p[3] << 8) | p[2]);   /* L */
         dst[i*2 + 1] = (int16_t)((p[1] << 8) | p[0]);   /* R */
#endif
      }

      audio_acc_frames       += chunk;
      smooth_pushed_this_run += chunk;   /* feeds the running-mean rate */
      off                    += chunk;

      if (audio_acc_frames == AUDIO_ACC_FRAMES && off < frames)
         emit_frames(audio_acc_frames);  /* emergency full drain */
   }
}

/* ---- next-style audio_out_backend_interface adapter ---------------------
 * next's init_ai() takes a (void* aout, const struct audio_out_backend_interface*)
 * pair. Wrap the existing libretro audio bridge in that interface so next's
 * ai_controller can drive it unchanged. set_frequency drops the 'bits' arg
 * (libretro output is always the fixed format push_audio_samples emits). */
#include "backends/api/audio_out_backend.h"

static void libretro_aout_set_frequency(void* aout, unsigned int frequency)
{
   set_audio_format_via_libretro(aout, frequency, 16);
}

static void libretro_aout_push_samples(void* aout, const void* samples, size_t size)
{
   push_audio_samples_via_libretro(aout, samples, size);
}

const struct audio_out_backend_interface audio_out_backend_libretro =
{
   libretro_aout_set_frequency,
   libretro_aout_push_samples
};
