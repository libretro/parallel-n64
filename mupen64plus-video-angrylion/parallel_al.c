#include "parallel_al.h"

#include <stdlib.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>

/* Worker pool for the angrylion software RDP.
 *
 * Fork-join barrier: parallel_run() broadcasts a single task to every
 * worker (worker 0 runs on the calling thread), then blocks until all
 * background workers have finished their slice.
 *
 * This was previously built on C++11 std::thread / std::mutex /
 * std::condition_variable and a std::atomic<uint64_t> done-bitmask updated
 * with fetch_or.  fetch_or that returns the previous value has no single
 * instruction on x86 (it lowers to a lock cmpxchg retry loop, and to a
 * 64-bit lock cmpxchg8b loop on 32-bit), and retro_atomic deliberately
 * offers no bitwise/64-bit RMW.  It is reworked here onto libretro-common's
 * rthreads + retro_atomic with two plain machine-word counters:
 *
 *   m_generation  - bumped once per task; workers wake when it changes
 *                   (sense/generation dispatch, replaces "clear the bit").
 *   m_done_count  - incremented once per background worker as it finishes;
 *                   the caller waits for it to reach m_workers_minus1
 *                   (completion counter, replaces "all bits set").
 *
 * Both are retro_atomic_int_t (32-bit), so the hot completion RMW is a
 * single lock xadd on x86 / a single ldadd on AArch64 LSE, with no CAS
 * loop and no 64-bit-on-32-bit penalty.  run() is synchronous (start then
 * wait), so the generation only ever advances by one between a worker's
 * tasks and every worker processes every generation exactly once.
 *
 * Memory ordering: the per-task release of m_generation (acq_rel fetch_add)
 * publishes both the m_done_count reset and the m_task pointer to any
 * worker that observes the new generation with its acquire load; each
 * worker's release of m_done_count (acq_rel fetch_add) publishes its
 * framebuffer writes to the caller's acquire load in parallel_wait().
 */

typedef struct worker_arg
{
   uint32_t worker_id;
} worker_arg_t;

static uint32_t            m_num_workers;     /* total, incl. worker 0 (caller) */
static int                 m_workers_minus1;  /* background workers / completion target */
static sthread_t         **m_workers;
static worker_arg_t       *m_worker_args;

static slock_t            *m_lock;
static scond_t            *m_cond_work;       /* wake workers for a new task    */
static scond_t            *m_cond_done;       /* wake caller when all finished  */

static retro_atomic_int_t  m_generation;      /* bumped once per task           */
static retro_atomic_int_t  m_done_count;      /* background workers finished     */
static retro_atomic_int_t  m_accept_work;     /* 0 => workers should exit        */

static void              (*m_task)(uint32_t);

static void parallel_wait(void)
{
   /* fast path: every background worker already finished */
   if (retro_atomic_load_acquire_int(&m_done_count) == m_workers_minus1)
      return;

   slock_lock(m_lock);
   while (retro_atomic_load_acquire_int(&m_done_count) != m_workers_minus1)
      scond_wait(m_cond_done, m_lock);
   slock_unlock(m_lock);
}

static void parallel_start(void)
{
   /* reset the completion counter, then publish a new generation.  The
    * fetch_add is a release operation, so the counter reset and the
    * preceding m_task write are visible to any worker that observes the
    * new generation with its acquire load. */
   slock_lock(m_lock);
   retro_atomic_store_release_int(&m_done_count, 0);
   retro_atomic_fetch_add_int(&m_generation, 1);
   scond_broadcast(m_cond_work);
   slock_unlock(m_lock);
}

