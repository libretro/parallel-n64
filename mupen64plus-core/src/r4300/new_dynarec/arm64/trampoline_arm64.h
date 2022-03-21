#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Currently these trampolines are used only for Apple ARM64 but they can be used on other platforms as well
void trampoline_init(void* base);
void* trampoline_jump_alloc_or_find(void* func);
void* trampoline_convert_trampoline_to_func(void* tramp);
void trampoline_commit(void);

#ifdef __cplusplus
}
#endif
