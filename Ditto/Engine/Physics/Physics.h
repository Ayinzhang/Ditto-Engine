#pragma once
#include <memory>
#include <map>
#include <set>
#include <utility>
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

// A collision/trigger Enter or Exit event detected this frame, consumed by the
// engine (dispatched to C# scripts) after UpdatePhysics returns.
struct ContactEvent
{
    GameObject* a = nullptr;
    GameObject* b = nullptr;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };   // from A towards B
    float depth = 0.0f;
    bool isTrigger = false;
};

// Result of a Physics::Raycast query.
struct RaycastHit
{
    GameObject* gameObject = nullptr;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
};

struct Physics
{
    Engine* engine;
    std::unique_ptr<BVHTree> bvhTree;
    std::vector<std::unique_ptr<Collider>> colliders;
    std::vector<std::pair<Collider*, Collider*>> colliderPairs;   // observers into `colliders`
    std::vector<CollisionData> collisionData;

    // --- Contact events (OnCollision/OnTrigger Enter/Exit) ---
    // Contact info captured per collider pair, aggregated across all substeps
    // of one UpdatePhysics call (each substep clears collisionData, so pairs
    // are accumulated after every substep via AccumulateFrameContacts).
    struct ContactInfo { glm::vec3 point, normal; float depth; bool isTrigger; };
    using ContactKey = std::pair<Collider*, Collider*>;          // ordered (min,max) by pointer
    std::map<ContactKey, ContactInfo> frameContacts;             // touching pairs this frame
    std::set<ContactKey> prevContacts;                            // touching pairs last frame
    std::vector<ContactEvent> enterEvents, exitEvents;            // consumed+cleared by Engine

    float t = 0, deltaTime = 1.0f / 60; int iterations = 4;

    float gravity = 9.8f;
    float linearDamping = 0.1f;
    float angularDamping = 0.2f;
    float restitution = 0.3f;
    float staticFriction = 0.8f;
    float dynamicFriction = 0.5f;
    float positionCorrectionFactor = 0.8f;

    virtual ~Physics() = default;

    virtual void GenerateColliders(const std::vector<GameObject*>& gameobjects);
    virtual void CollectCollidersRecursive(GameObject* obj, std::vector<std::unique_ptr<Collider>>& outColliders,
        RigidbodyComponent* activeBody = nullptr, TransformComponent* activeBodyTransform = nullptr, bool parentIsDynamic = false);
    virtual void ClearColliders();  // 清除所有碰撞体
    virtual void UpdatePhysics(float dt);

    virtual void IntegrateForce(float dt);
    virtual void HandleBroadCollisions();
    virtual void HandleNarrowCollisions();
    virtual void SolveCollisions(int iter);
    virtual void ApplyPositionCorrections();

    void ApplyImpulse(Collider* a, Collider* b, const glm::vec3& normal,
        const glm::vec3& contactPointA, const glm::vec3& contactPointB,
        float penetrationDepth, int iteration);

    // Record the current substep's collisionData pairs into frameContacts.
    // Call after every substep (collisionData is cleared per substep).
    void AccumulateFrameContacts();
    // Diff frameContacts against prevContacts -> enter/exit events; then
    // promote frameContacts to prevContacts and clear it. Call once per
    // UpdatePhysics after the substep loop. Both Physics and ParallelPhysics
    // UpdatePhysics implementations must call this.
    void DetectContactEvents();
    // Drop all contact-tracking state (colliders rebuilt or play stopped).
    void ResetContactState();

    // Cast a ray against the physics colliders (BVH broad phase + exact
    // ray-triangle narrow phase against collider meshes). Only valid while
    // colliders exist (Play mode). Returns true and fills `out` on hit.
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, RaycastHit& out) const;
};
