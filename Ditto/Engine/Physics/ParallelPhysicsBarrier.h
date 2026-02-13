#pragma once
#include "Physics.h"
#include <vector>
#include <thread>
#include <barrier>
#include <atomic>
#include <condition_variable>
#include <mutex>

class ParallelPhysicsBarrier;

struct PhysicsBarrierAction {
    ParallelPhysicsBarrier* engine;
    void operator()() noexcept;
};

struct ThreadLocalCache {
    std::vector<std::pair<Collider*, Collider*>> pairs;
    std::vector<CollisionData> data;
};

class ParallelPhysicsBarrier : public Physics {
public:
    ParallelPhysicsBarrier(uint32_t threadCount = std::thread::hardware_concurrency());
    ~ParallelPhysicsBarrier();

    void UpdatePhysics(float dt) override;
    void OnBarrierPhaseComplete() noexcept;

private:
    void WorkerLoop(uint32_t threadIndex);
    void ParallelIntegrate(uint32_t threadIndex);
    void ParallelBroadPhase(uint32_t threadIndex);
    void ParallelNarrowPhase(uint32_t threadIndex);

    uint32_t numThreads;
    std::vector<std::thread> workers;
    std::vector<ThreadLocalCache> threadCaches;

    std::atomic<bool> running{ true };
    std::atomic<int> activeSteps{ 0 };
    std::condition_variable cv_start;
    std::condition_variable cv_main;
    std::mutex taskMutex;

    // 新增：用于步进结束同步
    std::mutex stepMutex;
    std::condition_variable stepCV;
    bool stepDone = false;

    std::barrier<PhysicsBarrierAction> syncPoint;

    float currentDt = 0.0f;
    int barrierPhase = 0;
};