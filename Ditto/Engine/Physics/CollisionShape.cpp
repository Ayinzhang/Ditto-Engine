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

void Collider::UpdateBiasWorldModel()
{
    biasWorldModel = objectTransform ? objectTransform->GetWorldModel() * biasLocalModel : biasLocalModel;
}

void Collider::UpdateWorldAABB()
{
    
    UpdateBiasWorldModel();
    glm::mat4 worldMat = biasWorldModel;

    glm::vec3 corners[8] = {
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

    for (int i = 0; i < 8; ++i)
    {
        glm::vec4 worldPos = worldMat * glm::vec4(corners[i], 1.0f);
        aabb.Expand(glm::vec3(worldPos));
    }

    isDirty = false;
}

BVHNode::BVHNode() : isLeaf(true) {}

BVHNode::BVHNode(Collider* collider) : isLeaf(true), collider(collider) { aabb = collider->aabb; }

BVHNode::BVHNode(std::unique_ptr<BVHNode> left, std::unique_ptr<BVHNode> right) : isLeaf(false)
{
	this->left = std::move(left); this->right = std::move(right);
	this->left->parent = this; this->right->parent = this;

	aabb = this->left->aabb; aabb.Expand(this->right->aabb);
}

void BVHNode::UpdateAABB()
{
	if (isLeaf) { if (collider) aabb = collider->aabb; }
	else { aabb = left->aabb; aabb.Expand(right->aabb); }
}

BVHTree::BVHTree(std::vector<Collider*> colliders)
{
    if (colliders.empty()) return;
    root = BuildTopDown(colliders, 0, colliders.size());
}



BVHTree::~BVHTree() = default;

std::unique_ptr<BVHNode> BVHTree::BuildTopDown(std::vector<Collider*> colliders, int start, int end) {
    int count = end - start;

    if (count == 1)
    {
        auto node = std::make_unique<BVHNode>(colliders[start]);
        leafNodes.push_back(node.get()); return node;
    }

	AABB centroidBounds;
    for (int i = start; i < end; i++) centroidBounds.Expand(colliders[i]->aabb.GetCenter());

    glm::vec3 size = centroidBounds.max - centroidBounds.min;
    int axis = (size.x > size.y && size.x > size.z) ? 0 : (size.y > size.z) ? 1 : 2;

    std::sort(colliders.begin() + start, colliders.begin() + end, [axis](Collider* a, Collider* b) {
        return a->aabb.GetCenter()[axis] < b->aabb.GetCenter()[axis];});

    int mid = start + count / 2;
    return std::make_unique<BVHNode>(BuildTopDown(colliders, start, mid), BuildTopDown(colliders, mid, end));
}

void BVHTree::UpdateBVHTree() 
{
    if (!root) return;
    UpdateAllAABBs(root.get());
    SampleAndRebuild();
}

void BVHTree::UpdateAllAABBs(BVHNode* node)
{
    if (!node) return;

    if (node->isLeaf)
    {
        Collider* collider = node->collider;
        if (collider && collider->isDirty) { collider->UpdateWorldAABB(); node->aabb = collider->aabb; }
    }
    else
    {
        UpdateAllAABBs(node->left.get());
        UpdateAllAABBs(node->right.get());
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

    Collider* collider = node->collider;

    
    
    auto detached = DetachLeaf(node);

    if (collider)
    {
        auto newNode = std::make_unique<BVHNode>(collider);
        leafNodes[currentSampleIndex] = newNode.get();
        InsertLeafNode(std::move(newNode));
    }
}

void BVHTree::InsertLeafNode(std::unique_ptr<BVHNode> leaf)
{
    if (!root) { leaf->parent = nullptr; root = std::move(leaf); return; }

    BVHNode* bestNode = FindBestInsertionNode(leaf->aabb);   

    
    
    BVHNode* parent = bestNode->parent;
    if (!parent)   
    {
        root = std::make_unique<BVHNode>(std::move(root), std::move(leaf));
        RefitUpwards(root.get());
        return;
    }

    std::unique_ptr<BVHNode>& slot = (parent->left.get() == bestNode) ? parent->left : parent->right;
    auto newInternal = std::make_unique<BVHNode>(std::move(slot), std::move(leaf));
    newInternal->parent = parent;
    slot = std::move(newInternal);

    RefitUpwards(slot.get());
}

BVHNode* BVHTree::FindBestInsertionNode(AABB bounds) 
{
    if (!root) return nullptr;

    BVHNode* bestNode = root.get();
    float bestCost = std::numeric_limits<float>::max();

    std::queue<BVHNode*> queue;
    queue.push(root.get());

    while (!queue.empty()) 
    {
        BVHNode* node = queue.front(); queue.pop();
        float cost = CalculateInsertionCost(node, bounds);

        if (cost < bestCost) {
            bestCost = cost;
            bestNode = node;
        }

        if (!node->isLeaf) { queue.push(node->left.get()); queue.push(node->right.get()); }
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

std::unique_ptr<BVHNode> BVHTree::DetachLeaf(BVHNode* node)
{
    if (!node || !node->parent) return nullptr;

    BVHNode* parent = node->parent;
    auto& nodeSlot    = (parent->left.get() == node) ? parent->left : parent->right;
    auto& siblingSlot = (parent->left.get() == node) ? parent->right : parent->left;

    auto detached = std::move(nodeSlot);
    auto sibling  = std::move(siblingSlot);
    detached->parent = nullptr;

    
    
    if (parent == root.get())
    {
        root = std::move(sibling);
        if (root) root->parent = nullptr;
    }
    else
    {
        BVHNode* grandParent = parent->parent;
        auto& parentSlot = (grandParent->left.get() == parent) ? grandParent->left : grandParent->right;
        sibling->parent = grandParent;
        parentSlot = std::move(sibling);

        RefitUpwards(grandParent);
    }
    return detached;
}

std::vector<Collider*> BVHTree::Query(AABB bounds)
{
    std::vector<Collider*> results;
    if (root) QueryRecursive(root.get(), bounds, results);
    return results;
}

void BVHTree::QueryRecursive(BVHNode* node, AABB bounds, std::vector<Collider*>& results)
{
    if (!node || !node->aabb.CheckCollision(bounds)) return;

    if (node->isLeaf)
    {
        Collider* collider = node->collider;
        if (collider && collider->aabb.CheckCollision(bounds)) results.push_back(collider);
    }
    else
    {
        QueryRecursive(node->left.get(), bounds, results);
        QueryRecursive(node->right.get(), bounds, results);
    }
}
