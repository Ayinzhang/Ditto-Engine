// ThreadPool.cpp
#include "ThreadPool.h"
#include <algorithm>
#include <cassert>

ThreadPool::ThreadPool(size_t numThreads) : stop(false), next_thread_index(0) {
    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers.push_back(std::make_unique<Worker>());
    }

    threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        threads.emplace_back(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    stop = true;
    cv.notify_all(); // 唤醒所有可能等待的线程
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    // 轮询选择一个目标线程的本地队列
    size_t target = next_thread_index.fetch_add(1, std::memory_order_relaxed) % workers.size();
    {
        std::unique_lock lock(workers[target]->mutex);
        workers[target]->tasks.push_back(std::move(task));
    }
    cv.notify_one(); // 唤醒一个等待线程
}

void ThreadPool::parallel_for(size_t start, size_t end, std::function<void(size_t)> func) {
    if (start >= end) return;
    size_t total = end - start;
    size_t numThreads = workers.size();
    size_t chunkSize = (total + numThreads - 1) / numThreads; // 简单分块

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

    // 等待所有块完成
    std::unique_lock<std::mutex> lock(local_mutex);
    local_cv.wait(lock, [&remaining] { return remaining == 0; });
}

bool ThreadPool::has_any_task() const {
    for (const auto& worker : workers) {
        std::unique_lock lock(worker->mutex);
        if (!worker->tasks.empty()) {
            return true;
        }
    }
    return false;
}

void ThreadPool::worker_loop(size_t myIndex) {
    Worker& myWorker = *workers[myIndex];

    while (!stop) {
        std::function<void()> task;

        // 1. 尝试从本地队列头部取任务（LIFO 策略，提高缓存局部性）
        {
            std::unique_lock lock(myWorker.mutex, std::try_to_lock);
            if (lock.owns_lock() && !myWorker.tasks.empty()) {
                task = std::move(myWorker.tasks.front());
                myWorker.tasks.pop_front();
            }
        }

        // 2. 如果本地队列为空，尝试从其他线程的队列尾部窃取任务
        if (!task) {
            size_t numWorkers = workers.size();
            for (size_t offset = 1; offset < numWorkers; ++offset) {
                size_t victimIdx = (myIndex + offset) % numWorkers;
                Worker& victim = *workers[victimIdx];
                std::unique_lock lock(victim.mutex, std::try_to_lock);
                if (lock.owns_lock() && !victim.tasks.empty()) {
                    task = std::move(victim.tasks.back()); // 从尾部窃取
                    victim.tasks.pop_back();
                    break;
                }
            }
        }

        // 3. 执行任务
        if (task) {
            task();
        }
        else {
            // 4. 无任务，等待条件变量
            std::unique_lock<std::mutex> lock(cv_mutex);
            cv.wait(lock, [this] { return stop || has_any_task(); });
        }
    }
}