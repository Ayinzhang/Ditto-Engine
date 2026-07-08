#pragma once

#include <vector>
#include <set>
#include <map>
#include <utility>
#include "../../3rdParty/GLM/glm.hpp"

struct GameObject;
struct Scene;
struct TransformComponent;
struct Rigidbody2DComponent;
struct Collider2DComponent;

struct ContactEvent2D
{
    GameObject* a = nullptr;
    GameObject* b = nullptr;
    glm::vec2 point{ 0.0f };
    glm::vec2 normal{ 0.0f };   
    float depth = 0.0f;
    bool isTrigger = false;
};

struct Physics2DBody
{
    GameObject* object = nullptr;
    TransformComponent* transform = nullptr;
    Rigidbody2DComponent* rigidbody = nullptr;
    Collider2DComponent* collider = nullptr;
};

class Physics2DWorld
{
public:
    struct Contact
    {
        int a = -1;
        int b = -1;
        glm::vec2 normal{ 0.0f };
        glm::vec2 point{ 0.0f };
        float depth = 0.0f;
        bool isTrigger = false;
    };

    float fixedDeltaTime = 0.02f;
    glm::vec2 gravity{ 0.0f, -9.8f };
    int velocityIterations = 6;
    int positionIterations = 2;

    std::vector<ContactEvent2D> enterEvents;
    std::vector<ContactEvent2D> exitEvents;

    void Step(Scene* scene, float dt);
    void StepFixed(Scene* scene, float dt);
    void Rebuild(Scene* scene);
    void Clear();

private:
    std::vector<Physics2DBody> bodies;
    std::vector<Contact> contacts;
    float accumulator = 0.0f;
    using ContactKey = std::pair<GameObject*, GameObject*>;
    std::set<ContactKey> previousContacts;

    void FixedStep(Scene* scene, float dt);
    void CollectBodies(Scene* scene);
    void IntegrateForces(float dt);
    void DetectCollisions();
    void SolveVelocity(float dt);
    void SolvePositions();
    void UpdateSleeping(float dt);
    void SyncTransforms();
    void DetectContactEvents();

    static ContactKey MakeKey(GameObject* a, GameObject* b);
};
