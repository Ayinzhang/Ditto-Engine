#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include "GameObject.h"
#include "../Physics/Physics.h"
#include "../Graphics/RHI/IRenderer.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

class Resource;

struct BaseGeometry
{
    Ditto::MeshHandle mesh;   // owned by the renderer; freed via Scene's renderer
};

struct GeometryInstances
{
    RendererComponent::Type type;
    std::string meshPath;
    std::string shaderName;
    std::string texturePath;
    std::vector<glm::mat4> modelMatrices;
    std::vector<glm::vec4> instanceColors;
    Ditto::StorageBufferHandle modelSBO, colorSBO;   // owned by the renderer
    size_t instanceCount = 0;

    GeometryInstances(RendererComponent::Type t) : type(t) {}
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
    std::unordered_map<RendererComponent::Type, BaseGeometry> baseGeometries;
    std::unordered_map<RendererComponent::Type, std::unique_ptr<GeometryInstances>> geometryBatches;

    // Custom-mesh rendering runs as a parallel, lazily-populated pipeline keyed
    // by the renderer's meshPath. The built-in Cube/Sphere path above is left
    // untouched. GL buffers + batches here are owned by the Scene.
    std::unordered_map<std::string, BaseGeometry> customGeometries;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> customBatches;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> renderBatches;
    std::unordered_map<std::string, Ditto::PipelineHandle> shaderPipelines;
    std::unordered_map<std::string, Ditto::PipelineState> shaderPipelineStates;
    std::unordered_map<std::string, Ditto::TextureHandle> materialTextures;
    Ditto::TextureHandle whiteTexture;

    Resource* resource = nullptr;
    // Non-owning RHI pointer (owned by Engine). Set in InitializeBaseGeometries.
    // Used for all GPU resource creation/draw/teardown.
    Ditto::IRenderer* renderer = nullptr;

    std::function<void()> onModified;

    Scene();
    ~Scene();

    void ClearScene();
    bool SaveScene(const std::string& filepath);
    bool LoadScene(const std::string& filepath);

    void WriteToStream(std::ostream& file);
    bool ReadFromStream(std::istream& file);

    std::string CaptureSnapshot();
    bool RestoreSnapshot(const std::string& data);

    void MarkDirty() { if (onModified) onModified(); }

    void UnregisterSubtree(GameObject* obj);

    void CollectRenderData();
    void UpdateSSBOs();
    void Render(Ditto::PipelineHandle pipeline, const glm::mat4& view, const glm::mat4& projection,
        const glm::vec3& viewPos, int viewportWidth, int viewportHeight);

    void InitializeBaseGeometries(Resource* resource, Ditto::IRenderer* rhi);

    // Lazily load + GPU-upload a custom mesh (project-relative .obj) and create
    // its instance batch. No-op if already built or the path is empty.
    void EnsureCustomGeometry(const std::string& meshPath);

    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight);

    glm::vec3 GetLightColor() const;
    glm::vec3 GetLightDirection() const;
    float GetLightIntensity() const;

private:
    void DestroyAllObjects();
    Ditto::PipelineHandle GetOrCreateShaderPipeline(const std::string& shaderName, Ditto::PipelineHandle fallback);
    Ditto::PipelineState GetShaderPipelineState(const std::string& shaderName);
    Ditto::TextureHandle GetOrCreateMaterialTexture(const std::string& texturePath);
};
