/* Compile with C++11, -Ilibretro-common/include and link the C objects
 * rthreads.c and retro_eventcount.c with -pthread. */
#include <cassert>
#include <cstdio>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

static bool failThread;
static unsigned failEvent, eventCalls;
static bool initEvent(retro_eventcount_t *event)
{
   return ++eventCalls == failEvent ? false : retro_eventcount_init(event);
}
static sthread_t *createWorker(void (*entry)(void *), void *data)
{
   return failThread ? nullptr : sthread_create(entry, data);
}
#define sthread_create createWorker
#define retro_eventcount_init initEvent
#include "../mupen64plus-video-gliden64/src/RSPCommandThread.h"
#undef sthread_create
#undef retro_eventcount_init

class Command : public APICommand {
public:
   Command(unsigned &output, unsigned input, bool keepRunning = true)
      : output(output), input(input), keepRunning(keepRunning) {}
   bool run() override {
      // Output is deliberately non-atomic: completion must publish it.
      output = input;
      return keepRunning;
   }
private:
   unsigned &output;
   unsigned input;
   bool keepRunning;
};

int main()
{
   RSPCommandThread thread;
   unsigned output = 0;
#if defined(RETRO_ATOMIC_LOCK_FREE)
   for (unsigned cycle = 0; cycle < 16; ++cycle) {
      assert(thread.start());
      for (unsigned i = 1; i <= 10000; ++i) {
         Command command(output, i);
         thread.call(command);
         assert(output == i);
         if (i % 1000 == 0)
            sthread_yield();
      }
      Command stop(output, 0, false);
      thread.call(stop);
      thread.join();
      assert(output == 0);
   }
   failThread = true;
   assert(!thread.start());
   Command inlineCommand(output, 42);
   thread.call(inlineCommand);
   assert(output == 42);
   thread.join();
   failThread = false;
   for (failEvent = 1; failEvent <= 2; ++failEvent) {
      eventCalls = 0;
      assert(!thread.start());
      thread.call(inlineCommand);
      assert(output == 42);
      thread.join();
   }
   puts("160000 commands, reopen, shutdown, thread/event allocation failure passed");
#else
   assert(!thread.start());
   Command inlineCommand(output, 42);
   thread.call(inlineCommand);
   assert(output == 42);
   thread.join();
   puts("Non-lock-free target falls back to inline execution");
#endif
}
