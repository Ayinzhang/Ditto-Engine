#include "Engine.h"
#include <iostream>
#include <stdexcept>
#include "../../Editor/Editor.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"

using namespace std;
using namespace glm;

Engine::Engine()
{
    enableMouse = false;
    window_width = 1200; window_height = 900;
    keySpeed = 0.01f, mouseSpeed = 1.0f;

    if (!glfwInit()) throw runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(window_width, window_height, "Ditto", nullptr, nullptr);
    if (!window) glfwTerminate(), throw runtime_error("Window create failed");
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, Engine::MouseCallBack);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) throw runtime_error("Failed to initialize GLAD");

    resource = new Resource();
    scene = new Scene();
    // Scene编辑器相机 - 俯视角度方便编辑
    sceneCamera = new Camera(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    // Game游戏相机 - 更贴近游戏视角
    gameCamera = new Camera(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    sceneCamera = sceneCamera; // 默认激活Scene相机
    shader = new Shader("../../Ditto/Ditto/Assets/Shaders/Vertex.glsl", "../../Ditto/Ditto/Assets/Shaders/Fragment.glsl");
    editor = new Editor(window); editor->engine = this;
	physics = new ParallelPhysics(); physics->engine = this;

    scene->InitializeBaseGeometries(resource);
}

Engine::~Engine()
{
    delete editor;
    delete shader;
    delete sceneCamera;
    delete gameCamera;
    delete scene;
    delete resource;
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

void Engine::Run()
{
    while (state != Exit && !glfwWindowShouldClose(window))
    {
        curTime = glfwGetTime(); deltaTime = curTime - lastTime; lastTime = curTime;

        if (state == Play) { curTime = glfwGetTime(); physics->UpdatePhysics(deltaTime); physicsCnt++; physicsTime += glfwGetTime() - curTime; }
        ProcessInput(); glfwPollEvents();

        glfwGetFramebufferSize(window, &window_width, &window_height);
        if (window_width <= 0 || window_height <= 0) continue;
        glViewport(0, 0, window_width, window_height);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 现在由Editor在各个窗口内分别调用渲染
        editor->Draw();

        if (window) glfwSwapBuffers(window);
    }
}

void Engine::RenderSceneToViewport(ImRect viewport, bool isGameView)
{
    // 设置视口到对应窗口区域
    int x = (int)viewport.Min.x;
    int y = (int)(ImGui::GetIO().DisplaySize.y - viewport.Max.y); // OpenGL原点在左下，ImGui在左上，需要翻转Y
    int w = (int)(viewport.Max.x - viewport.Min.x);
    int h = (int)(viewport.Max.y - viewport.Min.y);
    
    glViewport(x, y, w, h);
    glScissor(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 根据窗口类型选择相机：Scene用sceneCamera，Game用gameCamera
    Camera* cam = isGameView ? gameCamera : sceneCamera;
    mat4 view = cam->GetViewMatrix();
    mat4 projection = perspective(radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);

    // 传窗口宽高用于gizmo之类计算
    scene->Render(shader, view, projection, cam->position, w, h);

    glDisable(GL_SCISSOR_TEST);
}

void Engine::ProcessInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) state = Exit;
    if (editor->isSceneActive)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) sceneCamera->position += sceneCamera->forward * keySpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) sceneCamera->position -= sceneCamera->forward * keySpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) sceneCamera->position -= sceneCamera->right * keySpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) sceneCamera->position += sceneCamera->right * keySpeed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) sceneCamera->position += sceneCamera->up * keySpeed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) sceneCamera->position -= sceneCamera->up * keySpeed;
    }

    static bool altPressedLastFrame = false;
    bool altPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    if (altPressedNow && !altPressedLastFrame) enableMouse = !enableMouse;
    altPressedLastFrame = altPressedNow;

    // Delete 键：优先处理文件删除，其次处理 GameObject 删除
    static bool deletePressedLastFrame = false;
    bool deletePressedNow = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (deletePressedNow && !deletePressedLastFrame) {
        if (editor->selectedFile.IsValid())
            editor->DeleteSelectedFile();
        else
            editor->DeleteSelectedObject();
    }
    deletePressedLastFrame = deletePressedNow;

    // Ctrl+D：优先处理文件复制，其次处理 GameObject 复制
    static bool ctrlDPressedLastFrame = false;
    bool ctrlDPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    if (ctrlDPressedNow && !ctrlDPressedLastFrame) {
        if (editor->selectedFile.IsValid())
            editor->DuplicateSelectedFile();
        else
            editor->CopySelectedObject();
    }
    ctrlDPressedLastFrame = ctrlDPressedNow;
}

void Engine::SetEngineState(State newState)
{
    if (state == newState) return;
    switch (newState)
    {
        case Play:
        {
            physics->GenerateColliders(scene->gameObjects);
            break;
		}
    }
    state = newState;
}

void Engine::MouseCallBack(GLFWwindow* window, double xpos, double ypos)
{
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine->enableMouse) { engine->lastX = xpos; engine->lastY = ypos; return; }
	if (engine->editor->isSceneActive)
    engine->sceneCamera->ProcessMouseMovement(engine->mouseSpeed * (xpos - engine->lastX) / engine->window_width,
        engine->mouseSpeed * (ypos - engine->lastY) / engine->window_height);
    engine->lastX = xpos;
    engine->lastY = ypos;
}