#pragma once
#include <unordered_map>
#include "CollisionShape.h"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"
#include "../../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../../3rdParty/GLM/ext/quaternion_common.hpp"
#include "../../3rdParty/GLM/ext/quaternion_geometric.hpp"
#include "../../3rdParty/GLM/ext/quaternion_trigonometric.hpp"

struct Engine;
struct Physics
{
	Engine* engine; BVHTree* bvhTree;
	std::vector<Collider*> colliders;

	void GenerateColliders(const std::vector<GameObject*>& gameobjects);
	void UpdatePhysics(float dt);
	void IntegrateForce(float dt);
	void HandleBoardCollisions();
	//void HandleNarrowCollisions();
	//void IntegrateVelocity();
};