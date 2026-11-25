#ifndef PARALLEL_SWITCH2_JIT_H
#define PARALLEL_SWITCH2_JIT_H

#include <stddef.h>

int switch2_jit_init(size_t size);
void switch2_jit_close(void);
void *switch2_jit_rw(void);
void *switch2_jit_rx(void);
void switch2_jit_publish(const void *start, const void *end);

#endif
