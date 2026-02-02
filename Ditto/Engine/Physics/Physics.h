#pragma once
#include <unordered_map>
#include "CollisionShape.h"
#include "../Resources/Resource.h"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"
#include "../../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../../3rdParty/GLM/ext/quaternion_common.hpp"
#include "../../3rdParty/GLM/ext/quaternion_geometric.hpp"
#include "../../3rdParty/GLM/ext/quaternion_trigonometric.hpp"

const float dt = 1.0f / 60;

struct Physics
{
	Resource* resource;
	std::vector<Collider*> colliders;

	void GenerateColliders(const std::vector<GameObject*>& gameobjects);
	//void UpdatePhysics();
	//void IntegrateForce();
	//void HandleBoardCollisions();
	//void HandleBoardCollisions();
	//void IntegrateVelocity();
};