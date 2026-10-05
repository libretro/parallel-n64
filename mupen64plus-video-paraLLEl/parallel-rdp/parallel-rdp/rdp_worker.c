/* Copyright (c) 2026 The RetroArch team
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#include "rdp_worker.h"

struct rdp_worker
{
   rdp_worker_start_t start;
   rdp_worker_work_t  work;
   void              *user;
   sthread_t         *thread;   /* NULL: work runs on the pusher */
   mpsc_stack_t       queue;
   retro_eventcount_t work_ec;  /* the worker parks here */
   retro_eventcount_t done_ec;  /* rdp_worker_wait parks here */
   retro_atomic_int_t stop;
};

/* The stack hands back newest-first; flip it to submission order. */
static mpsc_stack_node_t *rdp_worker_reverse(mpsc_stack_node_t *n)
{
   mpsc_stack_node_t *prev = NULL;
   while (n)
   {
      mpsc_stack_node_t *next = n->next;
      n->next                 = prev;
      prev                    = n;
      n                       = next;
   }
   return prev;
}

static void rdp_worker_loop(void *data)
{
   rdp_worker_t      *w = (rdp_worker_t*)data;
   mpsc_stack_node_t *n;
   int                key;

   if (w->start)
      w->start(w->user);

   for (;;)
   {
      n = mpsc_stack_drain(&w->queue);
      if (!n && retro_atomic_load_acquire_int(&w->stop))
      {
         /* stop is raised after the last push, so once it is seen one
          * more drain is guaranteed to pick up anything still queued */
         n = mpsc_stack_drain(&w->queue);
         if (!n)
            break;
      }
      if (!n)
      {
         key = retro_eventcount_prepare_wait(&w->work_ec);
         if (    !mpsc_stack_empty(&w->queue)
               || retro_atomic_load_acquire_int(&w->stop))
            retro_eventcount_cancel_wait(&w->work_ec);
         else
            retro_eventcount_commit_wait(&w->work_ec, key);
         continue;
      }

      n = rdp_worker_reverse(n);
      while (n)
      {
         mpsc_stack_node_t *next = n->next;
         w->work(w->user, n);
         /* per item: a waiter may be waiting on this one */
         retro_eventcount_notify(&w->done_ec);
         n = next;
      }
   }
}

rdp_worker_t *rdp_worker_new(rdp_worker_start_t start,
      rdp_worker_work_t work, void *user)
{
   rdp_worker_t *w = (rdp_worker_t*)calloc(1, sizeof(*w));
   if (!w)
      return NULL;

   w->start = start;
   w->work  = work;
   w->user  = user;
   mpsc_stack_init(&w->queue);
   retro_atomic_int_init(&w->stop, 0);

#if defined(RETRO_ATOMIC_LOCK_FREE)
   if (     retro_eventcount_init(&w->work_ec)
         && retro_eventcount_init(&w->done_ec))
      w->thread = sthread_create(rdp_worker_loop, w);
#endif

   if (!w->thread)
   {
      /* inline mode: no thread, so nothing will ever park */
      retro_eventcount_free(&w->work_ec);
      retro_eventcount_free(&w->done_ec);
      if (w->start)
         w->start(w->user);
   }
   return w;
}

void rdp_worker_push(rdp_worker_t *w, mpsc_stack_node_t *item)
{
   if (!w->thread)
   {
      w->work(w->user, item);
      return;
   }
   mpsc_stack_push(&w->queue, item);
   retro_eventcount_notify(&w->work_ec);
}

void rdp_worker_wait(rdp_worker_t *w, rdp_worker_pred_t pred, void *ctx)
{
   int key;

   if (!w->thread)
      return; /* inline mode: the work already ran */

   for (;;)
   {
      if (pred(ctx))
         return;
      key = retro_eventcount_prepare_wait(&w->done_ec);
      if (pred(ctx))
      {
         retro_eventcount_cancel_wait(&w->done_ec);
         return;
      }
      retro_eventcount_commit_wait(&w->done_ec, key);
   }
}

void rdp_worker_free(rdp_worker_t *w)
{
   if (!w)
      return;
   if (w->thread)
   {
      retro_atomic_store_release_int(&w->stop, 1);
      retro_eventcount_notify(&w->work_ec);
      sthread_join(w->thread);
      retro_eventcount_free(&w->work_ec);
      retro_eventcount_free(&w->done_ec);
   }
   free(w);
}
