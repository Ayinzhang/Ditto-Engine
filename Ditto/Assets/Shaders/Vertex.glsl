#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 pos;
out vec3 normal;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    pos = worldPos.xyz;
    normal = normalize(transpose(inverse(mat3(model))) * aNormal);
    gl_Position = projection * view * worldPos;
}