#pragma once
#define GLFW_INCLUDE_NONE
#include "Scene.h"
#include "../../Editor/Editor.h"
#include "../../Engine/Graphics/Shader.h"
#include "../../Engine/Graphics/Camera.h"
#include "../Physics/ParallelPhysics.h"
#include "../../Engine/Resources/Resource.h"
#include "../../3rdParty/GLFW/glfw3.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/ImGui/imgui_internal.h"

struct Engine
{
    enum State { Edit, Play, Pause, Stop, Exit } state = Edit;

    GLFWwindow* window;
    int window_width, window_height;
    Resource* resource;
    Scene* scene;
    Editor* editor;
    int physicsCnt;
    float deltaTime, lastTime, curTime, physicsTime;
    Camera* sceneCamera, *gameCamera;
    bool enableMouse;
    float keySpeed, mouseSpeed;
    double lastX, lastY;
    Shader* shader;
    Physics* physics;
    std::string gameProjectPath;
    bool gameMode = false;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Run();
    void ProcessInput();
    void RenderSceneToViewport(ImRect viewport, bool isGameView);
    void SetEngineState(State state);
    void SetProjectPath(const std::string& path);
    static void MouseCallBack(GLFWwindow* window, double xpos, double ypos);
};