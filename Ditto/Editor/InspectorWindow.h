#pragma once
#include <string>
#include <memory>
#include "../3rdParty/GLM/glm.hpp"
#include "../Engine/Graphics/RHI/IRenderer.h"
// 前向声明
struct Editor;
struct Camera;

class InspectorWindow
{
public:
    InspectorWindow(Editor* editor);
    ~InspectorWindow();

    void Draw();
    
    // 获取当前显示的物体（用于拖拽脚本时添加组件）
    struct GameObject* GetCurrentObject() const { return m_currentObject; }

    // 模型预览控制
    void InitModelPreview();
    void LoadPreviewModel(const std::string& modelPath);
    void RenderModelPreview();
    void CleanupModelPreview();

private:
    Editor* m_editor = nullptr;
    struct GameObject* m_currentObject = nullptr;  // 当前 Inspector 显示的物体

    // 模型预览相关成员变量（渲染走 RHI；句柄由 Engine 的 renderer 拥有）
    Ditto::RenderTargetHandle m_previewRT;
    Ditto::PipelineHandle m_previewPipeline;
    int m_previewWidth = 256;
    int m_previewHeight = 256;
    bool m_previewInitialized = false;
    bool m_modelInitialized = false;
    // Owned preview camera (Camera is forward-declared; the destructor stays
    // defined in InspectorWindow.cpp where the complete type is visible).
    std::unique_ptr<Camera> m_previewCamera;

    struct PreviewModel {
        Ditto::MeshHandle mesh;
        int vertexCount = 0;
        glm::vec3 center = glm::vec3(0);
        float radius = 1.0f;
    };
    PreviewModel m_currentPreviewModel;
    std::string m_currentPreviewPath;

    // 辅助函数
    struct ModelInfo {
        int vertexCount = 0;
        int faceCount = 0;
        bool loaded = false;
    };
    ModelInfo LoadModelInfo(const std::string& path);
};
