#include "ParallelPhysics.h"
#include <algorithm>
#include <vector>

ThreadPool ParallelPhysics::threadPool;

void ParallelPhysics::UpdatePhysics(float dt) {
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step) {
        collisionData.clear();
        colliderPairs.clear();

        IntegrateForce(deltaTime);
        if (bvhTree) bvhTree->UpdateBVHTree();
        HandleBroadCollisions();
        HandleNarrowCollisions();
        BuildCollisionGroups();

        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);

        ApplyPositionCorrections();
    }

    size_t n = colliders.size();
    if (n > 0) {
        threadPool.ParallelFor(0, n, [this](size_t i) {
            colliders[i]->transform->UpdateTransform(); });
    }
}

void ParallelPhysics::IntegrateForce(float dt) {
    size_t n = colliders.size();
    if (n == 0) return;

    threadPool.ParallelFor(0, n, [this, dt](size_t i) {
        Collider* collider = colliders[i];
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic) {
            auto* transform = collider->transform;
            auto* rb = collider->rigidbody;

            if (rb->useGravity)
                rb->velocity.y += -gravity * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));
            transform->position += rb->velocity * dt;
            transform->rotation += rb->angularVelocity * dt;

            transform->localDirty = true;
            collider->isDirty = true;
        }
        else if (collider->rigidbody->type == RigidbodyComponent::Kinematic) {
            // Kinematic bodies are driven by the Transform hierarchy (script or
            // parent), never integrated -- so no gravity, no double-counting.
            // Force an AABB refresh each step so a moving platform / parent-
            // following child is broad-phased at its current world pose.
            collider->isDirty = true;
        }
        });
}

void ParallelPhysics::HandleBroadCollisions() {
    colliderPairs.clear();
    if (!bvhTree) return;

    std::vector<size_t> dynamicIndices;
    for (size_t i = 0; i < colliders.size(); ++i) {
        if (colliders[i]->rigidbody->type == RigidbodyComponent::Dynamic)
            dynamicIndices.push_back(i);
    }
    size_t numDyn = dynamicIndices.size();
    if (numDyn == 0) return;

    size_t numThreads = threadPool.workers.size();
    size_t chunkSize = (numDyn + numThreads - 1) / numThreads;
    size_t numBlocks = (numDyn + chunkSize - 1) / chunkSize;

    std::vector<std::vector<std::pair<Collider*, Collider*>>> localPairs(numBlocks);

    threadPool.ParallelFor(0, numBlocks, [&](size_t blockIdx) {
        size_t start = blockIdx * chunkSize;
        size_t end = std::min(start + chunkSize, numDyn);
        auto& local = localPairs[blockIdx];
        for (size_t idx = start; idx < end; ++idx) {
            Collider* collider = colliders[dynamicIndices[idx]];
            std::vector<Collider*> potential = bvhTree->Query(collider->aabb);
            for (Collider* other : potential) {
                if (other == collider) continue;
                // All body types are valid partners for a Dynamic body: Static
                // and Kinematic both act as infinite-mass obstacles (Kinematic
                // additionally moves and pushes this Dynamic body).
                if (collider < other)
                    local.emplace_back(collider, other);
                else
                    local.emplace_back(other, collider);
            }
        }
        });

    std::vector<std::pair<Collider*, Collider*>> allPairs;
    for (auto& local : localPairs) {
        allPairs.insert(allPairs.end(), local.begin(), local.end());
    }

    std::sort(allPairs.begin(), allPairs.end());
    allPairs.erase(std::unique(allPairs.begin(), allPairs.end()), allPairs.end());
    colliderPairs = std::move(allPairs);
}

void ParallelPhysics::HandleNarrowCollisions() {
    collisionData.clear();
    size_t numPairs = colliderPairs.size();
    if (numPairs == 0) return;

    size_t numThreads = threadPool.workers.size();
    size_t chunkSize = (numPairs + numThreads - 1) / numThreads;
    size_t numBlocks = (numPairs + chunkSize - 1) / chunkSize;

    std::vector<std::vector<CollisionData>> localData(numBlocks);

    threadPool.ParallelFor(0, numBlocks, [&](size_t blockIdx) {
        size_t start = blockIdx * chunkSize;
        size_t end = std::min(start + chunkSize, numPairs);
        auto& local = localData[blockIdx];
        for (size_t i = start; i < end; ++i) {
            auto& pair = colliderPairs[i];
            CollisionInfo info = GJK_CheckCollision(pair.first, pair.second);
            if (info.flag && info.depth > 1e-3f) local.emplace_back(pair.first, pair.second, info);
        }});

    for (auto& local : localData) {
        collisionData.insert(collisionData.end(), local.begin(), local.end());
    }
}

