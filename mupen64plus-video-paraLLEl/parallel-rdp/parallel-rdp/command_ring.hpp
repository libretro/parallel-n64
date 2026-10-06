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

#pragma once

#include <rthreads/rthreads.h>
#include <vector>
#include <stdint.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

#ifdef PARALLEL_RDP_SHADER_DIR
#include "global_managers.hpp"
#endif

namespace RDP
{
class CommandProcessor;
class CommandRing
{
public:
	// Returns false if the ring cannot run; the caller then processes
	// commands on its own thread.
	bool init(
#ifdef PARALLEL_RDP_SHADER_DIR
			Granite::Global::GlobalManagersHandle global_handles,
#endif
			CommandProcessor *processor, unsigned count);
	~CommandRing();
	void drain();
	// Stops and joins the command thread; safe to call more than once.
	void teardown_thread();

	void enqueue_command(unsigned num_words, const uint32_t *words);

private:
	CommandProcessor *processor = nullptr;
	sthread_t *thr = nullptr;

	// Single producer (the emulation thread), single consumer (thr).
	// The counters are free-running and compared by difference, so they
	// may wrap. No lock: each side owns its own counter and publishes it
	// with a release store; the eventcounts only park and wake.
	std::vector<uint32_t> ring;
	retro_atomic_size_t write_count{0};     // written by the producer
	retro_atomic_size_t read_count{0};      // written by the consumer
	retro_atomic_size_t completed_count{0}; // written by the consumer
	retro_eventcount_t work_ec{};        // consumer parks: ring empty
	retro_eventcount_t done_ec{};        // producer parks: ring full / drain
	bool ec_live = false;

	void thread_loop();
	static void thread_entry(void *self);
#ifdef PARALLEL_RDP_SHADER_DIR
	Granite::Global::GlobalManagersHandle global_handles;
#endif
};
}
