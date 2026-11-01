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

#include "command_ring.hpp"
#include "rdp_device.hpp"
#include "thread_id.hpp"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

namespace RDP
{
static size_t ring_load(retro_atomic_size_t *v)
{
	return retro_atomic_load_acquire_size(v);
}

bool CommandRing::init(
#ifdef PARALLEL_RDP_SHADER_DIR
		Granite::Global::GlobalManagersHandle global_handles_,
#endif
		CommandProcessor *processor_, unsigned count)
{
	assert((count & (count - 1)) == 0);
	teardown_thread();
	processor = processor_;
	if (ring_size != count)
	{
		uint32_t *words = static_cast<uint32_t *>(realloc(ring, count * sizeof(uint32_t)));
		if (!words)
			return false;
		ring = words;
		ring_size = count;
	}
	retro_atomic_size_init(&write_count, 0);
	retro_atomic_size_init(&read_count, 0);
	retro_atomic_size_init(&completed_count, 0);
	if (!ec_live)
	{
		// Only fails on backends that need a condvar and cannot
		// allocate one.
		if (!retro_eventcount_init(&work_ec) || !retro_eventcount_init(&done_ec))
		{
			retro_eventcount_free(&work_ec);
			retro_eventcount_free(&done_ec);
			return false;
		}
		ec_live = true;
	}
#ifdef PARALLEL_RDP_SHADER_DIR
	global_handles = rdp_move(global_handles_);
#endif
	thr = sthread_create(thread_entry, this);
	return thr != nullptr;
}

void CommandRing::thread_entry(void *self)
{
	static_cast<CommandRing *>(self)->thread_loop();
}

void CommandRing::teardown_thread()
{
	if (thr)
	{
		enqueue_command(0, nullptr);
		sthread_join(thr);
		thr = nullptr;
	}
}

CommandRing::~CommandRing()
{
	teardown_thread();
	if (ec_live)
	{
		retro_eventcount_free(&work_ec);
		retro_eventcount_free(&done_ec);
	}
	free(ring);
	free(wrap_words);
}

void CommandRing::drain()
{
	size_t w = retro_atomic_load_relaxed_size(&write_count);
	for (;;)
	{
		int key;
		if (ring_load(&completed_count) == w)
			return;
		key = retro_eventcount_prepare_wait(&done_ec);
		if (ring_load(&completed_count) == w)
		{
			retro_eventcount_cancel_wait(&done_ec);
			return;
		}
		retro_eventcount_commit_wait(&done_ec, key);
	}
}

void CommandRing::enqueue_command(unsigned num_words, const uint32_t *words)
{
	size_t size = ring_size;
	size_t mask = size - 1;
	size_t w = retro_atomic_load_relaxed_size(&write_count);
	size_t at, first;

	// Wait for room for the header word plus the payload.
	for (;;)
	{
		int key;
		if ((w - ring_load(&read_count)) + num_words + 1 <= size)
			break;
		key = retro_eventcount_prepare_wait(&done_ec);
		if ((w - ring_load(&read_count)) + num_words + 1 <= size)
		{
			retro_eventcount_cancel_wait(&done_ec);
			break;
		}
		retro_eventcount_commit_wait(&done_ec, key);
	}

	// The header, then the payload: word by word when it is short or
	// wraps, where a call costs more than the copy, and in one memcpy
	// past that.
	ring[w++ & mask] = num_words;
	at = w & mask;
	if (num_words <= 8 || at + num_words > size)
	{
		for (first = 0; first < num_words; first++)
			ring[(w + first) & mask] = words[first];
	}
	else
		memcpy(ring + at, words, num_words * sizeof(uint32_t));
	w += num_words;

	retro_atomic_store_release_size(&write_count, w);
	retro_eventcount_notify(&work_ec);
}

void CommandRing::thread_loop()
{
	Util::register_thread_index(0);

#ifdef PARALLEL_RDP_SHADER_DIR
	// Here to let the RDP play nice with full Granite.
	// When we move to standalone Granite, we won't need to interact with global subsystems like this.
	Granite::Global::set_thread_context(*global_handles);
	global_handles.reset();
#endif

	size_t mask = ring_size - 1;
	size_t r = 0;
	static const uint32_t idle_words[1] = { uint32_t(Op::MetaIdle) << 24 };

	for (;;)
	{
		bool is_idle = false;
		bool in_place = false;
		const uint32_t *words = idle_words;
		uint32_t num_words = 1;
		bool have_work = ring_load(&write_count) != r;

		if (!have_work)
		{
			int key = retro_eventcount_prepare_wait(&work_ec);
			have_work = ring_load(&write_count) != r;
			if (have_work)
				retro_eventcount_cancel_wait(&work_ec);
			else
			{
				// If we don't receive commands at a steady pace,
				// notify rendering thread that we should probably kick some work.
				retro_eventcount_commit_wait_timeout(&work_ec, key, 500);
				have_work = ring_load(&write_count) != r;
			}
		}

		if (have_work)
		{
			num_words = ring[r++ & mask];
			if ((r & mask) + num_words <= ring_size)
			{
				// In one piece: run where it is, and hand the space back
				// once it has run.
				words = ring + (r & mask);
				r += num_words;
				in_place = true;
			}
			else
			{
				size_t at = r & mask;
				size_t first = ring_size - at;
				if (num_words > wrap_capacity)
				{
					uint32_t *grown = static_cast<uint32_t *>(realloc(wrap_words, num_words * sizeof(uint32_t)));
					if (!grown)
						abort();
					wrap_words = grown;
					wrap_capacity = num_words;
				}
				memcpy(wrap_words, ring + at, first * sizeof(uint32_t));
				memcpy(wrap_words + first, ring, (num_words - first) * sizeof(uint32_t));
				r += num_words;
				// The words are copied out: hand the space back right away.
				retro_atomic_store_release_size(&read_count, r);
				words = wrap_words;
			}
		}
		else
			is_idle = true;

		if (!num_words)
			break;

		processor->enqueue_command_direct(num_words, words);
		if (in_place)
			retro_atomic_store_release_size(&read_count, r);
		if (!is_idle)
		{
			retro_atomic_store_release_size(&completed_count, r);
			retro_eventcount_notify(&done_ec);
		}
	}
}
}
