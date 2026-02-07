#pragma once
#include "CollisionShape.h"
#include "../../3rdParty/GLM/glm.hpp"
#include <vector>

struct SupportPoint
{
    glm::vec3 point, pointA, pointB; 

    SupportPoint() : point(glm::vec3(0)), pointA(glm::vec3(0)), pointB(glm::vec3(0)) {}
    SupportPoint(glm::vec3 p, glm::vec3 a, glm::vec3 b) : point(p), pointA(a), pointB(b) {}
};

struct Edge {
    glm::vec3 a, b;
};

struct Triangle
{
    float dis;
    glm::vec3 a, b, c, normal;
    SupportPoint supportA, supportB, supportC;

	Triangle() : dis(0), a(glm::vec3(0)), b(glm::vec3(0)), c(glm::vec3(0)), normal(glm::vec3(0)), supportA(), supportB(), supportC() {}
};

struct CollisionInfo
{
    bool flag; float depth;
    glm::vec3 normal, contactPointA, contactPointB;

	CollisionInfo() : flag(false), depth(0.0f), normal(glm::vec3(0)), contactPointA(glm::vec3(0)), contactPointB(glm::vec3(0)) {}
};

CollisionInfo GJK_CheckCollision(Collider* colliderA, Collider* colliderB);

glm::mat3 CalculateWorldInverseInertia(Collider* collider);