#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComputePhysics.h"
#include "../Core/Engine.h"
#include <fstream>
#include <sstream>
#include <iostream>

// ���ߺ��������������ɫ��
static GLuint CompileComputeShader(const std::string& source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Compute Shader Compilation Failed:\n" << infoLog << std::endl;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Compute Shader Linking Failed:\n" << infoLog << std::endl;
    }
    glDeleteShader(shader);
    return program;
}

ComputePhysics::ComputePhysics() : maxPairs(1024) {
    rigidbodySSBO = transformSSBO = colliderSSBO = meshVertexSSBO = 0;
    pairSSBO = collisionDataSSBO = atomicCounter = atomicCounterNarrow = 0;
    CompileShaders();
}

ComputePhysics::~ComputePhysics() {
    glDeleteBuffers(1, &rigidbodySSBO);
    glDeleteBuffers(1, &transformSSBO);
    glDeleteBuffers(1, &colliderSSBO);
    glDeleteBuffers(1, &meshVertexSSBO);
    glDeleteBuffers(1, &pairSSBO);
    glDeleteBuffers(1, &collisionDataSSBO);
    glDeleteBuffers(1, &atomicCounter);
    glDeleteBuffers(1, &atomicCounterNarrow);
    glDeleteProgram(integrateForceProgram);
    glDeleteProgram(updateAABBProgram);
    glDeleteProgram(broadPhaseProgram);
    glDeleteProgram(narrowPhaseProgram);
    glDeleteProgram(solveCollisionsProgram);
    glDeleteProgram(positionCorrectionProgram);
}

