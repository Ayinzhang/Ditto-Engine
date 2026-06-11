#include "Scene.h"
#include "../../Engine/Resources/Resource.h"
#include "PathUtils.h"
#include "Logger.h"
#include "../Graphics/Shaders/ShaderAsset.h"
#include "../../3rdParty/stb_image.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cmath>

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

    rootGameObject = std::make_unique<GameObject>(name);
    rootGameObject->name = name;

    geometryBatches[RendererComponent::Cube] = std::make_unique<GeometryInstances>(RendererComponent::Cube);
    geometryBatches[RendererComponent::Sphere] = std::make_unique<GeometryInstances>(RendererComponent::Sphere);
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
    }
    for (auto& pair : baseGeometries)
        if (renderer) renderer->DestroyMesh(pair.second.mesh);

    for (auto& pair : customBatches)
    {
        if (renderer) { renderer->DestroyStorageBuffer(pair.second->modelSBO);
                        renderer->DestroyStorageBuffer(pair.second->colorSBO); }
    }
    for (auto& pair : renderBatches)
    {
        if (renderer) { renderer->DestroyStorageBuffer(pair.second->modelSBO);
                        renderer->DestroyStorageBuffer(pair.second->colorSBO); }
    }
    for (auto& pair : customGeometries)
        if (renderer) renderer->DestroyMesh(pair.second.mesh);
    for (auto& pair : shaderPipelines)
        if (renderer) renderer->DestroyPipeline(pair.second);
    for (auto& pair : materialTextures)
        if (renderer) renderer->DestroyTexture(pair.second);
    if (renderer) renderer->DestroyTexture(whiteTexture);
}

// Single source of truth for tearing down the object graph.
//
// Ownership model: `rootGameObject` owns the entire object tree (each
// GameObject owns its children and recursively deletes them). `gameObjects`
// is a NON-OWNING flattened view mirroring `rootGameObject->children`; it
// must never be deleted (doing so would double-free nodes).
void Scene::DestroyAllObjects()
{
    // Recreate a fresh root, mirroring the scene name (Unity-style). The
    // assignment destroys the old tree (recursively) before taking ownership.
    rootGameObject = std::make_unique<GameObject>(name);
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
        for (const auto& child : node->children) visit(child.get());
    };
    visit(obj);
}

void Scene::CollectRenderData()
{
    for (auto& pair : renderBatches)
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
            for (const auto& child : obj->children) findLight(child.get());
        };

    findLight(rootGameObject.get());

    std::function<void(GameObject*)> collect = [&](GameObject* obj)
        {
            if (!obj->enabled) return;

            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();

            if (renderer && renderer->enabled && transform && transform->enabled)
            {
                std::string meshKey;
                if (renderer->UsesFileMesh())
                {
                    // Custom mesh: lazily build its geometry/batch, then route to it.
                    EnsureCustomGeometry(renderer->meshPath);
                    meshKey = "file:" + renderer->meshPath;
                }
                else
                {
                    meshKey = std::string("builtin:") + (renderer->type == RendererComponent::Sphere ? "sphere" : "cube");
                }

                std::string shaderName = renderer->shaderName.empty() ? RendererComponent::DefaultShaderName : renderer->shaderName;
                std::string texturePath = renderer->mainTexturePath;
                std::string colorKey = std::to_string(renderer->color.r) + "," + std::to_string(renderer->color.g) + "," +
                    std::to_string(renderer->color.b) + "," + std::to_string(renderer->color.a);
                std::string batchKey = shaderName + "|" + meshKey + "|" + texturePath + "|" + colorKey;
                GeometryInstances* batch = nullptr;
                auto batchIt = renderBatches.find(batchKey);
                if (batchIt == renderBatches.end())
                {
                    auto newBatch = std::make_unique<GeometryInstances>(renderer->type);
                    newBatch->meshPath = renderer->UsesFileMesh() ? renderer->meshPath : "";
                    newBatch->shaderName = shaderName;
                    newBatch->texturePath = texturePath;
                    batch = newBatch.get();
                    renderBatches[batchKey] = std::move(newBatch);
                }
                else
                {
                    batch = batchIt->second.get();
                }
                if (batch)
                {
                    batch->modelMatrices.push_back(transform->GetWorldModel());
                    batch->instanceColors.push_back(glm::vec4(renderer->color));
                    batch->instanceCount++;
                }
            }

            for (const auto& child : obj->children) collect(child.get());
        };

    collect(rootGameObject.get());
}

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

