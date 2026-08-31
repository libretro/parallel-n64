/* Copyright  (C) 2010-2020 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (rthreads.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef __unix__
#ifndef __sun__
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309
#endif
#endif
#endif

#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <rthreads/rthreads.h>

/* with RETRO_WIN32_USE_PTHREADS, pthreads can be used even on win32.
 * Maybe only supported in MSVC>=2005 */

#if defined(_WIN32) && !defined(RETRO_WIN32_USE_PTHREADS)
#define USE_WIN32_THREADS
#ifdef _XBOX
#include <xtl.h>
#else
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500 /*_WIN32_WINNT_WIN2K */
#endif
#include <windows.h>
#endif
#elif defined(GEKKO)
#include <ogc/lwp_watchdog.h>
#include "gx_pthread.h"
#elif defined(_3DS)
#include "ctr_pthread.h"
#else
#include <pthread.h>
#include <time.h>
#endif

#if defined(VITA) || defined(BSD) || defined(ORBIS) || defined(_3DS) || defined(PSP)
#include <sys/time.h>
#endif

/* sthread_setname */
#if defined(__linux__) && !defined(USE_WIN32_THREADS) && !defined(GEKKO) && !defined(_3DS)
#include <sys/prctl.h>
#endif

#if defined(__ANDROID__)
#include <sys/resource.h>
#include <unistd.h>
#endif

#if (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) \
      || defined(__NetBSD__) || defined(__OpenBSD__)) \
      && !defined(USE_WIN32_THREADS) && !defined(GEKKO) && !defined(_3DS) \
      && !defined(__ANDROID__)
#include <sched.h>
#define RTHREADS_HAVE_SCHEDPARAM 1
#endif

#if defined(PS2)
#include <ps2sdkapi.h>
#endif

#if defined(__MACH__) && defined(__APPLE__)
#include <mach/clock.h>
#include <mach/mach.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h> /* MAC_OS_X_VERSION_MIN_REQUIRED (since 10.2) */
/* The pthread QoS override API (pthread_override_qos_class_start_np, used by
 * sthread_priority_override_*) exists only on macOS 10.10+ / iOS 8.0+, and
 * RetroArch still ships deployment targets below that (OS X 10.5, iOS 6)
 * where the symbol is absent in both SDK and runtime. Gate on the
 * deployment-target version. TARGET_OS_* keeps the macOS check from firing
 * on iOS; numeric literals are used because the MAC_OS_X_VERSION_10_10 /
 * __IPHONE_8_0 constants are undefined on old SDKs (and would expand to 0). */
#if (TARGET_OS_OSX && defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 101000) || \
    (TARGET_OS_IPHONE && defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED >= 80000)
#define RTHREADS_HAVE_QOS_OVERRIDE 1
#include <pthread/qos.h>
#endif
/* clock_gettime() arrived in macOS 10.12 / iOS 10.0 / tvOS 10.0. Below that
 * the Mach clock service is the only option; see the note on
 * rthreads_calendar_clock below for why it must not be re-acquired per call.
 * Same literal-constant rationale as above. */
#if (TARGET_OS_OSX && defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 101200) || \
    (TARGET_OS_IPHONE && defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED >= 100000)
#define RTHREADS_HAVE_CLOCK_GETTIME 1
#endif
#endif

#if defined(__MACH__) && defined(__APPLE__) && !defined(RTHREADS_HAVE_CLOCK_GETTIME)
/* Acquired once for the lifetime of the process.
 *
 * The previous code called host_get_clock_service(mach_host_self(), ...)
 * followed by mach_port_deallocate() on every scond_wait_timeout(). That is
 * wrong twice over:
 *
 *   - the send right returned by mach_host_self() was never deallocated, so
 *     every call leaked a user reference on the host port;
 *   - host_get_clock_service() allocates a fresh port *name* in the task IPC
 *     space which is then immediately freed, so a hot caller (the CoreAudio
 *     write path, the task queue worker, autosave) churns the task's port
 *     name space continuously for the whole session.
 *
 * Neither is acceptable in a function called at audio-buffer rate. */
static clock_serv_t   rthreads_calendar_clock;
static pthread_once_t rthreads_calendar_clock_once = PTHREAD_ONCE_INIT;

