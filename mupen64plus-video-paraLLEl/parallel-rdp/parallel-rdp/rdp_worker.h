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

#ifndef RDP_WORKER_H
#define RDP_WORKER_H

/* One background thread fed by any number of threads, in FIFO order.
 *
 * No lock: producers push onto a lock-free stack, the worker takes the
 * whole stack at once and reverses it, and the worker parks on an
 * eventcount when there is nothing to do. Callers that need to wait for
 * the worker park on a second eventcount, signalled after every item.
 *
 * Items are intrusive: the caller embeds an mpsc_stack_node_t in its own
 * allocation, and the work callback owns the item from then on (it
 * performs the work and frees it). Every item pushed before
 * rdp_worker_free() is processed before the thread exits.
 *
 * Without lock-free atomics there is no thread at all: rdp_worker_push()
 * runs the work on the calling thread. */

#include <boolean.h>
#include <retro_common_api.h>
#include <queues/mpsc_stack.h>

RETRO_BEGIN_DECLS

typedef struct rdp_worker rdp_worker_t;

/* Runs on the worker thread once, before any work. May be NULL. */
typedef void (*rdp_worker_start_t)(void *user);

/* Performs one item and releases it. */
typedef void (*rdp_worker_work_t)(void *user, mpsc_stack_node_t *item);

/* rdp_worker_wait()'s condition, evaluated on the waiting thread. */
typedef bool (*rdp_worker_pred_t)(void *ctx);

rdp_worker_t *rdp_worker_new(rdp_worker_start_t start,
      rdp_worker_work_t work, void *user);

/* Any thread. */
void rdp_worker_push(rdp_worker_t *w, mpsc_stack_node_t *item);

/* Blocks until pred(ctx) holds. pred is re-checked after every item the
 * worker finishes, so it must become true through the work callback. */
void rdp_worker_wait(rdp_worker_t *w, rdp_worker_pred_t pred, void *ctx);

/* Processes what is left, stops the thread and frees @w. No push may
 * race with this. Safe on NULL. */
void rdp_worker_free(rdp_worker_t *w);

RETRO_END_DECLS

#endif