// User shaders never see instance IDs. The generated VSMain wrapper captures
// SV_InstanceID internally, so helpers like ObjectToWorld() can still index the
// instancing buffers while the user's vert(appdata) signature stays clean.
static void DrawBatch(Ditto::IRenderer* r, const BaseGeometry& geometry, GeometryInstances* batch)
{
    if (!r || batch->instanceCount == 0 || !geometry.mesh) return;
    r->BindStorageBuffer(0, batch->modelSBO);   // ModelMatrices (binding 0)
    r->BindStorageBuffer(1, batch->colorSBO);   // InstanceColors (binding 1)
    r->DrawInstanced(geometry.mesh, static_cast<int>(batch->instanceCount));
}

void Scene::UpdateSSBOs()
{
    for (auto& pair : renderBatches) UploadBatch(renderer, pair.second.get());
}

void Scene::Render(Ditto::PipelineHandle pipeline, const glm::mat4& view, const glm::mat4& projection,
    const glm::vec3& viewPos, int viewportWidth, int viewportHeight)
{
    if (!renderer) return;

    CollectRenderData();
    UpdateSSBOs();

    Ditto::FrameUniforms fu;
    fu.view = view;
    fu.projection = projection;
    fu.viewPos = viewPos;
    fu.lightColor = GetLightColor();
    fu.lightDir = GetLightDirection();
    fu.lightIntensity = GetLightIntensity();

    using Clock = std::chrono::steady_clock;
    static const auto startTime = Clock::now();
    static auto lastTime = startTime;
    const auto now = Clock::now();
    const float t = std::chrono::duration<float>(now - startTime).count();
    const float dt = std::max(0.000001f, std::chrono::duration<float>(now - lastTime).count());
    lastTime = now;
    fu.time = glm::vec4(t / 20.0f, t, t * 2.0f, t * 3.0f);
    fu.sinTime = glm::vec4(std::sin(t / 8.0f), std::sin(t / 4.0f), std::sin(t / 2.0f), std::sin(t));
    fu.cosTime = glm::vec4(std::cos(t / 8.0f), std::cos(t / 4.0f), std::cos(t / 2.0f), std::cos(t));
    fu.deltaTime = glm::vec4(dt, 1.0f / dt, dt, 1.0f / dt);
    const float w = static_cast<float>(std::max(1, viewportWidth));
    const float h = static_cast<float>(std::max(1, viewportHeight));
    fu.screenParams = glm::vec4(w, h, 1.0f + 1.0f / w, 1.0f + 1.0f / h);
    for (auto& pair : renderBatches)
    {
        GeometryInstances* batch = pair.second.get();
        if (!batch || batch->instanceCount == 0) continue;

        Ditto::PipelineHandle shaderPipeline = GetOrCreateShaderPipeline(batch->shaderName, pipeline);
        renderer->BindPipeline(shaderPipeline ? shaderPipeline : pipeline);
        renderer->SetFrameUniforms(fu);
        renderer->BindTexture(2, GetOrCreateMaterialTexture(batch->texturePath));

        if (!batch->meshPath.empty())
        {
            auto geoIt = customGeometries.find(batch->meshPath);
            if (geoIt == customGeometries.end()) continue;
            DrawBatch(renderer, geoIt->second, batch);
        }
        else
        {
            auto geoIt = baseGeometries.find(batch->type);
            if (geoIt == baseGeometries.end()) continue;
            DrawBatch(renderer, geoIt->second, batch);
        }
    }
}

Ditto::PipelineHandle Scene::GetOrCreateShaderPipeline(const std::string& shaderName, Ditto::PipelineHandle fallback)
{
    std::string key = shaderName.empty() ? RendererComponent::DefaultShaderName : shaderName;
    if (key == RendererComponent::DefaultShaderName && fallback)
        return fallback;

    auto it = shaderPipelines.find(key);
    if (it != shaderPipelines.end())
        return it->second;

    Ditto::ShaderAsset shader = Ditto::LoadShaderAsset(key);
    if (!shader.ok)
    {
        DITTO_LOG_ERROR_STREAM("[Scene] Failed to load shader: " << key << " - " << shader.error);
        return fallback;
    }

    Ditto::PipelineHandle created = renderer ? renderer->CreatePipeline(shader.engineHLSL) : Ditto::PipelineHandle{};
    if (!created)
    {
        DITTO_LOG_ERROR_STREAM("[Scene] Failed to create shader pipeline: " << key);
        return fallback;
    }
    shaderPipelines[key] = created;
    return created;
}

