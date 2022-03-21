#include "apple_jit_protect.h"
#include "trampoline_arm64.h"

#ifdef __APPLE__
static __thread int Count = 0;

#include <pthread.h>

void apple_jit_wx_unprotect_enter()
{
    Count++;
    if (1 == Count)
    {
        pthread_jit_write_protect_np(0);
    }
}

void apple_jit_wx_unprotect_exit()
{
    Count--;
    if (0 == Count)
    {
        trampoline_commit();
        pthread_jit_write_protect_np(1);
    }
}
#endif
