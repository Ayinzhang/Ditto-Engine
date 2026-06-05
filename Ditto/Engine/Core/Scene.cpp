#include "Scene.h"
#include "../../Engine/Resources/Resource.h"
#include "../../Engine/Graphics/Shader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>

// Global current scene pointer
Scene* g_currentScene = nullptr;

// Helper function: read a length-prefixed string from any input stream
// (file OR in-memory snapshot buffer).
static std::string ReadString(std::istream& file)
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
    g_currentScene = this;

    geometryBatches[RendererComponent::Cube] = new GeometryInstances(RendererComponent::Cube);
    geometryBatches[RendererComponent::Sphere] = new GeometryInstances(RendererComponent::Sphere);
}

Scene::~Scene()
{
    DestroyAllObjects();
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

// Single source of truth for tearing down the object graph.
//
// Ownership model: a GameObject owns its children (its destructor deletes them
// recursively). Therefore only the TOP of the hierarchy may be deleted here:
//   - if rootGameObject exists, it owns the entire tree -> delete it alone.
//     `gameObjects` is then a NON-OWNING flattened view and must NOT be deleted
//     (doing so double-frees nodes already freed via the tree).
//   - otherwise `gameObjects` holds the top-level roots -> delete each.
// This replaces the previous split logic that relied on children.clear() and
// could still double-free grandchildren.
void Scene::DestroyAllObjects()
{
    if (rootGameObject)
    {
        delete rootGameObject;   // recursively deletes the whole tree
        rootGameObject = nullptr;
    }
    else
    {
        for (GameObject* obj : gameObjects) delete obj;
    }
    gameObjects.clear();         // clear the (now dangling) observer list
    mainLight = nullptr;         // non-owning pointer into the freed tree
}

void Scene::ClearScene()
{
    DestroyAllObjects();
}

void Scene::UnregisterSubtree(GameObject* obj)
{
    if (!obj) return;

    // Depth-first: collect the subtree, then erase each node from the
    // non-owning observer list. Also clear mainLight if it points inside.
    std::function<void(GameObject*)> visit = [&](GameObject* node)
    {
        if (!node) return;
        auto it = std::find(gameObjects.begin(), gameObjects.end(), node);
        if (it != gameObjects.end()) gameObjects.erase(it);
        if (mainLight == node) mainLight = nullptr;
        for (GameObject* child : node->children) visit(child);
    };
    visit(obj);
}

void Scene::CollectRenderData()
{
    for (auto& pair : geometryBatches)
    {
        pair.second->modelMatrices.clear();
        pair.second->instanceColors.clear();
        pair.second->instanceCount = 0;
    }

    mainLight = nullptr;
    std::function<void(GameObject*)> findLight = [&](GameObject* obj)
        {
            if (!obj->enabled) return;
            if (!mainLight && obj->GetComponent<LightComponent>()) mainLight = obj;
            for (auto child : obj->children) findLight(child);
        };
    
    // Traverse from rootGameObject or gameObjects list
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
                }
            }

            for (auto child : obj->children) collect(child);
        };

    // Traverse from rootGameObject or gameObjects list
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
        if (batch->instanceCount == 0) continue;

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

