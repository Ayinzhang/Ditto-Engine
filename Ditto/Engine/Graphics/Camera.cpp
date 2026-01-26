#include "Camera.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"

Camera::Camera(glm::vec3 _position, glm::vec3 target, glm::vec3 worldup)
{
	position = _position; worldUp = worldup;
	forward = normalize(target - position);
	right = normalize(cross(forward, worldUp));
	up = -normalize(cross(forward, right));
	yaw = atan2(forward.z, forward.x) * 180.0f / 3.14159265359f;
	pitch = asin(forward.y) * 180.0f / 3.14159265359f;
}

glm::mat4 Camera::GetViewMatrix()
{
	return glm::lookAt(position, forward + position, worldUp);
}

void Camera::ProcessMouseMovement(float daltax, float daltay)
{
	yaw -= daltax; pitch -= daltay;
	UpdateCameraVectors();
}

void Camera::UpdateCameraVectors()
{
	forward = glm::vec3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw));
	right = normalize(cross(forward, worldUp));
	up = -normalize(cross(forward, right));
}