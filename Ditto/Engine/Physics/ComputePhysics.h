#pragma once
#include "Physics.h"
#include "../../3rdParty/GLAD/glad.h"
#include <vector>

// GPU 端数据结构（必须与着色器中的布局严格一致，std430 对齐）
struct RigidbodyGPU {
    glm::vec4 velocity;          // (vx,vy,vz, 未使用)
    glm::vec4 angularVelocity;    // (ωx,ωy,ωz, 未使用)
    glm::mat3 invInertiaLocal;    // 局部惯性张量的逆
    int type;                     // 0=Static, 1=Dynamic
    float mass;
    float linearDamping;
    float angularDamping;
    int useGravity;
    int _pad[2];                  // 保持 16 字节对齐
};

struct TransformGPU {
    glm::vec4 position;           // (px,py,pz, 未使用)
    glm::vec4 rotation;           // 四元数 (x,y,z,w)
};

struct ColliderGPU {
    int transformIdx;             // 对应的 TransformGPU 索引
    int rigidbodyIdx;             // 对应的 RigidbodyGPU 索引
    int meshStart;                // 在全局顶点数组中的起始索引
    int meshCount;                // 顶点数量
    glm::vec4 localAABBMin;       // 局部 AABB 最小值 (xyz)
    glm::vec4 localAABBMax;       // 局部 AABB 最大值 (xyz)
    glm::vec4 worldAABBMin;       // 世界 AABB 最小值
    glm::vec4 worldAABBMax;       // 世界 AABB 最大值
    int isDirty;                  // 标记是否需要更新世界 AABB
    int _pad[3];
};

struct CollisionPairGPU {
    int colliderA;
    int colliderB;
};

struct CollisionDataGPU {
    int colliderA;
    int colliderB;
    float depth;
    glm::vec4 normal;             // (nx,ny,nz, 未使用)
    glm::vec4 contactPointA;      // (x,y,z, 未使用)
    glm::vec4 contactPointB;
};

class ComputePhysics : public Physics {
public:
    ComputePhysics();
    virtual ~ComputePhysics();

    // 重写基类虚函数
    virtual void GenerateColliders(const std::vector<GameObject*>& gameobjects) override;
    virtual void UpdatePhysics(float dt) override;

protected:
    // GPU 缓冲区对象
    GLuint rigidbodySSBO;          // binding = 0
    GLuint transformSSBO;          // binding = 1
    GLuint colliderSSBO;           // binding = 2
    GLuint meshVertexSSBO;         // binding = 3 (所有网格顶点平铺)
    GLuint pairSSBO;               // binding = 4 (碰撞对列表)
    GLuint collisionDataSSBO;      // binding = 5 (碰撞数据列表)
    GLuint atomicCounter;          // 原子计数器 buffer binding = 0 (用于生成碰撞对)
    GLuint atomicCounterNarrow;    // 另一个原子计数器 binding = 1 (用于生成碰撞数据)

    // 计算着色器程序
    GLuint integrateForceProgram;
    GLuint updateAABBProgram;
    GLuint broadPhaseProgram;
    GLuint narrowPhaseProgram;
    GLuint solveCollisionsProgram;
    GLuint positionCorrectionProgram;

    int numColliders;              // 当前碰撞体数量
    int maxPairs;                  // 预估最大碰撞对数量（动态增长）

    // 辅助函数
    void InitGPUResources();
    void CompileShaders();
    void UploadColliderData();     // 将 CPU colliders 数据转换为 GPU 格式并上传
    void ResizePairBuffers(int requiredPairs); // 若碰撞对数量超过 maxPairs，重新分配缓冲区

    // 调度各个物理阶段
    void DispatchIntegrateForce(float dt);
    void DispatchUpdateAABB();
    void DispatchBroadPhase();
    void DispatchNarrowPhase(GLuint pairCount);  // 需要传入实际对数量
    void DispatchSolveCollisions(int iteration);
    void DispatchPositionCorrection();

    // 可选：将 GPU 变换数据读回 CPU（供渲染使用）
    void ReadbackTransforms();
};