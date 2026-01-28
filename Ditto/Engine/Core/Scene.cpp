#include "Scene.h"
#include "../../Engine/Resources/Resource.h"
#include "../../Engine/Graphics/Shader.h"
#include <iostream>

Scene::Scene()
{
    name = "Default";

    GameObject* lightObj = new GameObject("DirLight");
    lightObj->AddComponent<LightComponent>();
	lightObj->GetComponent<TransformComponent>()->rotation[0] = -120.0f;
	lightObj->GetComponent<TransformComponent>()->UpdateTransform();
    gameObjects.push_back(lightObj);

    GameObject* gameObject = new GameObject("Cube");
    gameObject->AddComponent<RendererComponent>();
    gameObject->AddComponent<RigidbodyComponent>();
    gameObjects.push_back(gameObject);

    geometryBatches[RendererComponent::Cube] = new GeometryInstances(RendererComponent::Cube);
    geometryBatches[RendererComponent::Sphere] = new GeometryInstances(RendererComponent::Sphere);
    geometryBatches[RendererComponent::Plane] = new GeometryInstances(RendererComponent::Plane);
}

Scene::~Scene()
{
    for (GameObject* obj : gameObjects) delete obj;
    for (auto& pair : geometryBatches) delete pair.second;

    for (auto& pair : baseGeometries) 
    {
        if (pair.second.VAO) glDeleteVertexArrays(1, &pair.second.VAO);
        if (pair.second.VBO) glDeleteBuffers(1, &pair.second.VBO);
        if (pair.second.EBO) glDeleteBuffers(1, &pair.second.EBO);
    }
}

GeometryInstances::~GeometryInstances()
{
    if (modelSSBO) glDeleteBuffers(1, &modelSSBO);
    if (colorSSBO) glDeleteBuffers(1, &colorSSBO);
}

void Scene::CollectRenderData()
{
    for (auto& pair : geometryBatches) 
    {
        pair.second->modelMatrices.clear(); pair.second->instanceColors.clear();
        pair.second->instanceCount = 0; pair.second->dirty = true;
    }

    mainLight = nullptr;
    for (GameObject* obj : gameObjects) 
    {
        if (!obj->enabled) continue;

        LightComponent* light = obj->GetComponent<LightComponent>();
        if (light && light->enabled) { mainLight = obj; break; }
    }

    for (GameObject* obj : gameObjects) 
    {
        if (!obj->enabled) continue;

        RendererComponent* renderer = obj->GetComponent<RendererComponent>();
        TransformComponent* transform = obj->GetComponent<TransformComponent>();

        if (renderer && renderer->enabled && transform && transform->enabled) 
        {
            auto it = geometryBatches.find(renderer->type);
            if (it != geometryBatches.end()) 
            {
                GeometryInstances* batch = it->second;

                batch->modelMatrices.push_back(transform->model);
                batch->instanceColors.push_back(glm::vec4(renderer->color[0], renderer->color[1], renderer->color[2], renderer->color[3]));
                batch->instanceCount++;
                batch->dirty = true;
            }
        }
    }
}

void Scene::UpdateSSBOs()
{
    for (auto& pair : geometryBatches) 
    {
        GeometryInstances* batch = pair.second;

        if (!batch->dirty || batch->instanceCount == 0) continue;

        if (batch->modelSSBO == 0) glGenBuffers(1, &batch->modelSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch->modelSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, batch->instanceCount * sizeof(glm::mat4), batch->modelMatrices.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, batch->modelSSBO);

        if (batch->colorSSBO == 0) glGenBuffers(1, &batch->colorSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch->colorSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, batch->instanceCount * sizeof(glm::vec4), batch->instanceColors.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, batch->colorSSBO);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        batch->dirty = false;
    }
}

