#pragma once
#include <vector>
#include <list>
#include "../Core/GameObject.h"
#include "../Resources/Resource.h"
#include "../../3rdParty/GLM/glm.hpp"

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

	bool isDirty;

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

    // 存储所有叶子节点（用于轮询抽样）
    std::vector<BVHNode*> leafNodes;

    // 当前抽样索引（用于轮询）
    size_t currentSampleIndex = 0;

    // 构造函数：传入碰撞体数组构建BVH
    BVHTree(std::vector<Collider*> colliders);

    ~BVHTree();

    // 更新BVH树：自底向上更新AABB + 抽样重构
    void UpdateBVHTree();

    // 查询与AABB相交的碰撞体
    std::vector<Collider*> Query(AABB bounds);

private:
    // 自顶向下构建（初始构建）
    BVHNode* BuildTopDown(std::vector<Collider*> colliders, int start, int end);

    // 自底向上更新所有节点的AABB
    void UpdateAllAABBs(BVHNode* node);

    // 抽样重构：每帧抽取一个叶子节点重新插入
    void SampleAndRebuild();

    // 重新插入一个节点
    void ReinsertNode(BVHNode* node);

    // 插入叶子节点到树中
    void InsertLeafNode(BVHNode* leaf);

    // 寻找最佳插入位置
    BVHNode* FindBestInsertionNode(AABB bounds);

    // 计算插入成本
    float CalculateInsertionCost(BVHNode* node, AABB bounds);

    // 向上更新AABB
    void RefitUpwards(BVHNode* node);

    // 从树中移除节点
    void RemoveNodeFromTree(BVHNode* node);

    // 递归查询
    void QueryRecursive(BVHNode* node, AABB bounds, std::vector<Collider*>& results);
};