#pragma once
#include <string>
#include <vector>
#include <set>
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "BuildSystem.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui.h"
#include "../Engine/Core/CSharpScript.h"

struct Engine; struct GameObject; struct Project; struct Camera; struct Shader; struct SceneWindow;

struct SelectedFile 
{
	std::string path, name, extension, folder; // full path, file name with extension, file extension, parent folder

    bool IsValid() const { return !path.empty(); }
    void Clear() { path.clear(); name.clear(); extension.clear(); folder.clear(); }
};

struct Editor
{
    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr, *activeSelection = nullptr;
    std::set<GameObject*> m_expandedGameObjects;
    SelectedFile selectedFile;
    char sceneNameBuffer[16] = "Default";
    char layoutNameBuffer[32] = "Default";
    char projectNameBuffer[32] = "MyProject";
    bool isSceneActive;
    bool showSavePopup, showLoadPopup, showSaveLayoutPopup;
    bool showBuildPopup = false;
    bool showBuildSettingsWindow = false;
    bool isBuilding = false;
    float buildProgress = 0.0f;
    std::string buildStatus;
    BuildSettings buildSettings;
    bool showProjectManager = false;
    bool showNewProjectPopup = false;
    bool showRenameProjectPopup = false;
    std::string renameProjectOldPath;
    char renameProjectBuffer[64] = "";
    bool showProjectSelector = false;
    bool projectLoaded = false;
    bool sceneDirty = false;
    bool lockingSelection = false;
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    int frame; float fps, ppf, deltaTime;
    
    std::string m_tempScenePath;
    bool m_isPlaying = false;

    Editor(void* window, bool gameMode = false, const std::string& projectPath = "");
    ~Editor();
    void Draw();
    void DrawProjectSelector();
    

    bool gameMode = false;
    std::string gameProjectPath;
    void DrawProjectManager();
    void DrawToolbar();
    void DrawHierarchy();
    void DrawScene();
    void DrawGame();
    void DrawProject();
    void DrawInspector();
    void DrawPopups();
    void DrawBuildSettingsWindow();
    void DrawLayoutMenu();

    void SetupDocking();
    void SaveCurrentLayout();
    void LoadLayout(const std::string& layoutName);
    std::vector<std::string> GetSavedLayouts();

    void OpenProject(const std::string& projectPath);
    void LoadSceneFromProject(const std::string& scenePath);
    std::vector<std::string> GetProjectScenes();

    // Save current scene (with dirty check)
    void SaveCurrentScene();
    
    // Mark scene as modified
    void MarkSceneDirty() { sceneDirty = true; }
    
    // Build and package for release
    void BuildProject();
    
    // Compile scripts to DLL
    void BuildScripts();

    // Script drag-drop handling
    void OnScriptComponentDropped(const std::string& scriptPath);
    void OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath);

    // 3D model preview related (delegated to InspectorWindow)
    void InitModelPreview() { if (m_inspectorWindow) m_inspectorWindow->InitModelPreview(); }
    void LoadPreviewModel(const std::string& modelPath) { if (m_inspectorWindow) m_inspectorWindow->LoadPreviewModel(modelPath); }
    void CleanupModelPreview() { if (m_inspectorWindow) m_inspectorWindow->CleanupModelPreview(); }
    void AddConsoleMessage(const std::string& message) { if (m_projectWindow) m_projectWindow->AddConsoleMessage(message); }

    void DrawGameObjectNode(GameObject* obj, bool isRoot = false, int depth = 0);
    void CopySelectedObject();
    void DeleteSelectedObject();
    void DeleteSelectedFile();
    void DuplicateSelectedFile();

    // File icon related
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
    // File icons
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
    
    // Load single icon
    unsigned int LoadIcon(const std::string& iconPath);
    int GetIconIndex(const std::string& ext);

    // Window components
    ProjectWindow* m_projectWindow = nullptr;
    InspectorWindow* m_inspectorWindow = nullptr;
    SceneWindow* m_sceneWindow = nullptr;
};