#include "ParallelPhysicsBarrier.h"
#include <algorithm>

// --- 屏障动作实现 ---
void PhysicsBarrierAction::operator()() noexcept {
    engine->OnBarrierPhaseComplete();
}

ParallelPhysicsBarrier::ParallelPhysicsBarrier(uint32_t threadCount)
    : numThreads(threadCount),
    threadCaches(threadCount),
    syncPoint(threadCount, PhysicsBarrierAction{ this })
{
    for (uint32_t i = 0; i < numThreads; ++i) {
        workers.emplace_back(&ParallelPhysicsBarrier::WorkerLoop, this, i);
    }
}

ParallelPhysicsBarrier::~ParallelPhysicsBarrier() {
    running = false;
    cv_start.notify_all();
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
}

void ParallelPhysicsBarrier::OnBarrierPhaseComplete() noexcept {
    switch (barrierPhase) {
    case 0:
        this->colliderPairs.clear();
        if (this->bvhTree) this->bvhTree->UpdateBVHTree();
        break;
    case 1:
        for (auto& cache : threadCaches) {
            this->colliderPairs.insert(this->colliderPairs.end(), cache.pairs.begin(), cache.pairs.end());
            cache.pairs.clear();
        }
        this->collisionData.clear();
        break;
    case 2:
        for (auto& cache : threadCaches) {
            this->collisionData.insert(this->collisionData.end(), cache.data.begin(), cache.data.end());
            cache.data.clear();
        }
        for (int iter = 0; iter < iterations; ++iter) {
            SolveCollisions(iter);
        }
        ApplyPositionCorrections();
        break;
        // 可以预留 case 3 用于其他同步，但当前不需要
    }
    barrierPhase = (barrierPhase + 1) % 3;
}

void ParallelPhysicsBarrier::UpdatePhysics(float dt) {
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step) {
        currentDt = deltaTime;

        // 准备新一步
        {
            std::unique_lock<std::mutex> lock(taskMutex);
            activeSteps = 1;
            barrierPhase = 0;
        }
        {
            std::lock_guard<std::mutex> lock(stepMutex);
            stepDone = false;   // 重置步进结束标志
        }
        cv_start.notify_all();

        // 等待该步完成
        std::unique_lock<std::mutex> lock(taskMutex);
        cv_main.wait(lock, [this] { return activeSteps == 0; });
    }

    for (auto collider : colliders) {
        collider->transform->UpdateTransform();
    }
}

void ParallelPhysicsBarrier::WorkerLoop(uint32_t threadIndex) {
    while (running) {
        {
            std::unique_lock<std::mutex> lock(taskMutex);
            cv_start.wait(lock, [this] { return activeSteps > 0 || !running; });
        }
        if (!running) break;

        // 阶段 1: 并行积分
        ParallelIntegrate(threadIndex);
        syncPoint.arrive_and_wait();

        // 阶段 2: 并行宽相位
        ParallelBroadPhase(threadIndex);
        syncPoint.arrive_and_wait();

        // 阶段 3: 并行窄相位
        ParallelNarrowPhase(threadIndex);
        syncPoint.arrive_and_wait();

        // 同步点：所有线程在此等待，直到线程0将 activeSteps 清零并通知
        {
            std::unique_lock<std::mutex> lock(stepMutex);
            if (threadIndex == 0) {
                activeSteps = 0;           // 通知主线程步进结束
                cv_main.notify_one();
                stepDone = true;            // 允许其他线程继续
                stepCV.notify_all();
            }
            else {
                stepCV.wait(lock, [this] { return stepDone; });
            }
        }
        // 进入下一轮循环，此时 activeSteps == 0，会等待 cv_start
    }
}

// ParallelIntegrate、ParallelBroadPhase、ParallelNarrowPhase 保持不变
// 以下为原实现，略...
void ParallelPhysicsBarrier::ParallelIntegrate(uint32_t threadIndex) {
    const size_t count = colliders.size();
    for (size_t i = threadIndex; i < count; i += numThreads) {
        Collider* col = colliders[i];
        if (col->rigidbody->type == RigidbodyComponent::Dynamic) {
            RigidbodyComponent* rb = col->rigidbody;
            TransformComponent* tf = col->transform;

            if (rb->useGravity) rb->velocity.y += -gravity * currentDt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, currentDt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, currentDt));

            tf->position += rb->velocity * currentDt;
            tf->rotation += rb->angularVelocity * currentDt;

            tf->localDirty = true;
            col->UpdateWorldAABB();
            col->isDirty = true;
        }
    }
}

void ParallelPhysicsBarrier::ParallelBroadPhase(uint32_t threadIndex) {
    const size_t count = colliders.size();
    auto& localPairs = threadCaches[threadIndex].pairs;

    for (size_t i = threadIndex; i < count; i += numThreads) {
        Collider* colA = colliders[i];
        if (colA->rigidbody->type == RigidbodyComponent::Dynamic) {
            std::vector<Collider*> potentials;
            if (bvhTree) potentials = bvhTree->Query(colA->aabb);

            for (Collider* colB : potentials) {
                if (colA == colB) continue;
                if (colB->rigidbody->type == RigidbodyComponent::Dynamic ||
                    colB->rigidbody->type == RigidbodyComponent::Static) {
                    if (colA->id < colB->id) {
                        localPairs.push_back({ colA, colB });
                    }
                }
            }
        }
    }
}

void ParallelPhysicsBarrier::ParallelNarrowPhase(uint32_t threadIndex) {
    const size_t pairCount = colliderPairs.size();
    auto& localData = threadCaches[threadIndex].data;

    for (size_t i = threadIndex; i < pairCount; i += numThreads) {
        auto& pair = colliderPairs[i];
        CollisionInfo info = GJK_CheckCollision(pair.first, pair.second);
        if (info.flag && info.depth > 1e-3f) {
            localData.emplace_back(pair.first, pair.second, info);
        }
    }
}