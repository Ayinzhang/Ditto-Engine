#pragma once
#include <vector>
#include <unordered_map>
#include "../Core/GameObject.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"
#include "../../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../../3rdParty/GLM/ext/quaternion_common.hpp"
#include "../../3rdParty/GLM/ext/quaternion_geometric.hpp"
#include "../../3rdParty/GLM/ext/quaternion_trigonometric.hpp"

const float dt = 1.0f / 60;

struct Collider;
struct Physics
{
	//void GenerateColliders(const std::vector<GameObject*>& gameobjects);
	//void UpdatePhysics();
	//void IntegrateForce();
	//void HandleBoardCollisions();
	//void HandleBoardCollisions();
	//void IntegrateVelocity();
};

struct AABB
{
};

struct Collider
{

};