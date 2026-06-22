#include "Scene.h"
#include "RuntimeContext.h"
#ifndef DITTO_HEADLESS_SCENE
#include "../../Engine/Resources/Resource.h"
#include "../Graphics/Shaders/ShaderAsset.h"
#include "../Graphics/Materials/MaterialAsset.h"
#include "../Graphics/UI/UIRenderer.h"
#include "../Graphics/ParticleSystemComponent.h"
#include "../Resources/AssetPath.h"
#include "../../3rdParty/stb_image.h"
#endif
#include "Logger.h"
#include <iostream>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>

Scene* g_currentScene = nullptr;

#ifndef DITTO_HEADLESS_SCENE
static unsigned char* LoadImageRGBA(const std::filesystem::path& path, int* width, int* height, int* channels)
{
#ifdef _WIN32
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || !file)
        return nullptr;
    unsigned char* pixels = stbi_load_from_file(file, width, height, channels, 4);
    fclose(file);
    return pixels;
#else
    return stbi_load(path.string().c_str(), width, height, channels, 4);
#endif
}
#endif

Scene::Scene()
{
    name = "Default";
    g_currentScene = this;
    Ditto::RuntimeContext::SetCurrentScene(this);

    rootGameObject = std::make_unique<GameObject>(name);
    rootGameObject->name = name;
    EnsureDefaultCamera();
}

Scene::~Scene()
{
    if (Ditto::RuntimeContext::CurrentScene() == this)
        Ditto::RuntimeContext::SetCurrentScene(nullptr);
    if (g_currentScene == this)
        g_currentScene = nullptr;

    DestroyAllObjects();

    // Release all GPU resources through the renderer (sole owner). The renderer
    // is guaranteed alive here: Engine deletes the Scene before resetting it.
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
    raycastMeshCache.clear();

#ifndef DITTO_HEADLESS_SCENE
    if (uiRenderer)
    {
        uiRenderer->Shutdown();
        delete uiRenderer;
        uiRenderer = nullptr;
    }
#endif
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
    mainCamera = nullptr;
}

void Scene::ClearScene()
{
    DestroyAllObjects();
    EnsureDefaultCamera();
}

void Scene::EnsureDefaultCamera()
{
    if (!rootGameObject) return;

    bool hasCamera = false;
    std::function<void(GameObject*)> findCamera = [&](GameObject* obj)
    {
        if (!obj || hasCamera) return;
        CameraComponent* camera = obj->GetComponent<CameraComponent>();
        if (camera && camera->enabled && camera->mainCamera)
        {
            mainCamera = obj;
            hasCamera = true;
            return;
        }
        for (const auto& child : obj->children)
            findCamera(child.get());
    };
    findCamera(rootGameObject.get());
    if (hasCamera) return;

    auto cameraObject = std::make_unique<GameObject>("Main Camera");
    if (TransformComponent* transform = cameraObject->GetComponent<TransformComponent>())
    {
        transform->position = glm::vec3(0.0f, 2.0f, 5.0f);
        transform->rotation = glm::vec3(-21.8f, 0.0f, 0.0f);
        transform->localDirty = true;
        transform->UpdateTransform();
    }
    CameraComponent* camera = cameraObject->AddComponent<CameraComponent>();
    camera->mainCamera = true;
    mainCamera = cameraObject.get();
    gameObjects.push_back(cameraObject.get());
    rootGameObject->AddChild(std::move(cameraObject));
}

