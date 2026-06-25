#pragma once

struct Scene;
struct Physics;
class Physics2DWorld;

namespace Ditto::EngineLifecycle
{
    void EnterPlayMode(Scene* scene, Physics* physics, Physics2DWorld* physics2D, float& physics2DAccumulator);
    void StepPlayModeFrame(Scene* scene, Physics* physics, Physics2DWorld* physics2D,
        float deltaTime, float& physics2DAccumulator);
    void ExitPlayMode(Scene* scene, Physics* physics, Physics2DWorld* physics2D, float& physics2DAccumulator);
}