static void rthreads_calendar_clock_init(void)
{
   mach_port_t host = mach_host_self();
   host_get_clock_service(host, CALENDAR_CLOCK, &rthreads_calendar_clock);
   mach_port_deallocate(mach_task_self(), host);
}
#endif

struct thread_data
{
   void (*func)(void*);
   void *userdata;
};

struct sthread
{
#ifdef USE_WIN32_THREADS
   HANDLE thread;
   DWORD id;
#else
   pthread_t id;
#endif
};

struct slock
{
#ifdef USE_WIN32_THREADS
   CRITICAL_SECTION lock;
#else
   pthread_mutex_t lock;
#endif
};

#ifdef USE_WIN32_THREADS
/* One waiter of a condition variable. Each waiting thread blocks on an
 * event of its own, so a wake is delivered to exactly the thread it is
 * meant for: there is nothing to steal and nothing to lose, an event set
 * before the thread reaches its wait is simply still set when it gets
 * there. Nodes belong to the scond and are recycled through its spare
 * list, so a wait costs no allocation and no handle creation once the
 * scond has seen as many concurrent waiters before. */
struct scond_waiter
{
   struct scond_waiter *next;
   HANDLE event;
};
#endif

struct scond
{
#ifdef USE_WIN32_THREADS
   /* waiting threads in FIFO order, and the nodes not in use */
   struct scond_waiter *head;
   struct scond_waiter *tail;
   struct scond_waiter *spare;
   /* guards the three lists; SetEvent on a waiter is done under it so a
    * node returned to the spare list never carries a stale signal */
   CRITICAL_SECTION cs;
#else
   pthread_cond_t cond;
#endif
};

#ifdef USE_WIN32_THREADS
static DWORD CALLBACK thread_wrap(void *data_)
#else
static void *thread_wrap(void *data_)
#endif
{
   struct thread_data *data = (struct thread_data*)data_;
   if (!data)
      return 0;
   data->func(data->userdata);
   free(data);
   return 0;
}

sthread_t *sthread_create(void (*thread_func)(void*), void *userdata)
{
   return sthread_create_with_priority(thread_func, userdata, 0);
}

/* TODO/FIXME - this needs to be implemented for Switch/3DS */
#if !defined(SWITCH) && !defined(USE_WIN32_THREADS) && !defined(_3DS) && !defined(GEKKO) && !defined(__HAIKU__) && !defined(__EMSCRIPTEN__)
#define HAVE_THREAD_ATTR
#endif

sthread_t *sthread_create_with_priority(void (*thread_func)(void*), void *userdata, int thread_priority)
{
#ifdef HAVE_THREAD_ATTR
   pthread_attr_t thread_attr;
   bool thread_attr_needed  = false;
#endif
   bool thread_created      = false;
   struct thread_data *data = NULL;
   sthread_t *thread        = (sthread_t*)malloc(sizeof(*thread));

   if (!thread)
      return NULL;

   if (!(data = (struct thread_data*)malloc(sizeof(*data))))
   {
      free(thread);
      return NULL;
   }

   data->func               = thread_func;
   data->userdata           = userdata;

   thread->id               = 0;
#ifdef USE_WIN32_THREADS
   thread->thread           = CreateThread(NULL, 0, thread_wrap,
         data, 0, &thread->id);
   thread_created           = !!thread->thread;
#else
#ifdef HAVE_THREAD_ATTR
   pthread_attr_init(&thread_attr);

   if ((thread_priority >= 1) && (thread_priority <= 100))
   {
      struct sched_param sp;
      memset(&sp, 0, sizeof(struct sched_param));
      sp.sched_priority = thread_priority;
      pthread_attr_setschedpolicy(&thread_attr, SCHED_RR);
      pthread_attr_setschedparam(&thread_attr, &sp);

      thread_attr_needed = true;
   }

#if defined(VITA)
   pthread_attr_setstacksize(&thread_attr , 0x10000 );
   thread_attr_needed = true;
#elif defined(__APPLE__)
   /* Default stack size on Apple is 512Kb;
    * for PS2 disc scanning and other reasons, we'd like 2MB. */
   pthread_attr_setstacksize(&thread_attr , 0x200000 );
   thread_attr_needed = true;
#endif

   if (thread_attr_needed)
      thread_created = pthread_create(&thread->id, &thread_attr, thread_wrap, data) == 0;
   else
      thread_created = pthread_create(&thread->id, NULL, thread_wrap, data) == 0;

   pthread_attr_destroy(&thread_attr);
#else
   thread_created    = pthread_create(&thread->id, NULL, thread_wrap, data) == 0;
#endif

#endif

   if (thread_created)
      return thread;
   free(data);
   free(thread);
   return NULL;
}

