#pragma once
#include "GJK.h"
#include "CollisionShape.h"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"
#include "../../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../../3rdParty/GLM/ext/quaternion_common.hpp"
#include "../../3rdParty/GLM/ext/quaternion_geometric.hpp"
#include "../../3rdParty/GLM/ext/quaternion_trigonometric.hpp"

struct Engine;
struct CollisionData {
    Collider* colliderA;
    Collider* colliderB;
    CollisionInfo info;
    bool processed;

    CollisionData(Collider* a, Collider* b, const CollisionInfo& i)
        : colliderA(a), colliderB(b), info(i), processed(false) {
    }
};

struct Physics
{
    Engine* engine;
    BVHTree* bvhTree;
    std::vector<Collider*> colliders;
    std::vector<std::pair<Collider*, Collider*>> colliderPairs;
    std::vector<CollisionData> collisionData; // 存储所有碰撞信息

    float t = 0, deltaTime = 1.0f / 60; int iterations = 4;

    float gravity = 9.8f;
    float linearDamping = 0.1f;
    float angularDamping = 0.2f;
    float restitution = 0.3f; // 堆叠时用较小的恢复系数
    float staticFriction = 0.8f; // 较大的静摩擦
    float dynamicFriction = 0.5f; // 较大的动摩擦
    float positionCorrectionFactor = 0.8f; // 位置修正强度

    void GenerateColliders(const std::vector<GameObject*>& gameobjects);
    virtual void UpdatePhysics(float dt);

    void IntegrateForce(float dt);
    void HandleBroadCollisions();
    void HandleNarrowCollisions();
    void SolveCollisions(int iter);
    void ApplyPositionCorrections();

    void ApplyImpulse(Collider* a, Collider* b, const glm::vec3& normal,
        const glm::vec3& contactPointA, const glm::vec3& contactPointB,
        float penetrationDepth, int iteration);
};