static void worker_thread(void *data)
{
   worker_arg_t  *arg       = (worker_arg_t*)data;
   const uint32_t worker_id = arg->worker_id;
   int            last_gen  = 0;   /* matches the initial m_generation == 0 */

   for (;;)
   {
      /* wait for a new task generation (or for shutdown) */
      slock_lock(m_lock);
      while (   retro_atomic_load_acquire_int(&m_generation)  == last_gen
             && retro_atomic_load_acquire_int(&m_accept_work) != 0)
         scond_wait(m_cond_work, m_lock);
      last_gen = retro_atomic_load_acquire_int(&m_generation);
      slock_unlock(m_lock);

      if (retro_atomic_load_acquire_int(&m_accept_work) == 0)
         break;

      /* run this worker's slice */
      m_task(worker_id);

      /* mark done; the worker that completes the set wakes the caller.
       * The empty lock/unlock closes the lost-wakeup window against
       * parallel_wait()'s predicate check before its scond_wait. */
      if (retro_atomic_fetch_add_int(&m_done_count, 1) + 1 == m_workers_minus1)
      {
         slock_lock(m_lock);
         slock_unlock(m_lock);
         scond_signal(m_cond_done);
      }
   }
}

void parallel_alinit(uint32_t num)
{
   int i;

   /* auto-select the worker count from the number of cores */
   if (num == 0)
   {
      const char *env = getenv("ANGRYLION_NUM_THREADS");
      if (env)
         num = (uint32_t)strtoul(env, NULL, 0);
      else
         num = cpu_features_get_core_amount();
   }
   if (num == 0)
      num = 1;
   if (num > PARALLEL_MAX_WORKERS)
      num = PARALLEL_MAX_WORKERS;

   m_num_workers    = num;
   m_workers_minus1 = (int)num - 1;
   m_workers        = NULL;
   m_worker_args    = NULL;
   m_lock           = NULL;
   m_cond_work      = NULL;
   m_cond_done      = NULL;
   m_task           = NULL;

   retro_atomic_store_release_int(&m_generation,  0);
   retro_atomic_store_release_int(&m_done_count,  0);
   retro_atomic_store_release_int(&m_accept_work, 1);

   /* single-worker mode: worker 0 runs on the caller, no threads needed */
   if (m_workers_minus1 <= 0)
   {
      m_workers_minus1 = 0;
      return;
   }

   m_lock        = slock_new();
   m_cond_work   = scond_new();
   m_cond_done   = scond_new();
   m_workers     = (sthread_t**)  calloc((size_t)m_workers_minus1, sizeof(sthread_t*));
   m_worker_args = (worker_arg_t*)calloc((size_t)m_workers_minus1, sizeof(worker_arg_t));

   for (i = 0; i < m_workers_minus1; i++)
   {
      m_worker_args[i].worker_id = (uint32_t)(i + 1);
      m_workers[i] = sthread_create(worker_thread, &m_worker_args[i]);
   }
}

void parallel_run(void task(uint32_t))
{
   /* don't accept work once the pool is shutting down */
   if (retro_atomic_load_acquire_int(&m_accept_work) == 0)
      return;

   /* single-worker mode has no one to synchronise with: worker 0 is the
    * calling thread, so the whole protocol reduces to a plain call */
   if (m_workers_minus1 <= 0)
   {
      task(0);
      return;
   }

   m_task = task;
   parallel_start();   /* publish task, wake workers */
   task(0);            /* run worker 0 on the calling thread */
   parallel_wait();    /* wait for all background workers */
}

uint32_t parallel_num_workers(void)
{
   return m_num_workers;
}

void parallel_close(void)
{
   int i;

   if (m_workers_minus1 > 0)
   {
      /* let any in-flight task finish, then ask the workers to exit */
      parallel_wait();

      slock_lock(m_lock);
      retro_atomic_store_release_int(&m_accept_work, 0);
      scond_broadcast(m_cond_work);
      slock_unlock(m_lock);

      for (i = 0; i < m_workers_minus1; i++)
         if (m_workers[i])
            sthread_join(m_workers[i]);

      free(m_workers);
      free(m_worker_args);
      scond_free(m_cond_done);
      scond_free(m_cond_work);
      slock_free(m_lock);
   }

   m_workers        = NULL;
   m_worker_args    = NULL;
   m_lock           = NULL;
   m_cond_work      = NULL;
   m_cond_done      = NULL;
   m_num_workers    = 0;
   m_workers_minus1 = 0;
}
