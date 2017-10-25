#include "parallel_c.hpp"
#include "common.h"

#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

thread_local Worker* parallel_worker;

class Parallel
{
   friend class Worker;

public:
   Parallel(uint32_t num_workers);
   ~Parallel();
   void run(std::function<void(void)>&& task);

private:
   std::function<void()> m_task;
   std::vector<Worker> m_workers;
   std::mutex m_mutex;
   std::condition_variable m_signal_work;
   std::condition_variable m_signal_done;
   std::atomic_size_t m_workers_active;
   std::atomic_bool m_accept_work{true};

   void do_work();
   void wait();

   void operator=(const Parallel&) = delete;
   Parallel(const Parallel&) = delete;
};

Worker::Worker(uint32_t id, Parallel *parallel) :
   m_thread(&Parallel::do_work, parallel)
   ,m_worker_id(id)
{
}

static std::unique_ptr<Parallel> parallel;
static uint32_t worker_num;

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

Parallel::Parallel(uint32_t num_workers)
{
   uint32_t worker_id;

   std::unique_lock<std::mutex> ul(m_mutex);

   // create worker threads
   for (worker_id = 0; worker_id < num_workers; worker_id++)
   {
      m_workers.emplace_back(Worker(worker_id, this));
   }

   parallel_worker = &m_workers[0];
}

Parallel::~Parallel()
{
   // wait for all workers to finish their current work
   wait();

   // set a dummy task in case nothing has been done yet
   if (!m_task)
      m_task = []() {};

   // exit worker main loops
   m_accept_work = false;
   m_signal_work.notify_all();

   // join worker threads to make sure they have finished
   for (auto& thread : m_workers)
      thread.m_thread.join();

   // destroy all worker threads
   m_workers.clear();
}

void Parallel::run(std::function<void(void)>&& task)
{
   // don't allow more tasks if workers are stopping
   if (!m_accept_work)
      throw std::runtime_error("Workers are exiting and no longer accept work");

   // prepare task for workers and send signal so they start working
   m_task           = task;
   m_workers_active = m_workers.size();
   m_signal_work.notify_all();

   // wait for all workers to finish
   wait();
}

void Parallel::do_work()
{
   std::unique_lock<std::mutex> ul(m_mutex);

   for (auto& thread : m_workers)
      if (thread.m_thread.get_id() == std::this_thread::get_id())
         parallel_worker = &thread;

   while (m_accept_work)
   {
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
   while (m_workers_active)
      m_signal_done.wait(ul);
}

void parallel_alinit(uint32_t num)
{
    parallel_worker = NULL;

    // auto-select number of workers based on the number of cores
    if (num == 0)
        num = std::thread::hardware_concurrency();

    parallel = make_unique<Parallel>(num);

    worker_num = num;
}

void parallel_run(void task(void))
{
    parallel->run(task);
}

uint32_t parallel_worker_num(void)
{
    return worker_num;
}

void parallel_close(void)
{
    parallel.reset();
}
