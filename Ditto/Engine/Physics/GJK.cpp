#include "GJK.h"
#include <limits>
#include <algorithm>
#include <vector>

static glm::vec3 GetSupportPoint(Collider* collider, const glm::vec3& direction)
{
    glm::mat4 worldMat = collider->biasWorldModel;

    glm::mat3 worldToLocalRotScale = glm::mat3(glm::inverse(worldMat));
    glm::vec3 localDir = glm::normalize(worldToLocalRotScale * direction);

    glm::vec3 localSupport(0.0f);
    float maxDot = -std::numeric_limits<float>::infinity();
    for (const auto& vertex : collider->mesh->vertices)
    {
        float dot = glm::dot(vertex, localDir);
        if (dot > maxDot) { maxDot = dot; localSupport = vertex; }
    }

    return glm::vec3(worldMat * glm::vec4(localSupport, 1.0f));
}

static SupportPoint GetMinkowskiSupport(Collider* colliderA, Collider* colliderB, const glm::vec3& direction) {
    glm::vec3 supportA = GetSupportPoint(colliderA, direction);
    glm::vec3 supportB = GetSupportPoint(colliderB, -direction);
    return SupportPoint(supportA - supportB, supportA, supportB);
}

bool UpdateSimplex(std::vector<SupportPoint>& simplex, glm::vec3& direction) 
{
    if (simplex.size() == 2) 
    { 
        glm::vec3 A = simplex[1].point;
        glm::vec3 B = simplex[0].point;
        glm::vec3 AB = B - A;
        glm::vec3 AO = -A;

        if (glm::dot(AB, AO) > 0) direction = glm::cross(glm::cross(AB, AO), AB);
        else { simplex = { simplex[1] }; direction = AO;  }
    }
    else if (simplex.size() == 3) 
    { 
        glm::vec3 A = simplex[2].point;
        glm::vec3 B = simplex[1].point;
        glm::vec3 C = simplex[0].point;
        glm::vec3 AB = B - A;
        glm::vec3 AC = C - A;
        glm::vec3 AO = -A;
        glm::vec3 ABC = glm::cross(AB, AC);

        if (glm::dot(glm::cross(ABC, AC), AO) > 0) 
        {
            if (glm::dot(AC, AO) > 0) 
            {
                simplex = { simplex[0], simplex[2] };
                direction = glm::cross(glm::cross(AC, AO), AC);
            }
            else 
            {
                simplex = { simplex[1], simplex[2] };
                return UpdateSimplex(simplex, direction);
            }
        }
        else 
        {
            if (glm::dot(glm::cross(AB, ABC), AO) > 0) 
            {
                simplex = { simplex[1], simplex[2] };
                return UpdateSimplex(simplex, direction);
            }
            else 
            {
                if (glm::dot(ABC, AO) > 0) direction = ABC;
                else 
                {
                    simplex = { simplex[1], simplex[0], simplex[2] };
                    direction = -ABC;
                }
            }
        }
    }
    else if (simplex.size() == 4) 
    {
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
        return true;
    }
    return false;
}

glm::vec3 GetClosestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) 
{
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;

    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

glm::vec3 GetBarycentricCoordinates(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) 
{
    glm::vec3 v0 = b - a;
    glm::vec3 v1 = c - a;
    glm::vec3 v2 = p - a;

    float d00 = glm::dot(v0, v0);
    float d01 = glm::dot(v0, v1);
    float d11 = glm::dot(v1, v1);
    float d20 = glm::dot(v2, v0);
    float d21 = glm::dot(v2, v1);

    float denom = d00 * d11 - d01 * d01;
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;

    return glm::vec3(u, v, w);
}

CollisionInfo EPA(std::vector<SupportPoint> simplex, Collider* a, Collider* b) 
{
    std::vector<Triangle> faces;
    auto addFace = [&](int i, int j, int k) 
        {
        Triangle t;
        t.a = simplex[i].point;
        t.b = simplex[j].point;
        t.c = simplex[k].point;
        
        t.supportA = simplex[i];
        t.supportB = simplex[j];
        t.supportC = simplex[k];

        t.normal = glm::normalize(glm::cross(t.b - t.a, t.c - t.a));
        t.dis = glm::dot(t.normal, t.a);

        if (t.dis < 0) 
        {
            std::swap(t.b, t.c);
            std::swap(t.supportB, t.supportC);
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

        if (d - minDist < 0.001f) {
            CollisionInfo res;
            res.flag = true;
            res.normal = f.normal;
            res.depth = d;

            glm::vec3 closestPoint = GetClosestPointOnTriangle(glm::vec3(0), f.a, f.b, f.c);
            glm::vec3 barycentric = GetBarycentricCoordinates(closestPoint, f.a, f.b, f.c);

            res.contactPointA = barycentric.x * f.supportA.pointA +
                barycentric.y * f.supportB.pointA +
                barycentric.z * f.supportC.pointA;

            res.contactPointB = barycentric.x * f.supportA.pointB +
                barycentric.y * f.supportB.pointB +
                barycentric.z * f.supportC.pointB;

            return res;
        }

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


CollisionInfo GJK_CheckCollision(Collider* colliderA, Collider* colliderB) 
{
    CollisionInfo result;
    glm::vec3 direction = glm::vec3(1, 0, 0);

    std::vector<SupportPoint> simplex;
    simplex.push_back(GetMinkowskiSupport(colliderA, colliderB, direction));
    direction = -simplex[0].point;

    for (int i = 0; i < 50; ++i) 
    {
        SupportPoint next = GetMinkowskiSupport(colliderA, colliderB, direction);
        if (glm::dot(next.point, direction) < 0) return result;

        simplex.push_back(next);
        if (UpdateSimplex(simplex, direction)) return EPA(simplex, colliderA, colliderB);
    }
    return result;
}
