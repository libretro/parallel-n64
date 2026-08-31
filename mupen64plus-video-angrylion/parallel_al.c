/* Worker pool for the angrylion renderer.
 *
 * One thread per worker, worker 0 being the calling thread. Every
 * parallel_run() is a generation: the caller publishes the task, bumps
 * the generation counter, runs its own lane and waits for the other
 * workers to report in. Workers park on a condition variable between
 * generations and the caller parks on another while the workers finish,
 * so a pool that is not rendering costs nothing: every thread the
 * renderer is not using is a thread the emulator, the frontend or an SMT
 * sibling gets back.
 *
 * The two counters the threads hammer live on their own cache lines:
 * workers poll the generation while the completion counter is being
 * decremented, and neither should invalidate the other. The generation
 * bump is done under the work lock, which is what makes the "broadcast
 * only if someone is parked" test safe: a worker registers as a sleeper
 * and re-checks the generation under the same lock, so the caller either
 * sees the sleeper or the sleeper sees the bump.
 */

#include "parallel_al.h"

#include <stdlib.h>
#include <string.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>

#define PARALLEL_LINE_PAD 64

/* lane count the automatic setting stops at */
#define PARALLEL_AUTO_MAX_WORKERS 8

struct parallel_pool
{
    void (*task)(uint32_t);
    slock_t *work_lock;
    scond_t *work_cond;
    slock_t *done_lock;
    scond_t *done_cond;
    sthread_t *threads[PARALLEL_MAX_WORKERS];
    uint32_t worker_ids[PARALLEL_MAX_WORKERS];
    /* number of workers, worker 0 included; 0 while no pool is live */
    uint32_t num_workers;
    /* workers parked on work_cond, kept under work_lock */
    uint32_t sleepers;
    /* cleared under work_lock before the final generation bump */
    int accept_work;
    char pad0[PARALLEL_LINE_PAD];
    /* bumped once per parallel_run() and once at shutdown */
    retro_atomic_int_t generation;
    char pad1[PARALLEL_LINE_PAD];
    /* workers still busy in the current generation */
    retro_atomic_int_t remaining;
    char pad2[PARALLEL_LINE_PAD];
};

static struct parallel_pool pool;

/* Block until the generation moves past the one this worker last ran.
 * Returns the new generation. */
static int parallel_await_generation(struct parallel_pool *p, int seen)
{
    int gen;

    slock_lock(p->work_lock);
    p->sleepers++;
    for (;;)
    {
        gen = retro_atomic_load_acquire_int(&p->generation);
        if (gen != seen)
            break;
        scond_wait(p->work_cond, p->work_lock);
    }
    p->sleepers--;
    slock_unlock(p->work_lock);
    return gen;
}

static void parallel_worker(void *data)
{
    struct parallel_pool *p = &pool;
    uint32_t worker_id = *(const uint32_t*)data;
    int seen = 0;

    for (;;)
    {
        seen = parallel_await_generation(p, seen);
        if (!p->accept_work)
            break;

        p->task(worker_id);

        /* The empty lock/unlock pairs with the caller's check-then-wait
         * under done_lock, so a signal cannot slip between the two. */
        if (retro_atomic_fetch_sub_int(&p->remaining, 1) == 1)
        {
            slock_lock(p->done_lock);
            slock_unlock(p->done_lock);
            scond_signal(p->done_cond);
        }
    }
}

static void parallel_wait_completion(struct parallel_pool *p)
{
    if (retro_atomic_load_acquire_int(&p->remaining) == 0)
        return;

    slock_lock(p->done_lock);
    while (retro_atomic_load_acquire_int(&p->remaining) != 0)
        scond_wait(p->done_cond, p->done_lock);
    slock_unlock(p->done_lock);
}

/* Publish the next generation. A worker still on its way to park re-checks
 * the counter under the lock, so only registered sleepers need the
 * broadcast. */
