// SPDX-License-Identifier: MIT

// A tool's jobs spread over threads: RunParallel.

#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace xsflib::cli
{

// Runs `work(i)` once for each i below `count`, on up to `threads` threads: the caller's thread and up to `threads` - 1
// more. The i start in increasing order, each on the next thread that is free. `work` must be safe to call from several
// threads at once. When a thread can't start, the threads that run do the rest. When `work` throws, no further i
// starts. RunParallel then rethrows the first exception once every thread has stopped.
template <typename Work>
void RunParallel(std::size_t count, unsigned threads, const Work& work)
{
    std::atomic<std::size_t> next{0};
    std::mutex failure_mutex;
    std::exception_ptr failure;
    const auto worker = [&]
    {
        try
        {
            for (std::size_t i; (i = next++) < count;)
            {
                work(i);
            }
        }
        catch (...)
        {
            next = count; // The other threads take no more work.
            const std::lock_guard lock(failure_mutex);
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
    };

    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads && t < count; t++)
    {
        // If a thread can't start (the system is out of threads or memory), the threads that started do the work.
        try
        {
            pool.emplace_back(worker);
        }
        catch (const std::exception&)
        {
            break;
        }
    }

    worker();

    for (std::thread& thread : pool)
    {
        thread.join();
    }

    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

} // namespace xsflib::cli
