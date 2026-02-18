#pragma once
#include "ParallelPhysics.h"
#include <barrier>

class ParallelPhysicsBarrier : public ParallelPhysics {
public:
    virtual void UpdatePhysics(float dt) override;
};