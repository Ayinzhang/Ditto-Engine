#include "Physics.h"
#include "../Core/Engine.h"
#include <iostream>
#include <algorithm>
#include <set>

void Physics::UpdatePhysics(float dt)
{
    t += dt; if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime); t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step)
    {
        collisionData.clear(); colliderPairs.clear();

        IntegrateForce(deltaTime);

        if (bvhTree) bvhTree->UpdateBVHTree();

        HandleBroadCollisions();

        HandleNarrowCollisions();

        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);

        ApplyPositionCorrections();
    }

    for (auto collider : colliders) collider->transform->UpdateTransform();
}

void Physics::GenerateColliders(const std::vector<GameObject*>& gameobjects)
{
    for (auto collider : colliders) delete collider; colliders.clear();
    if (bvhTree) delete bvhTree;

    for (GameObject* root : gameobjects) CollectCollidersRecursive(root, colliders);

    if (!colliders.empty())
        bvhTree = new BVHTree(colliders);
}

void Physics::CollectCollidersRecursive(GameObject* obj, std::vector<Collider*>& outColliders, bool parentIsDynamic)
{
    if (!obj->enabled) return;

    TransformComponent* transform = obj->GetComponent<TransformComponent>();
    RendererComponent* renderer = obj->GetComponent<RendererComponent>();
    RigidbodyComponent* rigidbody = obj->GetComponent<RigidbodyComponent>();

    if (transform && renderer && rigidbody)
    {
        Collider* collider = new Collider();
        collider->transform = transform;
        collider->rigidbody = rigidbody;

        if (parentIsDynamic) collider->rigidbody->type = RigidbodyComponent::Static;

        switch (renderer->type)
        {
        case RendererComponent::Cube:  collider->mesh = engine->resource->cubeMesh; break;
        case RendererComponent::Sphere:collider->mesh = engine->resource->sphereMesh; break;
        default: break;
        }

        rigidbody->CalculateInertia(renderer->type, transform->scale);
        collider->localAABB = AABB(collider->mesh->aabbMin, collider->mesh->aabbMax);
		collider->UpdateWorldAABB(); outColliders.push_back(collider); collider->id = outColliders.size();
    }

    bool nextParentIsDynamic = parentIsDynamic || (rigidbody && rigidbody ->type == RigidbodyComponent::Dynamic);
    for (GameObject* child : obj->children) CollectCollidersRecursive(child, outColliders, nextParentIsDynamic);
}

void Physics::IntegrateForce(float dt)
{
    for (Collider* collider : colliders)
    {
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            TransformComponent* transform = collider->transform;
            RigidbodyComponent* rb = collider->rigidbody;

            if (rb->useGravity) rb->velocity.y += -gravity * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));
            transform->position += rb->velocity * dt;
            transform->rotation += rb->angularVelocity * dt;

            transform->localDirty = true; collider->isDirty = true;
        }
    }
}

void Physics::HandleBroadCollisions()
{
    for (Collider* collider : colliders)
    {
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            std::vector<Collider*> potentialCollisions;
            if (bvhTree) potentialCollisions = bvhTree->Query(collider->aabb);

            for (Collider* other : potentialCollisions)
            {
                if (other == collider) continue;

                bool alreadyExists = false;
                for (const auto& pair : colliderPairs)
                    if ((pair.first == collider && pair.second == other) || (pair.first == other && pair.second == collider))
                    {
                        alreadyExists = true; break;
                    }

                if (!alreadyExists)
                    if (other->rigidbody->type == RigidbodyComponent::Dynamic || other->rigidbody->type == RigidbodyComponent::Static)
                        colliderPairs.push_back({ collider, other });
            }
        }
    }
}

void Physics::HandleNarrowCollisions()
{
    for (auto& pair : colliderPairs)
    {
        Collider* colliderA = pair.first, * colliderB = pair.second;
        CollisionInfo collisionInfo = GJK_CheckCollision(colliderA, colliderB);

        if (collisionInfo.flag && collisionInfo.depth > 1e-3)
            collisionData.push_back(CollisionData(colliderA, colliderB, collisionInfo));
    }
}

void Physics::SolveCollisions(int iter)
{
     for (auto& data : collisionData) data.processed = false;

     std::sort(collisionData.begin(), collisionData.end(), 
         [](const CollisionData& a, const CollisionData& b) { return a.info.depth > b.info.depth; });

     for (auto& data : collisionData)
     {
         if (data.processed) continue;

         ApplyImpulse(data.colliderA, data.colliderB, data.info.normal, data.info.contactPointA, data.info.contactPointB, data.info.depth, iter);

         data.processed = true;
     }
}

