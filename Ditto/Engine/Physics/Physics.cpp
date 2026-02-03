#include "Physics.h"
#include "../Core/Engine.h"

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
			colliders.push_back(collider);
		}
	}
	bvhTree = new BVHTree(colliders);
}

void Physics::UpdatePhysics(float dt)
{
	bvhTree->UpdateBVHTree();
	IntegrateForce(dt);
}

void Physics::IntegrateForce(float dt)
{
	for (Collider* collider : colliders)
	{
		if (collider->rigidbody->type == RigidbodyComponent::Dynamic)
		{
			if (collider->rigidbody->useGravity) collider->rigidbody->velocity += glm::vec3(0.0f, -9.81f, 0.0f) * dt;
			collider->transform->position[0] += collider->rigidbody->velocity.x * dt;
			collider->transform->position[1] += collider->rigidbody->velocity.y * dt;
			collider->transform->position[2] += collider->rigidbody->velocity.z * dt;
			collider->transform->UpdateTransform();
		}
	}
}
