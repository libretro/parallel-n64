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
 *   frames per video frame. Doubling that absorbs the bursty mid-frame
 *   push schedule that some games use (multiple smaller AI DMAs per
 *   frame instead of one large one) plus any momentary phase drift
 *   between AI DMA completion scheduling and VI vertical-interrupt
 *   scheduling. 2048 stereo frames * 4 bytes = 8 KB total. */

#include "../../api/m64p_types.h"
#include <libretro.h>
#include "../../device/rcp/ai/ai_controller.h"
#include "../../main/main.h"
#include "../../main/device.h"
#include "../../main/rom.h"
#include "../core_plugin.h"
#include "../../device/rcp/ri/ri_controller.h"
#include "../../device/rcp/vi/vi_controller.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern retro_audio_sample_batch_t audio_batch_cb;
extern retro_environment_t        environ_cb;

#define AUDIO_ACC_FRAMES   2048u

static int16_t  audio_acc[AUDIO_ACC_FRAMES * 2];
static size_t   audio_acc_frames;        /* stereo frames currently held */
static unsigned current_sample_rate;     /* 0 until first set_audio_format */

void init_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
}

void deinit_audio_libretro(void)
{
   audio_acc_frames = 0;
   current_sample_rate = 0;
}

unsigned get_audio_sample_rate_libretro(void)
{
   /* 32040 Hz is by far the most common N64 game rate and a reasonable
    * fallback when retro_get_system_av_info is queried before the game
    * has had a chance to issue its first AI register write. */
   return current_sample_rate ? current_sample_rate : 32040u;
}

/* Drain the per-frame accumulator to the libretro frontend in a single
 * audio_batch_cb call (looped only to absorb partial-consume by the
 * frontend; in practice the first call accepts all frames). */
void flush_audio_libretro(void)
{
   const int16_t *out;
   size_t         remaining;

   if (audio_acc_frames == 0)
      return;

   out       = audio_acc;
   remaining = audio_acc_frames;

   while (remaining)
   {
      size_t ret = audio_batch_cb(out, remaining);
      if (ret == 0)
         break;                          /* frontend backpressure; drop the
                                          * remainder rather than stall the
                                          * emulator */
      remaining -= ret;
      out       += ret * 2;
   }

   audio_acc_frames = 0;
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
      flush_audio_libretro();

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
         flush_audio_libretro();
   }
}
