#include "Scene.h"
#include "../../Engine/Resources/Resource.h"
#include "PathUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <filesystem>

Scene* g_currentScene = nullptr;
std::uint32_t g_sceneLoadingVersion = 0;

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

    rootGameObject = new GameObject(name);
    rootGameObject->name = name;

    geometryBatches[RendererComponent::Cube] = new GeometryInstances(RendererComponent::Cube);
    geometryBatches[RendererComponent::Sphere] = new GeometryInstances(RendererComponent::Sphere);
}

Scene::~Scene()
{
    DestroyAllObjects();

    // Release all GPU resources through the renderer (sole owner). The renderer
    // is guaranteed alive here: Engine deletes the Scene before resetting it.
    for (auto& pair : geometryBatches)
    {
        if (renderer) { renderer->DestroyStorageBuffer(pair.second->modelSBO);
                        renderer->DestroyStorageBuffer(pair.second->colorSBO); }
        delete pair.second;
    }
    for (auto& pair : baseGeometries)
        if (renderer) renderer->DestroyMesh(pair.second.mesh);

    for (auto& pair : customBatches)
    {
        if (renderer) { renderer->DestroyStorageBuffer(pair.second->modelSBO);
                        renderer->DestroyStorageBuffer(pair.second->colorSBO); }
        delete pair.second;
    }
    for (auto& pair : customGeometries)
        if (renderer) renderer->DestroyMesh(pair.second.mesh);
}

// Single source of truth for tearing down the object graph.
//
// Ownership model: `rootGameObject` owns the entire object tree (each
// GameObject owns its children and recursively deletes them). `gameObjects`
// is a NON-OWNING flattened view mirroring `rootGameObject->children`; it
// must never be deleted (doing so would double-free nodes).
void Scene::DestroyAllObjects()
{
    delete rootGameObject;       // recursively deletes the whole tree
    // Recreate a fresh root, mirroring the scene name (Unity-style).
    rootGameObject = new GameObject(name);
    rootGameObject->name = name;

    gameObjects.clear();
    mainLight = nullptr;
}

void Scene::ClearScene()
{
    DestroyAllObjects();
}

void Scene::UnregisterSubtree(GameObject* obj)
{
    if (!obj) return;

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
    for (auto& pair : customBatches)
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

    findLight(rootGameObject);

    std::function<void(GameObject*)> collect = [&](GameObject* obj)
        {
            if (!obj->enabled) return;

            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();

            if (renderer && renderer->enabled && transform && transform->enabled)
            {
                GeometryInstances* batch = nullptr;
                if (!renderer->meshPath.empty())
                {
                    // Custom mesh: lazily build its geometry/batch, then route to it.
                    EnsureCustomGeometry(renderer->meshPath);
                    auto it = customBatches.find(renderer->meshPath);
                    if (it != customBatches.end()) batch = it->second;
                }
                else
                {
                    auto it = geometryBatches.find(renderer->type);
                    if (it != geometryBatches.end()) batch = it->second;
                }

                if (batch)
                {
                    batch->modelMatrices.push_back(transform->GetWorldModel());
                    batch->instanceColors.push_back(glm::vec4(renderer->color));
                    batch->instanceCount++;
                }
            }

            for (auto child : obj->children) collect(child);
        };

    collect(rootGameObject);
}

// Upload one batch's per-instance model/color arrays into its storage buffers.
static void UploadBatch(Ditto::IRenderer* r, GeometryInstances* batch)
{
    if (!r || batch->instanceCount == 0) return;

    if (!batch->modelSBO) batch->modelSBO = r->CreateStorageBuffer(0, /*dynamic=*/true);
    r->UpdateStorageBuffer(batch->modelSBO, batch->modelMatrices.data(),
        batch->instanceCount * sizeof(glm::mat4));

    if (!batch->colorSBO) batch->colorSBO = r->CreateStorageBuffer(0, /*dynamic=*/true);
    r->UpdateStorageBuffer(batch->colorSBO, batch->instanceColors.data(),
        batch->instanceCount * sizeof(glm::vec4));
}

