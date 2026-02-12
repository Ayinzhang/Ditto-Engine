#include "ParallelPhysics.h"
#include "../Core/Engine.h"
#include "../../3rdParty/TaskFlow/algorithm/for_each.hpp"
#include <iostream>
#include <algorithm>
#include <set>
#include <mutex>

ParallelPhysics::ParallelPhysics()
    : executor(std::thread::hardware_concurrency()) // 自动检测硬件并发度
{
}

void ParallelPhysics::BuildConflictGraphAndColorGroups(const std::vector<CollisionData>& data)
{
    const size_t N = data.size();
    collisionColors.assign(N, -1);
    colorGroups.clear();
    numColors = 0;

    if (N == 0) return;

    // 1. 建立 Collider* → 碰撞对索引列表 的映射
    std::unordered_map<const Collider*, std::vector<int>> colliderToPairs;
    for (int i = 0; i < N; ++i) {
        const CollisionData& cd = data[i];
        colliderToPairs[cd.colliderA].push_back(i);
        colliderToPairs[cd.colliderB].push_back(i);
    }

    // 2. 贪心着色
    for (int i = 0; i < N; ++i) {
        const CollisionData& cd = data[i];
        std::set<int> neighborColors;

        // 收集与当前碰撞对冲突的所有已着色邻居的颜色
        auto collectNeighbors = [&](const Collider* collider) {
            auto it = colliderToPairs.find(collider);
            if (it != colliderToPairs.end()) {
                for (int j : it->second) {
                    if (j != i && collisionColors[j] != -1) {
                        neighborColors.insert(collisionColors[j]);
                    }
                }
            }
            };

        collectNeighbors(cd.colliderA);
        collectNeighbors(cd.colliderB);

        // 分配最小的可用颜色
        int color = 0;
        while (neighborColors.count(color)) ++color;
        collisionColors[i] = color;
        if (color >= numColors) numColors = color + 1;
    }

    // 3. 构建颜色分组
    colorGroups.resize(numColors);
    for (int i = 0; i < N; ++i) {
        colorGroups[collisionColors[i]].push_back(i);
    }
}

