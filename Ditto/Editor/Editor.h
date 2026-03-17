#pragma once
#include "../3rdParty/ImGui/imgui.h"
#include <string>
#include <vector>

struct Engine;
struct GameObject;
struct Project;

struct Editor
{
    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr;
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

    void DrawGameObjectNode(GameObject* obj);
    void CopySelectedObject();
    void DeleteSelectedObject();
};