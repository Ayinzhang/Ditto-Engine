#pragma once
#include <string>
#include "../3rdParty/GLM/glm.hpp"
#include "../Engine/Graphics/Camera.h"

// 模型预览管理器 - 负责加载和渲染3D模型预览
class ModelPreviewManager
{
public:
    static ModelPreviewManager& GetInstance();
    
    // 初始化
    void Initialize(int width = 256, int height = 256);
    
    // 加载模型（支持 OBJ）
    bool LoadModel(const std::string& modelPath);
    
    // 渲染预览（返回纹理ID）
    unsigned int Render();
    
    // 清理资源
    void Cleanup();
    
    // 检查是否已加载模型
    // bool HasModel() const { return m_vao != 0; }
    
    // 获取模型路径
    const std::string& GetCurrentPath() const { return m_currentPath; }

private:
    ModelPreviewManager() = default;
    ~ModelPreviewManager() = default;
    ModelPreviewManager(const ModelPreviewManager&) = delete;
    ModelPreviewManager& operator=(const ModelPreviewManager&) = delete;
    
    struct ModelData {
        unsigned int vao = 0, vbo = 0, ebo = 0;
        int vertexCount = 0;
        int indexCount = 0;
        glm::vec3 center = glm::vec3(0);
        float radius = 1.0f;
    };
    
    void CreateFramebuffer();
    void CompileShaders();
    void RenderModel();
    
    int m_width = 256;
    int m_height = 256;
    
    unsigned int m_fbo = 0;
    unsigned int m_rbo = 0;
    unsigned int m_texture = 0;
    unsigned int m_program = 0;
    
    class Camera* m_camera = nullptr;
    ModelData m_model;
    std::string m_currentPath;
    
    bool m_initialized = false;
    bool m_modelLoaded = false;
};