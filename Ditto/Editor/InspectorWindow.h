#pragma once
#include <string>
#include <memory>
#include "../3rdParty/GLM/glm.hpp"
#include "../Engine/Graphics/RHI/IRenderer.h"

struct Editor;
struct Camera;

class InspectorWindow
{
public:
    InspectorWindow(Editor* editor);
    ~InspectorWindow();

    void Draw();
    
    
    struct GameObject* GetCurrentObject() const { return m_currentObject; }

    
    void InitModelPreview();
    void LoadPreviewModel(const std::string& modelPath);
    void RenderModelPreview();
    void CleanupModelPreview();

private:
    Editor* m_editor = nullptr;
    struct GameObject* m_currentObject = nullptr;  

    
    Ditto::RenderTargetHandle m_previewRT;
    Ditto::PipelineHandle m_previewPipeline;
    int m_previewWidth = 256;
    int m_previewHeight = 256;
    bool m_previewInitialized = false;
    bool m_modelInitialized = false;
    
    
    std::unique_ptr<Camera> m_previewCamera;

    struct PreviewModel {
        Ditto::MeshHandle mesh;
        int vertexCount = 0;
        glm::vec3 center = glm::vec3(0);
        float radius = 1.0f;
    };
    PreviewModel m_currentPreviewModel;
    std::string m_currentPreviewPath;

    
    struct ModelInfo {
        int vertexCount = 0;
        int faceCount = 0;
        bool loaded = false;
    };
    ModelInfo LoadModelInfo(const std::string& path);
};
