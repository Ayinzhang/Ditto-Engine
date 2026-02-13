#pragma once
#include "Physics.h"
#include "../../3rdParty/TaskFlow/taskflow.hpp"

struct ParallelPhysics : public Physics
{
    virtual void UpdatePhysics(float dt) override;
    virtual void SolveCollisions(int iter) override;

private:
    void BuildColorGroups();
    bool HasConflict(const CollisionData& a, const CollisionData& b) const;

    tf::Executor executor;
    tf::Taskflow taskflow;
    std::vector<std::vector<int>> colorGroups;
};