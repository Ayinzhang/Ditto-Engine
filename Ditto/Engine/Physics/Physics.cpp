#include "GJK.h"
#include "Physics.h"
#include "../Core/Engine.h"
#include <iostream>

void Physics::GenerateColliders(const std::vector<GameObject*>& gameobjects)
{
	for (GameObject* obj : gameobjects)
	{
		TransformComponent* transform = obj->GetComponent<TransformComponent>();
		RendererComponent* renderer = obj->GetComponent<RendererComponent>();
		RigidbodyComponent* rigidbody = obj->GetComponent<RigidbodyComponent>();
		if(transform && renderer && rigidbody)
		{
			Collider* collider = new Collider();
			collider->transform = transform;
			collider->rigidbody = rigidbody;
			switch (renderer->type)
			{
				case RendererComponent::Cube:
					collider->mesh = engine->resource->cubeMesh;
					break;
				case RendererComponent::Sphere:
					collider->mesh = engine->resource->sphereMesh;
					break;
				case RendererComponent::Plane:
					collider->mesh = engine->resource->planeMesh;
					break;
			}
			collider->localAABB = AABB(collider->mesh->aabbMin, collider->mesh->aabbMax);
			collider->predictedPosition = transform->position;
			collider->predictedRotation = transform->rotation;
			collider->UpdateWorldAABB(); colliders.push_back(collider);
		}
	}
	bvhTree = new BVHTree(colliders);
}

void Physics::UpdatePhysics(float dt)
{
	if ((t += dt) < deltaTime) return; t -= deltaTime;
	IntegrateForce(deltaTime);
	bvhTree->UpdateBVHTree();
	HandleBroadCollisions();
	HandleNarrowCollisions();
	IntegrateVelocity(deltaTime);
}

void Physics::IntegrateForce(float dt)
{
	for (Collider* collider : colliders)
	{
		if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
		{
			if (collider->rigidbody->useGravity)
				collider->rigidbody->velocity += glm::vec3(0.0f, -9.81f, 0.0f) * dt;

			collider->rigidbody->velocity *= pow(collider->rigidbody->damp, dt);
			collider->rigidbody->angularVelocity *= pow(collider->rigidbody->angularDamp, dt);

			// 预测位置
			collider->predictedPosition = collider->transform->position + dt * collider->rigidbody->velocity;

			collider->isDirty = true;
		}
	}
}

void Physics::HandleBroadCollisions()
{
	colliderPairs.clear();
	for (Collider* collider : colliders)
	{
		if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
		{
			auto cols = bvhTree->Query(collider->aabb); bool flag = false;
			for(auto other : cols)
			{
				if (other == collider) flag = true;
				else if (other->rigidbody->type == RigidbodyComponent::Static
					|| (flag && other->rigidbody->type == RigidbodyComponent::Dynamic))
					colliderPairs.push_back({ collider, other });
			}
		}
	}
}

void Physics::HandleNarrowCollisions()
{
	std::vector<std::pair<Collider*, Collider*>> collisionPairsCopy = colliderPairs;
	colliderPairs.clear(); // 清空原有列表，重新验证

	for (auto& pair : collisionPairsCopy)
	{
		Collider* colliderA = pair.first;
		Collider* colliderB = pair.second;

		// 使用GJK-EPA进行精确碰撞检测
		CollisionInfo info = GJK_CheckCollision(colliderA, colliderB);
		std::cout << "检测到碰撞对: ColliderA@" << colliderA << ", ColliderB@" << colliderB << std::endl;
		if (info.flag)
		{
			// 确保法线始终从 A 指向 B
			if (info.depth <= 0.0f)
			{
				std::cout << "警告：碰撞深度无效: " << info.depth << std::endl;
				continue;
			}
			// 1. 位置修正 (Linear Projection)
			const float percent = 0.8f; // 修正率，通常 0.2-0.8
			const float slop = 0.01f;   // 允许的穿透量

			float invMassA = (colliderA->rigidbody->type == RigidbodyComponent::Dynamic) ?
				1.0f / colliderA->rigidbody->mass : 0.0f;
			float invMassB = (colliderB->rigidbody->type == RigidbodyComponent::Dynamic) ?
				1.0f / colliderB->rigidbody->mass : 0.0f;
			float totalInvMass = invMassA + invMassB;

			if (totalInvMass > 0)
			{
				glm::vec3 correction = std::max(info.depth - slop, 0.0f) / totalInvMass * percent * info.normal;

				// 记录修正前的预测位置用于调试
				glm::vec3 oldPosA = colliderA->predictedPosition;
				glm::vec3 oldPosB = colliderB->predictedPosition;

				colliderA->predictedPosition -= correction * invMassA;
				colliderB->predictedPosition += correction * invMassB;

				std::cout << "位置修正 - A移动: " << (correction * invMassA).x << " " << (correction * invMassA).y << " " << (correction * invMassA).z
					<< ", B移动: " << glm::length(correction * invMassB) << std::endl;
			}

			std::cout << "碰撞处理完成 - 深度: " << info.depth
				<< " 法线: (" << info.normal.x << ", " << info.normal.y << ", " << info.normal.z << ")"
				<< std::endl << std::endl;
		}
	}
}

void Physics::IntegrateVelocity(float dt)
{
	for (Collider* collider : colliders)
	{
		if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
		{
			// 不再重新计算速度，直接使用碰撞响应中已修正的速度
			collider->rigidbody->velocity = (collider->predictedPosition - collider->transform->position) / dt;
			collider->transform->position = collider->predictedPosition;

			collider->transform->rotation = collider->predictedRotation;

			collider->isDirty = false;
			collider->transform->UpdateTransform();
		}
	}
}