Ditto::TextureHandle Scene::GetOrCreateMaterialTexture(const std::string& texturePath)
{
    if (!renderer) return {};
    if (texturePath.empty())
    {
        if (!whiteTexture)
        {
            const unsigned char white[4] = { 255, 255, 255, 255 };
            whiteTexture = renderer->CreateTexture(white, 1, 1, 4);
        }
        return whiteTexture;
    }

    auto it = materialTextures.find(texturePath);
    if (it != materialTextures.end())
        return it->second ? it->second : GetOrCreateMaterialTexture("");

    std::filesystem::path resolved = texturePath;
    if (!std::filesystem::exists(resolved))
        resolved = PathUtils::ResolveAsset(texturePath);

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* pixels = stbi_load(resolved.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
        DITTO_LOG_ERROR_STREAM("[Scene] Failed to load texture: " << resolved.string());
        if (pixels) stbi_image_free(pixels);
        materialTextures[texturePath] = {};
        return GetOrCreateMaterialTexture("");
    }

    Ditto::TextureHandle texture = renderer->CreateTexture(pixels, width, height, 4);
    stbi_image_free(pixels);
    materialTextures[texturePath] = texture;
    DITTO_LOG_INFO_STREAM("[Scene] Loaded texture: " << resolved.string() << " (" << width << "x" << height << ")");
    return texture ? texture : GetOrCreateMaterialTexture("");
}

void Scene::InitializeBaseGeometries(Resource* _resource, Ditto::IRenderer* rhi)
{
    this->resource = _resource;
    this->renderer = rhi;

    // Interleaved layout shared by all base/custom models: pos(vec3)+normal(vec3)+uv(vec2).
    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3}, {2, 2, 6} };

    if (renderer && resource->cubeModel && !resource->cubeModel->vertexData.empty())
    {
        const auto& m = *resource->cubeModel;
        baseGeometries[RendererComponent::Cube] =
            BaseGeometry{ renderer->CreateMesh(m.vertexData.data(), m.vertexData.size(), 8, attribs,
                                               m.indices.data(), m.indices.size()) };
    }

    if (renderer && resource->sphereModel && !resource->sphereModel->vertexData.empty())
    {
        const auto& m = *resource->sphereModel;
        baseGeometries[RendererComponent::Sphere] =
            BaseGeometry{ renderer->CreateMesh(m.vertexData.data(), m.vertexData.size(), 8, attribs,
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
            DITTO_LOG_ERROR_STREAM("[Scene] Custom mesh has no geometry: " << resolved);
        // Cache empties so we don't re-attempt the load every frame.
        customGeometries[meshPath] = BaseGeometry{};
        customBatches[meshPath] = std::make_unique<GeometryInstances>(RendererComponent::Cube);
        return;
    }

    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3}, {2, 2, 6} };
    BaseGeometry geo{ renderer->CreateMesh(model.vertexData.data(), model.vertexData.size(), 8, attribs,
                                           model.indices.data(), model.indices.size()) };

    customGeometries[meshPath] = geo;
    // `type` is unused for custom batches (geometry comes from customGeometries).
    customBatches[meshPath] = std::make_unique<GeometryInstances>(RendererComponent::Cube);
    DITTO_LOG_INFO_STREAM("[Scene] Loaded custom mesh: " << resolved
        << " (" << model.vertexData.size() / 8 << " verts)");
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

    DITTO_LOG_INFO_STREAM("[Raycast] MousePos: (" << mousePos.x << ", " << mousePos.y << ")");
    DITTO_LOG_INFO_STREAM("[Raycast] RayOrigin: (" << rayOrigin.x << ", " << rayOrigin.y << ", " << rayOrigin.z << ")");
    DITTO_LOG_INFO_STREAM("[Raycast] RayDir: (" << rayDir.x << ", " << rayDir.y << ", " << rayDir.z << ")");

    GameObject* closest = nullptr;
    float closestDist = FLT_MAX;
    int checkedCount = 0;

    auto getMeshData = [&](RendererComponent::Type type) -> MeshData* {
        if (!resource) return nullptr;
        if (type == RendererComponent::Cube) return resource->cubeMesh.get();
        if (type == RendererComponent::Sphere) return resource->sphereMesh.get();
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
            DITTO_LOG_INFO_STREAM("[Raycast] Object '" << obj->name << "' has no mesh data");
            return;
        }

        DITTO_LOG_INFO_STREAM("[Raycast] Checking object: " << obj->name << " (mesh vertices: " << mesh->vertices.size() << ", indices: " << mesh->indices.size() << ")");

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
            DITTO_LOG_INFO_STREAM("[Raycast] Hit object: " << obj->name << " at distance " << tMin);
            if (tMin < closestDist)
            {
                closestDist = tMin;
                closest = obj;
            }
        }
    };

    // Always traverse from rootGameObject (single-ownership model).
    DITTO_LOG_INFO_STREAM("[Raycast] Traversing rootGameObject hierarchy: " << rootGameObject->name);
    std::function<void(GameObject*)> traverseHierarchy = [&](GameObject* obj)
    {
        if (!obj) return;
        checkObject(obj);
        for (const auto& child : obj->children)
            traverseHierarchy(child.get());
    };
    traverseHierarchy(rootGameObject.get());

    DITTO_LOG_INFO_STREAM("[Raycast] Checked " << checkedCount << " objects, closest: " << (closest ? closest->name : "none"));

    return closest;
}

