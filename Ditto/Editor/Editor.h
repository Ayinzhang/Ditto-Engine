#pragma once
#include "../3rdParty/ImGui/imgui.h"

struct Engine;
struct GameObject;
struct Editor
{
    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr;
    char sceneNameBuffer[16] = "Default";
    bool showHierarchy, showScene, showInspector;
    Editor(void* window);
    ~Editor();
    void Draw();
    void DrawToolbar();
    void DrawHierarchy();
    void DrawScene();
    void DrawInspector();
};
