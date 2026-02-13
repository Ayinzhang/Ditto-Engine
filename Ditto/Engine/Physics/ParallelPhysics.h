#pragma once
#include "Physics.h"

class ParallelPhysics : public Physics
{
public:
    // 重写需要并行的核心函数
    virtual void IntegrateForce(float dt) override;
    virtual void HandleBroadCollisions() override;
    virtual void HandleNarrowCollisions() override;
    virtual void UpdatePhysics(float dt) override; // 完全重写更新循环
};