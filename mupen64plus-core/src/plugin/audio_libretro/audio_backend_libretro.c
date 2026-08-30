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
 *   not the sample depth). The frontend is told one fixed rate, once,
 *   and the game's stream is resampled to it here -- announcing the
 *   game's own rate would mean moving it later through RETRO_ENVIRONMENT
 *   _SET_SYSTEM_AV_INFO, which costs a CRT setup its switchres mode.
 *   See AUDIO_OUTPUT_RATE. Samples reach the accumulator through a
 *   byteswap-on-copy and a polyphase resampling step, per retro_run.
 *   The accumulator is drained exactly once per
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

/* The rate we declare to the frontend, once, from retro_get_system_av_info().
 *
 * The N64's own rate is a divider the game writes into AI_DACRATE and is
 * unknowable until the ROM runs, so the core used to declare a guess and
 * correct it later through RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO.  That is
 * legitimate, but RetroArch reinitialises the whole video pipeline on that
 * call whenever CRT switchres is on -- its no_video_reinit shortcut is
 * explicitly disabled there -- so a CRT setup loses the mode switchres picked
 * from the first frame, falls back to the default KMS mode and never returns.
 *
 * So never move the rate: declare a fixed one and resample the game's stream
 * to it here.  48000 is RetroArch's own default output rate, so the frontend's
 * resampler is usually a no-op and the stream is resampled once, not twice. */
#define AUDIO_OUTPUT_RATE  48000.0

static int16_t  audio_acc[AUDIO_ACC_FRAMES * 2];
static size_t   audio_acc_frames;        /* stereo frames currently held */
static unsigned current_sample_rate;     /* 0 until first set_audio_format */
static double   current_sample_rate_exact; /* the same rate, unrounded */

/* Polyphase resampler, game rate -> AUDIO_OUTPUT_RATE.
 *
 * The kernel is the Blackman-windowed sinc tap bank the HLE voice path
 * already carries (mupen64plus-rsp-hle/src/resample_hq.c): 256 phases, and a
 * set of cutoff bands so a ratio above unity pulls the cutoff down to 1/ratio
 * and keeps the image out of the passband.  Its rows are nudged to sum to
 * exactly 1.0 in Q15, so DC gain is exact at every phase.
 *
 * Declaring 48000 puts almost every game in the upsampling direction (the AI
 * divider tops out around 48009 Hz), which only produces images -- the safe
 * direction.  Downsampling would fold the top of the band back into the
 * audible range without a filter; the bands are what stops that on the rare
 * game above the declared rate.
 *
 * Width is fixed here rather than exposed: 32 taps costs ~3 M MACs/s at
 * 48 kHz stereo, which is noise even on the weakest board this builds for,
 * and a knob whose wrong setting is an audible regression is not worth it.
 *
 * The read position carries across calls and across frames: resetting it per
 * frame would quantise every frame's output to a whole number of samples and
 * drift against the DAC. */
#define RESAMP_TAPS   32
#define RESAMP_HALF   (RESAMP_TAPS / 2)   /* input frames of lookahead needed */
#define RESAMP_HIST   128u                /* power of two, > RESAMP_TAPS */
#define RESAMP_MASK   (RESAMP_HIST - 1u)

extern const int16_t* resample_hq_taps_fixed(int taps, uint32_t pitch,
                                             uint32_t pitch_accu);

static int16_t  resamp_hist[RESAMP_HIST][2];
static uint64_t resamp_written;          /* input frames ever fed */
static uint64_t resamp_rd;               /* integer part of the read position */
static double   resamp_frac;             /* its fraction, in [0, 1) */
static double   resamp_step = 1.0;       /* input frames per output frame */


static void reset_resampler(void)
{
   memset(resamp_hist, 0, sizeof(resamp_hist));
   resamp_written = 0;
   resamp_rd      = 0;
   resamp_frac    = 0.0;
}

