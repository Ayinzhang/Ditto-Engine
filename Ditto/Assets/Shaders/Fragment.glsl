#version 450 core

in vec3 pos;
in vec3 normal;
out vec4 col;

uniform vec4 objCol;
uniform vec3 lightCol;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform float lightIntensity;

const float ambientStrength = 0.3;
const float diffuseStrength = 0.5;
const float specularStrength = 0.2;
const int shininess = 32;

void main()
{
    vec3 ambient = ambientStrength * lightCol;
    vec3 diffuse = diffuseStrength * max(0, dot(normal, -lightDir)) * lightCol;

    vec3 viewDir = normalize(viewPos - pos);
    vec3 reflectDir = reflect(lightDir, normal);
    float spec = pow(max(0, dot(viewDir, reflectDir)), shininess);
    vec3 specular = specularStrength * spec * lightCol;
    
    vec3 lighting = (ambient + diffuse + specular) * lightIntensity;
    col = vec4(lighting * objCol.xyz, objCol.w);
}