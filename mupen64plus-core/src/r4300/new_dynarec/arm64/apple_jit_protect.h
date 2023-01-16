#pragma once

#ifdef __APPLE__
// Allow writing to WX pages, exec is blocked
// Only necessary on macOS ARM because Apple restrictions on MAP_JIT pages
void apple_jit_wx_unprotect_enter();
void apple_jit_wx_unprotect_exit();
#else
static inline void apple_jit_wx_unprotect_enter() {}
static inline void apple_jit_wx_unprotect_exit() {}
#endif
