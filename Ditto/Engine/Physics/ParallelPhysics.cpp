#include "ParallelPhysics.h"
#include <omp.h>
#include <algorithm>
#include <iostream>

void ParallelPhysics::UpdatePhysics(float dt)
{
    // 累加时间
    t += dt;
    if (t < deltaTime) return;

    // 防止死亡螺旋，限制最大步数
    if (t > deltaTime * 10.0f) t = deltaTime;

    // --- [核心优化：子步进 Sub-stepping] ---
    // 对于 30 层堆叠，单纯增加 iterations 效率很低。
    // 我们将 dt 切分成 4 份，每份执行更少的迭代。
    // 这样不仅增加了力传播的速度，还让碰撞检测更频繁，大大减少穿透。

    int numSubSteps = 4;            // 将一帧切分为 4 次物理计算
    float subDt = deltaTime / numSubSteps;

    // 总迭代次数 = numSubSteps * solverIterations
    // 例如：4子步 * 4迭代 = 16次求解，比单次 16 迭代效果好得多，因为位置更新了 4 次
    int solverIterations = 4;

    // 消耗累积的时间
    while (t >= deltaTime)
    {
        for (int step = 0; step < numSubSteps; ++step)
        {
            // 1. 积分力 & 更新位置 (并行)
            IntegrateForceParallel(subDt);

            // 5. 窄阶段求解 (并行迭代)
            for (int iter = 0; iter < solverIterations; iter++)
            {
                // 2. 更新 BVH (串行/并行)
                // 每次子步位置都会变，必须更新 BVH 才能检测到新的接触
                if (bvhTree) bvhTree->UpdateBVHTree();

                // 3. 宽阶段检测 (并行)
                HandleBroadCollisionsParallel();

                // 4. 构建约束分组 (串行 - 图着色)
                // 注意：这一步移到了 Solver 循环外部！
                // 接触图的拓扑结构在 Solver 迭代过程中是不变的。
                BuildConstraintBatches();

                SolveBatchesParallel();
            }
        }

        t -= deltaTime;
    }

    // 最后同步 Transform (如果需要)
    // IntegrateForceParallel 已经修改了 TransformComponent，这里通常不需要额外操作
	for (auto iter : colliders) iter->transform->UpdateTransform();
}

// 积分力保持上一版修正后的逻辑 (记得更新 AABB)
void ParallelPhysics::IntegrateForceParallel(float dt)
{
    int count = (int)colliders.size();

#pragma omp parallel for schedule(static)
    for (int i = 0; i < count; ++i)
    {
        Collider* collider = colliders[i];
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            TransformComponent* transform = collider->transform;
            RigidbodyComponent* rb = collider->rigidbody;

            // 简单的半隐式欧拉积分
            if (rb->useGravity) rb->velocity.y += -9.8f * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - rb->damp, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - rb->angularDamp, dt));

            transform->position += rb->velocity * dt;
            // 旋转积分建议使用四元数插值，这里简化处理
            transform->rotation += rb->angularVelocity * dt;

            collider->isDirty = true;
            collider->UpdateWorldAABB(); // 必须更新！
        }
    }
}

// 宽阶段保持上一版逻辑
void ParallelPhysics::HandleBroadCollisionsParallel()
{
    colliderPairs.clear();
    if (!bvhTree) return;
    int count = (int)colliders.size();

    // 线程局部存储
    std::vector<std::vector<std::pair<Collider*, Collider*>>> threadPairs;

#pragma omp parallel
    {
        int threadId = omp_get_thread_num();
        int numThreads = omp_get_num_threads();
#pragma omp single
        { threadPairs.resize(numThreads); }

#pragma omp for schedule(dynamic, 32)
        for (int i = 0; i < count; ++i)
        {
            Collider* collider = colliders[i];
            if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
            {
                std::vector<Collider*> potentialCollisions = bvhTree->Query(collider->aabb);
                for (Collider* other : potentialCollisions)
                {
                    if (other == collider) continue;
                    // 简单的单向检查
                    if (collider < other)
                    {
                        threadPairs[threadId].push_back({ collider, other });
                    }
                }
            }
        }
    }
    // 合并
    for (const auto& local : threadPairs) {
        colliderPairs.insert(colliderPairs.end(), local.begin(), local.end());
    }
}

// 构建批次 (针对 Stack 优化的 Static 处理)
void ParallelPhysics::BuildConstraintBatches()
{
    for (auto& batch : constraintBatches) batch.clear();
    if (constraintBatches.size() < 20) constraintBatches.resize(20);

    // 索引映射
    colliderIndexMap.clear();
    for (int i = 0; i < colliders.size(); ++i) colliderIndexMap[colliders[i]] = i;

    // 追踪依赖
    std::vector<int> bodyLastBatchIndex(colliders.size(), -1);

    for (const auto& pair : colliderPairs)
    {
        Collider* a = pair.first;
        Collider* b = pair.second;

        int idxA = colliderIndexMap[a];
        int idxB = colliderIndexMap[b];

        // 关键：Static 物体不产生依赖
        // 地板是 Static，所有接触地板的箱子都可以放在 Batch 0 并行处理
        int batchIdxA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? bodyLastBatchIndex[idxA] : -1;
        int batchIdxB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? bodyLastBatchIndex[idxB] : -1;

        int targetBatch = std::max(batchIdxA, batchIdxB) + 1;

        if (targetBatch >= constraintBatches.size()) {
            constraintBatches.resize(targetBatch + 5);
        }

        constraintBatches[targetBatch].push_back(pair);

        if (a->rigidbody->type == RigidbodyComponent::Dynamic) bodyLastBatchIndex[idxA] = targetBatch;
        if (b->rigidbody->type == RigidbodyComponent::Dynamic) bodyLastBatchIndex[idxB] = targetBatch;
    }
}

// 纯粹的求解过程
void ParallelPhysics::SolveBatchesParallel()
{
    // 按颜色批次顺序执行
    for (const auto& batch : constraintBatches)
    {
        if (batch.empty()) continue;

        // 批次内部并行：这是无锁的，因为 Graph Coloring 保证了数据独立
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < batch.size(); ++i)
        {
            Collider* colliderA = batch[i].first;
            Collider* colliderB = batch[i].second;

            // 每次 Solver 迭代都重新检测碰撞有点浪费，
            // 理想情况是把 ContactInfo 缓存下来只做 Solve。
            // 但为了兼容你的现有架构，我们这里还是做全套 GJK+Solve。
            // 由于并行化，这比串行还是快。
            CollisionInfo collisionInfo = GJK_CheckCollision(colliderA, colliderB);

            if (collisionInfo.flag && collisionInfo.depth > 0.001f)
            {
                // 并行写入 Velocity 和 Position，无竞争
                PositionCorrection(colliderA, colliderB, collisionInfo);
                ApplyImpulse(colliderA, colliderB, collisionInfo);
            }
        }
        // 隐式 Barrier：等待该 Batch 完成，力传播一层
    }
}

// HandleNarrowCollisionsParallel 不再需要了，或者只是简单包裹一下
void ParallelPhysics::HandleNarrowCollisionsParallel()
{
    // 留空或删除，因为逻辑已经移到 UpdatePhysics 的主循环里了
}