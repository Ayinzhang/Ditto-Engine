#include "ParallelPhysics.h"
#include <algorithm>
#include <vector>
#include <unordered_set>

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
        AccumulateFrameContacts();
        BuildCollisionGroups();

        for (int iter = 0; iter < iterations; ++iter)
            SolveCollisions(iter);

        ApplyPositionCorrections();
    }

    DetectContactEvents();

    size_t n = colliders.size();
    if (n > 0) {
        threadPool.ParallelFor(0, n, [this](size_t i) {
            colliders[i]->bodyTransform->UpdateTransform();
            colliders[i]->objectTransform->UpdateTransform();
            colliders[i]->UpdateBiasWorldModel(); });
    }
}

void ParallelPhysics::IntegrateForce(float dt) {
    size_t n = colliders.size();
    if (n == 0) return;

    std::unordered_set<RigidbodyComponent*> integratedBodies;
    for (size_t i = 0; i < n; ++i) {
        Collider* collider = colliders[i].get();
        collider->isDirty = true;
        if (!integratedBodies.insert(collider->rigidbody).second)
            continue;

        if (collider->rigidbody->type == RigidbodyComponent::Dynamic) {
            auto* transform = collider->bodyTransform;
            auto* rb = collider->rigidbody;

            if (rb->useGravity)
                rb->velocity.y += -gravity * dt;

            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - linearDamping, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - angularDamping, dt));
            transform->position += rb->velocity * dt;

            
            
            
            if (!transform->useQuatRotation)
                transform->SeedOrientationFromEuler();
            glm::vec3 wRad = glm::radians(rb->angularVelocity);
            glm::quat spin(0.0f, wRad.x, wRad.y, wRad.z);
            transform->orientation = glm::normalize(
                transform->orientation + 0.5f * spin * transform->orientation * dt);
            
            transform->rotation = glm::degrees(glm::eulerAngles(transform->orientation));

            transform->localDirty = true;
            collider->isDirty = true;
        }
        else if (collider->rigidbody->type == RigidbodyComponent::Kinematic) {
            
            
            
            
            collider->isDirty = true;
        }
    }
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
            Collider* collider = colliders[dynamicIndices[idx]].get();
            std::vector<Collider*> potential = bvhTree->Query(collider->aabb);
            for (Collider* other : potential) {
                if (other == collider) continue;
                if (other->rigidbody == collider->rigidbody) continue;
                
                
                
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

    std::vector<std::unordered_set<RigidbodyComponent*>> groupBodies;

    for (auto& data : collisionData) {
        RigidbodyComponent* bodyA = data.colliderA->rigidbody;
        RigidbodyComponent* bodyB = data.colliderB->rigidbody;
        int groupIdx = 0;
        while (true) {
            if (groupIdx >= (int)collisionGroups.size()) {
                collisionGroups.emplace_back();
                groupBodies.emplace_back();
                collisionGroups.back().push_back(&data);
                groupBodies.back().insert(bodyA);
                groupBodies.back().insert(bodyB);
                break;
            }
            bool aOccupied = groupBodies[groupIdx].count(bodyA) > 0;
            bool bOccupied = groupBodies[groupIdx].count(bodyB) > 0;
            if (!aOccupied && !bOccupied) {
                collisionGroups[groupIdx].push_back(&data);
                groupBodies[groupIdx].insert(bodyA);
                groupBodies[groupIdx].insert(bodyB);
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
                if (data->colliderA->isTrigger || data->colliderB->isTrigger) continue;
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
                if (data->colliderA->isTrigger || data->colliderB->isTrigger) return;
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
                    if (a->isTrigger || b->isTrigger) continue;
                    const CollisionInfo& info = data->info;

                    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                    float totalInvMass = invMassA + invMassB;

                    glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                    a->bodyTransform->position -= correction * invMassA;
                    b->bodyTransform->position += correction * invMassB;
                    a->bodyTransform->localDirty = true;
                    b->bodyTransform->localDirty = true;

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
                    if (a->isTrigger || b->isTrigger) return;
                    const CollisionInfo& info = data->info;

                    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / a->rigidbody->mass : 0.0f;
                    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ? 1.0f / b->rigidbody->mass : 0.0f;
                    float totalInvMass = invMassA + invMassB;

                    glm::vec3 correction = info.depth / totalInvMass * info.normal * positionCorrectionFactor;

                    a->bodyTransform->position -= correction * invMassA;
                    b->bodyTransform->position += correction * invMassB;
                    a->bodyTransform->localDirty = true;
                    b->bodyTransform->localDirty = true;

                    a->isDirty = true;
                    b->isDirty = true;
                }
                });
        }
    }
}
