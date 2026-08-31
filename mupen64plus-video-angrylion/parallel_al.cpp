#include "parallel_al.h"

#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

namespace
{
static inline void parallel_cpu_yield(void)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("pause");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    std::this_thread::yield();
#endif
}

static uint32_t clamp_worker_count(uint32_t count)
{
    if (count == 0)
        count = 1;
    return std::min(count, PARALLEL_MAX_WORKERS);
}

static uint32_t default_lane_count(uint32_t thread_count)
{
    uint32_t extra;

    if (thread_count <= 1)
        return 1;
    extra = (thread_count + 1) >> 1;
    return std::min(thread_count + extra, PARALLEL_MAX_WORKERS);
}

struct WorkQueue
{
    std::atomic<uint64_t> cursor;
    uint8_t padding[64];
    WorkQueue() : cursor(0) {}
};
}

class Parallel
{
public:
    typedef void (*Task)(uint32_t);

    Parallel(uint32_t num_threads, uint32_t num_workers) :
        m_accept_work(true),
        m_task(NULL),
        m_epoch(0),
        m_remaining(0),
        m_sleepers(0),
        m_num_threads(clamp_worker_count(num_threads)),
        m_num_workers(clamp_worker_count(num_workers))
    {
        if (m_num_threads > m_num_workers)
            m_num_threads = m_num_workers;

        for (uint32_t i = 1; i < m_num_threads; i++)
            m_threads.emplace_back(&Parallel::worker_main, this, i);
    }

    ~Parallel()
    {
        wait_for_completion();
        {
            std::lock_guard<std::mutex> lock(m_work_mutex);
            m_accept_work.store(false, std::memory_order_release);
            m_epoch.fetch_add(1, std::memory_order_release);
        }
        m_work_cv.notify_all();

        for (std::vector<std::thread>::iterator itr = m_threads.begin();
             itr != m_threads.end(); ++itr)
            itr->join();
    }

    void run(Task task)
    {
        if (!m_accept_work.load(std::memory_order_acquire))
            throw std::runtime_error("Workers are exiting and no longer accept work");

        if (m_num_workers == 1)
        {
            task(0);
            return;
        }
        uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(m_work_mutex);
            epoch = m_epoch.load(std::memory_order_relaxed) + 1;
            m_task = task;
            m_remaining.store(m_num_workers, std::memory_order_relaxed);
            for (uint32_t i = 0; i < m_num_threads; i++)
                m_queues[i].cursor.store(epoch << 8, std::memory_order_relaxed);
            m_epoch.store(epoch, std::memory_order_release);
        }

        if (m_sleepers.load(std::memory_order_relaxed) != 0)
            m_work_cv.notify_all();
        drain(0, epoch);
        wait_for_completion();
    }

    uint32_t num_workers() const
    {
        return m_num_workers;
    }

private:
    static const uint32_t kWorkerSpinCount = 2048;
    static const uint32_t kCallerSpinCount = 4096;

    std::atomic<bool> m_accept_work;
    Task m_task;
    std::atomic<uint64_t> m_epoch;
    std::atomic<uint32_t> m_remaining;
    std::atomic<uint32_t> m_sleepers;
    WorkQueue m_queues[PARALLEL_MAX_WORKERS];
    std::vector<std::thread> m_threads;
    std::mutex m_work_mutex;
    std::condition_variable m_work_cv;
    std::mutex m_done_mutex;
    std::condition_variable m_done_cv;
    uint32_t m_num_threads;
    const uint32_t m_num_workers;

    bool steal(uint32_t queue_id, uint64_t epoch, uint32_t *worker_id)
    {
        uint64_t cursor = m_queues[queue_id].cursor.load(std::memory_order_relaxed);

        for (;;)
        {
            uint32_t ticket;
            uint32_t lane;

            if ((cursor >> 8) != epoch)
                return false;
            ticket = static_cast<uint32_t>(cursor & 0xffu);
            lane = queue_id + ticket * m_num_threads;
            if (lane >= m_num_workers)
                return false;

            if (m_queues[queue_id].cursor.compare_exchange_weak(
                    cursor, cursor + 1, std::memory_order_relaxed,
                    std::memory_order_relaxed))
            {
                *worker_id = lane;
                return true;
            }
        }
    }

    bool find_work(uint32_t physical_id, uint64_t epoch, uint32_t *worker_id)
    {
        if (steal(physical_id, epoch, worker_id))
            return true;

        for (uint32_t n = 1; n < m_num_threads; n++)
        {
            uint32_t victim = physical_id + n;
            if (victim >= m_num_threads)
                victim -= m_num_threads;
            if (steal(victim, epoch, worker_id))
                return true;
        }
        return false;
    }

    void complete_one()
    {
        if (m_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            {
                std::lock_guard<std::mutex> lock(m_done_mutex);
            }
            m_done_cv.notify_one();
        }
    }

    void drain(uint32_t physical_id, uint64_t epoch)
    {
        uint32_t worker_id;
        while (find_work(physical_id, epoch, &worker_id))
        {
            m_task(worker_id);
            complete_one();
        }
    }

    uint64_t wait_for_next_epoch(uint64_t observed_epoch)
    {
        for (uint32_t i = 0; i < kWorkerSpinCount; i++)
        {
            uint64_t epoch = m_epoch.load(std::memory_order_acquire);
            if (epoch != observed_epoch || !m_accept_work.load(std::memory_order_relaxed))
                return epoch;
            parallel_cpu_yield();
        }

        std::unique_lock<std::mutex> lock(m_work_mutex);
        m_sleepers.fetch_add(1, std::memory_order_relaxed);
        m_work_cv.wait(lock, [this, observed_epoch] {
            return m_epoch.load(std::memory_order_acquire) != observed_epoch ||
                   !m_accept_work.load(std::memory_order_relaxed);
        });
        m_sleepers.fetch_sub(1, std::memory_order_relaxed);
        return m_epoch.load(std::memory_order_acquire);
    }

    void worker_main(uint32_t physical_id)
    {
        uint64_t observed_epoch = 0;

        for (;;)
        {
            uint64_t epoch = wait_for_next_epoch(observed_epoch);
            if (!m_accept_work.load(std::memory_order_acquire))
                break;
            observed_epoch = epoch;
            drain(physical_id, epoch);
        }
    }

    void wait_for_completion()
    {
        if (m_remaining.load(std::memory_order_acquire) == 0)
            return;
        for (uint32_t i = 0; i < kCallerSpinCount; i++)
        {
            if (m_remaining.load(std::memory_order_acquire) == 0)
                return;
            parallel_cpu_yield();
        }
        std::unique_lock<std::mutex> lock(m_done_mutex);
        m_done_cv.wait(lock, [this] {
            return m_remaining.load(std::memory_order_acquire) == 0;
        });
    }

    void operator=(const Parallel&) = delete;
    Parallel(const Parallel&) = delete;
};

static std::unique_ptr<Parallel> parallel;

void parallel_alinit(uint32_t num)
{
    uint32_t num_threads = num;
    uint32_t num_workers;
    uint32_t env_value;

    if (num_threads == 0)
        num_threads = std::thread::hardware_concurrency();
    num_threads = clamp_worker_count(num_threads);
    num_workers = default_lane_count(num_threads);
    num_workers = clamp_worker_count(num_workers);
    if (num_threads > num_workers)
        num_threads = num_workers;

    parallel.reset(new Parallel(num_threads, num_workers));
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
