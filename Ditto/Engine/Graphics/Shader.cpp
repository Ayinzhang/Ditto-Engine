#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    std::string vertSrc = ReadFile(vertexPath);
    std::string fragSrc = ReadFile(fragmentPath);

    const char* vSrc = vertSrc.c_str();
    const char* fSrc = fragSrc.c_str();

    uint32_t vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vSrc, nullptr);
    glCompileShader(vertex);

    uint32_t fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fSrc, nullptr);
    glCompileShader(fragment);

    id = glCreateProgram();
    glAttachShader(id, vertex);
    glAttachShader(id, fragment);
    glLinkProgram(id); glUseProgram(id);

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader()
{
    glDeleteProgram(id); glUseProgram(0);
}

void Shader::SetUniformMat4(const char* name, glm::mat4 mat)
{
    glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, value_ptr(mat));
}
void Shader::SetUniformVec2(const char* name, glm::vec2 vector)
{
    glUniform2f(glGetUniformLocation(id, name), vector.x, vector.y);
}
void Shader::SetUniformVec3(const char* name, glm::vec3 vector)
{
    glUniform3f(glGetUniformLocation(id, name), vector.x, vector.y, vector.z);
}
void Shader::SetUniformVec4(const char* name, glm::vec4 vector)
{
    glUniform4f(glGetUniformLocation(id, name), vector.x, vector.y, vector.z, vector.w);
}
void Shader::SetUniform1f(const char* name, float f)
{
    glUniform1f(glGetUniformLocation(id, name), f);
}
void Shader::SetUniform1i(const char* name, int slot)
{
    glUniform1i(glGetUniformLocation(id, name), slot);
}