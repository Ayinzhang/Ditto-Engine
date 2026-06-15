#include "Camera.h"
#include "../../3rdParty/GLM/ext/matrix_clip_space.hpp"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265359f;

    float ClampPitch(float value)
    {
        return std::clamp(value, -89.0f, 89.0f);
    }

    float SafeAspect(float aspect)
    {
        return aspect > 0.0001f ? aspect : 1.0f;
    }
}

Camera::Camera(glm::vec3 _position, glm::vec3 target, glm::vec3 worldup)
{
    position = _position;
    worldUp = glm::normalize(worldup);
    LookAt(target);
}

glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(position, position + forward, worldUp);
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) const
{
    aspect = SafeAspect(aspect);
    if (projectionType == ProjectionType::Orthographic)
    {
        const float halfHeight = std::max(0.0001f, orthographicSize);
        const float halfWidth = halfHeight * aspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearClipPlane, farClipPlane);
    }

    return glm::perspective(glm::radians(std::clamp(fieldOfView, 1.0f, 179.0f)),
        aspect, nearClipPlane, farClipPlane);
}

glm::mat4 Camera::GetViewProjectionMatrix(float aspect) const
{
    return GetProjectionMatrix(aspect) * GetViewMatrix();
}

Camera::Ray Camera::ScreenPointToRayFull(const glm::vec2& screenPoint, int viewportWidth, int viewportHeight) const
{
    const float w = static_cast<float>(std::max(1, viewportWidth));
    const float h = static_cast<float>(std::max(1, viewportHeight));
    const float ndcX = (2.0f * screenPoint.x / w) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenPoint.y / h);

    glm::mat4 invView = glm::inverse(GetViewMatrix());

    if (projectionType == ProjectionType::Orthographic)
    {
        const float halfHeight = std::max(0.0001f, orthographicSize);
        const float halfWidth = halfHeight * SafeAspect(w / h);
        glm::vec3 localPoint(ndcX * halfWidth, ndcY * halfHeight, 0.0f);
        glm::vec3 worldPoint = glm::vec3(invView * glm::vec4(localPoint, 1.0f));
        return Ray{ worldPoint, glm::normalize(forward) };
    }

    glm::mat4 invProj = glm::inverse(GetProjectionMatrix(w / h));
    glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    return Ray{ position, glm::normalize(glm::vec3(invView * rayEye)) };
}

glm::vec3 Camera::ScreenPointToRay(const glm::vec2& screenPoint, int viewportWidth, int viewportHeight) const
{
    return ScreenPointToRayFull(screenPoint, viewportWidth, viewportHeight).direction;
}

void Camera::LookAt(const glm::vec3& target)
{
    glm::vec3 direction = target - position;
    if (glm::length(direction) <= 0.0001f)
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        forward = glm::normalize(direction);

    yaw = std::atan2(forward.x, forward.z) * 180.0f / kPi;
    pitch = ClampPitch(std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / kPi);
    glm::vec3 rightCandidate = glm::cross(forward, worldUp);
    if (glm::length(rightCandidate) <= 0.0001f)
        rightCandidate = glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f));
    right = glm::normalize(rightCandidate);
    up = -glm::normalize(glm::cross(forward, right));
}

void Camera::SetPerspective(float fovDegrees, float nearClip, float farClip)
{
    projectionType = ProjectionType::Perspective;
    fieldOfView = std::clamp(fovDegrees, 1.0f, 179.0f);
    nearClipPlane = std::max(0.0001f, nearClip);
    farClipPlane = std::max(nearClipPlane + 0.0001f, farClip);
}

void Camera::SetOrthographic(float size, float nearClip, float farClip)
{
    projectionType = ProjectionType::Orthographic;
    orthographicSize = std::max(0.0001f, size);
    nearClipPlane = std::max(0.0001f, nearClip);
    farClipPlane = std::max(nearClipPlane + 0.0001f, farClip);
}

void Camera::ProcessMouseMovement(float daltax, float daltay)
{
    yaw -= daltax;
    pitch = ClampPitch(pitch - daltay);
    UpdateCameraVectors();
}

void Camera::UpdateCameraVectors()
{
    const float yawRad = glm::radians(yaw);
    const float pitchRad = glm::radians(ClampPitch(pitch));

    forward = glm::normalize(glm::vec3(
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad)));
    glm::vec3 rightCandidate = glm::cross(forward, worldUp);
    if (glm::length(rightCandidate) <= 0.0001f)
        rightCandidate = glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f));
    right = glm::normalize(rightCandidate);
    up = -glm::normalize(glm::cross(forward, right));
}

void Camera::RotateAroundOrigin(float deltaYaw, float deltaPitch)
{
    yaw += deltaYaw;
    pitch = ClampPitch(pitch + deltaPitch);

    float distance = glm::length(position);
    UpdateCameraVectors();
    position = forward * distance;
}
