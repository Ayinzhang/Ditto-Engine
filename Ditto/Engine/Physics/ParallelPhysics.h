#pragma once
#include "Physics.h"
#include <vector>
#include <unordered_map>

// 继承原有的 Physics 类
class ParallelPhysics : public Physics
{
public:
    ParallelPhysics() = default;
    ~ParallelPhysics() = default;

    // 重写主更新循环
    void UpdatePhysics(float dt) override;

private:
    void SolveBatchesParallel();

    // 子步进数量 (建议 4-8)
    int subSteps = 4;
    // 并行版本的力积分
    void IntegrateForceParallel(float dt);

    // 并行版本的宽阶段检测
    void HandleBroadCollisionsParallel();

    // 基于染色图的窄阶段处理（无锁并行响应）
    void HandleNarrowCollisionsParallel();

    // 辅助：构建染色分组
    // 将 colliderPairs 分配到不同的 batch 中，确保同一 batch 内的碰撞对不共享刚体
    void BuildConstraintBatches();

    // 存储分批后的碰撞对
    // batches[0] 包含第一批可以并行处理的碰撞对
    // batches[1] 包含第二批... 以此类推
    std::vector<std::vector<std::pair<Collider*, Collider*>>> constraintBatches;

    // 用于快速查找 Collider 索引的映射 (用于图着色算法)
    std::unordered_map<Collider*, int> colliderIndexMap;
};