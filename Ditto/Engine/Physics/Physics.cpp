#include "Physics.h"

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
					collider->mesh = resource->cubeMesh;
					break;
				case RendererComponent::Sphere:
					collider->mesh = resource->sphereMesh;
					break;
				case RendererComponent::Plane:
					collider->mesh = resource->planeMesh;
					break;
			}
			colliders.push_back(collider);
		}
	}
}
