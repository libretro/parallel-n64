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

#include <chrono>
#include "command_ring.hpp"
#include "rdp_device.hpp"
#include "thread_id.hpp"
#include <assert.h>

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
	ring.resize(count);
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
	global_handles = std::move(global_handles_);
#endif
	thr = std::thread(&CommandRing::thread_loop, this);
	return true;
}

void CommandRing::teardown_thread()
{
	if (thr.joinable())
	{
		enqueue_command(0, nullptr);
		thr.join();
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
	size_t size = ring.size();
	size_t mask = size - 1;
	size_t w = retro_atomic_load_relaxed_size(&write_count);

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

	ring[w++ & mask] = num_words;
	for (unsigned i = 0; i < num_words; i++)
		ring[w++ & mask] = words[i];

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

	std::vector<uint32_t> tmp_buffer;
	tmp_buffer.reserve(64);
	size_t mask = ring.size() - 1;
	size_t r = 0;

	for (;;)
	{
		bool is_idle = false;
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
			uint32_t num_words = ring[r++ & mask];
			tmp_buffer.resize(num_words);
			for (uint32_t i = 0; i < num_words; i++)
				tmp_buffer[i] = ring[r++ & mask];
			// The words are copied out: hand the space back right away.
			retro_atomic_store_release_size(&read_count, r);
		}
		else
		{
			tmp_buffer.resize(1);
			tmp_buffer[0] = uint32_t(Op::MetaIdle) << 24;
			is_idle = true;
		}

		if (tmp_buffer.empty())
			break;

		processor->enqueue_command_direct(tmp_buffer.size(), tmp_buffer.data());
		if (!is_idle)
		{
			retro_atomic_store_release_size(&completed_count, r);
			retro_eventcount_notify(&done_ec);
		}
	}
}
}
