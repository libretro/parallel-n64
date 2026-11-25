/* The emulator owns staging; RO owns the writable executable alias.
 * Publish only changed bytes before the CPU enters them. No mapping moves
 * or grows between content load and unload. */
#include <kaihen.h>
#include <stdlib.h>
#include <string.h>
#include "switch2_jit.h"

static Jit code;
static int ready;

int switch2_jit_init(size_t size)
{
   Result rc;
   if (ready)
      return code.size == size;
   memset(&code, 0, sizeof(code));
   rc = jitCreate(&code, size);
   if (R_FAILED(rc))
      return 0;
   /* Plain permission toggling cannot patch a block while its caller is
    * executing. Require CodeMemory or the persistent RO alias backend. */
   if (code.type != JitType_CodeMemory && code.rw_addr == code.src_addr)
   {
      jitClose(&code);
      return 0;
   }
   ready = 1;
   return 1;
}

void switch2_jit_close(void)
{
   if (ready && R_SUCCEEDED(jitClose(&code)))
   {
      ready = 0;
      memset(&code, 0, sizeof(code));
   }
}

void *switch2_jit_rw(void) { return ready ? jitGetRwAddr(&code) : NULL; }
void *switch2_jit_rx(void) { return ready ? jitGetRxAddr(&code) : NULL; }

void switch2_jit_publish(const void *start, const void *end)
{
   uintptr_t first = (uintptr_t)start;
   uintptr_t last = (uintptr_t)end;
   uintptr_t rx = (uintptr_t)code.rx_addr;
   size_t offset, size;
   if (!ready || first < rx || last < first || last - rx > code.size)
      abort();
   offset = (size_t)(first - rx);
   size = (size_t)(last - first);
   if (!size)
      return;
   if (code.type == JitType_CodeMemory)
      armDCacheFlush((char *)code.rw_addr + offset, size);
   else if (R_FAILED(kaihenPublishJitAlias(&code, offset,
               (char *)code.rw_addr + offset, size)))
      abort(); /* Never execute stale instructions after failed publication. */
   /* Required producer/consumer dependency: RO has copied and cleaned the
    * bytes; this thread must invalidate RX before executing the new code. */
   armICacheInvalidate((void *)start, size);
}
