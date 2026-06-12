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
#include "../Graphics/Shaders/ShaderAsset.h"
#ifdef DITTO_ENABLE_VULKAN
#include "../Graphics/RHI/Vulkan/VulkanRenderer.h"
#endif
#include <cstdlib>

using namespace std;
using namespace glm;
namespace fs = std::filesystem;

// Resolve an engine shader by name, anchored to the executable location.
static std::string FindShaderPath(const std::string& shaderName)
{
    fs::path resolved = PathUtils::ResolveAsset("Shaders/" + shaderName);
    if (!fs::exists(resolved))
        DITTO_LOG_WARN_STREAM("[Engine] Shader not found: " << shaderName
            << " (looked at " << resolved.string() << ")");
    return resolved.string();
}

// Resolve a shader preferring a specific project/base directory, then falling
// back to the engine's executable-anchored search.
static std::string FindShaderPathInDir(const std::string& baseDir, const std::string& shaderName)
{
    fs::path resolved = PathUtils::ResolveAsset("Shaders/" + shaderName, baseDir);
    if (!fs::exists(resolved))
        DITTO_LOG_WARN_STREAM("[Engine] Shader not found: " << shaderName
            << " (looked at " << resolved.string() << ")");
    return resolved.string();
}

static std::string FindDefaultSceneShaderPath(const std::string& shaderBaseDir)
{
    const std::string primary = "Lit_Toon.shader";
    const std::string fallback = "Lit_Toon.hlsl";

    std::string primaryPath = shaderBaseDir.empty()
        ? FindShaderPath(primary) : FindShaderPathInDir(shaderBaseDir, primary);
    if (fs::exists(primaryPath)) return primaryPath;

    std::string fallbackPath = shaderBaseDir.empty()
        ? FindShaderPath(fallback) : FindShaderPathInDir(shaderBaseDir, fallback);
    if (fs::exists(fallbackPath)) return fallbackPath;

    return shaderBaseDir.empty()
        ? FindShaderPath("Scene.hlsl") : FindShaderPathInDir(shaderBaseDir, "Scene.hlsl");
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
        for (const auto& child : obj->children)
            traverse(child.get());
    };
    traverse(scene->rootGameObject.get());
}

