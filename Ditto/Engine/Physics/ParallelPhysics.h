#pragma once
#include "Physics.h"
#include "ThreadPool.h"

struct ParallelPhysics : public Physics {
    virtual void IntegrateForce(float dt) override;
    virtual void HandleBroadCollisions() override;
    virtual void HandleNarrowCollisions() override;
    virtual void UpdatePhysics(float dt) override;
    virtual void SolveCollisions(int iter) override;
    virtual void ApplyPositionCorrections() override;

    void BuildCollisionGroups();

    std::vector<std::vector<CollisionData*>> collisionGroups;
    static constexpr size_t MIN_PARALLEL_GROUP_SIZE = 4;

    static ThreadPool threadPool;
};