static void parallel_bump_generation(struct parallel_pool *p)
{
    int gen;
    uint32_t sleepers;

    slock_lock(p->work_lock);
    gen = retro_atomic_load_acquire_int(&p->generation);
    retro_atomic_store_release_int(&p->generation, gen + 1);
    sleepers = p->sleepers;
    slock_unlock(p->work_lock);

    if (sleepers)
        scond_broadcast(p->work_cond);
}

void parallel_alinit(uint32_t num)
{
    struct parallel_pool *p = &pool;
    uint32_t i;

    if (p->num_workers)
        parallel_close();

    /* 0 selects the automatic count: the host's physical cores, at most
     * PARALLEL_AUTO_MAX_WORKERS of them. Every lane replays the whole
     * command stream, so past that count the replicated per-lane work
     * outweighs what the extra lanes take off the spans, and an SMT
     * sibling is a worse lane than none; ANGRYLION_NUM_THREADS still
     * overrides the choice outright. */
    if (num == 0)
    {
        const char *env = getenv("ANGRYLION_NUM_THREADS");
        if (env)
            num = (uint32_t)atoi(env);
        else
        {
            num = cpu_features_get_core_amount_physical();
            if (num > PARALLEL_AUTO_MAX_WORKERS)
                num = PARALLEL_AUTO_MAX_WORKERS;
        }
    }
    if (num == 0)
        num = 1;
    if (num > PARALLEL_MAX_WORKERS)
        num = PARALLEL_MAX_WORKERS;
#if !defined(RETRO_ATOMIC_LOCK_FREE)
    /* the counters need real atomics; without them the renderer stays
     * on the calling thread */
    num = 1;
#endif

    memset(p, 0, sizeof(*p));
    retro_atomic_int_init(&p->generation, 0);
    retro_atomic_int_init(&p->remaining, 0);
    p->num_workers = 1;
    if (num == 1)
        return;

    p->work_lock = slock_new();
    p->work_cond = scond_new();
    p->done_lock = slock_new();
    p->done_cond = scond_new();
    if (!p->work_lock || !p->work_cond || !p->done_lock || !p->done_cond)
    {
        parallel_close();
        p->num_workers = 1;
        return;
    }
    p->accept_work = 1;

    /* worker_ids is what the threads read their id from, so it has to
     * be final before the first thread starts */
    for (i = 0; i < num; i++)
        p->worker_ids[i] = i;

    /* a thread that fails to start caps the pool at the workers that
     * did: ids are dense, so the lanes stay consistent */
    for (i = 1; i < num; i++)
    {
        p->threads[i] = sthread_create(parallel_worker, &p->worker_ids[i]);
        if (!p->threads[i])
            break;
        p->num_workers = i + 1;
    }
}

void parallel_run(void task(uint32_t))
{
    struct parallel_pool *p = &pool;

    /* single-worker pools and no pool at all have nobody to hand the
     * work to */
    if (p->num_workers <= 1 || !p->accept_work)
    {
        task(0);
        return;
    }

    p->task = task;
    retro_atomic_store_release_int(&p->remaining, (int)(p->num_workers - 1));
    parallel_bump_generation(p);

    task(0);
    parallel_wait_completion(p);
}

uint32_t parallel_num_workers(void)
{
    return pool.num_workers ? pool.num_workers : 1;
}

void parallel_close(void)
{
    struct parallel_pool *p = &pool;
    uint32_t i;

    if (p->num_workers > 1)
    {
        slock_lock(p->work_lock);
        p->accept_work = 0;
        slock_unlock(p->work_lock);
        parallel_bump_generation(p);

        for (i = 1; i < p->num_workers; i++)
            sthread_join(p->threads[i]);
    }

    if (p->work_cond)
        scond_free(p->work_cond);
    if (p->work_lock)
        slock_free(p->work_lock);
    if (p->done_cond)
        scond_free(p->done_cond);
    if (p->done_lock)
        slock_free(p->done_lock);

    memset(p, 0, sizeof(*p));
}
