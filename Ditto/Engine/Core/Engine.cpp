#include "Engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <filesystem>
#include "../../Editor/Editor.h"
#include "../../3rdParty/GLM/glm.hpp"
#include "../../3rdParty/GLAD/glad.h"
#include "../../3rdParty/GLM/ext/matrix_transform.hpp"
#include "../../3rdParty/GLM/gtc/type_ptr.hpp"
#include "CSharpScript.h"
#include "PathUtils.h"
#include "Logger.h"
#include "../Graphics/RHI/GLRenderer.h"
#include "../Graphics/RHI/Vulkan/VulkanRenderer.h"
#include <cstdlib>

using namespace std;
using namespace glm;
namespace fs = std::filesystem;

// Resolve an engine shader by name, anchored to the executable location.
static std::string FindShaderPath(const std::string& shaderName)
{
    fs::path resolved = PathUtils::ResolveAsset("Shaders/" + shaderName);
    if (!fs::exists(resolved))
        std::cerr << "[Engine] Warning: Shader not found: " << shaderName
                  << " (looked at " << resolved.string() << ")" << std::endl;
    return resolved.string();
}

// Resolve a shader preferring a specific project/base directory, then falling
// back to the engine's executable-anchored search.
static std::string FindShaderPathInDir(const std::string& baseDir, const std::string& shaderName)
{
    fs::path resolved = PathUtils::ResolveAsset("Shaders/" + shaderName, baseDir);
    if (!fs::exists(resolved))
        std::cerr << "[Engine] Warning: Shader not found: " << shaderName
                  << " (looked at " << resolved.string() << ")" << std::endl;
    return resolved.string();
}

// Read a text file (shader source) into a string.
static std::string ReadTextFile(const std::string& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

template<typename Func>
void ForEachGameObject(Scene* scene, Func&& func)
{
    if (!scene) return;

    // Single-ownership: always traverse from rootGameObject.
    std::function<void(GameObject*)> traverse = [&](GameObject* obj)
    {
        if (!obj) return;
        // Skip invoking on an object that has a component pending removal this
        // frame, but ALWAYS recurse into its children: a pending removal on one
        // object must not silently freeze script/physics iteration for its whole
        // subtree.
        if (obj->removeComps.empty())
            func(obj);
        for (GameObject* child : obj->children)
            traverse(child);
    };
    traverse(scene->rootGameObject);
}

void Engine::InitCommon(bool createEditor, const std::string& shaderBaseDir)
{
    enableMouse = false;
    window_width = 1200; window_height = 900;
    keySpeed = 0.01f, mouseSpeed = 1.0f;
    editor = nullptr;

    if (!glfwInit()) throw runtime_error("GLFW init failed");

    // ---- Backend selection: default Vulkan; env DITTO_RHI overrides ----
    backend = Backend::Vulkan;
    bool explicitBackend = false;   // user pinned a backend via DITTO_RHI
    {
        char* rhi = nullptr; size_t rhiLen = 0;
        if (_dupenv_s(&rhi, &rhiLen, "DITTO_RHI") == 0 && rhi)
        {
            std::string v = rhi;
            free(rhi);
            if (v == "gl" || v == "opengl" || v == "OpenGL")       { backend = Backend::OpenGL;  explicitBackend = true; }
            else if (v == "dx" || v == "dx12" || v == "directx")   { backend = Backend::DirectX; explicitBackend = true; }
            else if (v == "vk" || v == "vulkan" || v == "Vulkan")  { backend = Backend::Vulkan;  explicitBackend = true; }
        }
    }

    // DirectX backend not implemented yet -> fall back to OpenGL.
    if (backend == Backend::DirectX)
    {
        Ditto::Logger::Get().Warning("[Engine] DirectX backend not implemented yet; falling back to OpenGL.");
        backend = Backend::OpenGL;
    }

    // Capability gating: Vulkan is the default, but it can't yet drive the editor
    // (needs the ImGui Vulkan backend, Vk3) or render the scene (needs the SPIR-V
    // pipeline, Vk4). When Vulkan is the DEFAULT (not explicitly requested) and a
    // required capability isn't built, fall back to OpenGL so the default app keeps
    // working. An explicit DITTO_RHI=vk still runs Vulkan (for bring-up testing).
    // Flip the default fully once DITTO_HAS_IMGUI_VULKAN + DITTO_VULKAN_SCENE exist.
    if (backend == Backend::Vulkan && !explicitBackend)
    {
        bool capable = true;
#ifndef DITTO_VULKAN_DEFAULT_EDITOR
        if (createEditor) capable = false;   // editor: keep GL default until verified
#endif
#ifndef DITTO_VULKAN_SCENE
        if (!createEditor) capable = false;  // game mode needs Vulkan scene rendering (Vk4)
#endif
        if (!capable)
        {
            Ditto::Logger::Get().Info("[Engine] Vulkan not yet the default for this mode; using OpenGL (set DITTO_RHI=vk to force Vulkan).");
            backend = Backend::OpenGL;
        }
    }

    // Create the window + renderer for `backend`; returns false on failure so the
    // caller can fall back to OpenGL.
    auto makeWindowRenderer = [&](Backend b) -> bool
    {
        glfwDefaultWindowHints();
        if (b == Backend::Vulkan)
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // Vulkan: no GL context
        else
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        }

        window = glfwCreateWindow(window_width, window_height, "Ditto", nullptr, nullptr);
        if (!window) return false;
        glfwSetWindowUserPointer(window, this);
        glfwSetCursorPosCallback(window, Engine::MouseCallBack);

        if (b == Backend::Vulkan)
        {
            auto vk = std::make_unique<Ditto::VulkanRenderer>(window);
            if (!vk->IsValid()) return false;   // device/swapchain init failed
            renderer = std::move(vk);
        }
        else
        {
            glfwMakeContextCurrent(window);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;
            renderer = std::make_unique<Ditto::GLRenderer>(window);
        }
        return true;
    };

    if (!makeWindowRenderer(backend))
    {
        if (backend != Backend::OpenGL)
        {
            Ditto::Logger::Get().Warning("[Engine] backend init failed; falling back to OpenGL.");
            renderer.reset();
            if (window) { glfwDestroyWindow(window); window = nullptr; }
            backend = Backend::OpenGL;
            if (!makeWindowRenderer(Backend::OpenGL)) { glfwTerminate(); throw runtime_error("Window/renderer create failed"); }
        }
        else { glfwTerminate(); throw runtime_error("Window/renderer create failed"); }
    }

    resource = std::make_unique<Resource>();
    scene = new Scene();
    sceneCamera = new Camera(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    gameCamera = new Camera(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));

    // Shaders: editor mode resolves from the executable location; game mode
    // resolves relative to the loaded project directory.
    std::string vertexPath = shaderBaseDir.empty()
        ? FindShaderPath("Vertex.glsl") : FindShaderPathInDir(shaderBaseDir, "Vertex.glsl");
    std::string fragmentPath = shaderBaseDir.empty()
        ? FindShaderPath("Fragment.glsl") : FindShaderPathInDir(shaderBaseDir, "Fragment.glsl");
    shaderPipeline = renderer->CreatePipeline(ReadTextFile(vertexPath), ReadTextFile(fragmentPath));

    if (createEditor)
    {
        editor = new Editor(window, false, "");
        editor->engine = this;
    }

    physics = std::make_unique<ParallelPhysics>(); physics->engine = this;

    scene->InitializeBaseGeometries(resource.get(), renderer.get());
    CSharpScriptSystem::Initialize();
}

