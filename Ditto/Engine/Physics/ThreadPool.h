// ThreadPool.h
#pragma once
#include <vector>
#include <thread>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 向线程池提交一个任务，任务会被分配到某个工作线程的本地队列
    void enqueue(std::function<void()> task);

    // 并行执行 [start, end) 范围内的 func(i)，等待所有任务完成
    void parallel_for(size_t start, size_t end, std::function<void(size_t)> func);

    size_t thread_count() const { return workers.size(); }

private:
    // 每个工作线程的本地数据结构
    struct Worker {
        std::deque<std::function<void()>> tasks;   // 任务队列（双端，用于窃取）
        std::mutex mutex;                           // 保护队列的互斥锁
    };

    std::vector<std::unique_ptr<Worker>> workers;   // 所有工作线程的本地数据
    std::vector<std::thread> threads;                // 工作线程
    std::atomic<bool> stop;                           // 停止标志
    std::atomic<size_t> next_thread_index;            // 用于轮询分配任务的索引

    std::mutex cv_mutex;                              // 与条件变量配合的互斥锁
    std::condition_variable cv;                        // 全局条件变量，用于通知有新任务

    // 检查所有队列是否有任务（用于条件变量等待）
    bool has_any_task() const;

    // 工作线程主循环
    void worker_loop(size_t myIndex);
};