void ComputePhysics::CompileShaders() {
    // ������ɫ��
    integrateForceProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 0) buffer Rigidbody { RigidbodyGPU rigidbodies[]; };
        layout(std430, binding = 1) buffer Transform { TransformGPU transforms[]; };
        layout(std430, binding = 2) buffer Collider { ColliderGPU colliders[]; };

        uniform float dt;
        uniform float gravity;
        uniform float linearDamping;
        uniform float angularDamping;

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= rigidbodies.length()) return;

            RigidbodyGPU rb = rigidbodies[idx];
            TransformGPU tr = transforms[idx];
            ColliderGPU col = colliders[idx];

            if (rb.type == 1) { // Dynamic
                if (rb.useGravity != 0)
                    rb.velocity.y += -gravity * dt;

                rb.velocity *= pow(1.0 - linearDamping, dt);
                rb.angularVelocity *= pow(1.0 - angularDamping, dt);

                tr.position.xyz += rb.velocity.xyz * dt;
                tr.rotation.xyz += rb.angularVelocity.xyz * dt; // �򵥻��֣�ʵ��Ӧ�淶����Ԫ��

                col.isDirty = 1; // �����Ҫ�������� AABB
            }

            rigidbodies[idx] = rb;
            transforms[idx] = tr;
            colliders[idx] = col;
        }
    )");

    // �������� AABB ��ɫ��
    updateAABBProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 1) buffer Transform { TransformGPU transforms[]; };
        layout(std430, binding = 2) buffer Collider { ColliderGPU colliders[]; };
        layout(std430, binding = 3) buffer MeshVertex { vec3 vertices[]; };

        // ��������������Ԫ��������ת����
        mat4 quatToMat4(vec4 q) {
            float xx = q.x * q.x;
            float yy = q.y * q.y;
            float zz = q.z * q.z;
            float xy = q.x * q.y;
            float xz = q.x * q.z;
            float yz = q.y * q.z;
            float wx = q.w * q.x;
            float wy = q.w * q.y;
            float wz = q.w * q.z;

            return mat4(
                1.0 - 2.0*(yy + zz), 2.0*(xy - wz), 2.0*(xz + wy), 0.0,
                2.0*(xy + wz), 1.0 - 2.0*(xx + zz), 2.0*(yz - wx), 0.0,
                2.0*(xz - wy), 2.0*(yz + wx), 1.0 - 2.0*(xx + yy), 0.0,
                0.0, 0.0, 0.0, 1.0
            );
        }

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= colliders.length()) return;

            ColliderGPU col = colliders[idx];
            if (col.isDirty == 0) return;

            TransformGPU tr = transforms[col.transformIdx];
            mat4 rot = quatToMat4(tr.rotation);
            mat4 worldMat = translate(mat4(1.0), tr.position.xyz) * rot;

            vec3 min = vec3(1e30);
            vec3 max = vec3(-1e30);
            for (int i = 0; i < col.meshCount; ++i) {
                vec3 localPos = vertices[col.meshStart + i];
                vec4 worldPos = worldMat * vec4(localPos, 1.0);
                min = min(min, worldPos.xyz);
                max = max(max, worldPos.xyz);
            }
            col.worldAABBMin = vec4(min, 0.0);
            col.worldAABBMax = vec4(max, 0.0);
            col.isDirty = 0;
            colliders[idx] = col;
        }
    )");

    // Broad phase ��ɫ������������
    broadPhaseProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 0) buffer Rigidbody { RigidbodyGPU rigidbodies[]; }; // ��Ҫ�ж϶�̬
        layout(std430, binding = 2) buffer Collider { ColliderGPU colliders[]; };
        layout(std430, binding = 4) buffer Pair { CollisionPairGPU pairs[]; };
        layout(binding = 0, offset = 0) uniform atomic_uint pairCounter;

        uniform vec3 worldMin;
        uniform vec3 worldMax;
        uniform ivec3 gridRes;

        // �������������㵥Ԫ������
        int getCell(vec3 p) {
            vec3 f = (p - worldMin) / (worldMax - worldMin);
            ivec3 cell = ivec3(f * vec3(gridRes));
            cell = clamp(cell, ivec3(0), gridRes - ivec3(1));
            return cell.x + cell.y * gridRes.x + cell.z * gridRes.x * gridRes.y;
        }

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= colliders.length()) return;

            ColliderGPU colA = colliders[idx];
            if (rigidbodies[colA.rigidbodyIdx].type != 1) return; // ֻ������̬����

            // �򻯣�ֱ������������������� AABB ���ԣ�������ʾ��
            for (uint j = 0; j < colliders.length(); ++j) {
                if (j == idx) continue;
                ColliderGPU colB = colliders[j];
                if (rigidbodies[colB.rigidbodyIdx].type == 1 && idx >= j) continue; // ��̬-��ֻ̬����һ��
                // ��� AABB �ص�
                if (colA.worldAABBMax.x < colB.worldAABBMin.x || colA.worldAABBMin.x > colB.worldAABBMax.x) continue;
                if (colA.worldAABBMax.y < colB.worldAABBMin.y || colA.worldAABBMin.y > colB.worldAABBMax.y) continue;
                if (colA.worldAABBMax.z < colB.worldAABBMin.z || colA.worldAABBMin.z > colB.worldAABBMax.z) continue;

                uint pairIdx = atomicCounterIncrement(pairCounter);
                pairs[pairIdx].colliderA = int(idx);
                pairs[pairIdx].colliderB = int(j);
            }
        }
    )");

    // Narrow phase ��ɫ����GJK+EPA �򻯰棩
    narrowPhaseProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 1) buffer Transform { TransformGPU transforms[]; };
        layout(std430, binding = 2) buffer Collider { ColliderGPU colliders[]; };
        layout(std430, binding = 3) buffer MeshVertex { vec3 vertices[]; };
        layout(std430, binding = 4) buffer Pair { CollisionPairGPU pairs[]; };
        layout(std430, binding = 5) buffer CollisionData { CollisionDataGPU collisions[]; };
        layout(binding = 1, offset = 0) uniform atomic_uint collisionCounter; // �ڶ���ԭ�Ӽ�����

        uniform uint pairCount; // ʵ����Ҫ��������ײ������

        // ������������ȡ֧�ֵ㣨�ֲ�����ϵ��
        vec3 getSupportPoint(ColliderGPU col, vec3 dirWorld, TransformGPU tr) {
            // ������任���ֲ�
            mat4 rot = quatToMat4(tr.rotation);
            mat3 rotMat = mat3(rot);
            vec3 dirLocal = transpose(rotMat) * dirWorld; // ���緽��ת�ֲ�

            vec3 localSupport = vec3(0);
            float maxDot = -1e30;
            for (int i = 0; i < col.meshCount; ++i) {
                vec3 v = vertices[col.meshStart + i];
                float d = dot(v, dirLocal);
                if (d > maxDot) {
                    maxDot = d;
                    localSupport = v;
                }
            }
            return (rotMat * localSupport) + tr.position.xyz; // ��������
        }

        // GJK ��ѭ�����򻯣�������Ƿ���ײ��
        bool GJK(ColliderGPU colA, ColliderGPU colB, TransformGPU trA, TransformGPU trB) {
            vec3 dir = vec3(1,0,0);
            vec3 support[4];
            int simplexSize = 0;
            support[simplexSize++] = getSupportPoint(colA, dir, trA) - getSupportPoint(colB, -dir, trB);
            dir = -support[0];

            for (int iter = 0; iter < 20; ++iter) {
                vec3 p = getSupportPoint(colA, dir, trA) - getSupportPoint(colB, -dir, trB);
                if (dot(p, dir) < 0) return false; // ����ײ
                support[simplexSize++] = p;
                // ���µ����β������·�������ʵ���ԣ��˴��򻯷��� true��
                if (simplexSize == 4) return true;
                dir = -p; // �򻯷������
            }
            return false;
        }

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= pairCount) return;

            CollisionPairGPU pair = pairs[idx];
            ColliderGPU colA = colliders[pair.colliderA];
            ColliderGPU colB = colliders[pair.colliderB];
            TransformGPU trA = transforms[colA.transformIdx];
            TransformGPU trB = transforms[colB.transformIdx];

            if (GJK(colA, colB, trA, trB)) {
                // �򻯣��������Ϊ 0.1������Ϊ (1,0,0)���Ӵ���ȡ����
                uint outIdx = atomicCounterIncrement(collisionCounter);
                CollisionDataGPU cd;
                cd.colliderA = pair.colliderA;
                cd.colliderB = pair.colliderB;
                cd.depth = 0.1;
                cd.normal = vec4(1,0,0,0);
                cd.contactPointA = vec4(trA.position.xyz, 0);
                cd.contactPointB = vec4(trB.position.xyz, 0);
                collisions[outIdx] = cd;
            }
        }
    )");

    // �����ײ��ɫ�������г�����
    solveCollisionsProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 0) buffer Rigidbody { RigidbodyGPU rigidbodies[]; };
        layout(std430, binding = 1) buffer Transform { TransformGPU transforms[]; };
        layout(std430, binding = 5) buffer CollisionData { CollisionDataGPU collisions[]; };

        uniform float deltaTime;
        uniform float restitution;
        uniform float staticFriction;
        uniform float dynamicFriction;
        uniform int iteration;

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= collisions.length()) return;

            CollisionDataGPU cd = collisions[idx];
            int idA = cd.colliderA;
            int idB = cd.colliderB;
            RigidbodyGPU rbA = rigidbodies[idA];
            RigidbodyGPU rbB = rigidbodies[idB];
            TransformGPU trA = transforms[idA];
            TransformGPU trB = transforms[idB];

            if (rbA.type == 0 && rbB.type == 0) return; // ������̬�������

            // ����������򻯣����Խ��ٶȣ�
            float invMassA = (rbA.type == 1) ? 1.0 / rbA.mass : 0.0;
            float invMassB = (rbB.type == 1) ? 1.0 / rbB.mass : 0.0;
            float totalInvMass = invMassA + invMassB;

            vec3 normal = cd.normal.xyz;
            vec3 rA = cd.contactPointA.xyz - trA.position.xyz;
            vec3 rB = cd.contactPointB.xyz - trB.position.xyz;

            // ����ٶ�
            vec3 velA = rbA.velocity.xyz; // ���Խ��ٶ�
            vec3 velB = rbB.velocity.xyz;
            vec3 relVel = velB - velA;
            float normalVel = dot(relVel, normal);

            // ���㷨�����
            float bias = 0.2 * cd.depth / deltaTime; // λ������ƫ��
            float j = -(1.0 + restitution) * normalVel + bias;
            j = max(j, 0.0) / (totalInvMass); // �򻯷�ĸ

            vec3 impulse = j * normal;

            // ֱ��д���ٶȣ����ܴ��ھ�������������������
            if (rbA.type == 1) {
                rbA.velocity.xyz -= impulse * invMassA;
                rigidbodies[idA].velocity = rbA.velocity;
            }
            if (rbB.type == 1) {
                rbB.velocity.xyz += impulse * invMassB;
                rigidbodies[idB].velocity = rbB.velocity;
            }
        }
    )");

    // λ��������ɫ��
    positionCorrectionProgram = CompileComputeShader(R"(
        #version 430 core
        layout(local_size_x = 64) in;
        layout(std430, binding = 1) buffer Transform { TransformGPU transforms[]; };
        layout(std430, binding = 2) buffer Collider { ColliderGPU colliders[]; };
        layout(std430, binding = 5) buffer CollisionData { CollisionDataGPU collisions[]; };
        layout(std430, binding = 0) buffer Rigidbody { RigidbodyGPU rigidbodies[]; }; // ��Ҫ����

        uniform float positionCorrectionFactor;

        void main() {
            uint idx = gl_GlobalInvocationID.x;
            if (idx >= collisions.length()) return;

            CollisionDataGPU cd = collisions[idx];
            int idA = cd.colliderA;
            int idB = cd.colliderB;
            ColliderGPU colA = colliders[idA];
            ColliderGPU colB = colliders[idB];
            TransformGPU trA = transforms[idA];
            TransformGPU trB = transforms[idB];

            float invMassA = (rigidbodies[idA].type == 1) ? 1.0 / rigidbodies[idA].mass : 0.0;
            float invMassB = (rigidbodies[idB].type == 1) ? 1.0 / rigidbodies[idB].mass : 0.0;
            float totalInvMass = invMassA + invMassB;

            vec3 correction = cd.depth / totalInvMass * cd.normal.xyz * positionCorrectionFactor;

            trA.position.xyz -= correction * invMassA;
            trB.position.xyz += correction * invMassB;

            transforms[idA] = trA;
            transforms[idB] = trB;
            colliders[idA].isDirty = 1;
            colliders[idB].isDirty = 1;
        }
    )");
}

