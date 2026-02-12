#pragma once

#include "Physics.h"
#include "../../3rdParty/TaskFlow/taskflow.hpp"
#include <mutex>
#include <vector>
#include <unordered_map>
#include <thread>

/**
 * @brief 并行物理引擎，继承自 Physics。
 *        使用 TaskFlow 任务图管理物理阶段依赖，并在碰撞求解阶段采用图染色实现无冲突并行。
 *        针对性能进行了优化：
 *        - 复用 taskflow 对象，避免每帧构造开销
 *        - 使用带分块的 parallel_for 减少任务数量
 *        - 粗测阶段采用线程本地缓冲降低锁争用
 *        - 迭代求解与位置修正按颜色组并行，组内使用分块并行
 */
struct ParallelPhysics : public Physics
{
    tf::Executor executor;          // 线程池
    tf::Taskflow taskflow;          // 每帧复用的任务图（每帧 clear 后重建）

    // 线程同步互斥锁
    std::mutex colliderPairsMutex;
    std::mutex collisionDataMutex;

    // 图染色相关数据
    std::vector<int> collisionColors;       // 每个碰撞对的颜色索引
    int numColors;                          // 总颜色数
    std::vector<std::vector<int>> colorGroups; // 按颜色分组后的碰撞对索引列表

    // 分块大小控制（根据场景规模可调）
    static constexpr size_t CHUNK_SIZE = 64;   // parallel_for 的默认分块大小

    ParallelPhysics();

    // 重写物理更新，采用任务图 + 染色并行
    virtual void UpdatePhysics(float dt) override;

private:
    // 根据当前碰撞数据构建冲突图并着色，同时生成颜色分组
    void BuildConflictGraphAndColorGroups(const std::vector<CollisionData>& data);

    // 粗测阶段的线程本地缓冲区类型
    struct ThreadLocalBuffer
    {
        std::vector<std::pair<Collider*, Collider*>> localPairs;
        void clear() { localPairs.clear(); }
    };
};