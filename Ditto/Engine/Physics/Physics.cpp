#include "Physics.h"
#include "../Core/Engine.h"
#include <iostream>
#include <algorithm>

void Physics::GenerateColliders(const std::vector<GameObject*>& gameobjects)
{
    for (auto collider : colliders) delete collider; colliders.clear();
    if (bvhTree) delete bvhTree;

    // 为每个游戏对象生成碰撞体
    for (GameObject* obj : gameobjects)
    {
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        RendererComponent* renderer = obj->GetComponent<RendererComponent>();
        RigidbodyComponent* rigidbody = obj->GetComponent<RigidbodyComponent>();

        if (transform && renderer && rigidbody)
        {
            Collider* collider = new Collider();
            collider->transform = transform;
            collider->rigidbody = rigidbody;

            // 根据渲染器类型选择网格
            switch (renderer->type)
            {
            case RendererComponent::Cube:
                collider->mesh = engine->resource->cubeMesh;
                break;
            case RendererComponent::Sphere:
                collider->mesh = engine->resource->sphereMesh;
                break;
            default:
                delete collider;
                continue; // 跳过不支持的类型
            }

            // 计算转动惯量张量（基于形状和缩放）
            rigidbody->CalculateInertia(renderer->type, transform->scale);

            // 设置本地AABB
            collider->localAABB = AABB(collider->mesh->aabbMin, collider->mesh->aabbMax);

            // 更新世界AABB
            collider->UpdateWorldAABB();

            // 添加到碰撞体列表
            colliders.push_back(collider);
        }
    }

    if (!colliders.empty()) bvhTree = new BVHTree(colliders);
}

// ============================================================================
// 更新物理（主循环）
// ============================================================================
void Physics::UpdatePhysics(float dt)
{
    t += dt; if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime); t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step)
    {
        // 1. 积分力（更新速度和角速度）
        IntegrateForce(deltaTime);

        for (int iter = 0; iter < iterations; iter++)
        {
            // 2. 更新BVH树
            if (bvhTree) bvhTree->UpdateBVHTree();

            // 3. 处理宽相位碰撞检测
            HandleBroadCollisions();

            // 4. 处理窄相位碰撞检测和响应
            HandleNarrowCollisions();
        }
    }

	for (auto iter : colliders) iter->transform->UpdateTransform();
}

// ============================================================================
// 积分力（更新速度和角速度）
// ============================================================================
void Physics::IntegrateForce(float dt)
{
    for (Collider* collider : colliders)
    {
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            TransformComponent* transform = collider->transform;
            RigidbodyComponent* rb = collider->rigidbody;

            // 应用重力
            if (rb->useGravity) rb->velocity.y += -9.8f * dt;

            // 应用阻尼（指数衰减）
            rb->velocity *= glm::max(0.0f, glm::pow(1.0f - rb->damp, dt));
            rb->angularVelocity *= glm::max(0.0f, glm::pow(1.0f - rb->angularDamp, dt));

            // 计算预测位置（用于碰撞检测）
            transform->position += rb->velocity * dt;
			transform->rotation += rb->angularVelocity * dt;

            // 标记为需要更新AABB
            collider->isDirty = true;
        }
    }
}

// ============================================================================
// 宽相位碰撞检测
// ============================================================================
void Physics::HandleBroadCollisions()
{
    colliderPairs.clear();

    // 为每个动态碰撞体查询潜在碰撞对
    for (Collider* collider : colliders)
    {
        if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
        {
            // 使用BVH树查询可能碰撞的物体
            std::vector<Collider*> potentialCollisions;
            if (bvhTree) {
                potentialCollisions = bvhTree->Query(collider->aabb);
            }

            // 过滤自身和重复碰撞对
            for (Collider* other : potentialCollisions)
            {
                if (other == collider) continue;

                // 避免重复添加碰撞对（A,B 和 B,A）
                bool alreadyExists = false;
                for (const auto& pair : colliderPairs)
                {
                    if ((pair.first == collider && pair.second == other) ||
                        (pair.first == other && pair.second == collider))
                    {
                        alreadyExists = true;
                        break;
                    }
                }

                if (!alreadyExists)
                {
                    // 只检测动态-动态和动态-静态碰撞
                    if (other->rigidbody->type == RigidbodyComponent::Dynamic ||
                        other->rigidbody->type == RigidbodyComponent::Static)
                    {
                        colliderPairs.push_back({ collider, other });
                    }
                }
            }
        }
    }
}

// ============================================================================
// 窄相位碰撞检测和响应（冲量法）
// ============================================================================
void Physics::HandleNarrowCollisions()
{
    // 遍历所有碰撞对
    for (auto& pair : colliderPairs)
    {
        Collider* colliderA = pair.first;
        Collider* colliderB = pair.second;

        // 使用GJK-EPA进行精确碰撞检测
        CollisionInfo collisionInfo = GJK_CheckCollision(colliderA, colliderB);

        if (collisionInfo.flag && collisionInfo.depth > 0.0f)
        {
            // 轻微的位置修正，防止持续穿透
            // 注意：这是辅助修正，不是主要的碰撞响应机制
			PositionCorrection(colliderA, colliderB, collisionInfo);

            // 应用冲量（更新速度和角速度）
            ApplyImpulse(colliderA, colliderB, collisionInfo);
        }
    }
}

