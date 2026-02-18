#pragma once
#include <vector>
#include <thread>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

struct ThreadPool 
{
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task);

    void parallel_for(size_t start, size_t end, std::function<void(size_t)> func);

    size_t thread_count() const { return workers.size(); }

private:
    struct Worker 
    {
        std::deque<std::function<void()>> tasks;
        std::mutex mutex;
    };

    std::vector<std::unique_ptr<Worker>> workers;
    std::vector<std::thread> threads;
    std::atomic<bool> stop;
    std::atomic<size_t> next_thread_index;

    std::mutex cv_mutex;
    std::condition_variable cv;

    bool has_any_task() const;

    void worker_loop(size_t myIndex);
};