// Issue one instanced draw call for a batch against its geometry.
static void DrawBatch(Ditto::IRenderer* r, const BaseGeometry& geometry, GeometryInstances* batch)
{
    if (!r || batch->instanceCount == 0 || !geometry.mesh) return;
    r->BindStorageBuffer(0, batch->modelSBO);   // ModelMatrices (binding 0)
    r->BindStorageBuffer(1, batch->colorSBO);   // InstanceColors (binding 1)
    r->DrawInstanced(geometry.mesh, static_cast<int>(batch->instanceCount));
}

void Scene::UpdateSSBOs()
{
    for (auto& pair : geometryBatches) UploadBatch(renderer, pair.second);
    for (auto& pair : customBatches)   UploadBatch(renderer, pair.second);
}

void Scene::Render(Ditto::PipelineHandle pipeline, const glm::mat4& view, const glm::mat4& projection,
    const glm::vec3& viewPos, int viewportWidth, int viewportHeight)
{
    if (!renderer) return;

    CollectRenderData();
    UpdateSSBOs();

    renderer->BindPipeline(pipeline);

    Ditto::FrameUniforms fu;
    fu.view = view;
    fu.projection = projection;
    fu.viewPos = viewPos;
    fu.lightColor = GetLightColor();
    fu.lightDir = GetLightDirection();
    fu.lightIntensity = GetLightIntensity();
    renderer->SetFrameUniforms(fu);

    // Built-in Cube/Sphere batches.
    for (auto& pair : geometryBatches)
    {
        auto geoIt = baseGeometries.find(pair.second->type);
        if (geoIt == baseGeometries.end()) continue;
        DrawBatch(renderer, geoIt->second, pair.second);
    }

    // Custom-mesh batches (keyed by meshPath).
    for (auto& pair : customBatches)
    {
        auto geoIt = customGeometries.find(pair.first);
        if (geoIt == customGeometries.end()) continue;
        DrawBatch(renderer, geoIt->second, pair.second);
    }
}

void Scene::InitializeBaseGeometries(Resource* _resource, Ditto::IRenderer* rhi)
{
    this->resource = _resource;
    this->renderer = rhi;

    // Interleaved layout shared by all base/custom models: pos(vec3)+normal(vec3).
    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3} };

    if (renderer && resource->cubeModel && !resource->cubeModel->vertexData.empty())
    {
        const auto& m = *resource->cubeModel;
        baseGeometries[RendererComponent::Cube] =
            BaseGeometry{ renderer->CreateMesh(m.vertexData.data(), m.vertexData.size(), 6, attribs,
                                               m.indices.data(), m.indices.size()) };
    }

    if (renderer && resource->sphereModel && !resource->sphereModel->vertexData.empty())
    {
        const auto& m = *resource->sphereModel;
        baseGeometries[RendererComponent::Sphere] =
            BaseGeometry{ renderer->CreateMesh(m.vertexData.data(), m.vertexData.size(), 6, attribs,
                                               m.indices.data(), m.indices.size()) };
    }
}