bool sthread_raise_current_priority(void)
{
#if defined(USE_WIN32_THREADS)
   return SetThreadPriority(GetCurrentThread(),
         THREAD_PRIORITY_TIME_CRITICAL) != 0;
#elif defined(__ANDROID__)
   /* Bionic lets an app move its own threads into the audio band
    * without privilege; -16 is ANDROID_PRIORITY_AUDIO. */
   return setpriority(PRIO_PROCESS, gettid(), -16) == 0;
#elif defined(RTHREADS_HAVE_SCHEDPARAM)
   /* Real-time round-robin at a middling priority: above every
    * time-shared thread, below anything the system runs at the top of
    * the band. Distributions that grant the audio group an rtprio
    * limit allow this without root; where it is refused the thread
    * simply keeps its default, which is the caller's contract. */
   struct sched_param sp;
   int lo  = sched_get_priority_min(SCHED_RR);
   int hi  = sched_get_priority_max(SCHED_RR);
   memset(&sp, 0, sizeof(sp));
   if (lo < 0 || hi < lo)
      return false;
   sp.sched_priority = lo + (hi - lo) / 2;
   return pthread_setschedparam(pthread_self(), SCHED_RR, &sp) == 0;
#else
   return false;
#endif
}

void sthread_setname(const char *name)
{
#if defined(__linux__) && !defined(USE_WIN32_THREADS) && !defined(GEKKO) && !defined(_3DS)
   /* prctl rather than pthread_setname_np: it is available on every
    * bionic and glibc version we build against, and takes the name as
    * a plain buffer, so the caller cannot be rejected outright for
    * overrunning the kernel's 16-byte limit. Copied rather than passed
    * through so an over-long name truncates instead of failing. */
   char buf[16];
   size_t i;
   if (!name)
      return;
   for (i = 0; i < sizeof(buf) - 1 && name[i]; i++)
      buf[i] = name[i];
   buf[i] = '\0';
   prctl(PR_SET_NAME, buf, 0, 0, 0);
#elif defined(__APPLE__)
   if (!name)
      return;
   pthread_setname_np(name);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
   if (!name)
      return;
   pthread_set_name_np(pthread_self(), name);
#elif defined(__NetBSD__)
   if (!name)
      return;
   pthread_setname_np(pthread_self(), "%s", (void*)name);
#else
   (void)name;
#endif
}

int sthread_detach(sthread_t *thread)
{
#ifdef USE_WIN32_THREADS
   if (!thread)
      return 0;
   CloseHandle(thread->thread);
   free(thread);
   return 0;
#else
   int ret;
   if (!thread)
      return 0;
   ret = pthread_detach(thread->id);
   free(thread);
   return ret;
#endif
}

void sthread_join(sthread_t *thread)
{
   if (!thread)
      return;
#ifdef USE_WIN32_THREADS
   WaitForSingleObject(thread->thread, INFINITE);
   CloseHandle(thread->thread);
#else
   pthread_join(thread->id, NULL);
#endif
   free(thread);
}

#if !defined(GEKKO)
bool sthread_isself(sthread_t *thread)
{
#ifdef USE_WIN32_THREADS
   return thread ? GetCurrentThreadId() == thread->id        : false;
#else
   return thread ? pthread_equal(pthread_self(), thread->id) : false;
#endif
}
#endif

slock_t *slock_new(void)
{
   slock_t      *lock = (slock_t*)calloc(1, sizeof(*lock));
   if (!lock)
      return NULL;
#ifdef USE_WIN32_THREADS
   InitializeCriticalSection(&lock->lock);
#else
   if (pthread_mutex_init(&lock->lock, NULL) != 0)
   {
      free(lock);
      return NULL;
   }
#endif
   return lock;
}

