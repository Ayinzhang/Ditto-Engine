#include <algorithm>
#include <queue>
#include <limits>
#include <iostream>
#include "CollisionShape.h"
#include "../../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "../../3rdParty/GLM/ext/quaternion_common.hpp"
#include "../../3rdParty/GLM/ext/quaternion_geometric.hpp"
#include "../../3rdParty/GLM/ext/quaternion_trigonometric.hpp"


AABB::AABB() : min(std::numeric_limits<float>::max()),max(std::numeric_limits<float>::lowest()) {}

AABB::AABB(glm::vec3 min, glm::vec3 max) : min(min), max(max) {}

bool AABB::CheckCollision(AABB other) 
{
	return (max.x >= other.min.x && min.x <= other.max.x) && (max.y >= other.min.y && min.y <= other.max.y) && (max.z >= other.min.z && min.z <= other.max.z);
}

void AABB::Expand(glm::vec3 point) { min = glm::min(min, point); max = glm::max(max, point);}

void AABB::Expand(AABB other) { min = glm::min(min, other.min); max = glm::max(max, other.max);}

glm::vec3 AABB::GetCenter() { return (min + max) * 0.5f;}

float AABB::GetSurfaceArea() 
{
	glm::vec3 size = max - min;
	return 2.0f * (size.x * size.y + size.x * size.z + size.y * size.z);
}

bool AABB::Contains(AABB other) 
{
	return (other.min.x >= min.x && other.max.x <= max.x) && (other.min.y >= min.y && other.max.y <= max.y) && (other.min.z >= min.z && other.max.z <= max.z);
}

void Collider::UpdateWorldAABB()
{
    glm::mat4 translationMat = glm::translate(glm::mat4(1.0f), transform->position);
    glm::mat4 rotationMat = glm::mat4_cast(glm::quat(transform->rotation));  // 使用预测的旋转
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), transform->scale);

    glm::mat4 transformMat = translationMat * rotationMat * scaleMat;

    glm::vec3 corners[8] =
    {
        glm::vec3(localAABB.min.x, localAABB.min.y, localAABB.min.z),
        glm::vec3(localAABB.max.x, localAABB.min.y, localAABB.min.z),
        glm::vec3(localAABB.min.x, localAABB.max.y, localAABB.min.z),
        glm::vec3(localAABB.max.x, localAABB.max.y, localAABB.min.z),
        glm::vec3(localAABB.min.x, localAABB.min.y, localAABB.max.z),
        glm::vec3(localAABB.max.x, localAABB.min.y, localAABB.max.z),
        glm::vec3(localAABB.min.x, localAABB.max.y, localAABB.max.z),
        glm::vec3(localAABB.max.x, localAABB.max.y, localAABB.max.z)
    };

    aabb.min = glm::vec3(std::numeric_limits<float>::max());
    aabb.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (int i = 0; i < 8; i++)
    {
        glm::vec4 worldPos = transformMat * glm::vec4(corners[i], 1.0f);
        aabb.Expand(glm::vec3(worldPos));
    }

    isDirty = false;
}

BVHNode::BVHNode() : isLeaf(true) { data.leaf.collider = nullptr; data.leaf.index = 0;}

BVHNode::BVHNode(Collider* collider) : isLeaf(true) { data.leaf.collider = collider; data.leaf.index = 0; aabb = collider->aabb;}

BVHNode::BVHNode(BVHNode* left, BVHNode* right) : isLeaf(false) 
{
	data.child.left = left; data.child.right = right;
	left->parent = this; right->parent = this;

	aabb = left->aabb; aabb.Expand(right->aabb);
}

void BVHNode::UpdateAABB() 
{
	if (isLeaf) { if (data.leaf.collider) aabb = data.leaf.collider->aabb; }
	else { aabb = data.child.left->aabb; aabb.Expand(data.child.right->aabb); }
}

void BVHNode::Release() 
{
	if (!isLeaf) 
	{
		data.child.left->Release(); data.child.right->Release();
		delete data.child.left; delete data.child.right;
	}
}

BVHTree::BVHTree(std::vector<Collider*> colliders) : root(nullptr) 
{
    if (colliders.empty()) return;
    root = BuildTopDown(colliders, 0, colliders.size());
}

BVHTree::~BVHTree() 
{
    if (root) { root->Release(); delete root; }
}

BVHNode* BVHTree::BuildTopDown(std::vector<Collider*> colliders, int start, int end) {
    int count = end - start;

    if (count == 1) 
    {
        BVHNode* node = new BVHNode(colliders[start]);
        leafNodes.push_back(node); return node;
    }

	AABB centroidBounds;
    for (int i = start; i < end; i++) centroidBounds.Expand(colliders[i]->aabb.GetCenter());

    glm::vec3 size = centroidBounds.max - centroidBounds.min;
    int axis = (size.x > size.y && size.x > size.z) ? 0 : (size.y > size.z) ? 1 : 2;

    std::sort(colliders.begin() + start, colliders.begin() + end, [axis](Collider* a, Collider* b) {
        return a->aabb.GetCenter()[axis] < b->aabb.GetCenter()[axis];});

    int mid = start + count / 2;
    return new BVHNode(BuildTopDown(colliders, start, mid), BuildTopDown(colliders, mid, end));
}