Camera Scene::GetMainCamera(const Camera& fallback)
{
    mainCamera = nullptr;
    CameraComponent* mainCameraComponent = nullptr;
    TransformComponent* mainCameraTransform = nullptr;

    std::function<void(GameObject*)> findCamera = [&](GameObject* obj)
    {
        if (!obj || mainCameraComponent || !obj->enabled) return;
        CameraComponent* camera = obj->GetComponent<CameraComponent>();
        TransformComponent* transform = obj->GetComponent<TransformComponent>();
        if (camera && camera->enabled && camera->mainCamera)
        {
            mainCamera = obj;
            mainCameraComponent = camera;
            mainCameraTransform = transform;
            return;
        }
        for (const auto& child : obj->children)
            findCamera(child.get());
    };
    findCamera(rootGameObject.get());

    if (!mainCameraComponent)
        return fallback;
    return mainCameraComponent->ToCamera(mainCameraTransform);
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

#ifndef DITTO_HEADLESS_SCENE
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

            TransformComponent* transform = obj->GetComponent<TransformComponent>();
            auto addBatchInstance = [&](const std::string& meshPath,
                const std::string& shaderName, const std::string& texturePath,
                const glm::vec4& color, int sortingOrder)
                {
                    if (!transform || !transform->enabled) return;

                    std::string meshKey = "file:" + meshPath;
                    std::string colorKey = std::to_string(color.r) + "," + std::to_string(color.g) + "," +
                        std::to_string(color.b) + "," + std::to_string(color.a);
                    std::string batchKey = shaderName + "|" + meshKey + "|" + texturePath + "|" + colorKey + "|" + std::to_string(sortingOrder);

                    GeometryInstances* batch = nullptr;
                    auto batchIt = renderBatches.find(batchKey);
                    if (batchIt == renderBatches.end())
                    {
                        auto newBatch = std::make_unique<GeometryInstances>();
                        newBatch->meshPath = meshPath;
                        newBatch->shaderName = shaderName;
                        newBatch->texturePath = texturePath;
                        newBatch->sortingOrder = sortingOrder;
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
                        batch->instanceColors.push_back(color);
                        batch->instanceCount++;
                    }
                };

            RendererComponent* renderer = obj->GetComponent<RendererComponent>();
            if (renderer && renderer->enabled && transform && transform->enabled &&
                !renderer->meshPath.empty())
            {
                // All meshes come from project Assets.
                EnsureCustomGeometry(renderer->meshPath);

                Ditto::MaterialAsset material = renderer->materialPath.empty()
                    ? Ditto::MakeDefaultMaterial("Inline Material")
                    : Ditto::LoadMaterialAsset(renderer->materialPath);
                if (renderer->materialPath.empty() || !material.ok)
                {
                    material.shaderName = renderer->shaderName.empty() ? RendererComponent::DefaultShaderName : renderer->shaderName;
                    material.color = renderer->color;
                    material.mainTexturePath = renderer->mainTexturePath;
                }

                std::string shaderName = material.shaderName.empty() ? RendererComponent::DefaultShaderName : material.shaderName;
                std::string texturePath = material.mainTexturePath;
                addBatchInstance(renderer->meshPath, shaderName, texturePath, glm::vec4(material.color), 0);
            }

            SpriteRendererComponent* sprite = obj->GetComponent<SpriteRendererComponent>();
            if (sprite && sprite->enabled && transform && transform->enabled &&
                !sprite->spritePath.empty())
            {
                const bool hasMaterial = !sprite->materialPath.empty();
                Ditto::MaterialAsset material = sprite->materialPath.empty()
                    ? Ditto::MakeDefaultMaterial("Inline Sprite Material")
                    : Ditto::LoadMaterialAsset(sprite->materialPath);
                if (sprite->materialPath.empty() || !material.ok)
                {
                    material.shaderName = sprite->shaderName.empty() ? SpriteRendererComponent::DefaultShaderName : sprite->shaderName;
                    material.color = sprite->color;
                    material.mainTexturePath = sprite->spritePath;
                }

                std::string shaderName = material.shaderName.empty() ? SpriteRendererComponent::DefaultShaderName : material.shaderName;
                std::string texturePath = material.mainTexturePath.empty() ? sprite->spritePath : material.mainTexturePath;
                glm::vec4 finalColor = hasMaterial && material.ok ? glm::vec4(material.color) * sprite->color : sprite->color;
                // Sprites use a built-in quad mesh — we just use a sentinel "sprite" meshPath
                addBatchInstance("__sprite_quad__", shaderName, texturePath,
                    finalColor, sprite->sortingOrder);
            }

            // Particle systems: each alive particle becomes a camera-facing Quad
            // instance. Particles carry per-instance color, so they cannot use the
            // color-keyed addBatchInstance path; push directly into a dedicated
            // batch keyed only by the system (one batch per particle component).
            ParticleSystemComponent* ps = obj->GetComponent<ParticleSystemComponent>();
            if (ps && ps->enabled)
            {
                const std::string shaderName = SpriteRendererComponent::DefaultShaderName;
                std::string batchKey = "particles|" + std::to_string(reinterpret_cast<uintptr_t>(ps));
                GeometryInstances* batch = nullptr;
                auto batchIt = renderBatches.find(batchKey);
                if (batchIt == renderBatches.end())
                {
                    auto newBatch = std::make_unique<GeometryInstances>();
                    newBatch->meshPath = "__particle_quad__";
                    newBatch->shaderName = shaderName;
                    newBatch->sortingOrder = 1000; // draw after opaque/sprites
                    batch = newBatch.get();
                    renderBatches[batchKey] = std::move(newBatch);
                }
                else
                {
                    batch = batchIt->second.get();
                }

                for (const Particle& p : ps->particles)
                {
                    if (!p.alive) continue;
                    // Billboard: columns are camera right/up scaled by size, facing
                    // the camera, positioned at the particle's world position.
                    glm::mat4 m(1.0f);
                    m[0] = glm::vec4(cameraRight * p.size, 0.0f);
                    m[1] = glm::vec4(cameraUp * p.size, 0.0f);
                    m[2] = glm::vec4(glm::normalize(glm::cross(cameraRight, cameraUp)), 0.0f);
                    m[3] = glm::vec4(p.position, 1.0f);
                    batch->modelMatrices.push_back(m);
                    batch->instanceColors.push_back(p.color);
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
    const glm::vec3& viewPos, int viewportWidth, int viewportHeight, bool renderUI)
{
    if (!renderer) return;

    // Extract the camera basis from the view matrix so particle systems can
    // build camera-facing billboards during CollectRenderData.
    cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    cameraUp    = glm::vec3(view[0][1], view[1][1], view[2][1]);

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
    std::vector<GeometryInstances*> drawBatches;
    drawBatches.reserve(renderBatches.size());
    for (auto& pair : renderBatches)
    {
        GeometryInstances* batch = pair.second.get();
        if (!batch || batch->instanceCount == 0) continue;
        drawBatches.push_back(batch);
    }

    std::stable_sort(drawBatches.begin(), drawBatches.end(),
        [&](GeometryInstances* a, GeometryInstances* b)
        {
            int queueA = GetShaderPipelineState(a->shaderName).renderQueue;
            int queueB = GetShaderPipelineState(b->shaderName).renderQueue;
            if (queueA != queueB) return queueA < queueB;
            return a->sortingOrder < b->sortingOrder;
        });

    for (GeometryInstances* batch : drawBatches)
    {
        Ditto::PipelineHandle shaderPipeline = GetOrCreateShaderPipeline(batch->shaderName, pipeline);
        renderer->BindPipeline(shaderPipeline ? shaderPipeline : pipeline);
        renderer->SetFrameUniforms(fu);
        renderer->BindTexture(2, GetOrCreateMaterialTexture(batch->texturePath));

        if (batch->meshPath.empty()) continue;

        // Special sentinel mesh paths for sprites and particles — these are
        // handled directly in the rendering pipeline (camera-facing quads).
        if (batch->meshPath == "__sprite_quad__" || batch->meshPath == "__particle_quad__")
        {
            auto geoIt = customGeometries.find(batch->meshPath);
            if (geoIt == customGeometries.end()) continue;
            DrawBatch(renderer, geoIt->second, batch);
            continue;
        }

        auto geoIt = customGeometries.find(batch->meshPath);
        if (geoIt == customGeometries.end()) continue;
        DrawBatch(renderer, geoIt->second, batch);
    }

    if (renderUI)
    {
        if (!uiRenderer) uiRenderer = new UIRenderer();
        uiRenderer->Render(this, viewportWidth, viewportHeight);
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

    shaderPipelineStates[key] = shader.pipelineState;
    Ditto::PipelineHandle created = renderer ? renderer->CreatePipeline(shader.engineHLSL, shader.pipelineState) : Ditto::PipelineHandle{};
    if (!created)
    {
        DITTO_LOG_ERROR_STREAM("[Scene] Failed to create shader pipeline: " << key);
        return fallback;
    }
    shaderPipelines[key] = created;
    return created;
}

Ditto::PipelineState Scene::GetShaderPipelineState(const std::string& shaderName)
{
    std::string key = shaderName.empty() ? RendererComponent::DefaultShaderName : shaderName;
    auto it = shaderPipelineStates.find(key);
    if (it != shaderPipelineStates.end())
        return it->second;

    Ditto::ShaderAsset shader = Ditto::LoadShaderAsset(key);
    if (shader.ok)
    {
        shaderPipelineStates[key] = shader.pipelineState;
        return shader.pipelineState;
    }
    shaderPipelineStates[key] = {};
    return shaderPipelineStates[key];
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
    if (texturePath == "builtin:circle-sprite")
    {
        auto it = materialTextures.find(texturePath);
        if (it != materialTextures.end())
            return it->second ? it->second : GetOrCreateMaterialTexture("");

        constexpr int size = 64;
        std::vector<unsigned char> pixels(size * size * 4, 0);
        const float center = (size - 1) * 0.5f;
        const float radius = center - 1.0f;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float dx = static_cast<float>(x) - center;
                float dy = static_cast<float>(y) - center;
                float dist = sqrtf(dx * dx + dy * dy);
                float edge = glm::clamp(radius + 1.0f - dist, 0.0f, 1.0f);
                int i = (y * size + x) * 4;
                pixels[i + 0] = 255;
                pixels[i + 1] = 255;
                pixels[i + 2] = 255;
                pixels[i + 3] = static_cast<unsigned char>(edge * 255.0f);
            }
        }
        Ditto::TextureHandle texture = renderer->CreateTexture(pixels.data(), size, size, 4);
        materialTextures[texturePath] = texture;
        return texture ? texture : GetOrCreateMaterialTexture("");
    }

    auto it = materialTextures.find(texturePath);
    if (it != materialTextures.end())
        return it->second ? it->second : GetOrCreateMaterialTexture("");

    std::filesystem::path resolved = Ditto::AssetPath::ResolveAssetPath(texturePath);

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* pixels = LoadImageRGBA(resolved, &width, &height, &channels);
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
    DITTO_LOG_VERBOSE_STREAM("[Scene] Loaded texture: " << resolved.string() << " (" << width << "x" << height << ")");
    return texture ? texture : GetOrCreateMaterialTexture("");
}

void Scene::InitializeBaseGeometries(Resource* _resource, Ditto::IRenderer* rhi)
{
    this->resource = _resource;
    this->renderer = rhi;

    // All visible geometry now comes from project Assets. The only base
    // geometry the scene still owns is the unit quad used for sprite and
    // particle billboarding.
    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3}, {2, 2, 6} };

    if (renderer)
    {
        // Unity-style 2D sprite plane: centered pivot, unit size, XY plane,
        // normal toward +Z so it faces the default camera at z=5.
        const float quadVerts[] = {
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,
        };
        const unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };
        customGeometries["__sprite_quad__"] =
            BaseGeometry{ renderer->CreateMesh(quadVerts, 32, 8, attribs, quadIndices, 6) };
        customGeometries["__particle_quad__"] =
            BaseGeometry{ renderer->CreateMesh(quadVerts, 32, 8, attribs, quadIndices, 6) };
    }
}