void Physics::PositionCorrection(Collider* a, Collider* b, CollisionInfo& info)
{
    if (info.depth > 0.001f)
    {
        float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ?
            1.0f / a->rigidbody->mass : 0.0f;
        float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ?
            1.0f / b->rigidbody->mass : 0.0f;
        float totalInvMass = invMassA + invMassB;

        if (totalInvMass > 0.0f)
        {
            // 使用较小的修正因子，避免与冲量法冲突
            glm::vec3 correction = (info.depth - 0.001f) / totalInvMass * info.normal;

            a->transform->position -= correction * invMassA;
            b->transform->position += correction * invMassB;

            // 标记为需要更新AABB
            a->isDirty = true;
            b->isDirty = true;
        }
    }
}

// ============================================================================
// 应用冲量（核心碰撞响应函数）
// ============================================================================
void Physics::ApplyImpulse(Collider* a, Collider* b, CollisionInfo& info)
{
    // 计算质量倒数
    float invMassA = (a->rigidbody->type == RigidbodyComponent::Dynamic) ?
        1.0f / a->rigidbody->mass : 0.0f;
    float invMassB = (b->rigidbody->type == RigidbodyComponent::Dynamic) ?
        1.0f / b->rigidbody->mass : 0.0f;

    // 获取世界坐标系下的逆惯量张量
    glm::mat3 invInertiaA = CalculateWorldInverseInertia(a);
    glm::mat3 invInertiaB = CalculateWorldInverseInertia(b);

    // 接触点相对于质心的向量
    glm::vec3 rA = info.contactPointA - a->transform->position;
    glm::vec3 rB = info.contactPointB - b->transform->position;

    //if (glm::dot(info.normal, a->transform->position - b->transform->position) < 0.0f) 

    // 计算接触点处的相对速度（考虑线速度和角速度）
    glm::vec3 velA = a->rigidbody->velocity + glm::cross(a->rigidbody->angularVelocity, rA);
    glm::vec3 velB = b->rigidbody->velocity + glm::cross(b->rigidbody->angularVelocity, rB);
    glm::vec3 relativeVel = velB - velA;

    // 计算法线方向的速度分量
    float normalVel = glm::dot(relativeVel, info.normal);

    // 如果物体正在分离，不应用冲量
    if (normalVel > 0.0f) return;

    // 计算恢复系数（取两个物体的最小值）
    float e = 0.8;

    // 计算有效质量项
    glm::vec3 crossA = glm::cross(rA, info.normal);
    glm::vec3 crossB = glm::cross(rB, info.normal);

    glm::vec3 invInertiaCrossA = invInertiaA * crossA;
    glm::vec3 invInertiaCrossB = invInertiaB * crossB;

    float termA = invMassA + glm::dot(crossA, invInertiaCrossA);
    float termB = invMassB + glm::dot(crossB, invInertiaCrossB);

    float denominator = termA + termB;
    if (denominator == 0.0f) return;

    // 计算法线冲量大小
    float j = -(1.0f + e) * normalVel / denominator;

    // 确保冲量是正的（推动物体分开）
    j = glm::max(j, 0.0f);

    // 法线冲量向量
    glm::vec3 impulse = j * info.normal;

    // === 应用线速度冲量 ===
    if (a->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        a->rigidbody->velocity -= impulse * invMassA;
    }

    if (b->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        b->rigidbody->velocity += impulse * invMassB;
    }

    // === 应用角速度冲量 ===
    if (a->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        glm::vec3 torque = glm::cross(rA, impulse);
        a->rigidbody->angularVelocity += invInertiaA * torque;
    }

    if (b->rigidbody->type == RigidbodyComponent::Dynamic)
    {
        glm::vec3 torque = glm::cross(rB, -impulse);
        b->rigidbody->angularVelocity += invInertiaB * torque;
    }

    // === 摩擦处理 ===
    // 计算切向方向
    glm::vec3 tangent = relativeVel - info.normal * normalVel;
    float tangentLen = glm::length(tangent);

    if (tangentLen > 0.001f)
    {
        tangent = glm::normalize(tangent);
        float tangentVel = glm::dot(relativeVel, tangent);

        // 计算切向有效质量
        glm::vec3 invInertiaCrossAT = invInertiaA * glm::cross(rA, tangent);
        glm::vec3 invInertiaCrossBT = invInertiaB * glm::cross(rB, tangent);

        float termAT = invMassA + glm::dot(glm::cross(rA, tangent), invInertiaCrossAT);
        float termBT = invMassB + glm::dot(glm::cross(rB, tangent), invInertiaCrossBT);

        float denominatorT = termAT + termBT;

        if (denominatorT != 0.0f)
        {
            float jt = -tangentVel / denominatorT;

            // 库仑摩擦定律
            float friction = 0.5f;
            float maxFriction = friction * fabs(j);
            jt = glm::clamp(jt, -maxFriction, maxFriction);

            glm::vec3 tangentImpulse = jt * tangent;

            // 应用摩擦冲量（线速度）
            if (a->rigidbody->type == RigidbodyComponent::Dynamic)
            {
                a->rigidbody->velocity += tangentImpulse * invMassA;
            }

            if (b->rigidbody->type == RigidbodyComponent::Dynamic)
            {
                b->rigidbody->velocity -= tangentImpulse * invMassB;
            }

            // 应用摩擦冲量（角速度）
            if (a->rigidbody->type == RigidbodyComponent::Dynamic)
            {
                glm::vec3 torqueFriction = glm::cross(rA, tangentImpulse);
                a->rigidbody->angularVelocity += invInertiaA * torqueFriction;
            }

            if (b->rigidbody->type == RigidbodyComponent::Dynamic)
            {
                glm::vec3 torqueFriction = glm::cross(rB, -tangentImpulse);
                b->rigidbody->angularVelocity += invInertiaB * torqueFriction;
            }
        }
    }
}