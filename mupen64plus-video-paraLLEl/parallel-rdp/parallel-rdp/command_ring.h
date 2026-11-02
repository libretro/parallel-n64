/* Copyright (c) 2020 Themaister
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

#ifndef RDP_COMMAND_RING_H
#define RDP_COMMAND_RING_H

#include <stdint.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* RDP commands handed from the emulation thread to a command thread.
 *
 * Single producer, single consumer, no lock: the counters are
 * free-running and compared by difference, so they may wrap; each side
 * owns its own and publishes it with a release store, and the
 * eventcounts only park and wake. */
typedef struct rdp_command_ring rdp_command_ring_t;

/* Runs one command on the command thread. Without commands for a while
 * it is also called with the single word MetaIdle << 24. */
typedef void (*rdp_command_ring_run_t)(void *user, unsigned num_words, const uint32_t *words);

/* words: a power of two. NULL if the thread cannot start; the caller then
 * runs commands on its own thread. */
rdp_command_ring_t *rdp_command_ring_new(rdp_command_ring_run_t run, void *user, unsigned words);

/* Runs what is queued, joins the thread, frees the ring. Safe on NULL. */
void rdp_command_ring_free(rdp_command_ring_t *ring);

void rdp_command_ring_enqueue(rdp_command_ring_t *ring, unsigned num_words, const uint32_t *words);

/* Returns once every command queued so far has run. */
void rdp_command_ring_drain(rdp_command_ring_t *ring);

RETRO_END_DECLS

#endif
