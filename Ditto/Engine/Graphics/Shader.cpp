#include "Shader.h"
#include "../../3rdParty/GLAD/glad.h"
#include <fstream>
#include <sstream>
#include <iostream>

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