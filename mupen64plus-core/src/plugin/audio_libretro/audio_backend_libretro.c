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
static int      rate_is_from_game;       /* the game wrote AI_DACRATE itself */
static double   current_sample_rate_exact; /* the same rate, unrounded */


void init_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   rate_is_from_game   = 0;
}

void deinit_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
   rate_is_from_game   = 0;
}

/* Whether the rate came from the game rather than from a substituted default.
 * The AI controller passes divider == 0 until AI_DACRATE has been written, so
 * a nonzero current_sample_rate proves nothing: the first call carries the
 * 44100 Hz stand-in, and the game's own rate still follows.  The pre-roll in
 * retro_load_game() waits on this. */
int audio_sample_rate_settled_libretro(void)
{
   return rate_is_from_game;
}

double get_audio_sample_rate_libretro(void)
{
   /* 32040 Hz is by far the most common N64 game rate and a reasonable
    * fallback when retro_get_system_av_info is queried before the game
    * has had a chance to issue its first AI register write. */
   if (current_sample_rate_exact != 0.0)
      return current_sample_rate_exact;
   return current_sample_rate ? (double)current_sample_rate : 32040.0;
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
   /* Same as the input callback: audio is produced before the frontend has
    * anywhere to put it (see the pre-roll in retro_load_game()). */
   {
      extern int libretro_preroll_active(void);
      if (!audio_batch_cb || libretro_preroll_active())
         return;
   }
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
 * frequency is the N64 game's chosen sample rate; we forward it to the
 * frontend via RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO when it changes. */
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
   if (divider != 0)
      rate_is_from_game = 1;


   /* During the pre-roll there is nothing to announce to: the frontend has not
    * set anything up yet and reads the rate from retro_get_system_av_info()
    * once we return from retro_load_game(). */
   {
      extern int libretro_preroll_active(void);
      if (libretro_preroll_active())
         return;
   }

   if (environ_cb)
   {
      struct retro_system_av_info info;
      extern void retro_get_system_av_info(struct retro_system_av_info *info);
      retro_get_system_av_info(&info);
      info.timing.sample_rate = exact;
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
void push_audio_samples_via_libretro(const void *buffer, size_t size)
{
   const uint8_t *src;
   size_t         frames;
   size_t         off;

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

      audio_acc_frames += chunk;
      off              += chunk;

      if (audio_acc_frames == AUDIO_ACC_FRAMES && off < frames)
         emit_frames(audio_acc_frames);  /* emergency full drain */
   }
}

/* Append n stereo frames of silence.  Used when the AI has no transfer
 * in flight: the DAC keeps clocking through that, so the stream should
 * carry silence rather than stop. */
void push_audio_silence_via_libretro(size_t frames)
{
   while (frames != 0)
   {
      size_t chunk = frames;

      if (chunk > AUDIO_ACC_FRAMES - audio_acc_frames)
         chunk = AUDIO_ACC_FRAMES - audio_acc_frames;

      if (chunk == 0)
      {
         emit_frames(audio_acc_frames);
         continue;
      }

      memset(audio_acc + audio_acc_frames * 2, 0,
             chunk * 2 * sizeof(int16_t));
      audio_acc_frames += chunk;
      frames           -= chunk;
   }
}
