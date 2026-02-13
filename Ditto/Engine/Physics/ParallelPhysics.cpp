#include "ParallelPhysics.h"
#include <future>
#include <thread>
#include <algorithm>

// 辅助：并行执行无返回值的任务，按范围分割
static void ParallelFor(size_t count, std::function<void(size_t)> func) 
{
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    size_t chunkSize = (count + numThreads - 1) / numThreads;
    std::vector<std::future<void>> futures;

    for (unsigned int t = 0; t < numThreads; ++t) {
        size_t start = t * chunkSize;
        size_t end = std::min(start + chunkSize, count);
        if (start >= end) break;

        futures.emplace_back(std::async(std::launch::async, [start, end, &func]() {
            for (size_t i = start; i < end; ++i) {
                func(i); }
            }));
    }

    for (auto& f : futures) {
        f.wait();
    }
}

void ParallelPhysics::IntegrateForce(float dt) {
    size_t n = colliders.size();
    if (n == 0) return;

    ParallelFor(n, [this, dt](size_t i) {
        Collider* collider = colliders[i];
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic) {
            TransformComponent* transform = collider->transform;
            RigidbodyComponent* rb = collider->rigidbody;

            if (rb->useGravity)
                rb->velocity.y += -gravity * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));
            transform->position += rb->velocity * dt;
            transform->rotation += rb->angularVelocity * dt;

            transform->localDirty = true;
            collider->isDirty = true;
        }
        });
}

void ParallelPhysics::HandleBroadCollisions() {
    colliderPairs.clear();
    if (!bvhTree) return;

    // 收集所有动态物体的索引
    std::vector<size_t> dynamicIndices;
    for (size_t i = 0; i < colliders.size(); ++i) {
        if (colliders[i]->rigidbody->type == RigidbodyComponent::Dynamic)
            dynamicIndices.push_back(i);
    }
    size_t numDyn = dynamicIndices.size();
    if (numDyn == 0) return;

    unsigned int numThreads = std::thread::hardware_concurrency();
    size_t chunkSize = (numDyn + numThreads - 1) / numThreads;
    std::vector<std::future<std::vector<std::pair<Collider*, Collider*>>>> futures;

    for (unsigned int t = 0; t < numThreads; ++t) {
        size_t start = t * chunkSize;
        size_t end = std::min(start + chunkSize, numDyn);
        if (start >= end) break;

        futures.emplace_back(std::async(std::launch::async, [this, &dynamicIndices, start, end]() 
            {
            std::vector<std::pair<Collider*, Collider*>> localPairs;
            for (size_t idx = start; idx < end; ++idx) {
                Collider* collider = colliders[dynamicIndices[idx]];

                // 查询可能与当前动态物体碰撞的其他物体
                std::vector<Collider*> potential = bvhTree->Query(collider->aabb);
                for (Collider* other : potential) {
                    if (other == collider) continue;
                    // 只考虑动态或静态物体
                    if (other->rigidbody->type != RigidbodyComponent::Dynamic &&
                        other->rigidbody->type != RigidbodyComponent::Static)
                        continue;

                    // 保证配对顺序一致，方便后续去重
                    if (collider < other)
                        localPairs.emplace_back(collider, other);
                    else
                        localPairs.emplace_back(other, collider);
                }
            }
            return localPairs;
            }));
    }

    // 收集所有线程的局部结果
    std::vector<std::pair<Collider*, Collider*>> allPairs;
    for (auto& f : futures) {
        auto local = f.get();
        allPairs.insert(allPairs.end(), local.begin(), local.end());
    }

    // 去重
    std::sort(allPairs.begin(), allPairs.end());
    allPairs.erase(std::unique(allPairs.begin(), allPairs.end()), allPairs.end());

    colliderPairs = std::move(allPairs);
}

void ParallelPhysics::HandleNarrowCollisions() 
{
    collisionData.clear();
    size_t numPairs = colliderPairs.size();
    if (numPairs == 0) return;

    unsigned int numThreads = std::thread::hardware_concurrency();
    size_t chunkSize = (numPairs + numThreads - 1) / numThreads;
    std::vector<std::future<std::vector<CollisionData>>> futures;

    for (unsigned int t = 0; t < numThreads; ++t) {
        size_t start = t * chunkSize;
        size_t end = std::min(start + chunkSize, numPairs);
        if (start >= end) break;

        futures.emplace_back(std::async(std::launch::async, [this, start, end]() {
            std::vector<CollisionData> localData;
            for (size_t i = start; i < end; ++i) {
                auto& pair = colliderPairs[i];
                CollisionInfo info = GJK_CheckCollision(pair.first, pair.second);
                if (info.flag && info.depth > 1e-3f) {
                    localData.emplace_back(pair.first, pair.second, info);
                }
            }
            return localData;
            }));
    }

    for (auto& f : futures) {
        auto local = f.get();
        collisionData.insert(collisionData.end(), local.begin(), local.end());
    }
}

void ParallelPhysics::UpdatePhysics(float dt) {
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step) 
    {
        // 清空上一帧数据
        collisionData.clear();
        colliderPairs.clear();

        // 并行积分
        IntegrateForce(deltaTime);

        // BVH 更新（单线程）
        if (bvhTree) bvhTree->UpdateBVHTree();

        // 并行宽阶段
        HandleBroadCollisions();

        // 并行窄阶段
        HandleNarrowCollisions();

        // 顺序求解碰撞（迭代）
        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);          // 调用基类实现

        // 顺序位置修正
        ApplyPositionCorrections();          // 调用基类实现
    }

    for (auto collider : colliders) collider->transform->UpdateTransform();
}