void BVHTree::UpdateBVHTree() 
{
    if (!root) return;
    UpdateAllAABBs(root);
    SampleAndRebuild();
}

void BVHTree::UpdateAllAABBs(BVHNode* node) 
{
    if (!node) return;

    if (node->isLeaf) 
    {
        Collider* collider = node->data.leaf.collider;
        if (collider && collider->isDirty) { collider->UpdateWorldAABB(); node->aabb = collider->aabb; }
    }
    else 
    {
        UpdateAllAABBs(node->data.child.left);
        UpdateAllAABBs(node->data.child.right);
        node->UpdateAABB();
    }
}

void BVHTree::SampleAndRebuild() 
{
    if (leafNodes.empty()) return;
    BVHNode* nodeToReinsert = leafNodes[currentSampleIndex];
    ReinsertNode(nodeToReinsert);
    currentSampleIndex = (currentSampleIndex + 1) % leafNodes.size();
}

void BVHTree::ReinsertNode(BVHNode* node) 
{
    if (!node || !node->parent) return;

    AABB nodeAABB = node->aabb;
    Collider* collider = node->data.leaf.collider;

    RemoveNodeFromTree(node);

    if (collider) 
    {
        BVHNode* newNode = new BVHNode(collider);
        leafNodes[currentSampleIndex] = newNode;
        InsertLeafNode(newNode);
        delete node;
    }
}

void BVHTree::InsertLeafNode(BVHNode* leaf) 
{
    if (!root) { root = leaf; leaf->parent = nullptr; return;}

    BVHNode* bestNode = FindBestInsertionNode(leaf->aabb);

    if (!bestNode) 
    {
        BVHNode* newRoot = new BVHNode(root, leaf);
        root = newRoot; return;
    }

    BVHNode* parent = bestNode->parent;
    BVHNode* newInternal = new BVHNode(bestNode, leaf);

    if (parent) 
    {
        if (parent->data.child.left == bestNode) parent->data.child.left = newInternal;
        else parent->data.child.right = newInternal;
        newInternal->parent = parent;
    }
    else root = newInternal;

    RefitUpwards(newInternal);
}

BVHNode* BVHTree::FindBestInsertionNode(AABB bounds) 
{
    if (!root) return nullptr;

    BVHNode* bestNode = root;
    float bestCost = std::numeric_limits<float>::max();

    std::queue<BVHNode*> queue;
    queue.push(root);

    while (!queue.empty()) 
    {
        BVHNode* node = queue.front(); queue.pop();
        float cost = CalculateInsertionCost(node, bounds);

        if (cost < bestCost) {
            bestCost = cost;
            bestNode = node;
        }

        if (!node->isLeaf) { queue.push(node->data.child.left); queue.push(node->data.child.right); }
    }

    return bestNode;
}

float BVHTree::CalculateInsertionCost(BVHNode* node, AABB bounds) 
{
    AABB merged = node->aabb;
    merged.Expand(bounds);
    return merged.GetSurfaceArea();
}

void BVHTree::RefitUpwards(BVHNode* node) 
{
    while (node) 
    {
        node->UpdateAABB();
        node = node->parent;
    }
}

void BVHTree::RemoveNodeFromTree(BVHNode* node) 
{
    if (!node || !node->parent) return;

    BVHNode* parent = node->parent;
    BVHNode* sibling = (parent->data.child.left == node) ? parent->data.child.right : parent->data.child.left;

    if (parent == root) 
    {
        root = sibling;
        if (root) root->parent = nullptr;
        delete parent;
    }
    else 
    {
        BVHNode* grandParent = parent->parent;
        if (grandParent->data.child.left == parent) grandParent->data.child.left = sibling;
        else grandParent->data.child.right = sibling;
        sibling->parent = grandParent;

        delete parent;
        RefitUpwards(grandParent);
    }
}

std::vector<Collider*> BVHTree::Query(AABB bounds) 
{
    std::vector<Collider*> results;
    if (root) QueryRecursive(root, bounds, results);
    return results;
}

void BVHTree::QueryRecursive(BVHNode* node, AABB bounds, std::vector<Collider*>& results) 
{
    if (!node || !node->aabb.CheckCollision(bounds)) return;

    if (node->isLeaf) 
    {
        Collider* collider = node->data.leaf.collider;
        if (collider //&& !(bounds.min == collider->aabb.min && bounds.max == collider->aabb.max) 
            && collider->aabb.CheckCollision(bounds)) results.push_back(collider);
    }
    else 
    {
        QueryRecursive(node->data.child.left, bounds, results);
        QueryRecursive(node->data.child.right, bounds, results);
    }
}