void ParallelPhysics::BuildCollisionGroups() {
    collisionGroups.clear();
    if (collisionData.empty()) return;

    std::sort(collisionData.begin(), collisionData.end(),
        [](const CollisionData& a, const CollisionData& b) {
            return a.info.depth > b.info.depth;
        });

    static std::vector<int> bodyMask;
    static std::vector<int> bodyEpoch;
    static int globalEpoch = 0;

    size_t maxId = 0;
    for (auto& data : collisionData) {
        maxId = std::max(maxId, (size_t)data.colliderA->id);
        maxId = std::max(maxId, (size_t)data.colliderB->id);
    }
    if (bodyMask.size() <= maxId) {
        bodyMask.resize(maxId + 1, 0);
        bodyEpoch.resize(maxId + 1, 0);
    }

    ++globalEpoch;

    for (auto& data : collisionData) {
        int aId = data.colliderA->id;
        int bId = data.colliderB->id;
        int groupIdx = 0;
        while (true) {
            if (groupIdx >= (int)collisionGroups.size()) {
                collisionGroups.emplace_back();
                collisionGroups.back().push_back(&data);
                bodyMask[aId] = groupIdx + 1;
                bodyEpoch[aId] = globalEpoch;
                bodyMask[bId] = groupIdx + 1;
                bodyEpoch[bId] = globalEpoch;
                break;
            }
            bool aOccupied = (bodyEpoch[aId] == globalEpoch && bodyMask[aId] == groupIdx + 1);
            bool bOccupied = (bodyEpoch[bId] == globalEpoch && bodyMask[bId] == groupIdx + 1);
            if (!aOccupied && !bOccupied) {
                collisionGroups[groupIdx].push_back(&data);
                bodyMask[aId] = groupIdx + 1;
                bodyEpoch[aId] = globalEpoch;
                bodyMask[bId] = groupIdx + 1;
                bodyEpoch[bId] = globalEpoch;
                break;
            }
            ++groupIdx;
        }
    }
}

void ParallelPhysics::SolveCollisions(int iter) {
    for (auto& group : collisionGroups) {
        size_t groupSize = group.size();
        if (groupSize == 0) continue;
        if (groupSize < MIN_PARALLEL_GROUP_SIZE) {
            for (CollisionData* data : group) {
                ApplyImpulse(data->colliderA, data->colliderB,
                    data->info.normal,
                    data->info.contactPointA,
                    data->info.contactPointB,
                    data->info.depth, iter);
            }
        }
        else {
            threadPool.ParallelFor(0, groupSize, [this, iter, &group](size_t idx) {
                CollisionData* data = group[idx];
                ApplyImpulse(data->colliderA, data->colliderB,
                    data->info.normal,
                    data->info.contactPointA,
                    data->info.contactPointB,
                    data->info.depth, iter);
                });
        }
    }
}

void ParallelPhysics::ApplyPositionCorrections() {
    for (auto& group : collisionGroups) {
        size_t groupSize = group.size();
        if (groupSize == 0) continue;
        if (groupSize < MIN_PARALLEL_GROUP_SIZE) {
            for (CollisionData* data : group) {
                if (data->info.depth > 1e-3f) {
                    Collider* a = data->colliderA;
                    Collider* b = data->colliderB;
                    const CollisionInfo& info = data->info;

                    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                    float totalInvMass = invMassA + invMassB;

                    glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                    a->transform->position -= correction * invMassA;
                    b->transform->position += correction * invMassB;

                    a->isDirty = true;
                    b->isDirty = true;
                }
            }
        }
        else {
            threadPool.ParallelFor(0, groupSize, [this, &group](size_t idx) {
                CollisionData* data = group[idx];
                if (data->info.depth > 1e-3f) {
                    Collider* a = data->colliderA;
                    Collider* b = data->colliderB;
                    const CollisionInfo& info = data->info;

                    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                    float totalInvMass = invMassA + invMassB;

                    glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                    a->transform->position -= correction * invMassA;
                    b->transform->position += correction * invMassB;

                    a->isDirty = true;
                    b->isDirty = true;
                }
                });
        }
    }
}