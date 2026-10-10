#pragma once

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

class APICommand {
public:
	virtual bool run() = 0;
};

// One API caller, one worker, and one synchronous command at a time.
// The caller retains the command (and its output pointers) until completion.
class RSPCommandThread {
public:
	RSPCommandThread() : m_thread(nullptr), m_command(nullptr) {
		retro_atomic_int_init(&m_pending, 0);
	}

	bool start() {
#if defined(RETRO_ATOMIC_LOCK_FREE)
		if (!retro_eventcount_init(&m_work))
			return false;
		if (!retro_eventcount_init(&m_done)) {
			retro_eventcount_free(&m_work);
			return false;
		}
		m_thread = sthread_create(worker, this);
		if (m_thread != nullptr)
			return true;
		retro_eventcount_free(&m_done);
		retro_eventcount_free(&m_work);
#endif
		return false;
	}

	void call(APICommand & command) {
		if (m_thread == nullptr) {
			command.run();
			return;
		}
		m_command = &command;
		retro_atomic_store_release_int(&m_pending, 1);
		retro_eventcount_notify(&m_work);
		wait(0, m_done);
		m_command = nullptr;
	}

	// Call a terminating command before joining; it releases the GL context.
	void join() {
		if (m_thread == nullptr)
			return;
		sthread_join(m_thread);
		m_thread = nullptr;
		retro_eventcount_free(&m_done);
		retro_eventcount_free(&m_work);
	}

private:
	RSPCommandThread(const RSPCommandThread &) = delete;
	RSPCommandThread & operator=(const RSPCommandThread &) = delete;

	void wait(int value, retro_eventcount_t & event) {
		while (retro_atomic_load_acquire_int(&m_pending) != value) {
			const int key = retro_eventcount_prepare_wait(&event);
			if (retro_atomic_load_acquire_int(&m_pending) == value)
				retro_eventcount_cancel_wait(&event);
			else
				retro_eventcount_commit_wait(&event, key);
		}
	}

	static void worker(void * data) {
		RSPCommandThread & self = *static_cast<RSPCommandThread *>(data);
		for (;;) {
			self.wait(1, self.m_work);
			const bool keepRunning = self.m_command->run();
			// No access to the command after publishing completion.
			retro_atomic_store_release_int(&self.m_pending, 0);
			retro_eventcount_notify(&self.m_done);
			if (!keepRunning)
				return;
		}
	}

	sthread_t * m_thread;
	APICommand * m_command;
	retro_atomic_int_t m_pending;
	retro_eventcount_t m_work;
	retro_eventcount_t m_done;
};
