#include "ParallelPhysicsBarrier.h"
#include "ThreadPool.h"
#include <algorithm>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <barrier>

void ParallelPhysicsBarrier::UpdatePhysics(float dt) {
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step) {
        // 清空全局容器
        collisionData.clear();
        colliderPairs.clear();

        size_t n = colliders.size();
        if (n == 0) continue;

        size_t numThreads = threadPool.thread_count();
        if (numThreads == 0) numThreads = 1;

        // C++20 屏障，用于线程间同步
        std::barrier sync(numThreads);

        std::atomic<size_t> tasksRemaining(numThreads);
        std::mutex cvMutex;
        std::condition_variable cv;

        // 线程局部存储：广相碰撞对 和 窄相碰撞数据
        std::vector<std::vector<std::pair<Collider*, Collider*>>> threadLocalPairs(numThreads);
        std::vector<std::vector<CollisionData>> threadLocalCollisionData(numThreads);

        for (size_t tid = 0; tid < numThreads; ++tid) {
            threadPool.enqueue([this, tid, numThreads, n, &sync,
                &threadLocalPairs, &threadLocalCollisionData,
                &tasksRemaining, &cv, &cvMutex] {
                    size_t start = tid * n / numThreads;
                    size_t end = (tid + 1) * n / numThreads;

                    // ---------- 阶段1: 积分力 ----------
                    for (size_t i = start; i < end; ++i) {
                        Collider* collider = colliders[i];
                        if (collider->rigidbody->type == RigidbodyComponent::Dynamic) {
                            auto* transform = collider->transform;
                            auto* rb = collider->rigidbody;

                            if (rb->useGravity)
                                rb->velocity.y += -gravity * deltaTime;

                            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, deltaTime));
                            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, deltaTime));
                            transform->position += rb->velocity * deltaTime;
                            transform->rotation += rb->angularVelocity * deltaTime;

                            transform->localDirty = true;
                            collider->isDirty = true;
                        }
                    }
                    sync.arrive_and_wait();

                    // ---------- 阶段2: 更新BVH树（由线程0执行）----------
                    if (tid == 0 && bvhTree) {
                        bvhTree->UpdateBVHTree();
                    }
                    sync.arrive_and_wait();

                    // ---------- 阶段3: 广相碰撞检测 ----------
                    if (bvhTree) {
                        // 收集所有动态物体的索引
                        std::vector<size_t> dynamicIndices;
                        for (size_t i = 0; i < colliders.size(); ++i) {
                            if (colliders[i]->rigidbody->type == RigidbodyComponent::Dynamic)
                                dynamicIndices.push_back(i);
                        }
                        size_t numDyn = dynamicIndices.size();
                        if (numDyn > 0) {
                            size_t dynStart = tid * numDyn / numThreads;
                            size_t dynEnd = (tid + 1) * numDyn / numThreads;

                            auto& local = threadLocalPairs[tid];
                            for (size_t j = dynStart; j < dynEnd; ++j) {
                                Collider* collider = colliders[dynamicIndices[j]];
                                std::vector<Collider*> potential = bvhTree->Query(collider->aabb);
                                for (Collider* other : potential) {
                                    if (other == collider) continue;
                                    if (other->rigidbody->type != RigidbodyComponent::Dynamic &&
                                        other->rigidbody->type != RigidbodyComponent::Static)
                                        continue;
                                    // 保持顺序以便后续去重
                                    if (collider < other)
                                        local.emplace_back(collider, other);
                                    else
                                        local.emplace_back(other, collider);
                                }
                            }
                        }
                    }
                    sync.arrive_and_wait();

                    // ---------- 合并广相碰撞对（线程0）----------
                    if (tid == 0) {
                        std::vector<std::pair<Collider*, Collider*>> allPairs;
                        for (auto& local : threadLocalPairs) {
                            allPairs.insert(allPairs.end(), local.begin(), local.end());
                        }
                        std::sort(allPairs.begin(), allPairs.end());
                        allPairs.erase(std::unique(allPairs.begin(), allPairs.end()), allPairs.end());
                        colliderPairs = std::move(allPairs);
                    }
                    sync.arrive_and_wait();

                    // ---------- 阶段4: 窄相碰撞检测 ----------
                    size_t numPairs = colliderPairs.size();
                    if (numPairs > 0) {
                        size_t pairStart = tid * numPairs / numThreads;
                        size_t pairEnd = (tid + 1) * numPairs / numThreads;
                        auto& localCollisions = threadLocalCollisionData[tid];
                        for (size_t i = pairStart; i < pairEnd; ++i) {
                            auto& pair = colliderPairs[i];
                            CollisionInfo info = GJK_CheckCollision(pair.first, pair.second);
                            if (info.flag && info.depth > 1e-3f) {
                                localCollisions.emplace_back(pair.first, pair.second, info);
                            }
                        }
                    }
                    sync.arrive_and_wait();

                    // ---------- 合并窄相碰撞数据（线程0）----------
                    if (tid == 0) {
                        collisionData.clear();
                        for (auto& local : threadLocalCollisionData) {
                            collisionData.insert(collisionData.end(), local.begin(), local.end());
                        }
                    }
                    sync.arrive_and_wait();

                    // ---------- 阶段5: 构建碰撞组（串行，由线程0执行）----------
                    if (tid == 0) {
                        BuildCollisionGroups();
                    }
                    sync.arrive_and_wait();

                    // ---------- 阶段6: 求解碰撞（多次迭代）----------
                    for (int iter = 0; iter < iterations; ++iter) {
                        size_t numGroups = collisionGroups.size();
                        if (numGroups > 0) {
                            size_t groupStart = tid * numGroups / numThreads;
                            size_t groupEnd = (tid + 1) * numGroups / numThreads;
                            for (size_t g = groupStart; g < groupEnd; ++g) {
                                auto& group = collisionGroups[g];
                                for (CollisionData* data : group) {
                                    ApplyImpulse(data->colliderA, data->colliderB,
                                        data->info.normal,
                                        data->info.contactPointA,
                                        data->info.contactPointB,
                                        data->info.depth, iter);
                                }
                            }
                        }
                        sync.arrive_and_wait();
                    }

                    // ---------- 阶段7: 应用位置修正 ----------
                    size_t numGroups = collisionGroups.size();
                    if (numGroups > 0) {
                        size_t groupStart = tid * numGroups / numThreads;
                        size_t groupEnd = (tid + 1) * numGroups / numThreads;
                        for (size_t g = groupStart; g < groupEnd; ++g) {
                            auto& group = collisionGroups[g];
                            for (CollisionData* data : group) {
                                if (data->info.depth > 1e-3f) {
                                    Collider* a = data->colliderA;
                                    Collider* b = data->colliderB;
                                    const CollisionInfo& info = data->info;

                                    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                                    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                                    float totalInvMass = invMassA + invMassB;

                                    glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                                    a->transform->position -= correction * invMassA;
                                    b->transform->position += correction * invMassB;

                                    a->isDirty = true;
                                    b->isDirty = true;
                                }
                            }
                        }
                    }
                    sync.arrive_and_wait();

                    // ---------- 最后更新所有 Transform ----------
                    for (size_t i = start; i < end; ++i) {
                        colliders[i]->transform->UpdateTransform();
                    }

                    // 通知主线程本任务完成
                    if (--tasksRemaining == 0) {
                        std::lock_guard<std::mutex> lock(cvMutex);
                        cv.notify_one();
                    }
                });
        }

        // 等待所有任务完成
        std::unique_lock<std::mutex> lock(cvMutex);
        cv.wait(lock, [&tasksRemaining] { return tasksRemaining == 0; });
    }
}