/* Standalone self-check for the libretro audio backend's resampler
 * (fixed declared rate, steady-state ratio, DC gain, silence phase, drift).
 *
 * Build & run from the repo root:
 *   cc -Imupen64plus-core/src -Imupen64plus-core/src/api -Ilibretro-common/include \
 *      -Imupen64plus-core/custom -Imupen64plus-core/subprojects/md5 -Iinclude \
 *      -o /tmp/test_ar tools/test_audio_resample.c \
 *      mupen64plus-core/src/plugin/audio_libretro/audio_backend_libretro.c \
 *      mupen64plus-rsp-hle/src/resample_hq.c -lm \
 *      && /tmp/test_ar
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

void   init_audio_libretro(void);
void   deinit_audio_libretro(void);
void   flush_audio_libretro(void);
double get_audio_sample_rate_libretro(void);
void   set_audio_format_via_libretro(unsigned frequency, unsigned clock, unsigned divider);
void   push_audio_samples_via_libretro(const void *buffer, size_t size);
void   push_audio_silence_via_libretro(size_t frames);

/* --- frontend stubs -------------------------------------------------- */
static size_t out_frames;
static int    out_expect;      /* every output sample must equal this */
static int    out_mismatch;

static size_t batch_cb(const int16_t *data, size_t frames)
{
   size_t i;
   for (i = 0; i < frames; ++i)
      if (data[i*2] != out_expect || data[i*2+1] != out_expect)
         ++out_mismatch;
   out_frames += frames;
   return frames;
}

retro_audio_sample_batch_t audio_batch_cb = batch_cb;
retro_environment_t        environ_cb     = NULL;

/* --- helpers ---------------------------------------------------------- */
/* One N64 AI stereo frame as the core sees it in RDRAM: big-endian s16
 * pairs, byte-swizzled on a little-endian host (S8 = 3). */
static void put_frame(uint8_t *p, int16_t l, int16_t r)
{
#ifdef MSB_FIRST
   p[0] = (uint8_t)(l >> 8); p[1] = (uint8_t)l;
   p[2] = (uint8_t)(r >> 8); p[3] = (uint8_t)r;
#else
   p[3] = (uint8_t)(l >> 8); p[2] = (uint8_t)l;
   p[1] = (uint8_t)(r >> 8); p[0] = (uint8_t)r;
#endif
}

static void feed_dc(unsigned frames, int16_t value)
{
   uint8_t *buf = malloc((size_t)frames * 4);
   unsigned i;
   assert(buf);
   for (i = 0; i < frames; ++i)
      put_frame(buf + i * 4, value, value);
   push_audio_samples_via_libretro(buf, (size_t)frames * 4);
   free(buf);
}

/* Start a run and get past the kernel's start-up window, so what follows is
 * steady state and not the fade-in out of the silence before frame zero. */
static void warm_up(unsigned clock, unsigned divider, int16_t dc)
{
   init_audio_libretro();
   set_audio_format_via_libretro(1, clock, divider);
   out_expect = dc; out_mismatch = 0;
   feed_dc(4096, dc);
   flush_audio_libretro();
   out_frames = 0; out_mismatch = 0;
}

int main(void)
{
   /* The declared rate is fixed: that is the whole point, moving it is what
    * costs a CRT setup its switchres mode. */
   init_audio_libretro();
   assert(get_audio_sample_rate_libretro() == 48000.0);
   set_audio_format_via_libretro(22050, 48681812, 2208);
   assert(get_audio_sample_rate_libretro() == 48000.0);
   set_audio_format_via_libretro(32040, 48681812, 1519);
   assert(get_audio_sample_rate_libretro() == 48000.0);

   /* Steady-state throughput: N input frames owe N * 48000/in_rate output
    * frames, whatever the start-up latency was. */
   {
      const double in_rate = 48681812.0 / 1519.0;   /* 32048.592 Hz */
      const double expect  = 32040.0 * (48000.0 / in_rate);
      long         diff;

      warm_up(48681812, 1519, 1000);
      feed_dc(32040, 1000);
      flush_audio_libretro();

      diff = (long)out_frames - (long)(expect + 0.5);
      if (diff < 0) diff = -diff;
      printf("  32048.6 -> 48000 : %zu frames out, attendu ~%.0f (ecart %ld)\n",
             out_frames, expect, diff);
      assert(diff <= 2);

      /* The tap rows are nudged to sum to exactly 1.0 in Q15, so a DC input
       * must come out at its own value at every phase -- no wobble. */
      printf("  gain DC exact    : %d echantillons hors valeur\n", out_mismatch);
      assert(out_mismatch == 0);
   }

   /* Above the declared rate the ratio exceeds unity, where the bank pulls the
    * cutoff down.  Check the rate is still honoured. */
   {
      warm_up(96000, 1, 0);
      feed_dc(4800, 0);
      flush_audio_libretro();
      printf("  96000 -> 48000   : %zu frames out, attendu ~2400\n", out_frames);
      assert(out_frames > 2398 && out_frames < 2402);
      assert(out_mismatch == 0);
   }

   /* Silence advances the phase like any other input: 16000 silent input
    * frames owe exactly what 16000 loud ones owe. */
   {
      size_t loud, quiet;

      warm_up(48681812, 1519, 0);
      feed_dc(16000, 0);
      flush_audio_libretro();
      loud = out_frames;

      warm_up(48681812, 1519, 0);
      push_audio_silence_via_libretro(16000);
      flush_audio_libretro();
      quiet = out_frames;

      printf("  phase du silence : %zu vs %zu frames\n", loud, quiet);
      assert(loud == quiet);
   }

   /* The position carries across calls, so ten pushes owe what one push of
    * the total owes.  This is the check that fails if the phase is ever
    * reset per frame. */
   {
      size_t one, many;
      int    i;

      warm_up(48681812, 1519, 0);
      feed_dc(8000, 0);
      flush_audio_libretro();
      one = out_frames;

      warm_up(48681812, 1519, 0);
      for (i = 0; i < 10; ++i)
         feed_dc(800, 0);
      flush_audio_libretro();
      many = out_frames;

      printf("  pas de derive    : %zu (1x8000) vs %zu (10x800)\n", one, many);
      assert(one == many);
   }

   deinit_audio_libretro();
   printf("audio resampler: all checks passed\n");
   return 0;
}
