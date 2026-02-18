#include "ThreadPool.h"
#include <algorithm>
#include <cassert>

ThreadPool::ThreadPool(size_t numThreads) : stop(false), next_thread_index(0) 
{
    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) 
        workers.push_back(std::make_unique<Worker>());

    threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) 
        threads.emplace_back(&ThreadPool::worker_loop, this, i);
}

ThreadPool::~ThreadPool() 
{
    stop = true;
    cv.notify_all();
    for (auto& t : threads) if (t.joinable()) t.join();
}

void ThreadPool::enqueue(std::function<void()> task) 
{
    size_t target = next_thread_index.fetch_add(1, std::memory_order_relaxed) % workers.size();
    {
        std::unique_lock lock(workers[target]->mutex);
        workers[target]->tasks.push_back(std::move(task));
    }
    cv.notify_one();
}

void ThreadPool::parallel_for(size_t start, size_t end, std::function<void(size_t)> func) 
{
    if (start >= end) return;
    size_t total = end - start;
    size_t numThreads = workers.size();
    size_t chunkSize = (total + numThreads - 1) / numThreads; 

    std::atomic<size_t> remaining(0);
    std::mutex local_mutex;
    std::condition_variable local_cv;

    for (size_t t = 0; t < numThreads; ++t) {
        size_t blockStart = start + t * chunkSize;
        size_t blockEnd = std::min(blockStart + chunkSize, end);
        if (blockStart >= blockEnd) break;

        remaining++;
        enqueue([this, blockStart, blockEnd, &func, &remaining, &local_cv, &local_mutex]() {
            for (size_t i = blockStart; i < blockEnd; ++i) {
                func(i);
            }
            std::unique_lock<std::mutex> lock(local_mutex);
            if (--remaining == 0) {
                local_cv.notify_one();
            }
            });
    }

    std::unique_lock<std::mutex> lock(local_mutex);
    local_cv.wait(lock, [&remaining] { return remaining == 0; });
}

bool ThreadPool::has_any_task() const 
{
    for (const auto& worker : workers) {
        std::unique_lock lock(worker->mutex);
        if (!worker->tasks.empty()) {
            return true;
        }
    }
    return false;
}

void ThreadPool::worker_loop(size_t myIndex) 
{
    Worker& myWorker = *workers[myIndex];

    while (!stop) 
    {
        std::function<void()> task;
        {
            std::unique_lock lock(myWorker.mutex, std::try_to_lock);
            if (lock.owns_lock() && !myWorker.tasks.empty()) {
                task = std::move(myWorker.tasks.front());
                myWorker.tasks.pop_front();
            }
        }

        if (!task) {
            size_t numWorkers = workers.size();
            for (size_t offset = 1; offset < numWorkers; ++offset) {
                size_t victimIdx = (myIndex + offset) % numWorkers;
                Worker& victim = *workers[victimIdx];
                std::unique_lock lock(victim.mutex, std::try_to_lock);
                if (lock.owns_lock() && !victim.tasks.empty()) {
                    task = std::move(victim.tasks.back());
                    victim.tasks.pop_back();
                    break;
                }
            }
        }

        if (task)  task();
        else 
        {
            std::unique_lock<std::mutex> lock(cv_mutex);
            cv.wait(lock, [this] { return stop || has_any_task(); });
        }
    }
}