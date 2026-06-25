#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <utility>
#include "GameObject.h"
#include "../Physics/Physics.h"
#include "../Graphics/RHI/IRenderer.h"
#include "../Graphics/Camera.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

struct Resource;
struct UIRenderer;

struct BaseGeometry
{
    Ditto::MeshHandle mesh;   // owned by the renderer; freed via Scene's renderer
};

struct GeometryInstances
{
    std::string meshPath;
    std::string shaderName;
    std::string texturePath;
    int sortingOrder = 0;
    std::vector<glm::mat4> modelMatrices;
    std::vector<glm::vec4> instanceColors;
    Ditto::StorageBufferHandle modelSBO, colorSBO;   // owned by the renderer
    size_t instanceCount = 0;

    GeometryInstances() = default;
    // No GL teardown here: the Scene destroys these storage buffers via its
    // renderer (GeometryInstances has no renderer pointer).
};

struct Scene
{
    std::string name;
    // Ownership: `rootGameObject` always OWNS the entire object tree.
    // `gameObjects` is a NON-OWNING flattened view mirroring
    // `rootGameObject->children`; it is purely for fast iteration/lookup.
    std::vector<GameObject*> gameObjects;
    std::unique_ptr<GameObject> rootGameObject;

    GameObject* mainLight = nullptr;

    // Mesh rendering: lazily-populated pipelines keyed by meshPath.
    // GL buffers + batches here are owned by the Scene.
    std::unordered_map<std::string, BaseGeometry> customGeometries;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> customBatches;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> renderBatches;
    std::unordered_map<std::string, Ditto::PipelineHandle> shaderPipelines;
    std::unordered_map<std::string, Ditto::PipelineState> shaderPipelineStates;
    std::unordered_map<std::string, Ditto::TextureHandle> materialTextures;
    std::unordered_map<std::string, std::pair<std::vector<glm::vec3>, std::vector<unsigned int>>> raycastMeshCache;
    Ditto::TextureHandle whiteTexture;

    Resource* resource = nullptr;
    // Non-owning RHI pointer (owned by Engine). Set in InitializeBaseGeometries.
    // Used for all GPU resource creation/draw/teardown.
    Ditto::IRenderer* renderer = nullptr;

    // In-game UI pass (owned; lazily created by Render, absent in headless
    // builds). Raw pointer so this header doesn't need the UIRenderer type.
    UIRenderer* uiRenderer = nullptr;
    GameObject* mainCamera = nullptr;

    // Camera basis for the current frame, set by Render() before CollectRenderData
    // so particle systems can build camera-facing billboard quads.
    glm::vec3 cameraRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3 cameraUp{ 0.0f, 1.0f, 0.0f };

    std::function<void()> onModified;

    Scene();
    ~Scene();

    void ClearScene();
    void EnsureDefaultCamera();
    Camera GetMainCamera(const Camera& fallback);
    bool SaveScene(const std::string& filepath);
    bool LoadScene(const std::string& filepath);

    void WriteToStream(std::ostream& file);
    bool ReadFromStream(std::istream& file);

    std::string CaptureSnapshot();
    bool RestoreSnapshot(const std::string& data);

    void MarkDirty() { if (onModified) onModified(); }

    void RegisterSubtree(GameObject* obj);
    void UnregisterSubtree(GameObject* obj);

    void CollectRenderData();
    void UpdateSSBOs();
    void Render(const glm::mat4& view, const glm::mat4& projection,
        const glm::vec3& viewPos, int viewportWidth, int viewportHeight, bool renderUI = false);

    void InitializeBaseGeometries(Resource* resource, Ditto::IRenderer* rhi);

    // Lazily load + GPU-upload a custom mesh (project-relative .obj) and create
    // its instance batch. No-op if already built or the path is empty.
    void EnsureCustomGeometry(const std::string& meshPath);

    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight);
    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const Camera& camera, int viewportWidth, int viewportHeight);

    glm::vec3 GetLightColor() const;
    glm::vec3 GetLightDirection() const;
    float GetLightIntensity() const;

    // Cached texture lookup (empty path = shared 1x1 white). Public: also used
    // by the UI renderer for UIImage textures.
    Ditto::TextureHandle GetOrCreateMaterialTexture(const std::string& texturePath);

private:
    void DestroyAllObjects();
    Ditto::PipelineHandle GetOrCreateShaderPipeline(const std::string& shaderName);
    Ditto::PipelineState GetShaderPipelineState(const std::string& shaderName);
};
