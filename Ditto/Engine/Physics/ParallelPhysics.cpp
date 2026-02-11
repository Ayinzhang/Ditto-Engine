#include "ParallelPhysics.h"
#include "../Core/Engine.h"
#include <algorithm>
#include <set>

// ============================================================================
// 主物理更新循环（完全重写）
// ============================================================================
void ParallelPhysics::UpdatePhysics(float dt)
{
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step)
    {
        // ---------- 1. 清空上一帧数据 ----------
        collisionData.clear();
        colliderPairs.clear();

        // ---------- 2. 并行积分力 ----------
        IntegrateForceParallel(deltaTime);

        // ---------- 3. 并行BVH更新 ----------
        if (bvhTree) ParallelUpdateBVHTree(bvhTree);

        // ---------- 4. 并行宽相位碰撞检测 ----------
        HandleBroadCollisionsParallel();

        // ---------- 5. 并行窄相位碰撞检测 ----------
        HandleNarrowCollisionsParallel();

        // ---------- 6. 无碰撞则跳过后续 ----------
        if (collisionData.empty()) continue;

        // ---------- 7. 按穿透深度排序（单次） + 图着色 ----------
        std::sort(collisionData.begin(), collisionData.end(),
            [](const CollisionData& a, const CollisionData& b) {
                return a.info.depth > b.info.depth;
            });
        BuildColorGroups();

        // ---------- 8. 多轮顺序冲量求解（并行） ----------
        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisionsParallel(iter);

        // ---------- 9. 并行位置修正 ----------
        ApplyPositionCorrectionsParallel();
    }

    // ---------- 10. 并行更新所有物体的变换矩阵 ----------
#pragma omp parallel for num_threads(4) schedule(static)
    for (int i = 0; i < (int)colliders.size(); ++i)
        colliders[i]->transform->UpdateTransform();
}

// ============================================================================
// 并行积分力
// ============================================================================
void ParallelPhysics::IntegrateForceParallel(float dt)
{
#pragma omp parallel for num_threads(4) schedule(static)
    for (int i = 0; i < (int)colliders.size(); ++i)
    {
        Collider* collider = colliders[i];
        if (collider->rigidbody->type != RigidbodyComponent::Dynamic)
            continue;

        TransformComponent* transform = collider->transform;
        RigidbodyComponent* rb = collider->rigidbody;

        if (rb->useGravity)
            rb->velocity.y += -gravity * dt;

        rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
        rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));

        transform->position += rb->velocity * dt;
        transform->rotation += rb->angularVelocity * dt;

        collider->isDirty = true;
    }
}

// ============================================================================
// 并行BVH更新（叶子并行 + 内部节点任务依赖）
// ============================================================================
void ParallelPhysics::ParallelUpdateBVHTree(BVHTree* tree)
{
    if (!tree || !tree->root) return;

    // 1. 并行更新所有叶子节点（每个collider独立）
#pragma omp parallel for num_threads(4) schedule(dynamic)
    for (int i = 0; i < (int)tree->leafNodes.size(); ++i)
    {
        BVHNode* leaf = tree->leafNodes[i];
        Collider* col = leaf->data.leaf.collider;
        if (col && col->isDirty)
        {
            col->UpdateWorldAABB();
            leaf->aabb = col->aabb;
        }
    }

    // 2. 使用OpenMP任务依赖递归更新内部节点
#pragma omp parallel
    {
#pragma omp single
        {
            ParallelUpdateNodeAABB(tree->root);
        }
    }
}

void ParallelPhysics::ParallelUpdateNodeAABB(BVHNode* node)
{
    if (node->isLeaf) return;

    // 左子树任务（依赖当前节点左子指针）
#pragma omp task depend(out: node->data.child.left)
    ParallelUpdateNodeAABB(node->data.child.left);

    // 右子树任务
#pragma omp task depend(out: node->data.child.right)
    ParallelUpdateNodeAABB(node->data.child.right);

    // 当前节点更新任务（依赖左右子任务完成）
#pragma omp task depend(in: node->data.child.left, node->data.child.right) \
                     depend(out: node)
    {
        node->UpdateAABB();
    }
}

// ============================================================================
// 并行宽相位碰撞检测（线程本地存储 + 合并）
// ============================================================================
void ParallelPhysics::HandleBroadCollisionsParallel()
{
    const int numThreads = 4;
    std::vector<std::vector<std::pair<Collider*, Collider*>>> localPairs(numThreads);

#pragma omp parallel num_threads(numThreads)
    {
        int tid = omp_get_thread_num();
        auto& myPairs = localPairs[tid];

#pragma omp for schedule(dynamic)
        for (int i = 0; i < (int)colliders.size(); ++i)
        {
            Collider* collider = colliders[i];
            if (collider->rigidbody->type != RigidbodyComponent::Dynamic)
                continue;

            std::vector<Collider*> potentials;
            if (bvhTree) potentials = bvhTree->Query(collider->aabb);

            for (Collider* other : potentials)
            {
                if (other == collider) continue;

                // 简单去重（每个线程的局部列表内去重）
                bool duplicate = false;
                for (auto& p : myPairs)
                    if ((p.first == collider && p.second == other) ||
                        (p.first == other && p.second == collider))
                    {
                        duplicate = true;
                        break;
                    }
                if (!duplicate)
                    myPairs.push_back({ collider, other });
            }
        }
    }

    // 合并所有线程的局部列表到全局colliderPairs
    colliderPairs.clear();
    for (auto& vec : localPairs)
        colliderPairs.insert(colliderPairs.end(), vec.begin(), vec.end());
}

