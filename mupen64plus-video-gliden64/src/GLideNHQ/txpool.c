/* Copyright (c) 2026 The RetroArch team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#include "txpool.h"

#define TXPOOL_MAX_LANES 64
#define TXPOOL_LINE_PAD  64

struct txpool_lane
{
   struct txpool *pool;
   unsigned       id;
};

struct txpool
{
   txpool_fn_t        fn;
   void              *ctx;
   unsigned           used;      /* lanes taking part in this run */
   unsigned           lanes;     /* caller included */
   int                stop;      /* published by the generation bump */
   int                ec_live;
   retro_eventcount_t work_ec;   /* workers park here between runs */
   retro_eventcount_t done_ec;   /* the caller parks here during a run */
   sthread_t         *threads[TXPOOL_MAX_LANES];
   struct txpool_lane lane[TXPOOL_MAX_LANES];
   char               pad0[TXPOOL_LINE_PAD];
   retro_atomic_int_t generation; /* bumped once per run and at stop */
   char               pad1[TXPOOL_LINE_PAD];
   retro_atomic_int_t remaining;  /* workers not yet done with this run */
   char               pad2[TXPOOL_LINE_PAD];
};

static int txpool_await(struct txpool *p, int seen)
{
   int gen, key;
   for (;;)
   {
      gen = retro_atomic_load_acquire_int(&p->generation);
      if (gen != seen)
         return gen;
      key = retro_eventcount_prepare_wait(&p->work_ec);
      gen = retro_atomic_load_acquire_int(&p->generation);
      if (gen != seen)
      {
         retro_eventcount_cancel_wait(&p->work_ec);
         return gen;
      }
      retro_eventcount_commit_wait(&p->work_ec, key);
   }
}

static void txpool_worker(void *data)
{
   struct txpool_lane *l = (struct txpool_lane*)data;
   struct txpool      *p = l->pool;
   int              seen = 0;

   for (;;)
   {
      seen = txpool_await(p, seen);
      if (p->stop)
         break;
      if (l->id < p->used)
         p->fn(p->ctx, l->id);
      /* every worker reports, used or not, so the count is fixed */
      if (retro_atomic_fetch_sub_int(&p->remaining, 1) == 1)
         retro_eventcount_notify(&p->done_ec);
   }
}

static void txpool_bump(struct txpool *p)
{
   int gen = retro_atomic_load_acquire_int(&p->generation);
   retro_atomic_store_release_int(&p->generation, gen + 1);
   retro_eventcount_notify(&p->work_ec);
}

txpool_t *txpool_new(unsigned lanes)
{
   unsigned i;
   struct txpool *p = (struct txpool*)calloc(1, sizeof(*p));
   if (!p)
      return NULL;

   retro_atomic_int_init(&p->generation, 0);
   retro_atomic_int_init(&p->remaining, 0);
   p->lanes = 1;

   if (lanes > TXPOOL_MAX_LANES)
      lanes = TXPOOL_MAX_LANES;
#if !defined(RETRO_ATOMIC_LOCK_FREE)
   lanes = 1;
#endif
   if (lanes <= 1)
      return p;

   p->ec_live = 1;
   if (     !retro_eventcount_init(&p->work_ec)
         || !retro_eventcount_init(&p->done_ec))
      return p;

   for (i = 0; i < lanes; i++)
   {
      p->lane[i].pool = p;
      p->lane[i].id   = i;
   }
   /* a thread that fails to start caps the pool at those that did */
   for (i = 1; i < lanes; i++)
   {
      p->threads[i] = sthread_create(txpool_worker, &p->lane[i]);
      if (!p->threads[i])
         break;
      p->lanes = i + 1;
   }
   return p;
}

unsigned txpool_lanes(const txpool_t *p)
{
   return p ? p->lanes : 1;
}

void txpool_run(txpool_t *p, txpool_fn_t fn, void *ctx, unsigned used)
{
   int key;

   /* Every lane asked for is a block of work that must run, so a run
    * the threads cannot cover is done on the caller, lane by lane. */
   if (!p || p->lanes <= 1 || used <= 1 || used > p->lanes)
   {
      unsigned i;
      for (i = 0; i < used; i++)
         fn(ctx, i);
      return;
   }

   p->fn   = fn;
   p->ctx  = ctx;
   p->used = used;
   retro_atomic_store_release_int(&p->remaining, (int)(p->lanes - 1));
   txpool_bump(p);

   fn(ctx, 0);

   for (;;)
   {
      if (retro_atomic_load_acquire_int(&p->remaining) == 0)
         return;
      key = retro_eventcount_prepare_wait(&p->done_ec);
      if (retro_atomic_load_acquire_int(&p->remaining) == 0)
      {
         retro_eventcount_cancel_wait(&p->done_ec);
         return;
      }
      retro_eventcount_commit_wait(&p->done_ec, key);
   }
}

void txpool_free(txpool_t *p)
{
   unsigned i;
   if (!p)
      return;
   if (p->lanes > 1)
   {
      p->stop = 1;
      txpool_bump(p);
      for (i = 1; i < p->lanes; i++)
         sthread_join(p->threads[i]);
   }
   if (p->ec_live)
   {
      retro_eventcount_free(&p->work_ec);
      retro_eventcount_free(&p->done_ec);
   }
   free(p);
}