void Scene::Render(Shader* shader, const glm::mat4& view, const glm::mat4& projection,
    const glm::vec3& viewPos, int viewportWidth, int viewportHeight)
{
    CollectRenderData();
    UpdateSSBOs();

    glUseProgram(shader->id);
	shader->SetUniformMat4("view", view);
	shader->SetUniformMat4("projection", projection);
	shader->SetUniformVec3("viewPos", viewPos);
	shader->SetUniformVec3("lightCol", GetLightColor());
	shader->SetUniformVec3("lightDir", GetLightDirection());
	shader->SetUniform1f("lightIntensity", GetLightIntensity());

    for (auto& pair : geometryBatches) 
    {
        GeometryInstances* batch = pair.second;

        if (batch->instanceCount == 0) continue;
        auto geoIt = baseGeometries.find(batch->type);
        if (geoIt == baseGeometries.end()) continue;

        const BaseGeometry& geometry = geoIt->second;

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch->modelSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, batch->modelSSBO);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch->colorSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, batch->colorSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        glBindVertexArray(geometry.VAO);

        if (geometry.indexCount > 0)  glDrawElementsInstanced(GL_TRIANGLES, geometry.indexCount, GL_UNSIGNED_INT, 0, batch->instanceCount);
        else glDrawArraysInstanced(GL_TRIANGLES, 0, geometry.vertexCount, batch->instanceCount);

        glBindVertexArray(0);
    }
}

void Scene::InitializeBaseGeometries(Resource* resource)
{
    if (resource->cubeModel && !resource->cubeModel->vertexData.empty()) 
    {
        BaseGeometry cubeGeo;

        glGenVertexArrays(1, &cubeGeo.VAO);
        glGenBuffers(1, &cubeGeo.VBO);

        glBindVertexArray(cubeGeo.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeGeo.VBO);

        glBufferData(GL_ARRAY_BUFFER, resource->cubeModel->vertexData.size() * sizeof(float), resource->cubeModel->vertexData.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        cubeGeo.vertexCount = static_cast<uint32_t>(resource->cubeModel->vertexData.size() / 6);
        baseGeometries[RendererComponent::Cube] = cubeGeo;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    if (resource->sphereModel && !resource->sphereModel->vertexData.empty())
    {
        BaseGeometry sphereGeo;

        glGenVertexArrays(1, &sphereGeo.VAO);
        glGenBuffers(1, &sphereGeo.VBO);

        glBindVertexArray(sphereGeo.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, sphereGeo.VBO);

        glBufferData(GL_ARRAY_BUFFER, resource->sphereModel->vertexData.size() * sizeof(float), resource->sphereModel->vertexData.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        sphereGeo.vertexCount = static_cast<uint32_t>(resource->sphereModel->vertexData.size() / 6);
        baseGeometries[RendererComponent::Sphere] = sphereGeo;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    if (resource->planeModel && !resource->planeModel->vertexData.empty())
    {
        BaseGeometry planeGeo;

        glGenVertexArrays(1, &planeGeo.VAO);
        glGenBuffers(1, &planeGeo.VBO);

        glBindVertexArray(planeGeo.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, planeGeo.VBO);

        glBufferData(GL_ARRAY_BUFFER, resource->planeModel->vertexData.size() * sizeof(float), resource->planeModel->vertexData.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        planeGeo.vertexCount = static_cast<uint32_t>(resource->planeModel->vertexData.size() / 6);
        baseGeometries[RendererComponent::Plane] = planeGeo;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
}

glm::vec3 Scene::GetLightColor() const
{
    if (mainLight) 
    {
        LightComponent* light = mainLight->GetComponent<LightComponent>();
        if (light) return glm::vec3(light->color[0], light->color[1], light->color[2]);
    }
    return glm::vec3(1.0f);
}

glm::vec3 Scene::GetLightDirection() const
{
    if (mainLight) 
    {
        TransformComponent* transform = mainLight->GetComponent<TransformComponent>();
        if (transform) return transform->forward;
    }
    return glm::normalize(glm::vec3(-1, -2, -1));
}

float Scene::GetLightIntensity() const
{
    if (mainLight) 
    {
        LightComponent* light = mainLight->GetComponent<LightComponent>();
        if (light) return light->intensity;
    }
    return 1.0f;
}

void Scene::SaveScene(const std::string& filepath)
{
    std::cout << "Scene saved to: " << filepath << std::endl;
}

void Scene::LoadScene(const std::string& filepath)
{
    std::cout << "Scene loaded from: " << filepath << std::endl;
}