void Scene::EnsureCustomGeometry(const std::string& meshPath)
{
    if (meshPath.empty()) return;
    if (customGeometries.find(meshPath) != customGeometries.end()) return; // already built (or cached as failed)

    // Resolve the path: accept an existing path as-is, else anchor it to the
    // executable/project asset location like the built-in models.
    std::string resolved = meshPath;
    if (!std::filesystem::exists(resolved))
        resolved = PathUtils::ResolveAsset(meshPath).string();

    ModelData model(resolved);
    if (!renderer || model.vertexData.empty())
    {
        if (model.vertexData.empty())
            std::cerr << "[Scene] Custom mesh has no geometry: " << resolved << std::endl;
        // Cache empties so we don't re-attempt the load every frame.
        customGeometries[meshPath] = BaseGeometry{};
        customBatches[meshPath] = new GeometryInstances(RendererComponent::Cube);
        return;
    }

    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3} };
    BaseGeometry geo{ renderer->CreateMesh(model.vertexData.data(), model.vertexData.size(), 6, attribs,
                                           model.indices.data(), model.indices.size()) };

    customGeometries[meshPath] = geo;
    // `type` is unused for custom batches (geometry comes from customGeometries).
    customBatches[meshPath] = new GeometryInstances(RendererComponent::Cube);
    std::cout << "[Scene] Loaded custom mesh: " << resolved
              << " (" << model.vertexData.size() / 6 << " verts)" << std::endl;
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

    auto getMeshData = [&](RendererComponent::Type type) -> MeshData* {
        if (!resource) return nullptr;
        if (type == RendererComponent::Cube) return resource->cubeMesh;
        if (type == RendererComponent::Sphere) return resource->sphereMesh;
        return nullptr;
    };

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

        for (size_t i = 0; i < mesh->indices.size(); i += 3)
        {
            if (i + 2 >= mesh->indices.size()) break;

            unsigned int idx0 = mesh->indices[i];
            unsigned int idx1 = mesh->indices[i + 1];
            unsigned int idx2 = mesh->indices[i + 2];

            if (idx0 >= mesh->vertices.size() || idx1 >= mesh->vertices.size() || idx2 >= mesh->vertices.size())
                continue;

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

    // Always traverse from rootGameObject (single-ownership model).
    std::cout << "[Raycast] Traversing rootGameObject hierarchy: " << rootGameObject->name << std::endl;
    std::function<void(GameObject*)> traverseHierarchy = [&](GameObject* obj)
    {
        if (!obj) return;
        checkObject(obj);
        for (auto* child : obj->children)
            traverseHierarchy(child);
    };
    traverseHierarchy(rootGameObject);

    std::cout << "[Raycast] Checked " << checkedCount << " objects, closest: " << (closest ? closest->name : "none") << std::endl;

    return closest;
}

struct SceneHeader
{
    char magic[4];
    uint32_t version;
    uint32_t gameObjectCount;
    uint64_t fileSize;
};
// v1: original format. v2: RendererComponent::meshPath (custom mesh override).
const uint32_t SCENE_VERSION = 2;
const char SCENE_MAGIC[4] = { 'S', 'C', 'N', '\0' };

void Scene::WriteToStream(std::ostream& file)
{
    SceneHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SCENE_MAGIC, 4);
    header.version = SCENE_VERSION;
    header.fileSize = 0;
    header.gameObjectCount = 1;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    uint32_t nameLength = static_cast<uint32_t>(name.length());
    file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
    file.write(name.c_str(), nameLength);

    rootGameObject->Serialize(file);

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
        if (header.version == 0 || header.version > SCENE_VERSION)
        {
            std::cerr << "[Scene::LoadScene] Unsupported scene version: " << header.version
                << " (this build reads up to: " << SCENE_VERSION << ")" << std::endl;
            return false;
        }
        // Expose the loading version so component deserializers can read
        // version-gated fields while still accepting older files.
        g_sceneLoadingVersion = header.version;

        uint32_t nameLength = 0;
        file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        std::vector<char> nameBuffer(nameLength + 1, '\0');
        file.read(nameBuffer.data(), nameLength);
        name = std::string(nameBuffer.data());
        std::cout << "[Scene::LoadScene] Scene name: " << name << std::endl;

        ClearScene();

        std::cout << "[Scene::LoadScene] Deserializing rootGameObject..." << std::endl;
        rootGameObject->Deserialize(file);
        rootGameObject->name = name;
        std::cout << "[Scene::LoadScene] rootGameObject deserialized: " << rootGameObject->name
                  << ", children: " << rootGameObject->children.size() << std::endl;

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

        // Find main light
        mainLight = nullptr;
        std::function<void(GameObject*)> findLight = [&](GameObject* obj) {
            if (!mainLight && obj->GetComponent<LightComponent>())
                mainLight = obj;
            for (auto child : obj->children)
                findLight(child);
        };
        findLight(rootGameObject);

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
        // Single-ownership: always start from rootGameObject.
        reportRaycastObjects(rootGameObject);
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