glm::mat3 CalculateWorldInverseInertia(Collider* collider)
{
    if (collider->rigidbody->type != RigidbodyComponent::Dynamic)
        return glm::mat3(0.0f);

    glm::mat4 worldMat = collider->transform->GetWorldModel();
    glm::mat3 rotationMatrix = glm::mat3(worldMat);

    return rotationMatrix * collider->rigidbody->inverseInertia * glm::transpose(rotationMatrix);
}

void Physics::ApplyImpulse(Collider* a, Collider* b, const glm::vec3& normal,
    const glm::vec3& contactPointA, const glm::vec3& contactPointB,
    float penetrationDepth, int iteration)
{
    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;


    glm::mat3 invInertiaA = CalculateWorldInverseInertia(a), invInertiaB = CalculateWorldInverseInertia(b);
    glm::vec3 rA = contactPointA - a->transform->position, rB = contactPointB - b->transform->position;

    glm::vec3 velA = a->rigidbody->velocity + glm::cross(a->rigidbody->angularVelocity, rA);
    glm::vec3 velB = b->rigidbody->velocity + glm::cross(b->rigidbody->angularVelocity, rB);
    glm::vec3 relativeVel = velB - velA;

    float normalVel = glm::dot(relativeVel, normal);
    if (normalVel > 0.2f) return;

    glm::vec3 crossA = glm::cross(rA, normal);
    glm::vec3 crossB = glm::cross(rB, normal);

    glm::vec3 invInertiaCrossA = invInertiaA * crossA;
    glm::vec3 invInertiaCrossB = invInertiaB * crossB;

    float termA = invMassA + glm::dot(crossA, invInertiaCrossA);
    float termB = invMassB + glm::dot(crossB, invInertiaCrossB);

    float denominator = termA + termB;
    if (denominator == 0.0f) return;

    float biasFactor = 0.3f *(1.0f - float(iteration) / iterations);
    float bias = biasFactor * penetrationDepth / deltaTime;
    float j = -(1.0f + restitution) * normalVel + bias;
    j = glm::max(0.0f, j / denominator);

    glm::vec3 impulse = j * normal;

    if (a->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        a->rigidbody->velocity -= impulse * invMassA;
        a->rigidbody->angularVelocity += invInertiaA * glm::cross(rA, impulse);
    }

    if (b->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        b->rigidbody->velocity += impulse * invMassB;
        glm::vec3 torque = glm::cross(rB, -impulse);
        b->rigidbody->angularVelocity += invInertiaB * glm::cross(rB, -impulse);
    }

    glm::vec3 tangent = relativeVel - normal * normalVel;
    float tangentLen = glm::length(tangent);

    if (tangentLen > 1e-3)
    {
        tangent = glm::normalize(tangent);
        float tangentVel = glm::dot(relativeVel, tangent);

        glm::vec3 crossAT = glm::cross(rA, tangent);
        glm::vec3 crossBT = glm::cross(rB, tangent);

        glm::vec3 invInertiaCrossAT = invInertiaA * crossAT;
        glm::vec3 invInertiaCrossBT = invInertiaB * crossBT;

        float termAT = invMassA + glm::dot(crossAT, invInertiaCrossAT);
        float termBT = invMassB + glm::dot(crossBT, invInertiaCrossBT);

        float denominatorT = termAT + termBT;

        if (denominatorT != 0.0f)
        {
            float jt = -tangentVel / denominatorT;

            float friction = (fabs(tangentVel) < 0.01f) ? staticFriction : dynamicFriction;
            float maxFriction = friction * fabs(j);
            jt = glm::clamp(jt, -maxFriction, maxFriction);

            glm::vec3 tangentImpulse = jt * tangent;
            if (a->rigidbody->type == RigidbodyComponent::Dynamic)
                a->rigidbody->velocity -= tangentImpulse * invMassA;

            if (b->rigidbody->type == RigidbodyComponent::Dynamic)
                b->rigidbody->velocity += tangentImpulse * invMassB;
        }
    }
}

void Physics::ApplyPositionCorrections()
{
    for (auto& data : collisionData)
    {
        if (data.info.depth > 1e-3)
        {
            Collider* a = data.colliderA;
            Collider* b = data.colliderB;
            const CollisionInfo& info = data.info;

            float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
            float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
            float totalInvMass = invMassA + invMassB;

            glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

            a->transform->position -= correction * invMassA;
            b->transform->position += correction * invMassB;

            a->isDirty = true; b->isDirty = true;
        }
    }
}
