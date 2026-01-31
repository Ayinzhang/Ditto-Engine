#pragma once
#define GLFW_INCLUDE_NONE
#include "Scene.h"
#include "../../Editor/Editor.h"
#include "../../Engine/Graphics/Shader.h"
#include "../../Engine/Graphics/Camera.h"
#include "../../Engine/Physics/Physics.h"
#include "../../Engine/Resources/Resource.h"
#include "../../3rdParty/GLFW/glfw3.h"
#include "../../3rdParty/ImGui/imgui.h"

struct Engine
{
    enum State { Edit, Play, Stop, Exit } state = Edit;

    GLFWwindow* window;
    int window_width, window_height;
    Resource* resource;
    Scene* scene;
    Editor* editor;
    Camera* camera;
    bool enableMouse;
    float keySpeed, mouseSpeed;
    double lastX, lastY;
    Shader* shader;
	Physics* physics;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Run();
    void ProcessInput();
    void RenderScene();
    static void MouseCallBack(GLFWwindow* window, double xpos, double ypos);
};