void slock_free(slock_t *lock)
{
   if (!lock)
      return;

#ifdef USE_WIN32_THREADS
   DeleteCriticalSection(&lock->lock);
#else
   pthread_mutex_destroy(&lock->lock);
#endif
   free(lock);
}

void slock_lock(slock_t *lock)
{
   if (!lock)
      return;
#ifdef USE_WIN32_THREADS
   EnterCriticalSection(&lock->lock);
#else
   pthread_mutex_lock(&lock->lock);
#endif
}

bool slock_try_lock(slock_t *lock)
{
#ifdef USE_WIN32_THREADS
   return lock && TryEnterCriticalSection(&lock->lock);
#else
   return lock && (pthread_mutex_trylock(&lock->lock) == 0);
#endif
}

void slock_unlock(slock_t *lock)
{
   if (!lock)
      return;
#ifdef USE_WIN32_THREADS
   LeaveCriticalSection(&lock->lock);
#else
   pthread_mutex_unlock(&lock->lock);
#endif
}

scond_t *scond_new(void)
{
   scond_t      *cond = (scond_t*)calloc(1, sizeof(*cond));

   if (!cond)
      return NULL;

#ifdef USE_WIN32_THREADS
   cond->head  = NULL;
   cond->tail  = NULL;
   cond->spare = NULL;
   InitializeCriticalSection(&cond->cs);
#else
   if (pthread_cond_init(&cond->cond, NULL) != 0)
   {
      free(cond);
      return NULL;
   }
#endif

   return cond;
}

void scond_free(scond_t *cond)
{
   if (!cond)
      return;

#ifdef USE_WIN32_THREADS
   {
      struct scond_waiter *w = cond->spare;
      while (w)
      {
         struct scond_waiter *next = w->next;
         CloseHandle(w->event);
         free(w);
         w = next;
      }
   }
   DeleteCriticalSection(&cond->cs);
#else
   pthread_cond_destroy(&cond->cond);
#endif
   free(cond);
}

#ifdef USE_WIN32_THREADS
/* Block on the caller's own event until signalled or dwMilliseconds have
 * passed. The node is queued before the lock is released, so a signal
 * that follows the release finds it; the event holds the signal until
 * the thread arrives at the wait. Returns false only on timeout. */
static bool scond_wait_win32(scond_t *cond, slock_t *lock, DWORD dwMilliseconds)
{
   struct scond_waiter *w;
   bool woken = true;

   EnterCriticalSection(&cond->cs);
   if ((w = cond->spare))
      cond->spare = w->next;
   else
   {
      if ((w = (struct scond_waiter *)malloc(sizeof(*w))))
      {
         if (!(w->event = CreateEvent(NULL, FALSE, FALSE, NULL)))
         {
            free(w);
            w = NULL;
         }
      }
      if (!w)
      {
         /* nothing to wait on: report a spurious wake-up, which every
          * caller handles by re-checking its predicate */
         LeaveCriticalSection(&cond->cs);
         return true;
      }
   }
   w->next = NULL;
   if (cond->tail)
      cond->tail->next = w;
   else
      cond->head = w;
   cond->tail = w;
   LeaveCriticalSection(&cond->cs);

   LeaveCriticalSection(&lock->lock);
   if (WaitForSingleObject(w->event, dwMilliseconds) == WAIT_TIMEOUT)
   {
      /* a signal may have taken the node and set the event between the
       * timeout and this lock; then consume it, otherwise unlink */
      EnterCriticalSection(&cond->cs);
      {
         struct scond_waiter **link = &cond->head;
         struct scond_waiter *prev  = NULL;
         while (*link && *link != w)
         {
            prev = *link;
            link = &(*link)->next;
         }
         if (*link)
         {
            *link = w->next;
            if (cond->tail == w)
               cond->tail = prev;
            woken = false;
         }
         else
            WaitForSingleObject(w->event, INFINITE);
      }
   }
   else
      EnterCriticalSection(&cond->cs);
   w->next     = cond->spare;
   cond->spare = w;
   LeaveCriticalSection(&cond->cs);
   EnterCriticalSection(&lock->lock);
   return woken;
}
#endif

