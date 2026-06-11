#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Physics.h"
#include "../Core/Engine.h"
#include "../Core/PathUtils.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <filesystem>
#include <unordered_set>

static RigidbodyComponent* GetSharedStaticRigidbody()
{
    static RigidbodyComponent staticRigidbody;
    static bool initialized = false;
    if (!initialized)
    {
        staticRigidbody.type = RigidbodyComponent::Static;
        staticRigidbody.useGravity = false;
        staticRigidbody.mass = 1.0f;
        staticRigidbody.velocity = glm::vec3(0.0f);
        staticRigidbody.angularVelocity = glm::vec3(0.0f);
        initialized = true;
    }
    return &staticRigidbody;
}

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

    for (auto& collider : colliders)
    {
        collider->bodyTransform->UpdateTransform();
        collider->objectTransform->UpdateTransform();
        collider->UpdateBiasWorldModel();
    }
}

void Physics::GenerateColliders(const std::vector<GameObject*>& gameobjects)
{
    // Resetting first also fixes the old dangling bvhTree: it was deleted but
    // not nulled, so an empty collect left it pointing at freed memory.
    colliders.clear();
    bvhTree.reset();

    for (GameObject* root : gameobjects) CollectCollidersRecursive(root, colliders);

    if (!colliders.empty())
    {
        std::vector<Collider*> rawColliders;
        rawColliders.reserve(colliders.size());
        for (auto& collider : colliders) rawColliders.push_back(collider.get());
        bvhTree = std::make_unique<BVHTree>(rawColliders);
    }
}

void Physics::ClearColliders()
{
    colliders.clear();
    bvhTree.reset();
}

void Physics::CollectCollidersRecursive(GameObject* obj, std::vector<std::unique_ptr<Collider>>& outColliders,
    RigidbodyComponent* activeBody, TransformComponent* activeBodyTransform, bool parentIsDynamic)
{
    if (!obj->enabled) return;

    TransformComponent* transform = obj->GetComponent<TransformComponent>();
    RendererComponent* renderer = obj->GetComponent<RendererComponent>();
    RigidbodyComponent* rigidbody = obj->GetComponent<RigidbodyComponent>();
    std::vector<ColliderComponent*> colliderComps = obj->GetComponents<ColliderComponent>();

    RigidbodyComponent* body = activeBody;
    TransformComponent* bodyTransform = activeBodyTransform;
    bool currentParentIsDynamic = parentIsDynamic;
    if (transform && rigidbody)
    {
        body = rigidbody;
        bodyTransform = transform;
        currentParentIsDynamic = rigidbody->type == RigidbodyComponent::Dynamic;
    }

    const bool legacyImplicitCollider = colliderComps.empty() && renderer && rigidbody;
    const size_t colliderCount = legacyImplicitCollider ? 1 : colliderComps.size();
    for (size_t colliderIndex = 0; transform && colliderIndex < colliderCount; ++colliderIndex)
    {
        ColliderComponent* colliderComp = legacyImplicitCollider ? nullptr : colliderComps[colliderIndex];
        if (colliderComp && !colliderComp->enabled) continue;

        auto owned = std::make_unique<Collider>();
        Collider* collider = owned.get();
        collider->objectTransform = transform;
        collider->biasLocalModel = colliderComp ? colliderComp->GetBiasMatrix() : glm::mat4(1.0f);
        collider->bodyTransform = bodyTransform ? bodyTransform : transform;
        collider->rigidbody = body;
        collider->isTrigger = colliderComp ? colliderComp->isTrigger : false;

        if (!collider->rigidbody)
        {
            collider->rigidbody = GetSharedStaticRigidbody();
            collider->bodyTransform = transform;
        }

        // A Dynamic body nested under another Dynamic body would double-count
        // gravity: its world pose is parentWorld * localPose, so the parent's
        // fall is inherited through the hierarchy and then integrated again
        // locally. Demote such a child to Kinematic -- it then follows the
        // parent rigidly (Kinematic refreshes its AABB every step) without
        // being independently simulated. Set a body to Kinematic yourself to
        // get this "follow the parent" behavior on purpose.
        if (parentIsDynamic && rigidbody && collider->rigidbody->type == RigidbodyComponent::Dynamic)
            collider->rigidbody->type = RigidbodyComponent::Kinematic;

        ColliderComponent::Type colliderType = ColliderComponent::Box;
        std::string colliderMeshPath;
        glm::vec3 colliderScale = collider->bodyTransform->scale * (colliderComp ? colliderComp->biasScale : glm::vec3(1.0f));
        if (colliderComp)
        {
            colliderType = colliderComp->type;
            colliderMeshPath = colliderComp->meshPath;
        }
        else if (renderer)
        {
            colliderType = (renderer->type == RendererComponent::Sphere)
                ? ColliderComponent::Sphere : ColliderComponent::Box;
        }

        switch (colliderType)
        {
        case ColliderComponent::Box:
            collider->mesh = engine->resource->cubeMesh.get();
            collider->rigidbody->CalculateInertia(RendererComponent::Cube, colliderScale);
            break;
        case ColliderComponent::Sphere:
            collider->mesh = engine->resource->sphereMesh.get();
            collider->rigidbody->CalculateInertia(RendererComponent::Sphere, colliderScale);
            break;
        case ColliderComponent::MeshConvex:
        {
            if (colliderMeshPath.empty() && renderer)
                colliderMeshPath = renderer->meshPath;

            std::filesystem::path resolved = colliderMeshPath;
            if (!colliderMeshPath.empty() && !std::filesystem::exists(resolved))
                resolved = PathUtils::ResolveAsset(colliderMeshPath);

            if (!colliderMeshPath.empty() && resolved.extension() == ".obj")
            {
                collider->ownedMesh = std::make_unique<MeshData>(resolved.string(), false);
                collider->mesh = collider->ownedMesh.get();
                collider->rigidbody->CalculateInertia(RendererComponent::Cube, colliderScale);
                DITTO_LOG_INFO_STREAM("[Physics] MeshConvex collider loaded OBJ point cloud: "
                    << resolved.string() << " (" << collider->mesh->vertices.size() << " verts)");
            }
            else
            {
                DITTO_LOG_WARN_STREAM("[Physics] MeshConvex collider requires an OBJ mesh path; using Box collider on " << obj->name);
                collider->mesh = engine->resource->cubeMesh.get();
                collider->rigidbody->CalculateInertia(RendererComponent::Cube, colliderScale);
            }
            break;
        }
        }

        if (collider->mesh && !collider->mesh->vertices.empty())
        {
            collider->localAABB = AABB(collider->mesh->aabbMin, collider->mesh->aabbMax);
		    collider->UpdateWorldAABB(); outColliders.push_back(std::move(owned)); collider->id = outColliders.size();
        }
    }

    for (const auto& child : obj->children)
        CollectCollidersRecursive(child.get(), outColliders, body, bodyTransform, currentParentIsDynamic);
}

