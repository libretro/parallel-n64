#pragma once

#include <stdint.h>
#ifdef __APPLE__
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uintptr_t indirect_jump_indexed;
    uintptr_t indirect_jump;
    uintptr_t jump_vaddr_reg[29];
} trampolines_reg_jump_t;

// Currently these trampolines are used only for Apple ARM64 but they can be used on other platforms as well

void trampoline_init(void* base);
trampolines_reg_jump_t trampoline_alloc_reg_jump(void* jump_vaddr_fn);
void* trampoline_jump_alloc_or_find(void* func);
void* trampoline_convert_trampoline_to_func(void* tramp);
void trampoline_commit(void);

#ifdef __cplusplus
}
#endif
