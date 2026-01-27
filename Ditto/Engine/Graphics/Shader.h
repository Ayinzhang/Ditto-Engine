#pragma once
#include <cstdint>
#include "../../3rdParty/GLM/glm.hpp"

struct Shader
{
    uint32_t id;
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
	void SetUniformMat4(const char* name, glm::mat4 mat);
	void SetUniformVec2(const char* name, glm::vec2 vector);
	void SetUniformVec3(const char* name, glm::vec3 vector);
	void SetUniformVec4(const char* name, glm::vec4 vector);
	void SetUniform1f(const char* name, float f);
	void SetUniform1i(const char* name, int slot);
};
