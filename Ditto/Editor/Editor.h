#pragma once
#include <string>
#include <vector>
#include <set>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui.h"

// 前向声明
struct Engine;
struct GameObject;
struct Project;
struct Camera;
struct Shader;

namespace ImGui { class ImTextureID; }

// 窗口类头文件
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "../Engine/Core/CSharpScript.h"

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
    GameObject* selectedObject = nullptr;  // Inspector 显示的物体（锁定后不变）
    GameObject* activeSelection = nullptr;  // 当前选中的物体（用于 Hierarchy 高亮）
    std::set<GameObject*> m_expandedGameObjects;  // Hierarchy 展开状态
    SelectedFile selectedFile;   // 选中的文件
    char sceneNameBuffer[16] = "Default";
    char layoutNameBuffer[32] = "Default";
    char projectNameBuffer[32] = "MyProject";
    bool isSceneActive;
    bool showSavePopup, showLoadPopup, showSaveLayoutPopup;
    bool showBuildPopup = false;  // 打包发布弹窗
    bool showProjectManager = false;  // 项目管理界面
    bool showNewProjectPopup = false;
    bool showRenameProjectPopup = false;
    std::string renameProjectOldPath;
    char renameProjectBuffer[64] = "";
    bool showProjectSelector = false;
    bool projectLoaded = false;
    bool sceneDirty = false;  // 场景是否有修改未保存
    bool lockingSelection = false;  // 是否锁定当前选择（Inspector锁定后不再切换）
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    int frame; float fps, ppf, deltaTime;
    
    // Play模式相关
    std::string m_tempScenePath;  // 临时场景文件路径（Play时保存的场景）
    bool m_isPlaying = false;     // 是否正在Play模式

    Editor(void* window, bool gameMode = false, const std::string& projectPath = "");
    ~Editor();
    void Draw();
    void DrawProjectSelector();  // 项目选择界面
    
    // 游戏模式相关
    bool gameMode = false;
    std::string gameProjectPath;
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

    // 保存当前场景（带修改检查）
    void SaveCurrentScene();
    
    // 标记场景为已修改
    void MarkSceneDirty() { sceneDirty = true; }
    
    // 打包发布
    void BuildProject();
    
    // 编译脚本为 DLL
    void BuildScripts();

    // 脚本拖拽处理
    void OnScriptComponentDropped(const std::string& scriptPath);
    void OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath);

    // 3D模型预览相关（委托给 InspectorWindow）
    void InitModelPreview() { if (m_inspectorWindow) m_inspectorWindow->InitModelPreview(); }
    void LoadPreviewModel(const std::string& modelPath) { if (m_inspectorWindow) m_inspectorWindow->LoadPreviewModel(modelPath); }
    void CleanupModelPreview() { if (m_inspectorWindow) m_inspectorWindow->CleanupModelPreview(); }
    void AddConsoleMessage(const std::string& message) { if (m_projectWindow) m_projectWindow->AddConsoleMessage(message); }

    void DrawGameObjectNode(GameObject* obj, bool isRoot = false, int depth = 0);
    void CopySelectedObject();
    void DeleteSelectedObject();
    void DeleteSelectedFile();
    void DuplicateSelectedFile();

    // 文件图标相关
    void InitFileIcons();
    void CleanupFileIcons();
    unsigned int GetIconByExtension(const std::string& extension);
    unsigned int GetFolderIcon() { return m_folderIcon; }
    unsigned int GetFolderEmptyIcon() { return m_folderEmptyIcon; }
    unsigned int GetFolderOpenedIcon() { return m_folderOpenedIcon; }
    unsigned int GetDittoIcon() { return m_dittoIcon; }
    unsigned int GetGameObjectIcon() { return m_gameObjectIcon; }
    unsigned int GetLockIcon() { return m_lockIcon; }
    unsigned int GetUnlockIcon() { return m_unlockIcon; }

private:
    // 文件图标
    unsigned int m_icons[7] = {0};  // 0:Default, 1:Cpp, 2:Prefab, 3:Text, 4:Shader, 5:Scene, 6:Folder
    unsigned int m_folderIcon = 0;
    unsigned int m_folderEmptyIcon = 0;
    unsigned int m_folderOpenedIcon = 0;
    unsigned int m_dittoIcon = 0;
    unsigned int m_gameObjectIcon = 0;
    unsigned int m_lockIcon = 0;
    unsigned int m_unlockIcon = 0;
    bool m_fileIconsInitialized = false;
    std::string m_assetsPath;
    
    // 加载单个图标
    unsigned int LoadIcon(const std::string& iconPath);
    int GetIconIndex(const std::string& ext);

    // 窗口组件
    ProjectWindow* m_projectWindow = nullptr;
    InspectorWindow* m_inspectorWindow = nullptr;
};