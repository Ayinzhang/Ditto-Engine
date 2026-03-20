#include "Camera.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include <cmath>

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

void Camera::RotateAroundOrigin(float deltaYaw, float deltaPitch)
{
	// 更新欧拉角
	yaw += deltaYaw;
	pitch += deltaPitch;
	
	// 限制 pitch 防止翻转
	if (pitch > 89.0f) pitch = 89.0f;
	if (pitch < -89.0f) pitch = -89.0f;
	
	// 保持距离不变，重新计算位置（相对于当前位置）
	float distance = glm::length(position);
	
	// 计算新的前方向量
	forward = glm::vec3(cos(glm::radians(pitch)) * sin(glm::radians(yaw)), 
						sin(glm::radians(pitch)), 
						cos(glm::radians(pitch)) * cos(glm::radians(yaw)));
	
	// 重新计算位置（保持距离）
	position = forward * distance;
	
	// 更新其他向量
	right = normalize(cross(forward, worldUp));
	up = -normalize(cross(forward, right));
}