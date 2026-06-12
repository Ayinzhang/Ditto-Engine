#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Physics.h"
#ifdef DITTO_HEADLESS_TESTS
#include "../Resources/Resource.h"
struct Engine
{
    std::unique_ptr<Resource> resource;
};
#else
#include "../Core/Engine.h"
#endif
#include "../Core/PathUtils.h"
#include "../Core/Logger.h"
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

        AccumulateFrameContacts();

        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);

        ApplyPositionCorrections();
    }

    DetectContactEvents();

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
    ResetContactState();

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
    ResetContactState();
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

// ==================== Contact events ====================

static GameObject* ColliderGameObject(Collider* c)
{
    return (c && c->objectTransform) ? c->objectTransform->gameObject : nullptr;
}

void Physics::AccumulateFrameContacts()
{
    for (const auto& data : collisionData)
    {
        Collider* a = data.colliderA; Collider* b = data.colliderB;
        ContactKey key = (a < b) ? ContactKey{ a, b } : ContactKey{ b, a };

        ContactInfo info;
        info.point = (data.info.contactPointA + data.info.contactPointB) * 0.5f;
        // Keep the normal oriented from key.first towards key.second so event
        // consumers get a stable convention regardless of GJK pair order.
        info.normal = (a < b) ? data.info.normal : -data.info.normal;
        info.depth = data.info.depth;
        info.isTrigger = a->isTrigger || b->isTrigger;

        // Last substep wins (most recent contact info for the frame).
        frameContacts[key] = info;
    }
}

void Physics::DetectContactEvents()
{
    enterEvents.clear();
    exitEvents.clear();

    // Enter: touching now, not touching last frame.
    for (const auto& [key, info] : frameContacts)
    {
        if (prevContacts.count(key)) continue;
        ContactEvent ev;
        ev.a = ColliderGameObject(key.first);
        ev.b = ColliderGameObject(key.second);
        ev.point = info.point; ev.normal = info.normal;
        ev.depth = info.depth; ev.isTrigger = info.isTrigger;
        if (ev.a && ev.b) enterEvents.push_back(ev);
    }

    // Exit: touching last frame, not touching now.
    for (const auto& key : prevContacts)
    {
        if (frameContacts.count(key)) continue;
        ContactEvent ev;
        ev.a = ColliderGameObject(key.first);
        ev.b = ColliderGameObject(key.second);
        // No contact data on exit; report trigger flag from the colliders.
        ev.isTrigger = key.first->isTrigger || key.second->isTrigger;
        if (ev.a && ev.b) exitEvents.push_back(ev);
    }

    prevContacts.clear();
    for (const auto& [key, info] : frameContacts) prevContacts.insert(key);
    frameContacts.clear();
}

void Physics::ResetContactState()
{
    frameContacts.clear();
    prevContacts.clear();
    enterEvents.clear();
    exitEvents.clear();
}

// ==================== Raycast ====================

// Slab-method ray vs AABB intersection. Returns true if the ray hits within
// [0, maxDist]; tMin receives the entry distance (clamped to 0 inside the box).
static bool RayIntersectsAABB(const glm::vec3& origin, const glm::vec3& invDir,
    const AABB& box, float maxDist, float& tMin)
{
    float t0 = 0.0f, t1 = maxDist;
    for (int axis = 0; axis < 3; ++axis)
    {
        float tNear = (box.min[axis] - origin[axis]) * invDir[axis];
        float tFar = (box.max[axis] - origin[axis]) * invDir[axis];
        if (tNear > tFar) std::swap(tNear, tFar);
        t0 = glm::max(t0, tNear);
        t1 = glm::min(t1, tFar);
        if (t0 > t1) return false;
    }
    tMin = t0;
    return true;
}

// Moller-Trumbore ray-triangle intersection (one-sided not enforced; both
// faces hit so rays work from inside boxes too).
static bool RayIntersectsTriangle(const glm::vec3& orig, const glm::vec3& dir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t)
{
    const float EPSILON = 1e-6f;
    glm::vec3 edge1 = v1 - v0, edge2 = v2 - v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * glm::dot(edge2, q);
    return t > EPSILON;
}

bool Physics::Raycast(const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, RaycastHit& out) const
{
    glm::vec3 dir = direction;
    float len = glm::length(dir);
    if (len < 1e-6f || maxDistance <= 0.0f || colliders.empty()) return false;
    dir /= len;

    glm::vec3 invDir(
        1.0f / (dir.x != 0.0f ? dir.x : 1e-30f),
        1.0f / (dir.y != 0.0f ? dir.y : 1e-30f),
        1.0f / (dir.z != 0.0f ? dir.z : 1e-30f));

    float closestT = maxDistance;
    Collider* closestCollider = nullptr;
    glm::vec3 closestNormal(0.0f);

    for (const auto& colliderPtr : colliders)
    {
        Collider* collider = colliderPtr.get();
        if (!collider->mesh || collider->mesh->vertices.empty()) continue;

        float aabbT;
        if (!RayIntersectsAABB(origin, invDir, collider->aabb, closestT, aabbT)) continue;

        const glm::mat4& worldMat = collider->biasWorldModel;
        const auto& verts = collider->mesh->vertices;
        const auto& indices = collider->mesh->indices;

        // Indexed triangles when available, else consecutive vertex triples.
        size_t triCount = indices.size() >= 3 ? indices.size() / 3
            : verts.size() / 3;

        for (size_t i = 0; i < triCount; ++i)
        {
            glm::vec3 v0, v1, v2;
            if (indices.size() >= 3)
            {
                v0 = glm::vec3(worldMat * glm::vec4(verts[indices[i * 3 + 0]], 1.0f));
                v1 = glm::vec3(worldMat * glm::vec4(verts[indices[i * 3 + 1]], 1.0f));
                v2 = glm::vec3(worldMat * glm::vec4(verts[indices[i * 3 + 2]], 1.0f));
            }
            else
            {
                v0 = glm::vec3(worldMat * glm::vec4(verts[i * 3 + 0], 1.0f));
                v1 = glm::vec3(worldMat * glm::vec4(verts[i * 3 + 1], 1.0f));
                v2 = glm::vec3(worldMat * glm::vec4(verts[i * 3 + 2], 1.0f));
            }

            float triT;
            if (RayIntersectsTriangle(origin, dir, v0, v1, v2, triT) && triT < closestT)
            {
                closestT = triT;
                closestCollider = collider;
                glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                // Face the normal against the ray.
                if (glm::dot(n, dir) > 0.0f) n = -n;
                closestNormal = n;
            }
        }
    }

    if (!closestCollider) return false;

    out.gameObject = ColliderGameObject(closestCollider);
    out.point = origin + dir * closestT;
    out.normal = closestNormal;
    out.distance = closestT;
    return out.gameObject != nullptr;
}
