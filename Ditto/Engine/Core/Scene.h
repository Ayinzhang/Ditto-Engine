#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "GameObject.h"
#include "../Physics/Physics.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include "../../3rdParty/GLAD/glad.h"

class Shader;
class Resource;

struct BaseGeometry
{
    GLuint VAO = 0, VBO = 0, EBO = 0;
    uint32_t vertexCount = 0, indexCount = 0;
};

struct GeometryInstances
{
    RendererComponent::Type type;
    std::vector<glm::mat4> modelMatrices;
    std::vector<glm::vec4> instanceColors;
    GLuint modelSSBO = 0, colorSSBO = 0;
    size_t instanceCount = 0;

    GeometryInstances(RendererComponent::Type t) : type(t) {}
    ~GeometryInstances();
};

struct Scene
{
    std::string name;
    std::vector<GameObject*> gameObjects;
    GameObject* rootGameObject = nullptr;

    GameObject* mainLight = nullptr;
    std::unordered_map<RendererComponent::Type, BaseGeometry> baseGeometries;
    std::unordered_map<RendererComponent::Type, GeometryInstances*> geometryBatches;
    
    Resource* resource = nullptr;

    std::function<void()> onModified;

    Scene();
    ~Scene();

    void ClearScene();
    bool SaveScene(const std::string& filepath);
    bool LoadScene(const std::string& filepath);

    void MarkDirty() { if (onModified) onModified(); }

    void CollectRenderData();
    void UpdateSSBOs();
    void Render(Shader* shader, const glm::mat4& view, const glm::mat4& projection,
        const glm::vec3& viewPos, int viewportWidth, int viewportHeight);

    void InitializeBaseGeometries(Resource* resource);

    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight);

    glm::vec3 GetLightColor() const;
    glm::vec3 GetLightDirection() const;
    float GetLightIntensity() const;
};