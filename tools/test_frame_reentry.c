/* Per-frame stop latches across the host alias configurations.
 *
 * retro_return (libretro.c) ends a slice by setting mupencorestop and the
 * active backend's stop (r4300_stop), and returns early while mupencorestop
 * is set. main_run clears both with R4300_CLEAR_FRAME_STOP before the next
 * slice. Where mupencorestop and r4300_stop name different fields, a clear
 * that misses mupencorestop leaves the gate latched after the first frame,
 * so no later slice ever ends (blank screen).
 *
 * The file is compiled once per alias configuration with FRAME_REENTRY_CASE
 * set (the Makefile passes the host defines that select it), and once
 * without it for main().
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FR_PASTE2(a, b) a##b
#define FR_PASTE(a, b) FR_PASTE2(a, b)
#define FR_STR2(x) #x
#define FR_STR(x) FR_STR2(x)

#ifdef FRAME_REENTRY_CASE

/* Each configuration has its own struct layout and accessor. */
#define r4300_stop FR_PASTE(fr_r4300_stop_, FRAME_REENTRY_CASE)
#define g_dev      FR_PASTE(fr_dev_, FRAME_REENTRY_CASE)

#include "device/device.h"
#include "device/r4300/r4300.h"

static struct device g_dev;
static int use_hot_state;

/* Same choice as r4300_core.c: ari64 uses the hot state, the interpreters
 * and Hacktarux use the base struct. */
int *r4300_stop(struct r4300_core *r4300)
{
   return use_hot_state ? &r4300->new_dynarec_hot_state.stop : &r4300->stop;
}

/* retro_return's VI path: returns 1 when it ended the slice. */
static int vi_return(void)
{
   if (mupencorestop)
      return 0;
   mupencorestop = 1;
   *r4300_stop(&g_dev.r4300) = 1;
   return 1;
}

int FR_PASTE(frame_reentry_, FRAME_REENTRY_CASE)(void)
{
   int failures = 0;
   int mode, frame;

   /* ari64 is the only hot-state backend; without it only the base
    * struct is ever active. */
#ifdef NEW_DYNAREC
   for (mode = 0; mode < 2; mode++)
#else
   for (mode = 0; mode < 1; mode++)
#endif
   {
      use_hot_state = mode;
      memset(&g_dev.r4300, 0, sizeof(g_dev.r4300));
      for (frame = 0; frame < 3; frame++)
      {
         R4300_CLEAR_FRAME_STOP(&g_dev.r4300);
         if (mupencorestop || *r4300_stop(&g_dev.r4300))
         {
            printf("%s: %s backend, frame %d: stop latch survived the clear\n",
                  FR_STR(FRAME_REENTRY_CASE),
                  mode ? "hot-state" : "base", frame);
            failures++;
         }
         if (!vi_return())
         {
            printf("%s: %s backend, frame %d: VI return did not end the slice\n",
                  FR_STR(FRAME_REENTRY_CASE),
                  mode ? "hot-state" : "base", frame);
            failures++;
         }
      }
   }
   return failures;
}

#else

int frame_reentry_base(void);
int frame_reentry_arm32(void);
int frame_reentry_x64(void);
int frame_reentry_arm64_interp(void);
int frame_reentry_arm64_dynarec(void);

int main(void)
{
   int failures = 0;
   failures += frame_reentry_base();
   failures += frame_reentry_arm32();
   failures += frame_reentry_x64();
   failures += frame_reentry_arm64_interp();
   failures += frame_reentry_arm64_dynarec();
   if (failures)
   {
      printf("frame re-entry: %d failure(s)\n", failures);
      return 1;
   }
   printf("frame re-entry: all alias configurations passed\n");
   return 0;
}

#endif
