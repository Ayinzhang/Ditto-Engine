#include "Engine.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <filesystem>
#include "../../Editor/Editor.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include "CSharpScript.h"

using namespace std;
using namespace glm;
namespace fs = std::filesystem;

// Helper function to find shader files
static std::string FindShaderPath(const std::string& shaderName)
{
    // List of possible paths to check
    const std::vector<std::string> possiblePaths = {
        "Assets/Shaders/" + shaderName,
        "Ditto/Assets/Shaders/" + shaderName,
        "../../Ditto/Ditto/Assets/Shaders/" + shaderName,
        "../Ditto/Assets/Shaders/" + shaderName,
        "Ditto/Ditto/Assets/Shaders/" + shaderName,
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path))
            return path;
    }
    
    // Return default if not found
    std::cerr << "[Engine] Warning: Shader not found: " << shaderName << std::endl;
    return "Assets/Shaders/" + shaderName;
}

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
    std::string vertexPath = FindShaderPath("Vertex.glsl");
    std::string fragmentPath = FindShaderPath("Fragment.glsl");
    shader = new Shader(vertexPath.c_str(), fragmentPath.c_str());
    editor = new Editor(window, gameMode, gameProjectPath); editor->engine = this;
    physics = new ParallelPhysics(); physics->engine = this;

    scene->InitializeBaseGeometries(resource);
    
    // 初始化 C# 脚本系统
    CSharpScriptSystem::Initialize();
}

Engine::~Engine()
{
    // 关闭 C# 脚本系统
    CSharpScriptSystem::Shutdown();
    
    delete editor;
    delete shader;
    delete sceneCamera;
    delete gameCamera;
    delete scene;
    delete resource;
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

// 辅助函数：遍历所有 GameObject 并执行回调
template<typename Func>
void ForEachGameObject(Scene* scene, Func&& func)
{
    if (!scene) return;
    
    // 从 rootGameObject 开始遍历
    if (scene->rootGameObject)
    {
        std::function<void(GameObject*)> traverse = [&](GameObject* obj)
        {
            if (!obj) return;
            // 跳过标记为删除的对象
            if (obj->removeComps.empty() == false) return;
            func(obj);
            for (GameObject* child : obj->children)
                traverse(child);
        };
        traverse(scene->rootGameObject);
    }
    else
    {
        for (GameObject* obj : scene->gameObjects)
        {
            if (!obj) continue;
            if (obj->removeComps.empty() == false) continue;
            func(obj);
        }
    }
}

void Engine::Run()
{
    // 如果editor还没有创建，现在创建它（此时gameMode已经确定）
    if (!editor)
    {
        editor = new Editor(window, gameMode, gameProjectPath);
        editor->engine = this;
    }
    
    while (state != Exit && !glfwWindowShouldClose(window))
    {
        curTime = glfwGetTime(); deltaTime = curTime - lastTime; lastTime = curTime;

        // Play 模式下更新物理和脚本，Pause 模式下不更新
        if (state == Play) 
        { 
            curTime = glfwGetTime(); 
            physics->UpdatePhysics(deltaTime); 
            physicsCnt++; 
            physicsTime += glfwGetTime() - curTime; 
            
            // 调用所有 C# 脚本的 Update 方法
            ForEachGameObject(scene, [](GameObject* obj)
            {
                for (Component* comp : obj->components)
                {
                    if (comp->index == (1 << 10))  // CSharpScriptComponent
                    {
                        CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                        script->Update();
                    }
                }
            });
        }
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
    // 相机控制已移到 SceneWindow，使用方向键控制

    static bool altPressedLastFrame = false;
    bool altPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    if (altPressedNow && !altPressedLastFrame) enableMouse = !enableMouse;
    altPressedLastFrame = altPressedNow;

    // Delete 键：优先处理文件删除，其次处理 GameObject 删除
    static bool deletePressedLastFrame = false;
    bool deletePressedNow = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (deletePressedNow && !deletePressedLastFrame && editor) {
        if (editor->selectedFile.IsValid())
            editor->DeleteSelectedFile();
        else
            editor->DeleteSelectedObject();
    }
    deletePressedLastFrame = deletePressedNow;

    // Ctrl+D：优先处理文件复制，其次处理 GameObject 复制
    static bool ctrlDPressedLastFrame = false;
    bool ctrlDPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    if (ctrlDPressedNow && !ctrlDPressedLastFrame && editor) {
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
    
    State oldState = state;
    
    switch (newState)
    {
        case Play:
        {
            // 从 Edit 模式进入 Play 模式时，生成碰撞体并调用 Start
            if (oldState == Edit)
            {
                // 如果存在 rootGameObject，从它开始收集碰撞体
                if (scene->rootGameObject)
                {
                    std::vector<GameObject*> rootObjects;
                    rootObjects.push_back(scene->rootGameObject);
                    physics->GenerateColliders(rootObjects);
                }
                else
                {
                    physics->GenerateColliders(scene->gameObjects);
                }
                
                // 调用所有 C# 脚本的 Start 方法
                ForEachGameObject(scene, [](GameObject* obj)
                {
                    for (Component* comp : obj->components)
                    {
                        if (comp->index == (1 << 10))  // CSharpScriptComponent
                        {
                            CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                            script->Start();
                        }
                    }
                });
            }
            // 从 Pause 模式恢复 Play 模式，不需要额外操作
            break;
        }
        case Pause:
        {
            // 暂停模式，不需要额外操作
            break;
        }
        case Stop:
        {
            // 停止 Play 模式，回到 Edit 模式
            // 调用所有 C# 脚本的 OnDestroy 方法
            ForEachGameObject(scene, [](GameObject* obj)
            {
                for (Component* comp : obj->components)
                {
                    if (comp->index == (1 << 10))  // CSharpScriptComponent
                    {
                        CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                        script->OnDestroy();
                        script->started = false;  // 重置 started 状态
                    }
                }
            });
            
            // 清理物理碰撞体
            physics->ClearColliders();
            break;
        }
    }
    state = newState;
}

void Engine::MouseCallBack(GLFWwindow* window, double xpos, double ypos)
{
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine->enableMouse) { engine->lastX = xpos; engine->lastY = ypos; return; }
    if (engine->editor && engine->editor->isSceneActive)
    engine->sceneCamera->ProcessMouseMovement(engine->mouseSpeed * (xpos - engine->lastX) / engine->window_width,
        engine->mouseSpeed * (ypos - engine->lastY) / engine->window_height);
    engine->lastX = xpos;
    engine->lastY = ypos;
}

void Engine::SetProjectPath(const std::string& path)
{
    gameProjectPath = path;
    gameMode = true;
}