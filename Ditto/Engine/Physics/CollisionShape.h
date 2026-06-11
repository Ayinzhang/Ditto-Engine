#pragma once
#include <vector>
#include <list>
#include <memory>
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
	TransformComponent* objectTransform;
    TransformComponent* bodyTransform;
	RigidbodyComponent* rigidbody;
	AABB aabb, localAABB;
	MeshData* mesh;
    std::unique_ptr<MeshData> ownedMesh;
    glm::mat4 biasLocalModel;
    glm::mat4 biasWorldModel;
    bool isTrigger;

    bool isDirty; int id;

    void UpdateBiasWorldModel();
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
	BVHNode* parent = nullptr;                 // observer (back-pointer)

	// Internal nodes own their children; leaf nodes observe their collider.
	// (Was a union of the two -- a union cannot hold unique_ptr members.)
	std::unique_ptr<BVHNode> left, right;
	Collider* collider = nullptr;

	BVHNode();
	BVHNode(Collider* collider);
	BVHNode(std::unique_ptr<BVHNode> left, std::unique_ptr<BVHNode> right);

	void UpdateAABB();
};

struct BVHTree
{
    std::unique_ptr<BVHNode> root;
    std::vector<BVHNode*> leafNodes;   // observers; lifetime owned by the tree
    size_t currentSampleIndex = 0;
	BVHTree(std::vector<Collider*> colliders);
    ~BVHTree();
    void UpdateBVHTree();
    std::vector<Collider*> Query(AABB bounds);

private:
    std::unique_ptr<BVHNode> BuildTopDown(std::vector<Collider*> colliders, int start, int end);
    void UpdateAllAABBs(BVHNode* node);
    void SampleAndRebuild();
    void ReinsertNode(BVHNode* node);
    void InsertLeafNode(std::unique_ptr<BVHNode> leaf);
    BVHNode* FindBestInsertionNode(AABB bounds);
    float CalculateInsertionCost(BVHNode* node, AABB bounds);
    void RefitUpwards(BVHNode* node);
    // Unlink `node` (a leaf) from the tree and return its ownership; its
    // now-single-child parent is collapsed away. Was RemoveNodeFromTree.
    std::unique_ptr<BVHNode> DetachLeaf(BVHNode* node);
    void QueryRecursive(BVHNode* node, AABB bounds, std::vector<Collider*>& results);
};
