#pragma once
#include <cstdint>

struct Shader
{
    uint32_t id;
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
};
