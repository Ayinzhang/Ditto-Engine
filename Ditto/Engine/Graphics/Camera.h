#pragma once
#include "../../3rdParty/GLM/glm.hpp"

struct Camera
{
	glm::vec3 position, forward, right, up, worldUp; float yaw, pitch;
	Camera(glm::vec3 _position, glm::vec3 target, glm::vec3 worldup);
	glm::mat4 GetViewMatrix();
	void ProcessMouseMovement(float daltax, float daltay);
	void UpdateCameraVectors();
};