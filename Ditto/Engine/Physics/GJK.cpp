#include "GJK.h"
#include <limits>
#include <algorithm>
#include <vector>

// --- 核心辅助函数 ---

static glm::vec3 GetSupportPoint(Collider* collider, const glm::vec3& direction) {
    // 1. 处理变换矩阵
    glm::mat4 translationMat = glm::translate(glm::mat4(1.0f), collider->predictedPosition);
    glm::mat4 rotationMat = glm::mat4_cast(collider->predictedRotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(collider->transform->scale[0], collider->transform->scale[1], collider->transform->scale[2]));
    glm::mat4 transformMat = translationMat * rotationMat * scaleMat;

    // 2. 转换方向到局部空间 (逆缩放和旋转)
    glm::vec3 localDir = glm::inverse(glm::mat3(rotationMat * scaleMat)) * direction;

    // 3. 寻找局部空间支持点 (修复：去掉除以 vertex 长度的逻辑)
    glm::vec3 localSupport(0.0f);
    float maxDot = -std::numeric_limits<float>::infinity();

    for (const auto& vertex : collider->mesh->vertices) {
        float dot = glm::dot(vertex, localDir);
        if (dot > maxDot) {
            maxDot = dot;
            localSupport = vertex;
        }
    }

    // 4. 转回世界空间
    return glm::vec3(transformMat * glm::vec4(localSupport, 1.0f));
}

static SupportPoint GetMinkowskiSupport(Collider* colliderA, Collider* colliderB, const glm::vec3& direction) {
    glm::vec3 supportA = GetSupportPoint(colliderA, direction);
    glm::vec3 supportB = GetSupportPoint(colliderB, -direction);
    return SupportPoint(supportA - supportB, supportA, supportB);
}

// --- GJK 状态机更新 ---

bool UpdateSimplex(std::vector<SupportPoint>& simplex, glm::vec3& direction) {
    if (simplex.size() == 2) { // 线段
        glm::vec3 A = simplex[1].point;
        glm::vec3 B = simplex[0].point;
        glm::vec3 AB = B - A;
        glm::vec3 AO = -A;

        if (glm::dot(AB, AO) > 0) {
            direction = glm::cross(glm::cross(AB, AO), AB);
        }
        else {
            simplex = { simplex[1] };
            direction = AO;
        }
    }
    else if (simplex.size() == 3) { // 三角形
        glm::vec3 A = simplex[2].point;
        glm::vec3 B = simplex[1].point;
        glm::vec3 C = simplex[0].point;
        glm::vec3 AB = B - A;
        glm::vec3 AC = C - A;
        glm::vec3 AO = -A;
        glm::vec3 ABC = glm::cross(AB, AC);

        if (glm::dot(glm::cross(ABC, AC), AO) > 0) {
            if (glm::dot(AC, AO) > 0) {
                simplex = { simplex[0], simplex[2] };
                direction = glm::cross(glm::cross(AC, AO), AC);
            }
            else {
                simplex = { simplex[1], simplex[2] };
                return UpdateSimplex(simplex, direction);
            }
        }
        else {
            if (glm::dot(glm::cross(AB, ABC), AO) > 0) {
                simplex = { simplex[1], simplex[2] };
                return UpdateSimplex(simplex, direction);
            }
            else {
                if (glm::dot(ABC, AO) > 0) {
                    direction = ABC;
                }
                else {
                    simplex = { simplex[1], simplex[0], simplex[2] }; // 保持逆时针
                    direction = -ABC;
                }
            }
        }
    }
    else if (simplex.size() == 4) { // 四面体
        glm::vec3 A = simplex[3].point;
        glm::vec3 B = simplex[2].point;
        glm::vec3 C = simplex[1].point;
        glm::vec3 D = simplex[0].point;
        glm::vec3 AB = B - A, AC = C - A, AD = D - A, AO = -A;
        glm::vec3 ABC = glm::cross(AB, AC);
        glm::vec3 ACD = glm::cross(AC, AD);
        glm::vec3 ADB = glm::cross(AD, AB);

        if (glm::dot(ABC, AO) > 0) { simplex = { simplex[1], simplex[2], simplex[3] }; direction = ABC; return false; }
        if (glm::dot(ACD, AO) > 0) { simplex = { simplex[0], simplex[1], simplex[3] }; direction = ACD; return false; }
        if (glm::dot(ADB, AO) > 0) { simplex = { simplex[2], simplex[0], simplex[3] }; direction = ADB; return false; }
        return true; // 包含原点
    }
    return false;
}

