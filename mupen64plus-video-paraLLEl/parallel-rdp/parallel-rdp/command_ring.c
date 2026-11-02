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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "command_ring.h"
#include "thread_id.h"

/* Op::MetaIdle, rdp_common.hpp */
#define RDP_COMMAND_RING_IDLE_OP 3u

struct rdp_command_ring
{
	rdp_command_ring_run_t run;
	void *user;
	sthread_t *thr;
	uint32_t *ring;
	size_t ring_size;               /* a power of two */
	uint32_t *wrap_words;           /* the consumer's copy of a wrapped command */
	size_t wrap_capacity;
	retro_atomic_size_t write_count;     /* written by the producer */
	retro_atomic_size_t read_count;      /* written by the consumer */
	retro_atomic_size_t completed_count; /* written by the consumer */
	retro_eventcount_t work_ec;          /* consumer parks: ring empty */
	retro_eventcount_t done_ec;          /* producer parks: ring full / drain */
};

static size_t ring_load(retro_atomic_size_t *v)
{
	return retro_atomic_load_acquire_size(v);
}

static void command_ring_thread(void *data)
{
	rdp_command_ring_t *cr = (rdp_command_ring_t *)data;
	static const uint32_t idle_words[1] = { RDP_COMMAND_RING_IDLE_OP << 24 };
	size_t mask = cr->ring_size - 1;
	size_t r = 0;

	granite_register_thread_index(0);

	for (;;)
	{
		bool is_idle = false;
		bool in_place = false;
		const uint32_t *words = idle_words;
		uint32_t num_words = 1;
		bool have_work = ring_load(&cr->write_count) != r;

		if (!have_work)
		{
			int key = retro_eventcount_prepare_wait(&cr->work_ec);
			have_work = ring_load(&cr->write_count) != r;
			if (have_work)
				retro_eventcount_cancel_wait(&cr->work_ec);
			else
			{
				/* Without a steady flow of commands, tell the processor,
				 * so it can kick off the work it has. */
				retro_eventcount_commit_wait_timeout(&cr->work_ec, key, 500);
				have_work = ring_load(&cr->write_count) != r;
			}
		}

		if (have_work)
		{
			num_words = cr->ring[r++ & mask];
			if ((r & mask) + num_words <= cr->ring_size)
			{
				/* In one piece: run where it is, and hand the space back
				 * once it has run. */
				words = cr->ring + (r & mask);
				r += num_words;
				in_place = true;
			}
			else
			{
				size_t at = r & mask;
				size_t first = cr->ring_size - at;
				if (num_words > cr->wrap_capacity)
				{
					uint32_t *grown = (uint32_t *)realloc(cr->wrap_words, num_words * sizeof(uint32_t));
					if (!grown)
						abort();
					cr->wrap_words = grown;
					cr->wrap_capacity = num_words;
				}
				memcpy(cr->wrap_words, cr->ring + at, first * sizeof(uint32_t));
				memcpy(cr->wrap_words + first, cr->ring, (num_words - first) * sizeof(uint32_t));
				r += num_words;
				/* The words are copied out: hand the space back right away. */
				retro_atomic_store_release_size(&cr->read_count, r);
				words = cr->wrap_words;
			}
		}
		else
			is_idle = true;

		if (!num_words)
			break;

		cr->run(cr->user, num_words, words);
		if (in_place)
			retro_atomic_store_release_size(&cr->read_count, r);
		if (!is_idle)
		{
			retro_atomic_store_release_size(&cr->completed_count, r);
			retro_eventcount_notify(&cr->done_ec);
		}
	}
}

rdp_command_ring_t *rdp_command_ring_new(rdp_command_ring_run_t run, void *user, unsigned words)
{
	rdp_command_ring_t *cr;

	assert(words && (words & (words - 1)) == 0);
	cr = (rdp_command_ring_t *)calloc(1, sizeof(*cr));
	if (!cr)
		return NULL;
	cr->run = run;
	cr->user = user;
	cr->ring = (uint32_t *)malloc(words * sizeof(uint32_t));
	cr->ring_size = words;
	retro_atomic_size_init(&cr->write_count, 0);
	retro_atomic_size_init(&cr->read_count, 0);
	retro_atomic_size_init(&cr->completed_count, 0);
	/* the eventcounts fail only on backends that need a condvar and
	 * cannot allocate one */
	if (!cr->ring)
		goto fail;
	if (!retro_eventcount_init(&cr->work_ec))
		goto fail;
	if (!retro_eventcount_init(&cr->done_ec))
	{
		retro_eventcount_free(&cr->work_ec);
		goto fail;
	}
	cr->thr = sthread_create(command_ring_thread, cr);
	if (!cr->thr)
	{
		retro_eventcount_free(&cr->work_ec);
		retro_eventcount_free(&cr->done_ec);
		goto fail;
	}
	return cr;

fail:
	free(cr->ring);
	free(cr);
	return NULL;
}

void rdp_command_ring_free(rdp_command_ring_t *cr)
{
	if (!cr)
		return;
	/* an empty command stops the thread, after everything before it */
	rdp_command_ring_enqueue(cr, 0, NULL);
	sthread_join(cr->thr);
	retro_eventcount_free(&cr->work_ec);
	retro_eventcount_free(&cr->done_ec);
	free(cr->ring);
	free(cr->wrap_words);
	free(cr);
}

void rdp_command_ring_drain(rdp_command_ring_t *cr)
{
	size_t w = retro_atomic_load_relaxed_size(&cr->write_count);
	for (;;)
	{
		int key;
		if (ring_load(&cr->completed_count) == w)
			return;
		key = retro_eventcount_prepare_wait(&cr->done_ec);
		if (ring_load(&cr->completed_count) == w)
		{
			retro_eventcount_cancel_wait(&cr->done_ec);
			return;
		}
		retro_eventcount_commit_wait(&cr->done_ec, key);
	}
}

void rdp_command_ring_enqueue(rdp_command_ring_t *cr, unsigned num_words, const uint32_t *words)
{
	size_t size = cr->ring_size;
	size_t mask = size - 1;
	size_t w = retro_atomic_load_relaxed_size(&cr->write_count);
	size_t at, i;

	/* Wait for room for the header word plus the payload. */
	for (;;)
	{
		int key;
		if ((w - ring_load(&cr->read_count)) + num_words + 1 <= size)
			break;
		key = retro_eventcount_prepare_wait(&cr->done_ec);
		if ((w - ring_load(&cr->read_count)) + num_words + 1 <= size)
		{
			retro_eventcount_cancel_wait(&cr->done_ec);
			break;
		}
		retro_eventcount_commit_wait(&cr->done_ec, key);
	}

	/* The header, then the payload: word by word when it is short or
	 * wraps, where a call costs more than the copy, and in one memcpy
	 * past that. */
	cr->ring[w++ & mask] = num_words;
	at = w & mask;
	if (num_words <= 8 || at + num_words > size)
	{
		for (i = 0; i < num_words; i++)
			cr->ring[(w + i) & mask] = words[i];
	}
	else
		memcpy(cr->ring + at, words, num_words * sizeof(uint32_t));
	w += num_words;

	retro_atomic_store_release_size(&cr->write_count, w);
	retro_eventcount_notify(&cr->work_ec);
}
