#include "Engine.h"
#include <iostream>
#include <fstream>
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

static std::string FindShaderPath(const std::string& shaderName)
{
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
    
    std::cerr << "[Engine] Warning: Shader not found: " << shaderName << std::endl;
    return "Assets/Shaders/" + shaderName;
}

static std::string FindShaderPathInDir(const std::string& baseDir, const std::string& shaderName)
{
    std::vector<std::string> candidates = {
        baseDir + "/Assets/Shaders/" + shaderName,
        baseDir + "/Shaders/" + shaderName,
    };
    for (const auto& p : candidates)
    {
        if (fs::exists(p))
            return p;
    }
    return FindShaderPath(shaderName);
}

template<typename Func>
void ForEachGameObject(Scene* scene, Func&& func)
{
    if (!scene) return;
    
    if (scene->rootGameObject)
    {
        std::function<void(GameObject*)> traverse = [&](GameObject* obj)
        {
            if (!obj) return;
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

Engine::Engine()
{
    enableMouse = false;
    window_width = 1200; window_height = 900;
    keySpeed = 0.01f, mouseSpeed = 1.0f;
    gameMode = false;
    editor = nullptr;

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
    sceneCamera = new Camera(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    gameCamera = new Camera(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    std::string vertexPath = FindShaderPath("Vertex.glsl");
    std::string fragmentPath = FindShaderPath("Fragment.glsl");
    shader = new Shader(vertexPath.c_str(), fragmentPath.c_str());
    editor = new Editor(window, false, "");
    editor->engine = this;
    physics = new ParallelPhysics(); physics->engine = this;

    scene->InitializeBaseGeometries(resource);
    CSharpScriptSystem::Initialize();
}

Engine::Engine(bool isGameMode, const std::string& projectPath, const std::string& startupScene)
{
    enableMouse = false;
    window_width = 1200; window_height = 900;
    keySpeed = 0.01f, mouseSpeed = 1.0f;
    gameMode = isGameMode;
    gameProjectPath = projectPath;
    startupSceneName = startupScene;
    editor = nullptr;

    std::cout << "[Engine] Game mode constructor" << std::endl;
    std::cout << "[Engine] Project path: " << projectPath << std::endl;
    std::cout << "[Engine] Startup scene: " << startupScene << std::endl;

    if (!glfwInit()) throw runtime_error("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    std::string windowTitle = "Ditto";
    window = glfwCreateWindow(window_width, window_height, windowTitle.c_str(), nullptr, nullptr);
    if (!window) glfwTerminate(), throw runtime_error("Window create failed");
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, Engine::MouseCallBack);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) throw runtime_error("Failed to initialize GLAD");

    resource = new Resource();
    scene = new Scene();
    sceneCamera = new Camera(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    gameCamera = new Camera(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));

    std::string vertexPath = FindShaderPathInDir(projectPath, "Vertex.glsl");
    std::string fragmentPath = FindShaderPathInDir(projectPath, "Fragment.glsl");
    std::cout << "[Engine] Vertex shader: " << vertexPath << std::endl;
    std::cout << "[Engine] Fragment shader: " << fragmentPath << std::endl;
    shader = new Shader(vertexPath.c_str(), fragmentPath.c_str());

    physics = new ParallelPhysics(); physics->engine = this;

    scene->InitializeBaseGeometries(resource);

    CSharpScriptSystem::Initialize();

    LoadGameScene();
}

Engine::~Engine()
{
    CSharpScriptSystem::Shutdown();
    
    if (editor) delete editor; if (physics) delete physics; if (shader) delete shader;
    if (sceneCamera) delete sceneCamera; if (gameCamera) delete gameCamera; if (scene) delete scene;
    if (resource) delete resource; if (window) glfwDestroyWindow(window); glfwTerminate();
}

void Engine::Run()
{
    while (state != Exit && !glfwWindowShouldClose(window))
    {
        curTime = glfwGetTime(); deltaTime = curTime - lastTime; lastTime = curTime;

        if (state == Play) 
        { 
            double physStart = glfwGetTime(); 
            physics->UpdatePhysics(deltaTime); 
            physicsCnt++; 
            physicsTime += glfwGetTime() - physStart; 
            
            ForEachGameObject(scene, [](GameObject* obj)
            {
                for (Component* comp : obj->components)
                {
                    if (comp->index == (1 << 10))
                    {
                        CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                        script->Update();

                        if (script->ShouldReload())
                        {
                            script->HotReloadScript();
                        }
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

        if (gameMode)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            
            mat4 view = gameCamera->GetViewMatrix();
            mat4 projection = perspective(radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);
            
            if (scene && shader)
            {
                scene->Render(shader, view, projection, gameCamera->position, window_width, window_height);
            }
        }
        else
        {
            if (editor)
            {
                editor->Draw();
            }
        }

        if (window) glfwSwapBuffers(window);
    }
}

void Engine::RenderSceneToViewport(ImRect viewport, bool isGameView)
{
    int x = (int)viewport.Min.x;
    int y = (int)(ImGui::GetIO().DisplaySize.y - viewport.Max.y);
    int w = (int)(viewport.Max.x - viewport.Min.x);
    int h = (int)(viewport.Max.y - viewport.Min.y);
    
    glViewport(x, y, w, h);
    glScissor(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    Camera* cam = isGameView ? gameCamera : sceneCamera;
    mat4 view = cam->GetViewMatrix();
    mat4 projection = perspective(radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);

    scene->Render(shader, view, projection, cam->position, w, h);

    glDisable(GL_SCISSOR_TEST);
}

void Engine::ProcessInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) state = Exit;

    static bool altPressedLastFrame = false;
    bool altPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS;
    if (altPressedNow && !altPressedLastFrame) enableMouse = !enableMouse;
    altPressedLastFrame = altPressedNow;

    static bool deletePressedLastFrame = false;
    bool deletePressedNow = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS;
    if (deletePressedNow && !deletePressedLastFrame && editor) {
        if (editor->selectedFile.IsValid())
            editor->DeleteSelectedFile();
        else
            editor->DeleteSelectedObject();
    }
    deletePressedLastFrame = deletePressedNow;

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
            if (oldState == Edit)
            {
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
                
                ForEachGameObject(scene, [](GameObject* obj)
                {
                    for (Component* comp : obj->components)
                    {
                        if (comp->index == (1 << 10))
                        {
                            CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                            script->Start();
                        }
                    }
                });
            }
            break;
        }
        case Pause:
        {
            break;
        }
        case Stop:
        {
            ForEachGameObject(scene, [](GameObject* obj)
            {
                for (Component* comp : obj->components)
                {
                    if (comp->index == (1 << 10))
                    {
                        CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                        script->OnDestroy();
                        script->started = false;
                    }
                }
            });
            
            physics->ClearColliders();
            break;
        }
    }
    state = newState;
}

void Engine::MouseCallBack(GLFWwindow* window, double xpos, double ypos)
{
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (!engine) return;
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

void Engine::LoadGameScene()
{
    if (gameProjectPath.empty()) return;
    
    std::string sceneName = startupSceneName;
    if (sceneName.empty())
    {
        fs::path configFile = fs::path(gameProjectPath) / "game.config";
        if (fs::exists(configFile))
        {
            std::ifstream file(configFile);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    size_t pos = line.find("\"startupScene\"");
                    if (pos != std::string::npos)
                    {
                        size_t colonPos = line.find(':', pos);
                        if (colonPos == std::string::npos) continue;
                        size_t start = line.find('"', colonPos);
                        if (start == std::string::npos) continue;
                        size_t end = line.find('"', start + 1);
                        if (end == std::string::npos) continue;
                        sceneName = line.substr(start + 1, end - start - 1);
                        std::cout << "[Engine] Startup scene from config: " << sceneName << std::endl;
                        break;
                    }
                }
                file.close();
            }
        }
    }

    if (sceneName.empty()) sceneName = "Default";

    std::string scenePath = gameProjectPath + "/Assets/Scenes/" + sceneName + ".bin";
    std::cout << "[Engine] Loading scene: " << scenePath << std::endl;
    
    bool loaded = false;
    
    if (scene && fs::exists(scenePath))
    {
        if (scene->LoadScene(scenePath.c_str()))
        {
            std::cout << "[Engine] Scene loaded: " << scene->name << std::endl;
            loaded = true;
        }
        else
        {
            std::cerr << "[Engine] Failed to load scene: " << scenePath << std::endl;
        }
    }
    
    if (!loaded)
    {
        std::string scenesDir = gameProjectPath + "/Assets/Scenes";
        if (fs::exists(scenesDir))
        {
            for (const auto& entry : fs::directory_iterator(scenesDir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".bin")
                {
                    std::string fallback = entry.path().string();
                    std::cout << "[Engine] Trying fallback scene: " << fallback << std::endl;
                    if (scene && scene->LoadScene(fallback.c_str()))
                    {
                        std::cout << "[Engine] Fallback scene loaded: " << scene->name << std::endl;
                        loaded = true;
                        break;
                    }
                }
            }
        }
    }
    
    if (!loaded)
    {
        std::cerr << "[Engine] No scene could be loaded!" << std::endl;
    }

    if (scene && scene->rootGameObject)
    {
        std::vector<GameObject*> rootObjects;
        rootObjects.push_back(scene->rootGameObject);
        physics->GenerateColliders(rootObjects);
    }
    else if (scene)
    {
        physics->GenerateColliders(scene->gameObjects);
    }

    ForEachGameObject(scene, [](GameObject* obj)
    {
        for (Component* comp : obj->components)
        {
            if (comp->index == (1 << 10))
            {
                CSharpScriptComponent* script = static_cast<CSharpScriptComponent*>(comp);
                script->Start();
            }
        }
    });

    state = Play;
    std::cout << "[Engine] Game mode active, state = Play" << std::endl;
}
