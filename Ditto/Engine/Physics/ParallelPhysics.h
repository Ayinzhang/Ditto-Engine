#pragma once
#include "Physics.h"
#include <vector>
#include <omp.h>

/**
 * @brief 完全并行化物理引擎（OpenMP 4线程 + 图着色 + 工作窃取风格任务依赖）
 *
 * 并行覆盖阶段：
 * - 积分力：parallel for
 * - BVH更新：叶子并行 + 内部节点OpenMP Task依赖
 * - 宽相位碰撞检测：TLS + 合并
 * - 窄相位GJK检测：TLS + 合并
 * - 冲量求解：图着色分组，组内parallel for
 * - 位置修正：复用着色分组，组内parallel for
 * - 变换更新：parallel for
 *
 * 完全无锁（仅合并阶段短暂临界，可忽略）
 */
class ParallelPhysics : public Physics
{
public:
    // 重写主物理更新函数（完全接管）
    void UpdatePhysics(float dt) override;

private:
    // ----------------------------- 并行阶段实现 -----------------------------

    /// 并行积分力（4线程）
    void IntegrateForceParallel(float dt);

    /// 并行BVH更新（叶子并行 + 内部节点任务依赖）
    static void ParallelUpdateBVHTree(BVHTree* tree);
    static void ParallelUpdateNodeAABB(BVHNode* node);   // 递归任务函数

    /// 并行宽相位碰撞检测（TLS）
    void HandleBroadCollisionsParallel();

    /// 并行窄相位GJK检测（TLS）
    void HandleNarrowCollisionsParallel();

    /// 构建冲突图并着色（生成颜色分组）
    void BuildColorGroups();

    /// 判断两个碰撞数据是否冲突（共享刚体）
    bool HasConflict(const CollisionData& a, const CollisionData& b) const;

    /// 并行冲量求解（单次迭代，组内并行）
    void SolveCollisionsParallel(int iter);

    /// 并行位置修正（复用颜色分组）
    void ApplyPositionCorrectionsParallel();

    // ----------------------------- 成员变量 -----------------------------
    std::vector<std::vector<int>> colorGroups;   ///< 颜色分组：每组是collisionData中的索引列表
};