void Engine::InitCommon(bool createEditor, const std::string& shaderBaseDir)
{
    enableMouse = false;
    window_width = 1200; window_height = 900;
    keySpeed = 0.01f, mouseSpeed = 1.0f;
    editor = nullptr;

    if (!glfwInit()) throw runtime_error("GLFW init failed");

    // ---- Backend selection: Vulkan first when built; OpenGL otherwise ----
#ifdef DITTO_ENABLE_VULKAN
    backend = Backend::Vulkan;
#else
    backend = Backend::OpenGL;
#endif
    {
        char* rhi = nullptr; size_t rhiLen = 0;
        if (_dupenv_s(&rhi, &rhiLen, "DITTO_RHI") == 0 && rhi)
        {
            std::string v = rhi;
            free(rhi);
            if (v == "gl" || v == "opengl" || v == "OpenGL")
                backend = Backend::OpenGL;
            else if (v == "dx" || v == "dx12" || v == "directx")
                backend = Backend::DirectX;
            else if (v == "vk" || v == "vulkan" || v == "Vulkan")
#ifdef DITTO_ENABLE_VULKAN
                backend = Backend::Vulkan;
#else
                Ditto::Logger::Get().Warning("[Engine] DITTO_RHI=vk ignored; this build was compiled without Vulkan SDK support.");
#endif
        }
    }

    // DirectX backend not implemented yet -> fall back to the best available backend.
    if (backend == Backend::DirectX)
    {
        Ditto::Logger::Get().Warning("[Engine] DirectX backend not implemented yet; using available backend.");
#ifdef DITTO_ENABLE_VULKAN
        backend = Backend::Vulkan;
#else
        backend = Backend::OpenGL;
#endif
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
#ifdef DITTO_ENABLE_VULKAN
            auto vk = std::make_unique<Ditto::VulkanRenderer>(window);
            if (!vk->IsValid()) return false;   // device/swapchain init failed
            renderer = std::move(vk);
#else
            return false;
#endif
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
    scene = std::make_unique<Scene>();
    sceneCamera = std::make_unique<Camera>(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    gameCamera = std::make_unique<Camera>(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));

    // Shaders: editor mode resolves from the executable location; game mode
    // resolves relative to the loaded project directory.
    std::string scenePath = FindDefaultSceneShaderPath(shaderBaseDir);
    Ditto::ShaderAsset defaultShader = Ditto::LoadShaderAsset(scenePath, shaderBaseDir);
    std::string pipelineSource = defaultShader.ok ? defaultShader.engineHLSL : ReadTextFile(scenePath);
    shaderPipeline = renderer->CreatePipeline(pipelineSource, defaultShader.pipelineState);

    if (createEditor)
    {
        editor = std::make_unique<Editor>(window, false, "");
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

    DITTO_LOG_INFO("[Engine] Game mode constructor");
    DITTO_LOG_INFO_STREAM("[Engine] Project path: " << projectPath);
    DITTO_LOG_INFO_STREAM("[Engine] Startup scene: " << startupScene);

    InitCommon(/*createEditor=*/false, /*shaderBaseDir=*/projectPath);

    LoadGameScene();
}

Engine::~Engine()
{
    CSharpScriptSystem::Shutdown();

    // Destruction order is load-bearing -- reset explicitly instead of relying
    // on member declaration order: the Scene frees its GPU handles through
    // `renderer`, so it must die first; the renderer must die while the GL
    // context is still current (before the window).
    editor.reset();
    sceneCamera.reset(); gameCamera.reset();
    scene.reset();

    // Release the renderer (owns all GL objects: pipelines, meshes, buffers,
    // textures) while the context is STILL current, before tearing down the
    // window. The Scene/editor were destroyed above and freed their handles first.
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
            CSharpScriptSystem::SetDeltaTime(deltaTime);

            if (enteredPlay)
            {
                ForEachGameObject(scene.get(), [](GameObject* obj)
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

            ForEachGameObject(scene.get(), [](GameObject* obj)
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

void* Engine::RenderSceneToTexture(int w, int h, bool isGameView)
{
    if (!renderer || w <= 0 || h <= 0) return nullptr;

    Ditto::RenderTargetHandle& rt = isGameView ? gameViewRT : sceneViewRT;
    int& rtW = isGameView ? gameViewW : sceneViewW;
    int& rtH = isGameView ? gameViewH : sceneViewH;

    // (Re)create the render target when the viewport size changes.
    if (rt && (rtW != w || rtH != h))
    {
        renderer->DestroyRenderTarget(rt);
        rt = {};
    }
    if (!rt)
    {
        rt = renderer->CreateRenderTarget(w, h);
        rtW = w; rtH = h;
    }
    if (!rt) return nullptr;

    renderer->BeginRenderTarget(rt);
    renderer->SetScissor(false);
    renderer->Clear(Ditto::ClearColor | Ditto::ClearDepth, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
    renderer->SetBlendState(false);
    renderer->SetDepthState(true);

    Camera* cam = isGameView ? gameCamera.get() : sceneCamera.get();
    mat4 view = cam->GetViewMatrix();
    mat4 projection = perspective(radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);

    scene->Render(shaderPipeline, view, projection, cam->position, w, h);

    renderer->EndRenderTarget();

    return renderer->GetImGuiTextureID(renderer->GetColorTexture(rt));
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
        ForEachGameObject(scene.get(), [](GameObject* obj)
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
    // Game mode has NO ImGui context (only the editor calls CreateContext), so
    // guard the GetIO() call -- it asserts/crashes on a null context otherwise.
    bool ctrlDown = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool textInputActive = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;

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
                    rootObjects.push_back(scene->rootGameObject.get());
                    physics->GenerateColliders(rootObjects);
                }

                ForEachGameObject(scene.get(), [](GameObject* obj)
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
            ForEachGameObject(scene.get(), [](GameObject* obj)
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
                        DITTO_LOG_INFO_STREAM("[Engine] Startup scene from config: " << sceneName);
                        break;
                    }
                }
                file.close();
            }
        }
    }

    if (sceneName.empty()) sceneName = "Default";

    std::string scenePath = gameProjectPath + "/Assets/Scenes/" + sceneName + ".bin";
    DITTO_LOG_INFO_STREAM("[Engine] Loading scene: " << scenePath);
    
    bool loaded = false;
    
    if (scene && fs::exists(scenePath))
    {
        if (scene->LoadScene(scenePath.c_str()))
        {
            DITTO_LOG_INFO_STREAM("[Engine] Scene loaded: " << scene->name);
            loaded = true;
        }
        else
        {
            DITTO_LOG_ERROR_STREAM("[Engine] Failed to load scene: " << scenePath);
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
                    DITTO_LOG_INFO_STREAM("[Engine] Trying fallback scene: " << fallback);
                    if (scene && scene->LoadScene(fallback.c_str()))
                    {
                        DITTO_LOG_INFO_STREAM("[Engine] Fallback scene loaded: " << scene->name);
                        loaded = true;
                        break;
                    }
                }
            }
        }
    }
    
    if (!loaded)
    {
        DITTO_LOG_ERROR("[Engine] No scene could be loaded!");
    }

    if (scene && scene->rootGameObject)
    {
        // Single-ownership: physics only needs the root to traverse the
        // whole tree.
        std::vector<GameObject*> rootObjects;
        rootObjects.push_back(scene->rootGameObject.get());
        physics->GenerateColliders(rootObjects);
    }

    ForEachGameObject(scene.get(), [](GameObject* obj)
    {
        ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
        {
            script->Start();
        });
    });

    state = Play;
    DITTO_LOG_INFO("[Engine] Game mode active, state = Play");
}