void Physics::IntegrateForce(float dt)
{
    std::unordered_set<RigidbodyComponent*> integratedBodies;
    for (auto& collider : colliders)
    {
        if (!integratedBodies.insert(collider->rigidbody).second)
        {
            collider->isDirty = true;
            continue;
        }

        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            TransformComponent* transform = collider->bodyTransform;
            RigidbodyComponent* rb = collider->rigidbody;

            if (rb->useGravity) rb->velocity.y += -gravity * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));
            transform->position += rb->velocity * dt;
            transform->rotation += rb->angularVelocity * dt;

            transform->localDirty = true; collider->isDirty = true;
        }
        else if (collider->rigidbody->type == RigidbodyComponent::Kinematic)
        {
            // Driven by the Transform hierarchy (script/parent), not integrated.
            // Refresh the AABB each step so it broad-phases at its current pose.
            collider->isDirty = true;
        }
    }
}

void Physics::HandleBroadCollisions()
{
    for (auto& colliderPtr : colliders)
    {
        Collider* collider = colliderPtr.get();
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            std::vector<Collider*> potentialCollisions;
            if (bvhTree) potentialCollisions = bvhTree->Query(collider->aabb);

            for (Collider* other : potentialCollisions)
            {
                if (other == collider) continue;
                if (other->rigidbody == collider->rigidbody) continue;

                bool alreadyExists = false;
                for (const auto& pair : colliderPairs)
                    if ((pair.first == collider && pair.second == other) || (pair.first == other && pair.second == collider))
                    {
                        alreadyExists = true; break;
                    }

                // Static/Dynamic/Kinematic are all valid partners for a Dynamic
                // body (Static & Kinematic are infinite-mass obstacles).
                if (!alreadyExists)
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
        if (colliderA->rigidbody == colliderB->rigidbody) continue;
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
         if (data.colliderA->isTrigger || data.colliderB->isTrigger) { data.processed = true; continue; }

         ApplyImpulse(data.colliderA, data.colliderB, data.info.normal, data.info.contactPointA, data.info.contactPointB, data.info.depth, iter);

         data.processed = true;
     }
}

glm::mat3 CalculateWorldInverseInertia(Collider* collider)
{
    if (collider->rigidbody->type != RigidbodyComponent::Dynamic)
        return glm::mat3(0.0f);

    glm::mat4 worldMat = collider->bodyTransform->GetWorldModel();
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
    glm::vec3 rA = contactPointA - a->bodyTransform->position, rB = contactPointB - b->bodyTransform->position;

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

    float biasFactor = 0.3f * (1.0f - float(iteration) / iterations);
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
                a->rigidbody->velocity -= tangentImpulse * invMassA,
                a->rigidbody->angularVelocity += invInertiaA * glm::cross(rA, tangentImpulse);
            if (b->rigidbody->type == RigidbodyComponent::Dynamic)
                b->rigidbody->velocity += tangentImpulse * invMassB,
                b->rigidbody->angularVelocity += invInertiaB * glm::cross(rB, -tangentImpulse);
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
            if (a->isTrigger || b->isTrigger) continue;
            const CollisionInfo& info = data.info;

            float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
            float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
            float totalInvMass = invMassA + invMassB;

            glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

            a->bodyTransform->position -= correction * invMassA;
            b->bodyTransform->position += correction * invMassB;
            a->bodyTransform->localDirty = true;
            b->bodyTransform->localDirty = true;

            a->isDirty = true; b->isDirty = true;
        }
    }
}
