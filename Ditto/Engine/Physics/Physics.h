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



struct ContactEvent
{
    GameObject* a = nullptr;
    GameObject* b = nullptr;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };   
    float depth = 0.0f;
    bool isTrigger = false;
};


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
    std::vector<std::pair<Collider*, Collider*>> colliderPairs;   
    std::vector<CollisionData> collisionData;

    
    
    
    
    struct ContactInfo { glm::vec3 point, normal; float depth; bool isTrigger; };
    using ContactKey = std::pair<Collider*, Collider*>;          
    std::map<ContactKey, ContactInfo> frameContacts;             
    std::set<ContactKey> prevContacts;                            
    std::vector<ContactEvent> enterEvents, exitEvents;            

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
    virtual void ClearColliders();  
    virtual void UpdatePhysics(float dt);

    virtual void IntegrateForce(float dt);
    virtual void HandleBroadCollisions();
    virtual void HandleNarrowCollisions();
    virtual void SolveCollisions(int iter);
    virtual void ApplyPositionCorrections();

    void ApplyImpulse(Collider* a, Collider* b, const glm::vec3& normal,
        const glm::vec3& contactPointA, const glm::vec3& contactPointB,
        float penetrationDepth, int iteration);

    
    
    void AccumulateFrameContacts();
    
    
    
    
    void DetectContactEvents();
    
    void ResetContactState();

    
    
    
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, RaycastHit& out) const;
};
