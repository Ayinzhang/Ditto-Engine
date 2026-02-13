#include "ParallelPhysics.h"
#include "../Core/Engine.h"
#include "../../3rdParty/TaskFlow/algorithm/for_each.hpp"

void ParallelPhysics::UpdatePhysics(float dt)
{
    t += dt;
    if (t < deltaTime) return;
    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step)
    {
        collisionData.clear();
        colliderPairs.clear();

        IntegrateForce(deltaTime);

        if (bvhTree) bvhTree->UpdateBVHTree();

        HandleBroadCollisions();

        HandleNarrowCollisions();

        BuildColorGroups();

        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);

        ApplyPositionCorrections();
    }

    for (auto collider : colliders)
        collider->transform->UpdateTransform();
}

void ParallelPhysics::SolveCollisions(int iter)
{
    taskflow.clear();
    tf::Task prev_gate;

    for (const auto& group : colorGroups)
    {
        std::vector<tf::Task> tasks;
        tasks.reserve(group.size());

        for (int idx : group)
        {
            CollisionData& data = collisionData[idx];
            tf::Task t = taskflow.emplace([this, &data, iter]() {
                ApplyImpulse(data.colliderA, data.colliderB,
                    data.info.normal,
                    data.info.contactPointA,
                    data.info.contactPointB,
                    data.info.depth,
                    iter);
                });

            // 依赖：必须在前一组所有任务完成后才能开始
            if (!prev_gate.empty()) t.succeed(prev_gate);
            tasks.push_back(t);
        }

        // 创建本组的“门任务”：依赖本组所有任务，同时作为下一组的依赖
        tf::Task gate = taskflow.emplace([]() {}).name("gate");
        for (auto& t : tasks) gate.succeed(t);
        prev_gate = gate;
    }

    // 整个迭代只提交一次，等待一次
    executor.run(taskflow).wait();
}

bool ParallelPhysics::HasConflict(const CollisionData& a, const CollisionData& b) const
{
    return a.colliderA == b.colliderA || a.colliderA == b.colliderB || a.colliderB == b.colliderA || a.colliderB == b.colliderB;
}

void ParallelPhysics::BuildColorGroups()
{
    colorGroups.clear(); const size_t n = collisionData.size(); if (n == 0) return;
    
    std::vector<int> colors(n, -1);
    
    for (size_t i = 0; i < n; ++i)
    {
        std::vector<bool> used(colorGroups.size(), false);
    
        for (size_t j = 0; j < n; ++j)
        {
            if (i == j) continue;
            if (colors[j] != -1 && HasConflict(collisionData[i], collisionData[j])) used[colors[j]] = true;
        }
    
        int color = 0;
        while (color < (int)used.size() && used[color]) ++color;
    
        colors[i] = color;
        if (color == (int)colorGroups.size()) colorGroups.emplace_back();
        colorGroups[color].push_back((int)i);
    }
}