void scond_wait(scond_t *cond, slock_t *lock)
{
#ifdef USE_WIN32_THREADS
   scond_wait_win32(cond, lock, INFINITE);
#else
   pthread_cond_wait(&cond->cond, &lock->lock);
#endif
}

int scond_broadcast(scond_t *cond)
{
#ifdef USE_WIN32_THREADS
   struct scond_waiter *w;
   EnterCriticalSection(&cond->cs);
   for (w = cond->head; w; w = w->next)
      SetEvent(w->event);
   cond->head = NULL;
   cond->tail = NULL;
   LeaveCriticalSection(&cond->cs);
   return 0;
#else
   return pthread_cond_broadcast(&cond->cond);
#endif
}

void scond_signal(scond_t *cond)
{
#ifdef USE_WIN32_THREADS
   struct scond_waiter *w;
   EnterCriticalSection(&cond->cs);
   if ((w = cond->head))
   {
      if (!(cond->head = w->next))
         cond->tail = NULL;
      SetEvent(w->event);
   }
   LeaveCriticalSection(&cond->cs);
#else
   pthread_cond_signal(&cond->cond);
#endif
}

bool scond_wait_timeout(scond_t *cond, slock_t *lock, int64_t timeout_us)
{
#ifdef USE_WIN32_THREADS
   /* How to convert a microsecond (us) timeout to millisecond (ms)?
    *
    * Someone asking for a 0 timeout clearly wants immediate timeout.
    * Someone asking for a 1 timeout clearly wants an actual timeout
    * of the minimum length */
   /* The implementation of a 0 timeout here with pthreads is sketchy.
    * It isn't clear what happens if pthread_cond_timedwait is called with NOW.
    * Moreover, it is possible that this thread gets preempted after the
    * clock_gettime but before the pthread_cond_timedwait.
    * In order to help smoke out problems caused by this strange usage,
    * let's treat a 0 timeout as always timing out.
    */
   if (timeout_us == 0)
      return false;
   else if (timeout_us < 1000)
      return scond_wait_win32(cond, lock, 1);
   /* Someone asking for 1000 or 1001 timeout shouldn't
    * accidentally get 2ms. */
   return scond_wait_win32(cond, lock, timeout_us / 1000);
#else
   int64_t seconds, remainder;
   struct timespec now;
#if defined(__MACH__) && defined(__APPLE__) && !defined(RTHREADS_HAVE_CLOCK_GETTIME)
   mach_timespec_t mts;
#endif
#if defined(__MACH__) && defined(__APPLE__)
   /* CALENDAR_CLOCK is the Mach equivalent of CLOCK_REALTIME, which is what
    * pthread_cond_timedwait() below expects. */
#ifdef RTHREADS_HAVE_CLOCK_GETTIME
   clock_gettime(CLOCK_REALTIME, &now);
#else
   pthread_once(&rthreads_calendar_clock_once, rthreads_calendar_clock_init);
   clock_get_time(rthreads_calendar_clock, &mts);
   now.tv_sec  = mts.tv_sec;
   now.tv_nsec = mts.tv_nsec;
#endif
#elif !defined(__PSL1GHT__) && defined(__PS3__)
   sys_time_sec_t s;
   sys_time_nsec_t n;
   sys_time_get_current_time(&s, &n);
   now.tv_sec            = s;
   now.tv_nsec           = n;
#elif defined(PS2)
   {
      int tickms            = ps2_clock();
      now.tv_sec            = tickms / 1000;
      now.tv_nsec           = (long)(tickms % 1000) * 1000000L;
   }
#elif !defined(DINGUX_BETA) && (defined(VITA) || defined(_3DS) || defined(PSP))
   {
      struct timeval tm;
      gettimeofday(&tm, NULL);
      now.tv_sec            = tm.tv_sec;
      now.tv_nsec           = tm.tv_usec * 1000;
   }
#elif defined(RETRO_WIN32_USE_PTHREADS)
   _ftime64_s(&now);
#elif defined(GEKKO)
   {
      const uint64_t tickms = gettime() / TB_TIMER_CLOCK;
      now.tv_sec            = tickms / 1000;
      now.tv_nsec           = (long)(tickms % 1000) * 1000000L;
   }
#else
   clock_gettime(CLOCK_REALTIME, &now);
#endif

   seconds              = timeout_us / INT64_C(1000000);
   remainder            = timeout_us % INT64_C(1000000);

   now.tv_sec          += seconds;
   now.tv_nsec         += remainder * INT64_C(1000);

   if (now.tv_nsec >= 1000000000)
   {
      now.tv_nsec      -= 1000000000;
      now.tv_sec       += 1;
   }

   return (pthread_cond_timedwait(&cond->cond, &lock->lock, &now) == 0);
#endif
}