void ParallelPhysics::UpdatePhysics(float dt)
{
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step)
    {
        // ------------------------------------------------------------
        // 1. 清空上一子步的碰撞数据
        // ------------------------------------------------------------
        collisionData.clear();
        colliderPairs.clear();

        // ------------------------------------------------------------
        // 2. 构建当前子步的任务图
        // ------------------------------------------------------------
        tf::Taskflow taskflow;

        // ---------- 2.1 积分力（并行） ----------
        auto integrateTask = taskflow.for_each(colliders.begin(), colliders.end(),
            [this](Collider* collider) {
                if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
                {
                    TransformComponent* transform = collider->transform;
                    RigidbodyComponent* rb = collider->rigidbody;

                    if (rb->useGravity)
                        rb->velocity.y += -gravity * deltaTime;

                    rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, deltaTime));
                    rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, deltaTime));

                    transform->position += rb->velocity * deltaTime;
                    transform->rotation += rb->angularVelocity * deltaTime;

                    transform->localDirty = true;
                    collider->isDirty = true;
                }
            });

        // ---------- 2.2 更新BVH树（串行，依赖积分完成） ----------
        auto bvhTask = taskflow.emplace([this]() {
            if (bvhTree) bvhTree->UpdateBVHTree();
            });
        bvhTask.succeed(integrateTask);

        // ---------- 2.3 粗测阶段（并行子流） ----------
        auto broadPhaseTask = taskflow.emplace([this](tf::Subflow& subflow) {
            // 收集所有动态碰撞体
            std::vector<Collider*> dynamicColliders;
            for (Collider* collider : colliders) {
                if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
                    dynamicColliders.push_back(collider);
            }

            // 为每个动态碰撞体创建子任务，并行执行BVH查询
            for (Collider* collider : dynamicColliders) {
                subflow.emplace([this, collider]() {
                    std::vector<Collider*> potentialCollisions;
                    if (bvhTree) potentialCollisions = bvhTree->Query(collider->aabb);

                    for (Collider* other : potentialCollisions) {
                        if (other == collider) continue;

                        // 互斥保护 colliderPairs
                        std::lock_guard<std::mutex> lock(colliderPairsMutex);
                        bool alreadyExists = false;
                        for (const auto& pair : colliderPairs) {
                            if ((pair.first == collider && pair.second == other) ||
                                (pair.first == other && pair.second == collider)) {
                                alreadyExists = true;
                                break;
                            }
                        }
                        if (!alreadyExists) {
                            if (other->rigidbody->type == RigidbodyComponent::Dynamic ||
                                other->rigidbody->type == RigidbodyComponent::Static) {
                                colliderPairs.push_back({ collider, other });
                            }
                        }
                    }
                    });
            }
            subflow.join(); // 等待所有子任务完成
            });
        broadPhaseTask.succeed(bvhTask);

        // ---------- 2.4 精测阶段（并行子流） ----------
        auto narrowPhaseTask = taskflow.emplace([this](tf::Subflow& subflow) {
            // 为每一对候选碰撞体创建子任务，并行执行GJK检测
            for (auto& pair : colliderPairs) {
                subflow.emplace([this, pair]() {
                    Collider* colliderA = pair.first;
                    Collider* colliderB = pair.second;
                    CollisionInfo collisionInfo = GJK_CheckCollision(colliderA, colliderB);

                    if (collisionInfo.flag && collisionInfo.depth > 1e-3) {
                        std::lock_guard<std::mutex> lock(collisionDataMutex);
                        collisionData.push_back(CollisionData(colliderA, colliderB, collisionInfo));
                    }
                    });
            }
            subflow.join();
            });
        narrowPhaseTask.succeed(broadPhaseTask);

        // ---------- 2.5 图染色任务（串行） ----------
        auto colorTask = taskflow.emplace([this]() {
            BuildConflictGraphAndColorGroups(collisionData);
            });
        colorTask.succeed(narrowPhaseTask);

        // ---------- 2.6 迭代求解（每轮迭代内并行，迭代间串行） ----------
        tf::Task prevTask = colorTask;

        for (int iter = 0; iter < iterations; ++iter) {
            auto iterTask = taskflow.emplace([this, iter](tf::Subflow& subflow) {
                // 按穿透深度排序（串行）
                std::sort(collisionData.begin(), collisionData.end(),
                    [](const CollisionData& a, const CollisionData& b) {
                        return a.info.depth > b.info.depth;
                    });

                // 重新着色（碰撞数据未变，但排序后索引顺序已变）
                BuildConflictGraphAndColorGroups(collisionData);

                // 重置 processed 标记
                for (auto& data : collisionData) data.processed = false;

                // 按颜色分组并行应用冲量
                // 为每个颜色组创建一个并行迭代任务
                for (int c = 0; c < numColors; ++c) {
                    const auto& group = colorGroups[c];
                    if (group.empty()) continue;

                    // 使用 subflow.for_each 并行处理该颜色组内的所有碰撞对
                    subflow.for_each(group.begin(), group.end(),
                        [this, iter](int idx) {
                            CollisionData& data = collisionData[idx];
                            if (!data.processed) {
                                ApplyImpulse(
                                    data.colliderA, data.colliderB,
                                    data.info.normal,
                                    data.info.contactPointA,
                                    data.info.contactPointB,
                                    data.info.depth,
                                    iter
                                );
                                data.processed = true;
                            }
                        });
                }
                subflow.join(); // 等待所有颜色组的并行任务完成
                });

            iterTask.succeed(prevTask);
            prevTask = iterTask;
        }

        // ---------- 2.7 位置修正（并行，同样基于图染色） ----------
        auto posCorrTask = taskflow.emplace([this](tf::Subflow& subflow) {
            // 重新着色（确保分组与当前碰撞数据顺序一致）
            BuildConflictGraphAndColorGroups(collisionData);

            // 按颜色分组并行修正位置
            for (int c = 0; c < numColors; ++c) {
                const auto& group = colorGroups[c];
                if (group.empty()) continue;

                subflow.for_each(group.begin(), group.end(),
                    [this](int idx) {
                        CollisionData& data = collisionData[idx];
                        if (data.info.depth > 1e-3) {
                            Collider* a = data.colliderA;
                            Collider* b = data.colliderB;
                            const CollisionInfo& info = data.info;

                            float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                            float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                            float totalInvMass = invMassA + invMassB;

                            glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                            a->transform->position -= correction * invMassA;
                            b->transform->position += correction * invMassB;

                            a->isDirty = true;
                            b->isDirty = true;
                        }
                    });
            }
            subflow.join();
            });
        posCorrTask.succeed(prevTask); // 依赖于最后一个迭代任务

        // ---------- 2.8 更新所有碰撞体的世界变换（并行） ----------
        auto updateTransformTask = taskflow.for_each(colliders.begin(), colliders.end(),
            [](Collider* collider) {
                collider->transform->UpdateTransform();
            });
        updateTransformTask.succeed(posCorrTask);

        // ------------------------------------------------------------
        // 3. 执行整个任务图，等待完成
        // ------------------------------------------------------------
        executor.run(taskflow).wait();
    }
}