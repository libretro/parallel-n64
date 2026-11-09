/* What a parallel-rdp pipeline miss uses on the frame path
 * (parallel-rdp/pipeline_pending.h): the set of hashes sent to the
 * worker holds up to its limit, refuses past it without touching what
 * it has, and has room for 0; nodes given back by another thread are
 * taken again, each exactly once, and the pool never makes one up. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#include "pipeline_pending.h"

#define MAX_HASHES 64u
#define NODES      32u
#define ROUNDS     400000u

typedef struct node
{
   mpsc_stack_node_t link;
   unsigned          in_use;
} node_t;

static unsigned failures;
static node_t nodes[NODES];
static rdp_node_pool_t pool;
static retro_atomic_size_t published, returned_count;
static node_t *volatile mailbox[NODES];

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void test_set(void)
{
   rdp_hash_set_t set;
   uint64_t i;

   CHECK(rdp_hash_set_init(&set, MAX_HASHES));
   CHECK(set.mask + 1 >= 2 * MAX_HASHES);
   CHECK(rdp_hash_set_insert(&set, 0));
   CHECK(!rdp_hash_set_insert(&set, 0));
   /* every hash lands on the same first slot, so probing must wrap */
   for (i = 1; i <= MAX_HASHES; i++)
   {
      uint64_t h = (i << 32) | set.mask;
      CHECK(rdp_hash_set_insert(&set, h));
      CHECK(!rdp_hash_set_insert(&set, h));
   }
   CHECK(set.count == MAX_HASHES);
   /* full: a new one is refused, one it has is still found as such */
   CHECK(!rdp_hash_set_insert(&set, 0x1234567890ull));
   CHECK(set.count == MAX_HASHES);
   CHECK(!rdp_hash_set_insert(&set, (1ull << 32) | set.mask));
   rdp_hash_set_free(&set);
   /* a set that has no table, by failure or after free, refuses */
   CHECK(!rdp_hash_set_insert(&set, 5));
   CHECK(!rdp_hash_set_insert(&set, 0));
}

static void test_pool_empty(void)
{
   rdp_node_pool_t p;
   node_t a, b;
   mpsc_stack_node_t *first, *second;

   rdp_node_pool_init(&p);
   CHECK(!rdp_node_pool_acquire(&p));
   rdp_node_pool_release(&p, &a.link);
   rdp_node_pool_release(&p, &b.link);
   first  = rdp_node_pool_acquire(&p);
   second = rdp_node_pool_acquire(&p);
   CHECK(first && second && first != second);
   CHECK(!rdp_node_pool_acquire(&p));
}

/* The worker: takes what was handed to it and gives each back. */
static void worker(void *arg)
{
   size_t seen = 0;

   (void)arg;
   while (seen < ROUNDS)
   {
      node_t *n;
      if (seen == retro_atomic_load_acquire_size(&published))
         continue;
      n = mailbox[seen % NODES];
      if (!n || n->in_use != 1)
         failures++;
      else
         n->in_use = 0;
      seen++;
      rdp_node_pool_release(&pool, &n->link);
      retro_atomic_fetch_add_size(&returned_count, 1);
   }
}

static void test_pool_threads(void)
{
   sthread_t *t;
   size_t i, sent = 0;
   size_t refused = 0;

   rdp_node_pool_init(&pool);
   retro_atomic_size_init(&published, 0);
   retro_atomic_size_init(&returned_count, 0);
   for (i = 0; i < NODES; i++)
      rdp_node_pool_release(&pool, &nodes[i].link);
   t = sthread_create(worker, NULL);
   CHECK(t != NULL);
   while (sent < ROUNDS)
   {
      /* at most NODES out at a time, and the mailbox has that many slots */
      mpsc_stack_node_t *l = rdp_node_pool_acquire(&pool);
      node_t *n = (node_t*)l;
      if (!n)
      {
         refused++;
         continue;
      }
      if (n->in_use != 0)
         failures++;
      n->in_use = 1;
      mailbox[sent % NODES] = n;
      sent++;
      retro_atomic_store_release_size(&published, sent);
   }
   sthread_join(t);
   CHECK(retro_atomic_load_acquire_size(&returned_count) == ROUNDS);
   /* all of them are back, and no more than there were */
   for (i = 0; i < NODES; i++)
      CHECK(rdp_node_pool_acquire(&pool) != NULL);
   CHECK(!rdp_node_pool_acquire(&pool));
   printf("pool: %lu jobs, refused %lu times while all %u were out\n",
         (unsigned long)sent, (unsigned long)refused, NODES);
}

int main(void)
{
   test_set();
   test_pool_empty();
   test_pool_threads();
   if (failures)
   {
      printf("pipeline_pending: %u failures\n", failures);
      return 1;
   }
   printf("pipeline_pending: ok\n");
   return 0;
}
