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

// Initializes trampolines to be generated from 'base'
void trampoline_init(void* base);

// Hint trampolines where data is. Greatly helps with data fragmentation.
void trampoline_add_data_hint(void* ptr, size_t size);

// Create jump trampolines that are needed for 'jump_vaddr'
trampolines_reg_jump_t trampoline_alloc_reg_jump(void* jump_vaddr_fn);

// Returns a trampoline that allows jumping to a given 'func' near 'base'
void* trampoline_jump_alloc_or_find(void* func);

typedef struct
{
    void** base;
    // Base is guaranteed to be reachable within 16MB so pair of 'add' call
    int off;
} trampoline_data_alloc_or_find_return_t;

// Returns a pointer that can be deferenced to get 'data' near 'base'
trampoline_data_alloc_or_find_return_t trampoline_data_alloc_or_find(void* pdata);

// Given jump 'tramp' returns original 'func'
// 'tramp' does not have to be returned from 'trampoline_jump_alloc_or_find' in which case 'tramp' is returned
void* trampoline_convert_trampoline_to_func(void* tramp);

// Given data 'tramp' returns original 'data'
// 'tramp' does not have to be returned from 'trampoline_data_alloc_or_find' in which case 'tramp' is returned
void* trampoline_convert_trampoline_to_data(void** tramp);

// Invalidates code cache for the trampolines
void trampoline_commit(void);

#ifdef __cplusplus
}
#endif