void Scene::InitializeBaseGeometries(Resource* _resource)
{
    this->resource = _resource;
    
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

GameObject* Scene::RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight)
{
    float ndcX = (2.0f * mousePos.x / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * mousePos.y / viewportHeight);
    glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::mat4 invProj = glm::inverse(projection);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 rayOrigin = glm::vec3(invView[3]);
    glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));

    std::cout << "[Raycast] MousePos: (" << mousePos.x << ", " << mousePos.y << ")" << std::endl;
    std::cout << "[Raycast] RayOrigin: (" << rayOrigin.x << ", " << rayOrigin.y << ", " << rayOrigin.z << ")" << std::endl;
    std::cout << "[Raycast] RayDir: (" << rayDir.x << ", " << rayDir.y << ", " << rayDir.z << ")" << std::endl;

    GameObject* closest = nullptr;
    float closestDist = FLT_MAX;
    int checkedCount = 0;

    // Get mesh data based on renderer type
    auto getMeshData = [&](RendererComponent::Type type) -> MeshData* {
        if (!resource) return nullptr;
        if (type == RendererComponent::Cube) return resource->cubeMesh;
        if (type == RendererComponent::Sphere) return resource->sphereMesh;
        return nullptr;
    };

    // Ray-triangle intersection (Möller–Trumbore algorithm)
    auto rayTriangleIntersect = [&](const glm::vec3& orig, const glm::vec3& dir,
                                    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                    float& t) -> bool {
        const float EPSILON = 0.000001f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(dir, edge2);
        float a = glm::dot(edge1, h);
        if (a > -EPSILON && a < EPSILON) return false;
        float f = 1.0f / a;
        glm::vec3 s = orig - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) return false;
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(dir, q);
        if (v < 0.0f || u + v > 1.0f) return false;
        t = f * glm::dot(edge2, q);
        return t > EPSILON;
    };

    std::function<void(GameObject*)> checkObject = [&](GameObject* obj)
    {
        if (!obj->enabled) return;
        RendererComponent* renderer = obj->GetComponent<RendererComponent>();
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (!renderer || !transform || !renderer->enabled || !transform->enabled) return;

        checkedCount++;
        glm::mat4 worldMat = transform->GetWorldModel();
        MeshData* mesh = getMeshData(renderer->type);
        
        if (!mesh)
        {
            std::cout << "[Raycast] Object '" << obj->name << "' has no mesh data" << std::endl;
            return;
        }

        std::cout << "[Raycast] Checking object: " << obj->name << " (mesh vertices: " << mesh->vertices.size() << ", indices: " << mesh->indices.size() << ")" << std::endl;

        float tMin = FLT_MAX;
        bool hit = false;

        // Check all triangles in the mesh
        for (size_t i = 0; i < mesh->indices.size(); i += 3)
        {
            if (i + 2 >= mesh->indices.size()) break;

            unsigned int idx0 = mesh->indices[i];
            unsigned int idx1 = mesh->indices[i + 1];
            unsigned int idx2 = mesh->indices[i + 2];

            if (idx0 >= mesh->vertices.size() || idx1 >= mesh->vertices.size() || idx2 >= mesh->vertices.size())
                continue;

            // Transform vertices to world space
            glm::vec3 v0 = glm::vec3(worldMat * glm::vec4(mesh->vertices[idx0], 1.0f));
            glm::vec3 v1 = glm::vec3(worldMat * glm::vec4(mesh->vertices[idx1], 1.0f));
            glm::vec3 v2 = glm::vec3(worldMat * glm::vec4(mesh->vertices[idx2], 1.0f));

            float t;
            if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t))
            {
                if (t < tMin)
                {
                    tMin = t;
                    hit = true;
                }
            }
        }

        if (hit)
        {
            std::cout << "[Raycast] Hit object: " << obj->name << " at distance " << tMin << std::endl;
            if (tMin < closestDist)
            {
                closestDist = tMin;
                closest = obj;
            }
        }
    };

    // Traverse all objects - if rootGameObject exists, traverse its hierarchy, otherwise use gameObjects list
    if (rootGameObject)
    {
        std::cout << "[Raycast] Traversing rootGameObject hierarchy: " << rootGameObject->name << std::endl;
        std::function<void(GameObject*)> traverseHierarchy = [&](GameObject* obj)
        {
            if (!obj) return;
            checkObject(obj);
            for (auto* child : obj->children)
                traverseHierarchy(child);
        };
        traverseHierarchy(rootGameObject);
    }
    else
    {
        std::cout << "[Raycast] Traversing gameObjects list, count: " << gameObjects.size() << std::endl;
        for (auto obj : gameObjects) checkObject(obj);
    }

    std::cout << "[Raycast] Checked " << checkedCount << " objects, closest: " << (closest ? closest->name : "none") << std::endl;

    return closest;
}

// --- Scene file header struct definition --- 
struct SceneHeader
{
    char magic[4];
    uint32_t version;
    uint32_t gameObjectCount;
    uint64_t fileSize;
};
const uint32_t SCENE_VERSION = 1;
const char SCENE_MAGIC[4] = { 'S', 'C', 'N', '\0' };

// Core serialization, stream-based so it works with both files (SaveScene) and
// in-memory buffers (CaptureSnapshot). Uses seekp to backfill the header size,
// which both std::ofstream and std::ostringstream support.
void Scene::WriteToStream(std::ostream& file)
{
    SceneHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SCENE_MAGIC, 4);
    header.version = SCENE_VERSION;
    header.fileSize = 0;

    // If rootGameObject exists it serializes the whole tree (count = 1);
    // otherwise the flat gameObjects list owns the top-level objects.
    if (rootGameObject)
        header.gameObjectCount = 1;
    else
        header.gameObjectCount = static_cast<uint32_t>(gameObjects.size());

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    uint32_t nameLength = static_cast<uint32_t>(name.length());
    file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
    file.write(name.c_str(), nameLength);

    if (rootGameObject)
        rootGameObject->Serialize(file);
    else
        for (GameObject* obj : gameObjects)
            obj->Serialize(file);

    // Backfill total size into the header.
    std::streampos endPos = file.tellp();
    header.fileSize = static_cast<uint64_t>(endPos);
    file.seekp(0);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

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
        WriteToStream(file);
        file.close();
        std::cout << "[Scene::SaveScene] Save completed." << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Scene::SaveScene] Error saving scene: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