void ComputePhysics::InitGPUResources() {
    if (rigidbodySSBO) glDeleteBuffers(1, &rigidbodySSBO);
    if (transformSSBO) glDeleteBuffers(1, &transformSSBO);
    if (colliderSSBO) glDeleteBuffers(1, &colliderSSBO);
    if (meshVertexSSBO) glDeleteBuffers(1, &meshVertexSSBO);
    if (pairSSBO) glDeleteBuffers(1, &pairSSBO);
    if (collisionDataSSBO) glDeleteBuffers(1, &collisionDataSSBO);
    if (atomicCounter) glDeleteBuffers(1, &atomicCounter);
    if (atomicCounterNarrow) glDeleteBuffers(1, &atomicCounterNarrow);

    // ��������ԭ�Ӽ�����
    glGenBuffers(1, &atomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicCounter);

    glGenBuffers(1, &atomicCounterNarrow);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounterNarrow);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 1, atomicCounterNarrow);
}

void ComputePhysics::UploadColliderData() {
    std::vector<RigidbodyGPU> rbGPU(numColliders);
    std::vector<TransformGPU> trGPU(numColliders);
    std::vector<ColliderGPU> colGPU(numColliders);
    std::vector<glm::vec3> meshVertices;

    for (int i = 0; i < numColliders; ++i) {
        Collider* c = colliders[i];
        RigidbodyComponent* rb = c->rigidbody;
        TransformComponent* tr = c->transform;

        rbGPU[i].velocity = glm::vec4(rb->velocity, 0);
        rbGPU[i].angularVelocity = glm::vec4(rb->angularVelocity, 0);
        rbGPU[i].invInertiaLocal = rb->inverseInertia; // �����Ѽ���
        // GPU path: 1 = Dynamic (integrated), 0 = non-integrated. Kinematic maps
        // to 0 here (infinite-mass obstacle); hierarchy-driven Kinematic motion
        // is a CPU-path feature and is not yet modeled on the GPU solver.
        rbGPU[i].type = (rb->type == RigidbodyComponent::Dynamic) ? 1 : 0;
        rbGPU[i].mass = rb->mass;
        rbGPU[i].linearDamping = linearDamping;   // ʹ��ȫ��ֵ��Ҳ�ɴ� rb ��ȡ
        rbGPU[i].angularDamping = angularDamping;
        rbGPU[i].useGravity = rb->useGravity ? 1 : 0;
        rbGPU[i]._pad[0] = rbGPU[i]._pad[1] = 0;

        trGPU[i].position = glm::vec4(tr->position, 0);

        // ��ŷ���ǣ�vec3��ת��Ϊ��Ԫ����vec4��
        glm::quat q = glm::quat(tr->rotation); // ���� tr->rotation ��ŷ���ǣ����ȣ�
        trGPU[i].rotation = glm::vec4(q.x, q.y, q.z, q.w);

        colGPU[i].transformIdx = i;
        colGPU[i].rigidbodyIdx = i;
        if (c->mesh) {
            colGPU[i].meshStart = (int)meshVertices.size();
            colGPU[i].meshCount = (int)c->mesh->vertices.size();
            meshVertices.insert(meshVertices.end(), c->mesh->vertices.begin(), c->mesh->vertices.end());
        }
        else {
            colGPU[i].meshStart = 0;
            colGPU[i].meshCount = 0;
        }
        colGPU[i].localAABBMin = glm::vec4(c->localAABB.min, 0);
        colGPU[i].localAABBMax = glm::vec4(c->localAABB.max, 0);
        colGPU[i].worldAABBMin = glm::vec4(c->aabb.min, 0);
        colGPU[i].worldAABBMax = glm::vec4(c->aabb.max, 0);
        colGPU[i].isDirty = c->isDirty ? 1 : 0;
        colGPU[i]._pad[0] = colGPU[i]._pad[1] = colGPU[i]._pad[2] = 0;
    }

    // �������ϴ� SSBO
    glGenBuffers(1, &rigidbodySSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, rigidbodySSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, rbGPU.size() * sizeof(RigidbodyGPU), rbGPU.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, rigidbodySSBO);

    glGenBuffers(1, &transformSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transformSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, trGPU.size() * sizeof(TransformGPU), trGPU.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, transformSSBO);

    glGenBuffers(1, &colliderSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, colliderSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, colGPU.size() * sizeof(ColliderGPU), colGPU.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, colliderSSBO);

    glGenBuffers(1, &meshVertexSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, meshVertexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, meshVertices.size() * sizeof(glm::vec3), meshVertices.data(), GL_STATIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, meshVertexSSBO);

    // ��ײ�Ժ���ײ���ݻ���������ʼ��С��
    glGenBuffers(1, &pairSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pairSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxPairs * sizeof(CollisionPairGPU), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, pairSSBO);

    glGenBuffers(1, &collisionDataSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, collisionDataSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxPairs * sizeof(CollisionDataGPU), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, collisionDataSSBO);
}

