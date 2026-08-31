/* Pure worker-pool protocol cost: dispatches an empty task, so the time
 * per run is the dispatch plus the join and nothing else.
 *
 * Built by `make tools` alongside the other angrylion harnesses.
 *
 * Usage: angrylion_pool_overhead [WORKERS] [RUNS]
 */
#include "parallel_al.h"
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* the shared tool objects pull in n64video.o; these are its plugin-side
 * symbols, unused here */
struct n64video_config;
void msg_error(const char *e, ...) { va_list a; va_start(a, e); vfprintf(stderr, e, a); va_end(a); exit(1); }
void msg_warning(const char *e, ...) { (void)e; }
void msg_debug(const char *e, ...) { (void)e; }
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb; (void)w; (void)h; (void)p; (void)o; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }

static volatile unsigned sink;

static void task_empty(uint32_t worker_id)
{
    sink += worker_id;
}

int main(int argc, char **argv)
{
    unsigned workers = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    unsigned runs    = argc > 2 ? (unsigned)atoi(argv[2]) : 200000;
    retro_time_t t0, t1;
    unsigned i;

    parallel_alinit(workers);
    /* warm the pool: the first dispatch starts the threads */
    for (i = 0; i < 1000; i++)
        parallel_run(task_empty);

    t0 = cpu_features_get_time_usec();
    for (i = 0; i < runs; i++)
        parallel_run(task_empty);
    t1 = cpu_features_get_time_usec();

    printf("lanes=%2u : %.3f us per dispatch+join, %.3f ms per 600 (a frame of batches)\n",
           parallel_num_workers(),
           (double)(t1 - t0) / runs,
           (double)(t1 - t0) / runs * 600.0 / 1000.0);
    parallel_close();
    return 0;
}
