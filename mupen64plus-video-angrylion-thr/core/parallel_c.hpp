#pragma once

#include <thread>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "core.h"

#ifdef __cplusplus
}
#endif

class Parallel;

class Worker
{
public:
   Worker(uint32_t id, Parallel *parallel);
   std::thread m_thread;
   uint32_t m_worker_id;
   struct rdp_globals globals;
};

extern thread_local Worker* parallel_worker;

void parallel_alinit(uint32_t num);
void parallel_run(void task(void));
uint32_t parallel_worker_num(void);
void parallel_close(void);
