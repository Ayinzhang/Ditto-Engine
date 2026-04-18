#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

layout(std430, binding = 0) readonly buffer ModelMatrices { mat4 model[];};
layout(std430, binding = 1) readonly buffer InstanceColors { vec4 color[];};

out vec3 pos;
out vec3 normal;
out vec4 vertexColor;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    mat4 instanceModel = model[gl_InstanceID];
    vertexColor = color[gl_InstanceID];
    
    vec4 worldPos = instanceModel * vec4(aPos, 1.0); pos = worldPos.xyz;
    normal = normalize(transpose(inverse(mat3(instanceModel))) * aNormal);

    gl_Position = projection * view * worldPos;
}