#include "parallel_al.h"

#include <stdlib.h>

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>

class Parallel
{
public:
    Parallel(std::uint32_t num_workers) :
        m_num_workers(std::min(num_workers, PARALLEL_MAX_WORKERS))
    {
        // mask for m_tasks_done when all workers have finished their task
        // except for worker 0, which runs in the main thread
        if (m_num_workers == 64)
            m_all_tasks_done = (~(0LL)) & ~(1LL);
        else
            m_all_tasks_done = ((1LL << m_num_workers) - 1) & ~(1LL);

        // give workers an empty task
        m_task = [](std::uint32_t) {};
        m_accept_work = true;
        start_work();

        // create worker threads
        for (std::uint32_t worker_id = 1; worker_id < m_num_workers; worker_id++) {
            m_workers.emplace_back(std::thread(&Parallel::do_work, this, worker_id));
        }

        // synchronize workers to prepare them for real tasks
        wait();
    }

    ~Parallel() {
        // wait for all workers to finish their current work
        wait();

        // exit worker main loops
        m_accept_work = false;
        start_work();

        // join worker threads to make sure they have finished
        for (auto& thread : m_workers) {
            thread.join();
        }

        // destroy all worker threads
        m_workers.clear();
    }

    void run(std::function<void(std::uint32_t)>&& task) {
        // don't allow more tasks if workers are stopping
        if (!m_accept_work) {
            throw std::runtime_error("Workers are exiting and no longer accept work");
        }

        // single-worker mode has no one to synchronize with: worker 0 is
        // the calling thread, so the whole signal/wait protocol reduces
        // to a plain call
        if (m_num_workers <= 1) {
            task(0);
            return;
        }

        // prepare task for workers and send signal so they start working
        m_task = std::move(task);
        start_work();

        // run worker 0 directly on main thread
        m_task(0);

        // wait for all workers to finish
        wait();
    }

    std::uint32_t num_workers() {
        return m_num_workers;
    }

private:
    std::function<void(std::uint32_t)> m_task;
    std::vector<std::thread> m_workers;
    std::mutex m_signal_mutex;
    std::condition_variable m_signal_work;
    std::condition_variable m_signal_done;
    std::atomic<uint64_t> m_tasks_done;
    std::uint64_t m_all_tasks_done;
    std::atomic<bool> m_accept_work;
    const std::uint32_t m_num_workers;

    void start_work() {
        std::unique_lock<std::mutex> ul(m_signal_mutex);

        // clear task bits for all workers
        m_tasks_done = 0;

        // wake up all workers
        m_signal_work.notify_all();
    }

    void do_work(std::uint32_t worker_id) {
        const std::uint64_t worker_mask = 1LL << worker_id;

        while (m_accept_work) {
            // do the work
            m_task(worker_id);

            // mark task as done; m_tasks_done is atomic, so the bit can
            // be set without the mutex. Only the worker whose bit
            // completes the set wakes the main thread - the others used
            // to issue a redundant notify (and a futex wake) each.
            if ((m_tasks_done.fetch_or(worker_mask) | worker_mask)
                    == m_all_tasks_done) {
                // the empty lock pairs with wait()'s predicate check so
                // the notify cannot fall between the main thread's check
                // and its sleep (the classic lost-wakeup window)
                { std::unique_lock<std::mutex> ul(m_signal_mutex); }
                m_signal_done.notify_one();
            }

            // if the next task already started (our bit was cleared),
            // skip the sleep entirely; otherwise wait as before
            if ((m_tasks_done.load() & worker_mask) != 0) {
                std::unique_lock<std::mutex> ul(m_signal_mutex);
                m_signal_work.wait(ul, [worker_mask, this] {
                    return (m_tasks_done & worker_mask) == 0;
                });
            }
        }
    }

    void wait() {
        // fast path: all workers already finished, no need to lock
        if (m_tasks_done.load() == m_all_tasks_done)
            return;

        // wait for all workers to set their task bits
        std::unique_lock<std::mutex> ul(m_signal_mutex);
        m_signal_done.wait(ul, [this] {
            return m_tasks_done == m_all_tasks_done;
        });
    }

    void operator=(const Parallel&) = delete;
    Parallel(const Parallel&) = delete;
};

// C interface for the Parallel class
static std::unique_ptr<Parallel> parallel;

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

void parallel_alinit(uint32_t num)
{
    // auto-select number of workers based on the number of cores
    if (num == 0) {
        const char *env = getenv("ANGRYLION_NUM_THREADS");
        if (env)
            num = atoi(env);
        else
            num = std::thread::hardware_concurrency();
    }

    parallel = make_unique<Parallel>(num);
}

void parallel_run(void task(uint32_t))
{
    parallel->run(task);
}

uint32_t parallel_num_workers(void)
{
    return parallel->num_workers();
}

void parallel_close(void)
{
    parallel.reset();
}