void ComputePhysics::GenerateColliders(const std::vector<GameObject*>& gameobjects) {
    // ���û������� colliders����� colliders ������
    Physics::GenerateColliders(gameobjects);
    numColliders = (int)colliders.size();
    if (numColliders == 0) return;

    InitGPUResources();
    UploadColliderData();
}

void ComputePhysics::UpdatePhysics(float dt) {
    t += dt;
    if (t < deltaTime) return;

    int steps = glm::min(3.0f, t / deltaTime);
    t = fmod(t, deltaTime);

    for (int step = 0; step < steps; ++step) {
        // ����
        DispatchIntegrateForce(deltaTime);

        // �������� AABB
        DispatchUpdateAABB();

        // Broad phase
        DispatchBroadPhase();

        // ��ȡʵ����ײ������
        GLuint pairCount;
        glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter);
        glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &pairCount);
        if (pairCount >= (GLuint)maxPairs) {
            maxPairs = pairCount + 1024; // ��չ
            ResizePairBuffers(maxPairs);
        }

        // Narrow phase������ʵ�ʶ�������
        DispatchNarrowPhase(pairCount);

        // �����ײ����ε�����
        for (int iter = 0; iter < iterations; ++iter) {
            DispatchSolveCollisions(iter);
        }

        // λ������
        DispatchPositionCorrection();
    }

    // ���任���ݶ��� CPU���Ա���Ⱦ��
    ReadbackTransforms();
}

