#include "parallel_c.hpp"
#include "parallel.hpp"

#include <atomic>


static std::unique_ptr<Parallel> parallel;
static thread_local uint32_t worker_id;
static uint32_t worker_num;

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

void parallel_alinit(uint32_t num)
{
    // auto-select number of workers based on the number of cores
    if (num == 0)
        num = std::thread::hardware_concurrency();

    parallel = make_unique<Parallel>(num, [](uint32_t id) {
        worker_id = id;
    });

    worker_num = num;
}

void parallel_run(void task(void))
{
    parallel->run(task);
}

uint32_t parallel_worker_id(void)
{
    return worker_id;
}

uint32_t parallel_worker_num(void)
{
    return worker_num;
}

void parallel_close(void)
{
    parallel.reset();
}
