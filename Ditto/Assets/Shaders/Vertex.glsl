#version 330 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 1) in vec2 uv;

out vec3 ourColor;  // 输出给片段着色器

uniform mat4 model;  // 模型矩阵
uniform mat4 view;   // 视图矩阵
uniform mat4 projection;  // 投影矩阵

void main()
{
    gl_Position = projection * view * model * vec4(pos, 1.0);  // 计算最终的顶点位置
}
