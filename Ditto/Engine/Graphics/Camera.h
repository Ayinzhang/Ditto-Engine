#pragma once
#include "../../3rdParty/GLM/glm.hpp"

struct Camera
{
    enum class ProjectionType { Perspective, Orthographic };
    struct Ray
    {
        glm::vec3 origin{ 0.0f };
        glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
    };

    glm::vec3 position, forward, right, up, worldUp;
    float yaw, pitch;
    ProjectionType projectionType = ProjectionType::Perspective;
    float fieldOfView = 45.0f;
    float orthographicSize = 5.0f;
    float nearClipPlane = 0.1f;
    float farClipPlane = 100.0f;
    glm::vec4 backgroundColor{ 0.1f, 0.1f, 0.1f, 1.0f };

    Camera(glm::vec3 _position, glm::vec3 target, glm::vec3 worldup);
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspect) const;
    glm::mat4 GetViewProjectionMatrix(float aspect) const;
    Ray ScreenPointToRayFull(const glm::vec2& screenPoint, int viewportWidth, int viewportHeight) const;
    glm::vec3 ScreenPointToRay(const glm::vec2& screenPoint, int viewportWidth, int viewportHeight) const;
    glm::vec3 GetRayOrigin() const { return position; }
    void LookAt(const glm::vec3& target);
    void SetPerspective(float fovDegrees, float nearClip, float farClip);
    void SetOrthographic(float size, float nearClip, float farClip);
    void ProcessMouseMovement(float daltax, float daltay);
    void UpdateCameraVectors();
    void RotateAroundOrigin(float deltaYaw, float deltaPitch);
};