Engine::Engine()
{
    gameMode = false;
    InitCommon(/*createEditor=*/true, /*shaderBaseDir=*/"");
}

Engine::Engine(bool isGameMode, const std::string& projectPath, const std::string& startupScene)
{
    gameMode = isGameMode;
    gameProjectPath = projectPath;
    startupSceneName = startupScene;

    std::cout << "[Engine] Game mode constructor" << std::endl;
    std::cout << "[Engine] Project path: " << projectPath << std::endl;
    std::cout << "[Engine] Startup scene: " << startupScene << std::endl;

    InitCommon(/*createEditor=*/false, /*shaderBaseDir=*/projectPath);

    LoadGameScene();
}

Engine::~Engine()
{
    CSharpScriptSystem::Shutdown();

    // Raw-owned members are released here, in the original safe order (scene is
    // freed before `resource`, which it referenced). unique_ptr members
    // (resource, shader, physics) release automatically afterwards.
    if (editor) delete editor;
    if (sceneCamera) delete sceneCamera; if (gameCamera) delete gameCamera; if (scene) delete scene;

    // Release the renderer (owns all GL objects: pipelines, meshes, buffers,
    // textures) while the context is STILL current, before tearing down the
    // window. The Scene/editor were deleted above and freed their handles first.
    renderer.reset();

    if (window) glfwDestroyWindow(window); glfwTerminate();
}

