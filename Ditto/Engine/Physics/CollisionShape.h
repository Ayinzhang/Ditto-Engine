#pragma once
#include <vector>
#include <list>
#include "../Core/GameObject.h"
#include "../Resources/Resource.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/quaternion.hpp"

struct AABB
{
	glm::vec3 min, max;

	AABB();
	AABB(glm::vec3 min, glm::vec3 max);

	bool CheckCollision(AABB other);

	void Expand(glm::vec3 point);
	void Expand(AABB other);

	glm::vec3 GetCenter();
	float GetSurfaceArea();
	bool Contains(AABB other);
};

struct Collider
{
	TransformComponent* transform;
	RigidbodyComponent* rigidbody;
	AABB aabb, localAABB;
	MeshData* mesh;

    bool isDirty; int id;

	void UpdateWorldAABB();
};

struct CollisonPairs
{
    Collider* colliderA;
    Collider* colliderB;
};

struct BVHNode
{
	AABB aabb; bool isLeaf;
	BVHNode* parent = nullptr;

	union 
	{
		struct { BVHNode* left, * right; } child;
		struct { Collider* collider; int index; } leaf;
	} data;

	BVHNode();
	BVHNode(Collider* collider);
	BVHNode(BVHNode* left, BVHNode* right);

	void UpdateAABB();
	void Release();
};

struct BVHTree
{
    BVHNode* root = nullptr;
    std::vector<BVHNode*> leafNodes;
    size_t currentSampleIndex = 0;
	BVHTree(std::vector<Collider*> colliders);
    ~BVHTree();
    void UpdateBVHTree();
    std::vector<Collider*> Query(AABB bounds);

private:
    BVHNode* BuildTopDown(std::vector<Collider*> colliders, int start, int end);
    void UpdateAllAABBs(BVHNode* node);
    void SampleAndRebuild();
    void ReinsertNode(BVHNode* node);
    void InsertLeafNode(BVHNode* leaf);
    BVHNode* FindBestInsertionNode(AABB bounds);
    float CalculateInsertionCost(BVHNode* node, AABB bounds);
    void RefitUpwards(BVHNode* node);
    void RemoveNodeFromTree(BVHNode* node);
    void QueryRecursive(BVHNode* node, AABB bounds, std::vector<Collider*>& results);
};