#ifdef HAVE_THREAD_STORAGE
bool sthread_tls_create(sthread_tls_t *tls)
{
#ifdef USE_WIN32_THREADS
   return (*tls = TlsAlloc()) != TLS_OUT_OF_INDEXES;
#else
   return pthread_key_create((pthread_key_t*)tls, NULL) == 0;
#endif
}

bool sthread_tls_create_with_dtor(sthread_tls_t *tls,
      void (*destructor)(void *value))
{
#ifdef USE_WIN32_THREADS
   /* TlsAlloc() provides no destructor callback; created without one. */
   (void)destructor;
   return (*tls = TlsAlloc()) != TLS_OUT_OF_INDEXES;
#else
   return pthread_key_create((pthread_key_t*)tls, destructor) == 0;
#endif
}

bool sthread_tls_delete(sthread_tls_t *tls)
{
#ifdef USE_WIN32_THREADS
   return TlsFree(*tls) != 0;
#else
   return pthread_key_delete(*tls) == 0;
#endif
}

void *sthread_tls_get(sthread_tls_t *tls)
{
#ifdef USE_WIN32_THREADS
   return TlsGetValue(*tls);
#else
   return pthread_getspecific(*tls);
#endif
}

bool sthread_tls_set(sthread_tls_t *tls, const void *data)
{
#ifdef USE_WIN32_THREADS
   return TlsSetValue(*tls, (void*)data) != 0;
#else
   return pthread_setspecific(*tls, data) == 0;
#endif
}
#endif

uintptr_t sthread_get_thread_id(sthread_t *thread)
{
   if (thread)
      return (uintptr_t)thread->id;
   return 0;
}

uintptr_t sthread_get_current_thread_id(void)
{
#ifdef USE_WIN32_THREADS
   return (uintptr_t)GetCurrentThreadId();
#else
   return (uintptr_t)pthread_self();
#endif
}

bool sthread_is_main_thread(void)
{
#if defined(__APPLE__)
   /* BSD/Darwin extension reporting whether the caller is the initial
    * thread. pthread.h is already included on this backend. */
   return pthread_main_np() != 0;
#else
   /* No native predicate on this backend; current callers are Apple-only.
    * See the header note for the portable captured-id alternative. */
   return false;
#endif
}

/* pthread_cancel / pthread_setcancelstate are POSIX but not universally
 * available: notably absent on Android/Bionic, and meaningless on the
 * non-pthread backends. Enable only where the backend provides them. */
#if !defined(USE_WIN32_THREADS) && !defined(GEKKO) && !defined(_3DS) && !defined(__ANDROID__)
#define RTHREADS_HAVE_CANCEL 1
#endif

void sthread_set_cancel_enable(bool enable)
{
#ifdef RTHREADS_HAVE_CANCEL
   pthread_setcancelstate(
         enable ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, NULL);
#else
   (void)enable;
#endif
}

bool sthread_cancel(sthread_t *thread)
{
#ifdef RTHREADS_HAVE_CANCEL
   if (thread)
      return pthread_cancel(thread->id) == 0;
   return false;
#else
   (void)thread;
   return false;
#endif
}

void *sthread_priority_override_begin(void)
{
#ifdef RTHREADS_HAVE_QOS_OVERRIDE
   return (void*)pthread_override_qos_class_start_np(
         pthread_self(), QOS_CLASS_USER_INTERACTIVE, 0);
#else
   return NULL;
#endif
}

void sthread_priority_override_end(void *ovr)
{
#ifdef RTHREADS_HAVE_QOS_OVERRIDE
   if (ovr)
      pthread_override_qos_class_end_np((pthread_override_t)ovr);
#else
   (void)ovr;
#endif
}
