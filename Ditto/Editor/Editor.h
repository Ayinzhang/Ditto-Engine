#pragma once
#include "../3rdParty/ImGui/imgui.h"
#include <string>
#include <vector>

struct Engine;
struct GameObject;
struct Editor
{
    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr;
    char sceneNameBuffer[16] = "Default";
    char layoutNameBuffer[32] = "Default";
    bool isSceneActive;
    bool showSavePopup, showLoadPopup, showSaveLayoutPopup;
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    int frame; float fps, ppf, deltaTime;
    
    Editor(void* window);
    ~Editor();
    void Draw();
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

    void DrawGameObjectNode(GameObject* obj);
    void CopySelectedObject();
    void DeleteSelectedObject();
};