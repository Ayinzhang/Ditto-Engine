#pragma once
#include "GJK.h"
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
	std::vector<std::pair<Collider*, Collider*>> colliderPairs;
	float t = 0, deltaTime = 1.0f / 60; int iterations = 4;

	void GenerateColliders(const std::vector<GameObject*>& gameobjects);
	virtual void UpdatePhysics(float dt);
	void IntegrateForce(float dt);
	void HandleBroadCollisions();
	void HandleNarrowCollisions();

	void PositionCorrection(Collider* a, Collider* b, CollisionInfo& info);
	void ApplyImpulse(Collider* a, Collider* b, CollisionInfo& info);
};