void init_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   reset_resampler();
}

void deinit_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   reset_resampler();
}

/* Constant by construction: see AUDIO_OUTPUT_RATE.  The game's own rate never
 * reaches the frontend, it only sets the resampler ratio. */
double get_audio_sample_rate_libretro(void)
{
   return AUDIO_OUTPUT_RATE;
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

/* Drain the frame's audio.
 *
 * The AI hands over what the DAC clocked out during the frame just run,
 * and silence for the frames it had nothing queued for, so what is held
 * here is already one frame's worth paced by the hardware.  There is
 * nothing left to smooth: this used to carry a running mean of the
 * production rate and a multi-field cushion, both of which existed to
 * rebuild a steady rate from delivery that followed the game's polling
 * pattern.  Emitting what is held keeps the frontend's latency to the
 * frame's own audio instead of the cushion's several fields.
 *
 * Anything the frontend declines to take stays in the accumulator and
 * goes out with the next frame. */
void flush_audio_libretro(void)
{
   emit_frames(audio_acc_frames);
}

/* bits is ignored: the AI controller always produces 16-bit stereo.
 * frequency is the N64 game's chosen sample rate; it never reaches the
 * frontend, it only retunes the resampler. */
/* frequency is the rate rounded to whole Hz; clock and divider give the
 * same rate exactly, as the DAC derives it.  The divider is zero when the
 * game has not set DACRATE and the caller substituted a default. */
void set_audio_format_via_libretro(unsigned int frequency,
      unsigned int clock, unsigned int divider)
{
   double exact;

   if (frequency == 0)
      return;

   /* The DAC divides the clock by an integer and the quotient is not a
    * whole number of Hz: 48681812 / 2209 is 22037.94.  libretro takes the
    * rate as a double, so hand it the quotient rather than the floor -
    * declaring 22037 for a stream that arrives at 22037.94 leaves the
    * frontend resampling at a permanently wrong ratio, and a frontend
    * without dynamic rate control has nothing to absorb the drift with. */
   exact = (divider != 0) ? ((double)clock / (double)divider)
                          : (double)frequency;

   if (frequency == current_sample_rate && exact == current_sample_rate_exact)
      return;

   current_sample_rate       = frequency;
   current_sample_rate_exact = exact;

   /* Retune the resampler in place.  The frontend is told nothing: the rate it
    * was given at load time does not move, which is the whole point (see
    * AUDIO_OUTPUT_RATE).  The phase is deliberately left alone so a mid-game
    * rate change does not restart the interpolation on a stale frame. */
   resamp_step = exact / AUDIO_OUTPUT_RATE;
}

/* Append one output frame, draining first if the accumulator is full.  A
 * frontend under backpressure can decline the drain, in which case the frame
 * is dropped rather than run off the end of audio_acc. */
static void acc_push_frame(int16_t l, int16_t r)
{
   if (audio_acc_frames >= AUDIO_ACC_FRAMES)
   {
      emit_frames(audio_acc_frames);
      if (audio_acc_frames >= AUDIO_ACC_FRAMES)
         return;
   }

   audio_acc[audio_acc_frames * 2 + 0] = l;
   audio_acc[audio_acc_frames * 2 + 1] = r;
   ++audio_acc_frames;
}

static int16_t clamp_s16(int32_t v)
{
   if (v >  32767) return  32767;
   if (v < -32768) return -32768;
   return (int16_t)v;
}

/* Ring slot for a stream index, which is negative for the silence that
 * precedes the first frame.  RESAMP_HIST is a power of two, so the bias only
 * has to lift the index above zero; it does not change the slot. */
static const int16_t* resamp_at(int64_t idx)
{
   return resamp_hist[(size_t)(idx + (int64_t)RESAMP_HIST) & RESAMP_MASK];
}

/* Reconstruct one output frame at the current read position and append it. */
static void resample_emit(void)
{
   /* pitch is Q16.16 and picks the cutoff band; the accumulator is the phase,
    * of which the bank uses the top 8 bits. */
   const uint32_t pitch = (uint32_t)(resamp_step * 65536.0 + 0.5);
   const uint32_t accu  = (uint32_t)(resamp_frac * 65536.0) & 0xffffu;
   const int16_t *k     = resample_hq_taps_fixed(RESAMP_TAPS, pitch, accu);

   if (k != NULL)
   {
      /* The kernel is centred at tap (taps/2 - 1) + phase, so a window that
       * starts that far back reconstructs at the read position itself.  32
       * taps of 16-bit input against Q15 coefficients reach 2^35, past int32,
       * so the accumulator is 64-bit; rounding half-up keeps the error
       * zero-mean rather than biasing every sample toward zero. */
      const int64_t base = (int64_t)resamp_rd - (RESAMP_TAPS / 2 - 1);
      int64_t       al   = 0;
      int64_t       ar   = 0;
      int           t;

      for (t = 0; t < RESAMP_TAPS; ++t)
      {
         const int16_t *in = resamp_at(base + t);
         al += (int64_t)in[0] * k[t];
         ar += (int64_t)in[1] * k[t];
      }

      acc_push_frame(clamp_s16((int32_t)((al + 0x4000) >> 15)),
                     clamp_s16((int32_t)((ar + 0x4000) >> 15)));
   }
   else
   {
      /* The bank could not be allocated.  Linear rather than silence. */
      const int16_t *a = resamp_at((int64_t)resamp_rd);
      const int16_t *b = resamp_at((int64_t)resamp_rd + 1);

      acc_push_frame((int16_t)(a[0] + (b[0] - a[0]) * resamp_frac),
                     (int16_t)(a[1] + (b[1] - a[1]) * resamp_frac));
   }
}

/* Feed one input frame (host order) and emit every output frame the kernel now
 * has enough input to reconstruct.  That leaves RESAMP_HALF input frames of
 * latency -- about a third of a millisecond -- not drift: they come out with
 * the next frame's audio. */
static void resample_feed(int16_t l, int16_t r)
{
   int16_t *slot = resamp_hist[(size_t)resamp_written & RESAMP_MASK];

   slot[0] = l;
   slot[1] = r;
   ++resamp_written;

   while (resamp_rd + RESAMP_HALF < resamp_written)
   {
      resample_emit();

      resamp_frac += resamp_step;
      while (resamp_frac >= 1.0)
      {
         resamp_frac -= 1.0;
         ++resamp_rd;
      }
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
void push_audio_samples_via_libretro(const void *buffer, size_t size)
{
   const uint8_t *src;
   size_t         frames;
   size_t         off;

   if (buffer == NULL || size < 4)
      return;

   src    = (const uint8_t*)buffer;
   frames = size >> 2;                   /* 4 bytes per stereo frame */

   for (off = 0; off < frames; ++off)
   {
      const uint8_t *p = src + off * 4;
#ifdef MSB_FIRST
      const int16_t  l = (int16_t)((p[0] << 8) | p[1]);
      const int16_t  r = (int16_t)((p[2] << 8) | p[3]);
#else
      const int16_t  l = (int16_t)((p[3] << 8) | p[2]);
      const int16_t  r = (int16_t)((p[1] << 8) | p[0]);
#endif
      resample_feed(l, r);
   }
}

/* Append n stereo frames of silence.  Used when the AI has no transfer
 * in flight: the DAC keeps clocking through that, so the stream should
 * carry silence rather than stop. */
void push_audio_silence_via_libretro(size_t frames)
{
   /* Through the resampler like any other input: frames counts input frames at
    * the game's rate, and the phase has to advance by them or the stream drifts
    * against the DAC by however much silence the game let through.  It also
    * ramps out of the last real sample instead of cutting to zero. */
   while (frames-- != 0)
      resample_feed(0, 0);
}