std::string Scene::CaptureSnapshot()
{
    std::ostringstream oss(std::ios::binary);
    WriteToStream(oss);
    return oss.str();
}

bool Scene::RestoreSnapshot(const std::string& data)
{
    if (data.empty()) return false;
    std::istringstream iss(data, std::ios::binary);
    return ReadFromStream(iss);   // ReadFromStream calls ClearScene() internally
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
    return ReadFromStream(file);
}

// Core deserialization, stream-based (file OR in-memory snapshot). Calls
// ClearScene() before rebuilding; on failure leaves the scene cleared.
bool Scene::ReadFromStream(std::istream& file)
{
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

        // Check first object's name - if it matches scene name, it's rootGameObject
        // Pre-read first object's name
        std::streampos firstObjPos = file.tellg();
        bool firstEnabled = false, firstLocked = false;
        file.read(reinterpret_cast<char*>(&firstEnabled), sizeof(firstEnabled));
        file.read(reinterpret_cast<char*>(&firstLocked), sizeof(firstLocked));
        std::string firstObjName = ReadString(file);
        file.seekg(firstObjPos);

        std::cout << "[Scene::LoadScene] First object name: " << firstObjName << std::endl;

        // If first object's name matches scene name, it's rootGameObject
        bool hasRootGameObject = (firstObjName == name);
        std::cout << "[Scene::LoadScene] hasRootGameObject: " << (hasRootGameObject ? "true" : "false") << std::endl;

        if (hasRootGameObject && header.gameObjectCount > 0)
        {
            std::cout << "[Scene::LoadScene] Deserializing rootGameObject..." << std::endl;
            rootGameObject = new GameObject(false);
            rootGameObject->Deserialize(file);
            std::cout << "[Scene::LoadScene] rootGameObject deserialized: " << rootGameObject->name 
                      << ", children: " << rootGameObject->children.size() << std::endl;
            
            // Collect all parentless objects into gameObjects list
            std::function<void(GameObject*)> collectRootObjects = [&](GameObject* obj) {
                for (auto child : obj->children)
                {
                    if (child->children.empty())
                    {
                        // Leaf node
                        gameObjects.push_back(child);
                    }
                    else
                    {
                        // Non-leaf node, continue traversal
                        collectRootObjects(child);
                        gameObjects.push_back(child);
                    }
                }
            };
            collectRootObjects(rootGameObject);
            std::cout << "[Scene::LoadScene] Collected " << gameObjects.size() << " objects to gameObjects list" << std::endl;
        }
        else
        {
            std::cout << "[Scene::LoadScene] Loading " << header.gameObjectCount << " gameObjects (old format)..." << std::endl;
            // Old format: directly load all objects into gameObjects
            gameObjects.reserve(header.gameObjectCount);
            for (uint32_t i = 0; i < header.gameObjectCount; i++)
            {
                GameObject* newObj = new GameObject(false);
                newObj->Deserialize(file);
                gameObjects.push_back(newObj);
            }
            
            // Create rootGameObject and reorganize parent-child relationships
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

        // Find main light
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

        // Report raycast-capable objects
        std::cout << "[Scene::LoadScene] === Raycast-capable objects ===" << std::endl;
        int raycastObjCount = 0;
        std::function<void(GameObject*)> reportRaycastObjects = [&](GameObject* obj) {
            if (!obj) return;
            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();
            if (renderer && transform)
            {
                std::cout << "[Scene::LoadScene] Raycast object: " << obj->name 
                          << " (enabled=" << obj->enabled 
                          << ", renderer=" << (renderer->enabled ? "enabled" : "disabled")
                          << ", transform=" << (transform->enabled ? "enabled" : "disabled")
                          << ", type=" << (renderer->type == RendererComponent::Cube ? "Cube" : "Sphere")
                          << ", pos=[" << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << "])" 
                          << std::endl;
                raycastObjCount++;
            }
            for (auto child : obj->children)
                reportRaycastObjects(child);
        };
        if (rootGameObject)
            reportRaycastObjects(rootGameObject);
        else
            for (auto obj : gameObjects) reportRaycastObjects(obj);
        std::cout << "[Scene::LoadScene] Total raycast-capable objects: " << raycastObjCount << std::endl;

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading scene: " << e.what() << std::endl;
        ClearScene();
        name = "Load Failed";
        return false;   // istream& has no close(); LoadScene's ifstream closes via RAII
    }
}