// --- EPA 实现 ---

CollisionInfo EPA(std::vector<SupportPoint> simplex, Collider* a, Collider* b) {
    std::vector<Triangle> faces;
    // 初始四面体的4个面 (确保法线朝外)
    auto addFace = [&](int i, int j, int k) {
        Triangle t;
        t.a = simplex[i].point; t.b = simplex[j].point; t.c = simplex[k].point;
        t.normal = glm::normalize(glm::cross(t.b - t.a, t.c - t.a));
        t.dis = glm::dot(t.normal, t.a);
        if (t.dis < 0) { // 翻转法线确保朝外
            std::swap(t.b, t.c);
            t.normal = -t.normal;
            t.dis = -t.dis;
        }
        faces.push_back(t);
        };

    addFace(0, 1, 2); addFace(0, 2, 3); addFace(0, 3, 1); addFace(1, 3, 2);

    for (int iter = 0; iter < 30; ++iter) {
        int closestFaceIdx = 0;
        float minDist = std::numeric_limits<float>::max();
        for (int i = 0; i < faces.size(); ++i) {
            if (faces[i].dis < minDist) {
                minDist = faces[i].dis;
                closestFaceIdx = i;
            }
        }

        Triangle& f = faces[closestFaceIdx];
        SupportPoint p = GetMinkowskiSupport(a, b, f.normal);
        float d = glm::dot(f.normal, p.point);

        if (d - minDist < 0.001f) { // 收敛
            CollisionInfo res;
            res.flag = true;
            res.normal = f.normal;
            res.depth = 1 - d;
            // 简化接触点：使用面中心投影回物体空间
            res.contactPointA = (f.a + f.b + f.c) / 3.0f;
            return res;
        }

        // 移除可见面并重建 (此处简化处理，实际生产建议使用 Horizon Edge 算法)
        std::vector<Edge> edges;
        for (int i = (int)faces.size() - 1; i >= 0; i--) {
            if (glm::dot(faces[i].normal, p.point - faces[i].a) > 0) {
                Edge e1 = { faces[i].a, faces[i].b }, e2 = { faces[i].b, faces[i].c }, e3 = { faces[i].c, faces[i].a };
                auto addOrRemove = [&](Edge e) {
                    for (auto it = edges.begin(); it != edges.end(); ++it) {
                        if (glm::length(it->a - e.b) < 1e-4f && glm::length(it->b - e.a) < 1e-4f) {
                            edges.erase(it); return;
                        }
                    }
                    edges.push_back(e);
                    };
                addOrRemove(e1); addOrRemove(e2); addOrRemove(e3);
                faces.erase(faces.begin() + i);
            }
        }
        for (auto& e : edges) {
            Triangle newFace;
            newFace.a = e.a; newFace.b = e.b; newFace.c = p.point;
            newFace.normal = glm::normalize(glm::cross(newFace.b - newFace.a, newFace.c - newFace.a));
            newFace.dis = glm::dot(newFace.normal, newFace.a);
            if (newFace.dis < 0) { std::swap(newFace.b, newFace.c); newFace.normal = -newFace.normal; newFace.dis = -newFace.dis; }
            faces.push_back(newFace);
        }
    }
    return {};
}

// --- 主入口 ---

CollisionInfo GJK_CheckCollision(Collider* colliderA, Collider* colliderB) {
    CollisionInfo result;
    glm::vec3 direction = glm::vec3(1, 0, 0); // 初始方向

    std::vector<SupportPoint> simplex;
    simplex.push_back(GetMinkowskiSupport(colliderA, colliderB, direction));
    direction = -simplex[0].point;

    for (int i = 0; i < 50; ++i) {
        SupportPoint next = GetMinkowskiSupport(colliderA, colliderB, direction);
        if (glm::dot(next.point, direction) < 0) return result; // 没过原点，无碰撞

        simplex.push_back(next);
        if (UpdateSimplex(simplex, direction)) {
            return EPA(simplex, colliderA, colliderB); // 包含原点，进 EPA
        }
    }
    return result;
}