void Engine::Run()
{
    while (state != Exit && !glfwWindowShouldClose(window))
    {
        static Engine::State prevState = Edit;
        curTime = glfwGetTime(); deltaTime = curTime - lastTime; lastTime = curTime;

        bool enteredPlay = (state == Play && prevState != Play);
        prevState = state;

        if (state == Play)
        {
            if (enteredPlay)
            {
                ForEachGameObject(scene, [](GameObject* obj)
                {
                    ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
                    {
                        if (script->ShouldReload())
                        {
                            script->HotReloadScript();
                        }
                        else
                        {
                            script->scriptInstance.reset();
                            script->started = false;
                        }
                    });
                });
            }

            double physStart = glfwGetTime();
            physics->UpdatePhysics(deltaTime);
            physicsCnt++;
            physicsTime += glfwGetTime() - physStart;

            ForEachGameObject(scene, [](GameObject* obj)
            {
                ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
                {
                    script->Update();
                });
            });
        }
        ProcessInput(); glfwPollEvents();

        glfwGetFramebufferSize(window, &window_width, &window_height);
        if (window_width <= 0 || window_height <= 0) continue;

        renderer->BeginFrame();
        renderer->SetViewport(0, 0, window_width, window_height);
        renderer->Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));

        if (gameMode)
        {
            renderer->SetDepthState(true);

            mat4 view = gameCamera->GetViewMatrix();
            mat4 projection = perspective(radians(45.0f), (float)window_width / (float)window_height, 0.1f, 100.0f);
            
            if (scene && shaderPipeline)
            {
                scene->Render(shaderPipeline, view, projection, gameCamera->position, window_width, window_height);
            }
        }
        else
        {
            if (editor)
            {
                editor->Draw();
            }
        }

        renderer->EndFrame();
    }
}

void Engine::RenderSceneToViewport(ImRect viewport, bool isGameView)
{
    int x = (int)viewport.Min.x;
    int y = (int)(ImGui::GetIO().DisplaySize.y - viewport.Max.y);
    int w = (int)(viewport.Max.x - viewport.Min.x);
    int h = (int)(viewport.Max.y - viewport.Min.y);
    
    renderer->SetViewport(x, y, w, h);
    renderer->SetScissor(true, x, y, w, h);
    renderer->Clear(Ditto::ClearDepth, glm::vec4(0.0f));
    renderer->SetBlendState(false);
    renderer->SetDepthState(true);

    Camera* cam = isGameView ? gameCamera : sceneCamera;
    mat4 view = cam->GetViewMatrix();
    mat4 projection = perspective(radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);

    scene->Render(shaderPipeline, view, projection, cam->position, w, h);

    renderer->SetScissor(false);
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

    static bool ctrlRPressedLastFrame = false;
    bool ctrlRPressedNow = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (ctrlRPressedNow && !ctrlRPressedLastFrame) {
        ForEachGameObject(scene, [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                if (!script->scriptPath.empty() && fs::exists(script->scriptPath))
                {
                    script->HotReloadScript();
                }
            });
        });
    }
    ctrlRPressedLastFrame = ctrlRPressedNow;

    // Ctrl+Z Undo / Ctrl+Y Redo (editor, edit-mode only). Skip while typing in a
    // text field so the shortcuts don't fight ImGui's own text editing.
    bool ctrlDown = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool textInputActive = ImGui::GetIO().WantTextInput;

    static bool ctrlZLastFrame = false;
    bool ctrlZNow = ctrlDown && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
    if (ctrlZNow && !ctrlZLastFrame && editor && state == Edit && !textInputActive)
        editor->Undo();
    ctrlZLastFrame = ctrlZNow;

    static bool ctrlYLastFrame = false;
    bool ctrlYNow = ctrlDown && glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
    if (ctrlYNow && !ctrlYLastFrame && editor && state == Edit && !textInputActive)
        editor->Redo();
    ctrlYLastFrame = ctrlYNow;
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
                // Single-ownership: physics only needs the root to traverse the
                // whole tree.
                if (scene && scene->rootGameObject)
                {
                    std::vector<GameObject*> rootObjects;
                    rootObjects.push_back(scene->rootGameObject);
                    physics->GenerateColliders(rootObjects);
                }

                ForEachGameObject(scene, [](GameObject* obj)
                {
                    ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
                    {
                        script->Start();
                    });
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
                ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
                {
                    script->OnDestroy();
                    script->started = false;
                });

                // Drop back to euler-authored rotation so the next Play seeds the
                // quaternion fresh from the (possibly restored) euler values.
                if (TransformComponent* t = obj->GetComponent<TransformComponent>())
                {
                    t->useQuatRotation = false;
                    t->localDirty = true;
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
        // Single-ownership: physics only needs the root to traverse the
        // whole tree.
        std::vector<GameObject*> rootObjects;
        rootObjects.push_back(scene->rootGameObject);
        physics->GenerateColliders(rootObjects);
    }

    ForEachGameObject(scene, [](GameObject* obj)
    {
        ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
        {
            script->Start();
        });
    });

    state = Play;
    std::cout << "[Engine] Game mode active, state = Play" << std::endl;
}
