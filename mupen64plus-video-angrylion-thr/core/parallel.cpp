#include "parallel.hpp"

Parallel::Parallel(uint32_t num_workers, std::function<void(uint32_t)>&& func_worker_id)
{
    // create worker threads
    for (uint32_t worker_id = 0; worker_id < num_workers; worker_id++) {
        m_workers.emplace_back(std::thread(&Parallel::do_work, this, worker_id, func_worker_id));
    }
}

Parallel::~Parallel()
{
    // wait for all workers to finish their current work
    wait();

    // set a dummy task in case nothing has been done yet
    if (!m_task) {
        m_task = []() {};
    }

    // exit worker main loops
    m_accept_work = false;
    m_signal_work.notify_all();

    // join worker threads to make sure they have finished
    for (auto& thread : m_workers) {
        thread.join();
    }

    // destroy all worker threads
    m_workers.clear();
}

void Parallel::run(std::function<void(void)>&& task)
{
    // don't allow more tasks if workers are stopping
    if (!m_accept_work) {
        throw std::runtime_error("Workers are exiting and no longer accept work");
    }

    // prepare task for workers and send signal so they start working
    m_task = task;
    m_workers_active = m_workers.size();
    m_signal_work.notify_all();

    // wait for all workers to finish
    wait();
}

void Parallel::do_work(int32_t worker_id, std::function<void(uint32_t)>&& func_worker_id)
{
    std::unique_lock<std::mutex> ul(m_mutex);
    func_worker_id(worker_id);
    while (m_accept_work) {
        m_signal_work.wait(ul);
        ul.unlock();
        m_task();
        ul.lock();
        m_workers_active--;
        m_signal_done.notify_all();
    }
}

void Parallel::wait()
{
    std::unique_lock<std::mutex> ul(m_mutex);
    while (m_workers_active) {
        m_signal_done.wait(ul);
    }
}