// ============================================================================
// 并行窄相位GJK检测（线程本地存储 + 合并）
// ============================================================================
void ParallelPhysics::HandleNarrowCollisionsParallel()
{
    const int numThreads = 4;
    std::vector<std::vector<CollisionData>> localCollisionData(numThreads);

    int totalPairs = (int)colliderPairs.size();

#pragma omp parallel num_threads(numThreads)
    {
        int tid = omp_get_thread_num();
        auto& myData = localCollisionData[tid];

#pragma omp for schedule(dynamic)
        for (int i = 0; i < totalPairs; ++i)
        {
            auto& pair = colliderPairs[i];
            CollisionInfo info = GJK_CheckCollision(pair.first, pair.second);
            if (info.flag && info.depth > 1e-3f)
            {
                myData.emplace_back(pair.first, pair.second, info);
            }
        }
    }

    // 合并到全局collisionData
    collisionData.clear();
    for (auto& vec : localCollisionData)
        collisionData.insert(collisionData.end(), vec.begin(), vec.end());
}

// ============================================================================
// 冲突判断：两个碰撞数据是否共享任意一个刚体
// ============================================================================
bool ParallelPhysics::HasConflict(const CollisionData& a, const CollisionData& b) const
{
    return a.colliderA == b.colliderA ||
        a.colliderA == b.colliderB ||
        a.colliderB == b.colliderA ||
        a.colliderB == b.colliderB;
}

// ============================================================================
// 构建冲突图 + 贪心着色（生成颜色分组）
// ============================================================================
void ParallelPhysics::BuildColorGroups()
{
    int n = (int)collisionData.size();
    if (n == 0) return;

    std::vector<int> colors(n, -1);

    // 贪心序着色（按碰撞数据当前顺序）
    for (int i = 0; i < n; ++i)
    {
        std::set<int> neighborColors;
        for (int j = 0; j < i; ++j)
        {
            if (colors[j] != -1 && HasConflict(collisionData[i], collisionData[j]))
                neighborColors.insert(colors[j]);
        }

        int color = 0;
        while (neighborColors.find(color) != neighborColors.end())
            ++color;
        colors[i] = color;
    }

    // 确定最大颜色数并重组为颜色组
    int maxColor = 0;
    for (int c : colors) if (c > maxColor) maxColor = c;

    colorGroups.clear();
    colorGroups.resize(maxColor + 1);
    for (int i = 0; i < n; ++i)
        colorGroups[colors[i]].push_back(i);
}

// ============================================================================
// 并行冲量求解（单次迭代）
// ============================================================================
void ParallelPhysics::SolveCollisionsParallel(int iter)
{
    // 顺序处理每个颜色组（组间串行）
    for (const auto& group : colorGroups)
    {
        int groupSize = (int)group.size();

        // 组内完全并行（无冲突）
#pragma omp parallel for num_threads(4) schedule(static)
        for (int i = 0; i < groupSize; ++i)
        {
            int idx = group[i];
            CollisionData& data = collisionData[idx];

            ApplyImpulse(
                data.colliderA,
                data.colliderB,
                data.info.normal,
                data.info.contactPointA,
                data.info.contactPointB,
                data.info.depth,
                iter
            );
        }
    }
}

// ============================================================================
// 并行位置修正（复用颜色分组）
// ============================================================================
void ParallelPhysics::ApplyPositionCorrectionsParallel()
{
    if (colorGroups.empty()) return;

    for (const auto& group : colorGroups)
    {
        int groupSize = (int)group.size();

#pragma omp parallel for num_threads(4) schedule(static)
        for (int i = 0; i < groupSize; ++i)
        {
            int idx = group[i];
            CollisionData& data = collisionData[idx];
            if (data.info.depth <= 1e-3f) continue;

            Collider* a = data.colliderA;
            Collider* b = data.colliderB;
            const CollisionInfo& info = data.info;

            float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
            float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
            float totalInvMass = invMassA + invMassB;
            if (totalInvMass == 0.0f) continue;

            glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;
            a->transform->position -= correction * invMassA;
            b->transform->position += correction * invMassB;
            a->isDirty = true;
            b->isDirty = true;
        }
    }
}