struct SceneHeader
{
    char magic[4];
    uint32_t version;
    uint32_t gameObjectCount;
    uint64_t fileSize;
};
// v1: original format. v2: RendererComponent::meshPath. v3: ColliderComponent. v4: ColliderComponent::isTrigger. v5: ColliderComponent bias TRS. v6: Renderer mesh source + shader name. v7: Renderer main texture path.
const uint32_t SCENE_VERSION = 7;
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
    DITTO_LOG_INFO_STREAM("[Scene::SaveScene] Starting save to: " << filepath);

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Failed to open file for writing: " << filepath);
        return false;
    }

    try
    {
        WriteToStream(file);
        file.close();
        DITTO_LOG_INFO("[Scene::SaveScene] Save completed.");
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Scene::SaveScene] Error saving scene: " << e.what());
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
    DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Starting load from: " << filepath);

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Failed to open file for reading: " << filepath);
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
        DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Header: magic=" << header.magic[0] << header.magic[1] << header.magic[2]
            << ", version=" << header.version << ", gameObjectCount=" << header.gameObjectCount
            << ", fileSize=" << header.fileSize);
        
        if (memcmp(header.magic, SCENE_MAGIC, 4) != 0)
        {
            DITTO_LOG_ERROR("[Scene::LoadScene] Invalid scene file: wrong magic number");
            return false;
        }
        if (header.version == 0 || header.version > SCENE_VERSION)
        {
            DITTO_LOG_ERROR_STREAM("[Scene::LoadScene] Unsupported scene version: " << header.version
                << " (this build reads up to: " << SCENE_VERSION << ")");
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
        DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Scene name: " << name);

        ClearScene();

        DITTO_LOG_INFO("[Scene::LoadScene] Deserializing rootGameObject...");
        rootGameObject->Deserialize(file);
        rootGameObject->name = name;
        DITTO_LOG_INFO_STREAM("[Scene::LoadScene] rootGameObject deserialized: " << rootGameObject->name
            << ", children: " << rootGameObject->children.size());

        std::function<void(GameObject*)> collectRootObjects = [&](GameObject* obj) {
            for (const auto& child : obj->children)
            {
                if (child->children.empty())
                {
                    // Leaf node
                    gameObjects.push_back(child.get());
                }
                else
                {
                    // Non-leaf node, continue traversal
                    collectRootObjects(child.get());
                    gameObjects.push_back(child.get());
                }
            }
        };
        collectRootObjects(rootGameObject.get());
        DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Collected " << gameObjects.size() << " objects to gameObjects list");

        // Find main light
        mainLight = nullptr;
        std::function<void(GameObject*)> findLight = [&](GameObject* obj) {
            if (!mainLight && obj->GetComponent<LightComponent>())
                mainLight = obj;
            for (const auto& child : obj->children)
                findLight(child.get());
        };
        findLight(rootGameObject.get());

        // Report raycast-capable objects
        DITTO_LOG_INFO("[Scene::LoadScene] === Raycast-capable objects ===");
        int raycastObjCount = 0;
        std::function<void(GameObject*)> reportRaycastObjects = [&](GameObject* obj) {
            if (!obj) return;
            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            TransformComponent* transform = obj->GetComponent<TransformComponent>();
            if (renderer && transform)
            {
                DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Raycast object: " << obj->name
                    << " (enabled=" << obj->enabled
                    << ", renderer=" << (renderer->enabled ? "enabled" : "disabled")
                    << ", transform=" << (transform->enabled ? "enabled" : "disabled")
                    << ", type=" << (renderer->type == RendererComponent::Cube ? "Cube" : "Sphere")
                    << ", pos=[" << transform->position.x << ", " << transform->position.y << ", " << transform->position.z << "])");
                raycastObjCount++;
            }
            for (const auto& child : obj->children)
                reportRaycastObjects(child.get());
        };
        // Single-ownership: always start from rootGameObject.
        reportRaycastObjects(rootGameObject.get());
        DITTO_LOG_INFO_STREAM("[Scene::LoadScene] Total raycast-capable objects: " << raycastObjCount);

        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("Error loading scene: " << e.what());
        ClearScene();
        name = "Load Failed";
        return false;   // istream& has no close(); LoadScene's ifstream closes via RAII
    }
}
