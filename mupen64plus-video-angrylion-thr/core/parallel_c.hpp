#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void parallel_alinit(uint32_t num);
void parallel_run(void task(void));
uint32_t parallel_worker_id(void);
uint32_t parallel_worker_num(void);
void parallel_close(void);

#ifdef __cplusplus
}
#endif
