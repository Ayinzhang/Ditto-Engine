#pragma once
#include "../3rdParty/ImGui/imgui.h"

struct Engine;
struct Editor
{
    Engine* engine;
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