void ComputePhysics::DispatchIntegrateForce(float dt) {
    glUseProgram(integrateForceProgram);
    glUniform1f(glGetUniformLocation(integrateForceProgram, "dt"), dt);
    glUniform1f(glGetUniformLocation(integrateForceProgram, "gravity"), gravity);
    glUniform1f(glGetUniformLocation(integrateForceProgram, "linearDamping"), linearDamping);
    glUniform1f(glGetUniformLocation(integrateForceProgram, "angularDamping"), angularDamping);
    glDispatchCompute((numColliders + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ComputePhysics::DispatchUpdateAABB() {
    glUseProgram(updateAABBProgram);
    glDispatchCompute((numColliders + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ComputePhysics::DispatchBroadPhase() {
    // ����ԭ�Ӽ�����
    GLuint zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    glUseProgram(broadPhaseProgram);
    glUniform3f(glGetUniformLocation(broadPhaseProgram, "worldMin"), -100, -100, -100);
    glUniform3f(glGetUniformLocation(broadPhaseProgram, "worldMax"), 100, 100, 100);
    glUniform3i(glGetUniformLocation(broadPhaseProgram, "gridRes"), 32, 32, 32);
    glDispatchCompute((numColliders + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
}

void ComputePhysics::DispatchNarrowPhase(GLuint pairCount) {
    // ������ײ���ݼ�����
    GLuint zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounterNarrow);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    glUseProgram(narrowPhaseProgram);
    glUniform1ui(glGetUniformLocation(narrowPhaseProgram, "pairCount"), pairCount);
    glDispatchCompute((pairCount + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
}

void ComputePhysics::DispatchSolveCollisions(int iteration) {
    glUseProgram(solveCollisionsProgram);
    glUniform1f(glGetUniformLocation(solveCollisionsProgram, "deltaTime"), deltaTime);
    glUniform1f(glGetUniformLocation(solveCollisionsProgram, "restitution"), restitution);
    glUniform1f(glGetUniformLocation(solveCollisionsProgram, "staticFriction"), staticFriction);
    glUniform1f(glGetUniformLocation(solveCollisionsProgram, "dynamicFriction"), dynamicFriction);
    glUniform1i(glGetUniformLocation(solveCollisionsProgram, "iteration"), iteration);
    glDispatchCompute((maxPairs + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ComputePhysics::DispatchPositionCorrection() {
    glUseProgram(positionCorrectionProgram);
    glUniform1f(glGetUniformLocation(positionCorrectionProgram, "positionCorrectionFactor"), positionCorrectionFactor);
    glDispatchCompute((maxPairs + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ComputePhysics::ReadbackTransforms() {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transformSSBO);
    TransformGPU* transforms = (TransformGPU*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
    if (transforms) {
        for (int i = 0; i < numColliders; ++i) {
            Collider* c = colliders[i];
            c->transform->position = glm::vec3(transforms[i].position);
            // ����Ԫ��ת��ŷ����
            glm::quat q(transforms[i].rotation); // ֱ��ʹ�� vec4 ���죬˳��Ϊ (x,y,z,w)
            c->transform->rotation = glm::eulerAngles(q); // ���� vec3 ŷ���ǣ����ȣ�
            c->transform->localDirty = true;
        }
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
}

void ComputePhysics::ResizePairBuffers(int requiredPairs) {
    maxPairs = requiredPairs;
    glDeleteBuffers(1, &pairSSBO);
    glDeleteBuffers(1, &collisionDataSSBO);

    glGenBuffers(1, &pairSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pairSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxPairs * sizeof(CollisionPairGPU), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, pairSSBO);

    glGenBuffers(1, &collisionDataSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, collisionDataSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxPairs * sizeof(CollisionDataGPU), nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, collisionDataSSBO);
}