#include "Scene.h"
#include "../../Engine/Resources/Resource.h"
#include "../../Engine/Graphics/Shader.h"
#include <iostream>
#include <fstream>
#include <functional>

// 全局当前场景指针
Scene* g_currentScene = nullptr;

// 辅助函数：从文件读取字符串
static std::string ReadString(std::ifstream& file)
{
    uint32_t length = 0;
    file.read(reinterpret_cast<char*>(&length), sizeof(length));
    std::vector<char> buffer(length + 1, '\0');
    file.read(buffer.data(), length);
    return std::string(buffer.data());
}

Scene::Scene()
{
    name = "Default";
    g_currentScene = this;  // 设置全局指针

    geometryBatches[RendererComponent::Cube] = new GeometryInstances(RendererComponent::Cube);
    geometryBatches[RendererComponent::Sphere] = new GeometryInstances(RendererComponent::Sphere);
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

void Scene::ClearScene()
{
    // 如果有 rootGameObject，先清空它的 children 指针（避免重复删除）
    // 因为 children 可能也在 gameObjects 列表中
    if (rootGameObject)
    {
        // 清空 children 指针，不删除对象（它们会在下面被删除）
        rootGameObject->children.clear();
        delete rootGameObject;
        rootGameObject = nullptr;
    }
    
    // 删除 gameObjects 列表中的所有对象
    for (GameObject* obj : gameObjects) delete obj;
    gameObjects.clear();
    
    mainLight = nullptr;
}

void Scene::CollectRenderData()
{
    for (auto& pair : geometryBatches)
    {
        pair.second->modelMatrices.clear();
        pair.second->instanceColors.clear();
        pair.second->instanceCount = 0;
        pair.second->dirty = true;
    }

    mainLight = nullptr;
    std::function<void(GameObject*)> findLight = [&](GameObject* obj)
        {
            if (!obj->enabled) return;
            if (!mainLight && obj->GetComponent<LightComponent>()) mainLight = obj;
            for (auto child : obj->children) findLight(child);
        };
    
    // 从 rootGameObject 或 gameObjects 列表开始遍历
    if (rootGameObject)
    {
        findLight(rootGameObject);
    }
    else
    {
        for (auto root : gameObjects) findLight(root);
    }

    std::function<void(GameObject*)> collect = [&](GameObject* obj)
        {
            if (!obj->enabled) return;

            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();

            if (renderer && renderer->enabled && transform && transform->enabled)
            {
                auto it = geometryBatches.find(renderer->type);
                if (it != geometryBatches.end())
                {
                    GeometryInstances* batch = it->second;
                    batch->modelMatrices.push_back(transform->GetWorldModel());
                    batch->instanceColors.push_back(glm::vec4(renderer->color));
                    batch->instanceCount++;
                    batch->dirty = true;
                }
            }

            for (auto child : obj->children) collect(child);
        };

    // 从 rootGameObject 或 gameObjects 列表开始遍历
    if (rootGameObject)
    {
        collect(rootGameObject);
    }
    else
    {
        for (auto root : gameObjects)
            collect(root);
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
        glBufferData(GL_SHADER_STORAGE_BUFFER, batch->instanceCount * sizeof(glm::mat4),
            batch->modelMatrices.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, batch->modelSSBO);

        if (batch->colorSSBO == 0) glGenBuffers(1, &batch->colorSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch->colorSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, batch->instanceCount * sizeof(glm::vec4),
            batch->instanceColors.data(), GL_DYNAMIC_DRAW);
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

        if (geometry.indexCount > 0)
            glDrawElementsInstanced(GL_TRIANGLES, geometry.indexCount, GL_UNSIGNED_INT, 0, batch->instanceCount);
        else
            glDrawArraysInstanced(GL_TRIANGLES, 0, geometry.vertexCount, batch->instanceCount);

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
}

glm::vec3 Scene::GetLightColor() const
{
    if (mainLight)
    {
        LightComponent* light = mainLight->GetComponent<LightComponent>();
        if (light) return glm::vec3(light->color);
    }
    return glm::vec3(1.0f);
}

glm::vec3 Scene::GetLightDirection() const
{
    if (mainLight)
    {
        TransformComponent* transform = mainLight->GetComponent<TransformComponent>();
        if (transform)
        {
            glm::mat4 world = transform->GetWorldModel();
            glm::vec3 worldForward = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
            return worldForward;
        }
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

// --- ���л�ͷ ---
struct SceneHeader
{
    char magic[4];
    uint32_t version;
    uint32_t gameObjectCount;
    uint64_t fileSize;
};
const uint32_t SCENE_VERSION = 1;
const char SCENE_MAGIC[4] = { 'S', 'C', 'N', '\0' };

bool Scene::SaveScene(const std::string& filepath)
{
    std::cout << "[Scene::SaveScene] Starting save to: " << filepath << std::endl;
    
    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        std::cerr << "[Scene::SaveScene] Failed to open file for writing: " << filepath << std::endl;
        return false;
    }

    try
    {
        SceneHeader header;
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, SCENE_MAGIC, 4);
        header.version = SCENE_VERSION;
        header.fileSize = 0;

        // 计算对象数量：如果存在 rootGameObject，只算1个（它会序列化自己和所有children）
        // 否则使用 gameObjects 列表的数量
        if (rootGameObject)
        {
            header.gameObjectCount = 1;  // 只有 rootGameObject，它会包含所有子孙
            std::cout << "[Scene::SaveScene] Using rootGameObject, count = 1" << std::endl;
            std::cout << "[Scene::SaveScene] rootGameObject name: " << rootGameObject->name << std::endl;
            std::cout << "[Scene::SaveScene] rootGameObject children: " << rootGameObject->children.size() << std::endl;
        }
        else
        {
            header.gameObjectCount = static_cast<uint32_t>(gameObjects.size());
            std::cout << "[Scene::SaveScene] Using gameObjects list, count = " << gameObjects.size() << std::endl;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        uint32_t nameLength = static_cast<uint32_t>(name.length());
        file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
        file.write(name.c_str(), nameLength);
        std::cout << "[Scene::SaveScene] Scene name: " << name << std::endl;

        // 序列化对象：如果存在 rootGameObject，从它开始，否则使用 gameObjects
        if (rootGameObject)
        {
            std::cout << "[Scene::SaveScene] Serializing rootGameObject..." << std::endl;
            rootGameObject->Serialize(file);
        }
        else
        {
            std::cout << "[Scene::SaveScene] Serializing " << gameObjects.size() << " gameObjects..." << std::endl;
            for (GameObject* obj : gameObjects)
                obj->Serialize(file);
        }

        std::streampos endPos = file.tellp();
        header.fileSize = static_cast<uint64_t>(endPos);
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        file.close();
        std::cout << "[Scene::SaveScene] Save completed. File size: " << header.fileSize << " bytes" << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Scene::SaveScene] Error saving scene: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

bool Scene::LoadScene(const std::string& filepath)
{
    std::cout << "[Scene::LoadScene] Starting load from: " << filepath << std::endl;
    
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "[Scene::LoadScene] Failed to open file for reading: " << filepath << std::endl;
        return false;
    }

    try
    {
        SceneHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        std::cout << "[Scene::LoadScene] Header: magic=" << header.magic[0] << header.magic[1] << header.magic[2] 
                  << ", version=" << header.version << ", gameObjectCount=" << header.gameObjectCount 
                  << ", fileSize=" << header.fileSize << std::endl;
        
        if (memcmp(header.magic, SCENE_MAGIC, 4) != 0)
        {
            std::cerr << "[Scene::LoadScene] Invalid scene file: wrong magic number" << std::endl;
            return false;
        }
        if (header.version != SCENE_VERSION)
        {
            std::cerr << "[Scene::LoadScene] Unsupported scene version: " << header.version
                << " (expected: " << SCENE_VERSION << ")" << std::endl;
            return false;
        }

        uint32_t nameLength = 0;
        file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        std::vector<char> nameBuffer(nameLength + 1, '\0');
        file.read(nameBuffer.data(), nameLength);
        name = std::string(nameBuffer.data());
        std::cout << "[Scene::LoadScene] Scene name: " << name << std::endl;

        ClearScene();

        // 检查第一个对象的名称，如果是场景名称，则它是 rootGameObject
        // 先预读第一个对象的名称
        std::streampos firstObjPos = file.tellg();
        bool firstEnabled = false, firstLocked = false;
        file.read(reinterpret_cast<char*>(&firstEnabled), sizeof(firstEnabled));
        file.read(reinterpret_cast<char*>(&firstLocked), sizeof(firstLocked));
        std::string firstObjName = ReadString(file);
        file.seekg(firstObjPos);  // 回到第一个对象的位置

        std::cout << "[Scene::LoadScene] First object name: " << firstObjName << std::endl;

        // 如果第一个对象的名称与场景名称相同，说明它是 rootGameObject
        bool hasRootGameObject = (firstObjName == name);
        std::cout << "[Scene::LoadScene] hasRootGameObject: " << (hasRootGameObject ? "true" : "false") << std::endl;

        if (hasRootGameObject && header.gameObjectCount > 0)
        {
            std::cout << "[Scene::LoadScene] Deserializing rootGameObject..." << std::endl;
            // 从 rootGameObject 开始反序列化
            rootGameObject = new GameObject(false);
            rootGameObject->Deserialize(file);
            std::cout << "[Scene::LoadScene] rootGameObject deserialized: " << rootGameObject->name 
                      << ", children: " << rootGameObject->children.size() << std::endl;
            
            // 将所有无父物体的对象添加到 gameObjects 列表
            std::function<void(GameObject*)> collectRootObjects = [&](GameObject* obj) {
                for (auto child : obj->children)
                {
                    if (child->children.empty())
                    {
                        // 叶子节点
                        gameObjects.push_back(child);
                    }
                    else
                    {
                        // 非叶子节点，继续遍历
                        collectRootObjects(child);
                        gameObjects.push_back(child);
                    }
                }
            };
            collectRootObjects(rootGameObject);
            std::cout << "[Scene::LoadScene] Collected " << gameObjects.size() << " objects to gameObjects list" << std::endl;
            
            // 跳过剩余的已反序列化的对象（因为 rootGameObject 已经包含了所有子孙）
            // 实际上不需要跳过，因为 rootGameObject->Deserialize 已经读取了所有数据
        }
        else
        {
            std::cout << "[Scene::LoadScene] Loading " << header.gameObjectCount << " gameObjects (old format)..." << std::endl;
            // 旧格式：直接读取所有对象到 gameObjects
            gameObjects.reserve(header.gameObjectCount);
            for (uint32_t i = 0; i < header.gameObjectCount; i++)
            {
                GameObject* newObj = new GameObject(false);
                newObj->Deserialize(file);
                gameObjects.push_back(newObj);
            }
            
            // 创建 rootGameObject 并重新组织父子关系
            rootGameObject = new GameObject(name);
            for (GameObject* obj : gameObjects)
            {
                if (!obj->parent)
                {
                    rootGameObject->children.push_back(obj);
                    obj->parent = rootGameObject;
                }
            }
        }

        // 查找主光源
        mainLight = nullptr;
        std::function<void(GameObject*)> findLight = [&](GameObject* obj) {
            if (!mainLight && obj->GetComponent<LightComponent>())
                mainLight = obj;
            for (auto child : obj->children)
                findLight(child);
        };
        if (rootGameObject)
            findLight(rootGameObject);
        else
        {
            for (GameObject* obj : gameObjects)
                if (obj->GetComponent<LightComponent>())
                {
                    mainLight = obj;
                    break;
                }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading scene: " << e.what() << std::endl;
        ClearScene();
        name = "Load Failed";
        file.close();
        return false;
    }
}