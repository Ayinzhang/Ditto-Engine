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
    Ditto::MeshHandle mesh;   
};

struct GeometryInstances
{
    std::string meshPath;
    std::string shaderName;
    std::string texturePath;
    int sortingOrder = 0;
    std::vector<glm::mat4> modelMatrices;
    std::vector<glm::vec4> instanceColors;
    Ditto::StorageBufferHandle modelSBO, colorSBO;   
    size_t instanceCount = 0;

    GeometryInstances() = default;
    
    
};

struct Scene
{
    std::string name;
    
    
    
    std::vector<GameObject*> gameObjects;
    std::unique_ptr<GameObject> rootGameObject;

    GameObject* mainLight = nullptr;

    
    
    std::unordered_map<std::string, BaseGeometry> customGeometries;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> customBatches;
    std::unordered_map<std::string, std::unique_ptr<GeometryInstances>> renderBatches;
    std::unordered_map<std::string, Ditto::PipelineHandle> shaderPipelines;
    std::unordered_map<std::string, Ditto::PipelineState> shaderPipelineStates;
    std::unordered_map<std::string, Ditto::TextureHandle> materialTextures;
    std::unordered_map<std::string, std::pair<std::vector<glm::vec3>, std::vector<unsigned int>>> raycastMeshCache;
    Ditto::TextureHandle whiteTexture;

    Resource* resource = nullptr;
    
    
    Ditto::IRenderer* renderer = nullptr;

    
    
    UIRenderer* uiRenderer = nullptr;
    GameObject* mainCamera = nullptr;

    
    
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

    
    
    void EnsureCustomGeometry(const std::string& meshPath);

    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const glm::mat4& view, const glm::mat4& projection, int viewportWidth, int viewportHeight);
    GameObject* RaycastGameObjects(const glm::vec2& mousePos, const Camera& camera, int viewportWidth, int viewportHeight);

    glm::vec3 GetLightColor() const;
    glm::vec3 GetLightDirection() const;
    float GetLightIntensity() const;

    
    
    Ditto::TextureHandle GetOrCreateMaterialTexture(const std::string& texturePath);

private:
    void DestroyAllObjects();
    Ditto::PipelineHandle GetOrCreateShaderPipeline(const std::string& shaderName);
    Ditto::PipelineState GetShaderPipelineState(const std::string& shaderName);
};
