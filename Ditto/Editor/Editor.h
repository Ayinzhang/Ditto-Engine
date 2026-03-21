#pragma once
#include <string>
#include <vector>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui.h"

// 前向声明
struct Engine;
struct GameObject;
struct Project;
struct Camera;
struct Shader;

namespace ImGui { class ImTextureID; }

// 选中的文件信息
struct SelectedFile {
    std::string path;        // 完整路径
    std::string name;        // 文件名（不含扩展名）
    std::string extension;   // 扩展名
    std::string folder;      // 所属文件夹 (Assets/Scenes 等)
    
    bool IsValid() const { return !path.empty(); }
    void Clear() { path.clear(); name.clear(); extension.clear(); folder.clear(); }
};

struct Editor
{
    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr;
    SelectedFile selectedFile;   // 选中的文件
    char sceneNameBuffer[16] = "Default";
    char layoutNameBuffer[32] = "Default";
    char projectNameBuffer[32] = "MyProject";
    bool isSceneActive;
    bool showSavePopup, showLoadPopup, showSaveLayoutPopup;
    bool showProjectManager = false;  // 项目管理界面
    bool showNewProjectPopup = false;
    bool showProjectSelector = false;
    bool projectLoaded = false;
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    int frame; float fps, ppf, deltaTime;
    
    Editor(void* window);
    ~Editor();
    void Draw();
    void DrawProjectSelector();  // 项目选择界面
    void DrawProjectManager();   // 项目管理器
    void DrawToolbar();
    void DrawHierarchy();
    void DrawScene();
    void DrawGame();
    void DrawProject();
    void DrawInspector();
    void DrawPopups();
    void DrawLayoutMenu();
    
    void SetupDocking();
    void SaveCurrentLayout();
    void LoadLayout(const std::string& layoutName);
    std::vector<std::string> GetSavedLayouts();
    
    void OpenProject(const std::string& projectPath);
    void LoadSceneFromProject(const std::string& scenePath);
    std::vector<std::string> GetProjectScenes();

    // 3D模型预览相关
    unsigned int previewFBO = 0;
    unsigned int previewRBO = 0;
    unsigned int previewTexture = 0;
    int previewSize = 256; // 正方形
    int previewWidth = 256;
    int previewHeight = 256;
    bool previewInitialized = false, modelInitialized = false;
    Camera* previewCamera = nullptr;
    
    // 独立的预览着色器（不用 SSBO）
    unsigned int previewProgram = 0;
    
    // 预览的模型数据
    struct PreviewModel {
        unsigned int VAO = 0, VBO = 0, EBO = 0;
        int vertexCount = 0;
        int indexCount = 0;
        glm::vec3 center = glm::vec3(0);
        float radius = 1.0f;
    };
    PreviewModel currentPreviewModel;
    std::string currentPreviewPath;
    
    void InitModelPreview();
    void LoadPreviewModel(const std::string& modelPath);
    void RenderModelPreview();
    void CleanupModelPreview();

    void DrawGameObjectNode(GameObject* obj);
    void CopySelectedObject();
    void DeleteSelectedObject();
    void DeleteSelectedFile();
    void DuplicateSelectedFile();
};