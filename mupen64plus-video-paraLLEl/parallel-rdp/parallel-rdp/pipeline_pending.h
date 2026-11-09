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

#ifndef RDP_PIPELINE_PENDING_H
#define RDP_PIPELINE_PENDING_H

/* What a pipeline miss needs on the frame path, allocated once before
 * the first frame. C89.
 *
 * The set remembers which hashes went to the worker. Full, it refuses:
 * such a variant is not specialised and the draw stays on the generic
 * one, which draws the same.
 *
 * The pool holds the job nodes. One thread (the owner) takes them, any
 * thread gives them back, no lock. Empty, it refuses: the job waits for
 * the variant's next miss. */

#include <stdint.h>
#include <stdlib.h>

#include <boolean.h>
#include <retro_inline.h>
#include <queues/mpsc_stack.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rdp_hash_set
{
   uint64_t *keys;
   unsigned  mask;
   unsigned  count;
   unsigned  max_count;
   int       has_zero;
} rdp_hash_set_t;

/* @max_count hashes, in a table twice that (rounded up to a power of
 * two). On failure the set refuses everything. */
static INLINE bool rdp_hash_set_init(rdp_hash_set_t *set, unsigned max_count)
{
   unsigned capacity = 2;

   while (capacity < 2 * max_count)
      capacity *= 2;
   set->count     = 0;
   set->has_zero  = 0;
   set->max_count = 0;
   set->mask      = 0;
   if (!(set->keys = (uint64_t*)calloc(capacity, sizeof(uint64_t))))
      return false;
   set->mask      = capacity - 1;
   set->max_count = max_count;
   return true;
}

static INLINE void rdp_hash_set_free(rdp_hash_set_t *set)
{
   free(set->keys);
   set->keys      = NULL;
   set->max_count = 0;
   set->count     = 0;
   set->has_zero  = 0;
}

/* True when @hash is new and now in the set; false when it was there
 * already, or the set is full. */
static INLINE bool rdp_hash_set_insert(rdp_hash_set_t *set, uint64_t hash)
{
   unsigned i;

   if (!set->keys)
      return false;
   if (!hash)
   {
      if (set->has_zero)
         return false;
      set->has_zero = 1;
      return true;
   }
   for (i = (unsigned)hash & set->mask; set->keys[i]; i = (i + 1) & set->mask)
      if (set->keys[i] == hash)
         return false;
   if (set->count >= set->max_count)
      return false;
   set->keys[i] = hash;
   set->count++;
   return true;
}

typedef struct rdp_node_pool
{
   mpsc_stack_t       returned; /* given back, by any thread */
   mpsc_stack_node_t *free_list; /* the owner's */
} rdp_node_pool_t;

static INLINE void rdp_node_pool_init(rdp_node_pool_t *pool)
{
   mpsc_stack_init(&pool->returned);
   pool->free_list = NULL;
}

/* Any thread. */
static INLINE void rdp_node_pool_release(rdp_node_pool_t *pool,
      mpsc_stack_node_t *node)
{
   mpsc_stack_push(&pool->returned, node);
}

/* The owner only. NULL when every node is out. */
static INLINE mpsc_stack_node_t *rdp_node_pool_acquire(rdp_node_pool_t *pool)
{
   mpsc_stack_node_t *node = pool->free_list;

   if (!node)
      node = mpsc_stack_drain(&pool->returned);
   if (node)
      pool->free_list = node->next;
   return node;
}

#ifdef __cplusplus
}
#endif

#endif