void Scene::EnsureCustomGeometry(const std::string& meshPath)
{
    if (meshPath.empty()) return;
    if (meshPath == "__sprite_quad__" || meshPath == "__particle_quad__") return;
    if (customGeometries.find(meshPath) != customGeometries.end()) return; // already built (or cached as failed)

    std::string resolved = Ditto::AssetPath::ResolveTypedAssetPath(meshPath, "Models").string();

    ModelData model(resolved);
    if (!renderer || model.vertexData.empty())
    {
        if (model.vertexData.empty())
            DITTO_LOG_ERROR_STREAM("[Scene] Custom mesh has no geometry: " << resolved);
        // Cache empties so we don't re-attempt the load every frame.
        customGeometries[meshPath] = BaseGeometry{};
        customBatches[meshPath] = std::make_unique<GeometryInstances>();
        return;
    }

    const std::vector<Ditto::VertexAttrib> attribs = { {0, 3, 0}, {1, 3, 3}, {2, 2, 6} };
    BaseGeometry geo{ renderer->CreateMesh(model.vertexData.data(), model.vertexData.size(), 8, attribs,
                                           model.indices.data(), model.indices.size()) };

    customGeometries[meshPath] = geo;
    customBatches[meshPath] = std::make_unique<GeometryInstances>();
    DITTO_LOG_VERBOSE_STREAM("[Scene] Loaded custom mesh: " << resolved
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

#else
void Scene::CollectRenderData() {}
void Scene::UpdateSSBOs() {}
void Scene::Render(Ditto::PipelineHandle, const glm::mat4&, const glm::mat4&, const glm::vec3&, int, int, bool) {}
void Scene::InitializeBaseGeometries(Resource* _resource, Ditto::IRenderer* rhi)
{
    resource = _resource;
    renderer = rhi;
}
void Scene::EnsureCustomGeometry(const std::string&) {}
#endif
