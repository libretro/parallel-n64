/*
  libco.pthread (2013-03-28)
  license: public domain
*/

#define LIBCO_C
#include "libco.h"
#include <stdlib.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  void (*entry)(void);
} cothread_struct;

static cothread_struct *co_primary = 0;
static cothread_struct *co_running = 0;

static void* springboard(void* cothread) {
  cothread_struct* t = (cothread_struct*)cothread;
  pthread_mutex_lock(&t->mutex);
  pthread_cond_wait(&t->cond, &t->mutex);
  t->entry();
  abort();
}

static void create_primary() {
  co_primary = (cothread_struct*)malloc(sizeof(cothread_struct));
  pthread_mutex_init(&co_primary->mutex, 0);
  pthread_cond_init(&co_primary->cond, 0);
  pthread_mutex_lock(&co_primary->mutex);
  co_primary->thread = pthread_self();
}

cothread_t co_active() {
  if(!co_primary) create_primary();
  if(!co_running) co_running = co_primary;

  return (cothread_t)co_running;
}

cothread_t co_create(unsigned int size, void (*coentry)(void)) {
  if(!co_primary) create_primary();
  if(!co_running) co_running = co_primary;

  cothread_struct *thread = (cothread_struct*)malloc(sizeof(cothread_struct));
  if(thread) {
    thread->entry = coentry;
    pthread_mutex_init(&thread->mutex, 0);
    pthread_cond_init(&thread->cond, 0);
    pthread_create(&thread->thread, 0, springboard, thread);
  }

  return (cothread_t)thread;
}

void co_delete(cothread_t cothread) {
  if(cothread) {
    cothread_struct* t = (cothread_struct*)cothread;

    pthread_cancel(t->thread);

    // TODO: Mutex and cond?

    free(cothread);
  }
}

void co_switch(cothread_t cothread) {
  cothread_struct* running = co_running;
  cothread_struct* starting = (cothread_struct*)cothread;
  
  co_running = starting;

  pthread_mutex_lock(&starting->mutex);
  pthread_cond_signal(&starting->cond);
  pthread_mutex_unlock(&starting->mutex);
  pthread_cond_wait(&running->cond, &running->mutex);
}

#ifdef __cplusplus
}
#endif
