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
#include "EngineLifecycle.h"
#include "GlfwWindow.h"
#include "Input.h"
#include "InputKeyCodes.h"
#include "PathUtils.h"
#include "Logger.h"
#include "JsonConfig.h"
#include "../Animation/AnimatorComponent.h"
#include "../Graphics/ParticleSystemComponent.h"
#include "../Audio/AudioEngine.h"
#include "../Graphics/RHI/GLRenderer.h"
#ifdef DITTO_ENABLE_VULKAN
#include "../Graphics/RHI/Vulkan/VulkanRenderer.h"
#endif
#ifdef DITTO_ENABLE_DX12
#include "../Graphics/RHI/DirectX12/DirectX12Renderer.h"
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

using namespace std;
using namespace glm;
namespace fs = std::filesystem;

static Ditto::IWindow* g_gladLoadWindow = nullptr;

static void* LoadGLProcAddress(const char* name)
{
    return g_gladLoadWindow ? g_gladLoadWindow->GetProcAddress(name) : nullptr;
}

static const char* BackendName(Engine::Backend backend)
{
    switch (backend)
    {
    case Engine::Backend::DirectX12: return "DirectX 12";
    case Engine::Backend::Vulkan: return "Vulkan";
    case Engine::Backend::OpenGL: return "OpenGL";
    }
    return "Unknown";
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
    window.reset();

    if (!Ditto::InitializeWindowSystem()) throw runtime_error("Window system init failed");

    std::vector<Backend> backendCandidates;
#ifdef DITTO_ENABLE_DX12
    backendCandidates = { Backend::DirectX12,
#ifdef DITTO_ENABLE_VULKAN
        Backend::Vulkan,
#endif
        Backend::OpenGL };
#elif defined(DITTO_ENABLE_VULKAN)
    backendCandidates = { Backend::Vulkan, Backend::OpenGL };
#else
    backendCandidates = { Backend::OpenGL };
#endif

    char* rhi = nullptr; size_t rhiLen = 0;
    if (_dupenv_s(&rhi, &rhiLen, "DITTO_RHI") == 0 && rhi)
    {
        std::string v = rhi;
        free(rhi);
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (v == "gl" || v == "opengl")
            backendCandidates = { Backend::OpenGL };
        else if (v == "vk" || v == "vulkan")
        {
#ifdef DITTO_ENABLE_VULKAN
            backendCandidates = { Backend::Vulkan, Backend::OpenGL };
#else
            Ditto::Logger::Get().Warning("[Engine] DITTO_RHI=vulkan ignored; this build was compiled without Vulkan support.");
            backendCandidates = { Backend::OpenGL };
#endif
        }
        else if (v == "dx" || v == "dx12" || v == "directx")
        {
#ifdef DITTO_ENABLE_DX12
            backendCandidates = { Backend::DirectX12,
#ifdef DITTO_ENABLE_VULKAN
                Backend::Vulkan,
#endif
                Backend::OpenGL };
#else
            Ditto::Logger::Get().Warning("[Engine] DITTO_RHI=dx12 ignored; this build was compiled without DirectX 12 support.");
            backendCandidates = {
#ifdef DITTO_ENABLE_VULKAN
                Backend::Vulkan,
#endif
                Backend::OpenGL };
#endif
        }
    }

    // Create the window + renderer for `backend`; returns false on failure so the
    // caller can fall back to OpenGL.
    auto makeWindowRenderer = [&](Backend b) -> bool
    {
        window = std::make_unique<Ditto::GlfwWindow>();
        Ditto::WindowDesc desc;
        desc.width = window_width;
        desc.height = window_height;
        desc.title = "Ditto";
        desc.backendHint = (b == Backend::Vulkan)
            ? Ditto::WindowBackendHint::Vulkan
            : (b == Backend::DirectX12 ? Ditto::WindowBackendHint::DirectX12 : Ditto::WindowBackendHint::OpenGL);
        if (!window->Create(desc)) return false;
        window->SetCursorCallback([this](double xpos, double ypos)
        {
            if (!enableMouse) { lastX = xpos; lastY = ypos; return; }
            if (editor && editor->isSceneActive)
                sceneCamera->ProcessMouseMovement(mouseSpeed * static_cast<float>(xpos - lastX) / window_width,
                    mouseSpeed * static_cast<float>(ypos - lastY) / window_height);
            lastX = xpos;
            lastY = ypos;
        });
        if (b == Backend::DirectX12)
        {
#ifdef DITTO_ENABLE_DX12
            auto dx12 = std::make_unique<Ditto::DirectX12Renderer>(window.get());
            if (!dx12->IsValid()) return false;
            renderer = std::move(dx12);
#else
            return false;
#endif
        }
        else if (b == Backend::Vulkan)
        {
#ifdef DITTO_ENABLE_VULKAN
            auto vk = std::make_unique<Ditto::VulkanRenderer>(window.get());
            if (!vk->IsValid()) return false;   // device/swapchain init failed
            renderer = std::move(vk);
#else
            return false;
#endif
        }
        else
        {
            window->MakeContextCurrent();
            g_gladLoadWindow = window.get();
            if (!gladLoadGLLoader((GLADloadproc)LoadGLProcAddress))
            {
                g_gladLoadWindow = nullptr;
                return false;
            }
            g_gladLoadWindow = nullptr;
            renderer = std::make_unique<Ditto::GLRenderer>(window.get());
        }
        return true;
    };

    bool initialized = false;
    for (Backend candidate : backendCandidates)
    {
        backend = candidate;
        Ditto::Logger::Get().Info(std::string("[Engine] Trying RHI: ") + BackendName(candidate));
        if (makeWindowRenderer(candidate))
        {
            Ditto::Logger::Get().Info(std::string("[Engine] Active RHI: ") + BackendName(candidate));
            initialized = true;
            break;
        }

        Ditto::Logger::Get().Warning(std::string("[Engine] RHI init failed: ") + BackendName(candidate));
        renderer.reset();
        if (window) { window->Destroy(); window.reset(); }
    }
    if (!initialized) { Ditto::ShutdownWindowSystem(); throw runtime_error("Window/renderer create failed"); }

    resource = std::make_unique<Resource>();
    scene = std::make_unique<Scene>();
    sceneCamera = std::make_unique<Camera>(vec3(0, 10, 10), vec3(0, 0, 0), vec3(0, 1, 0));
    gameCamera = std::make_unique<Camera>(vec3(0, 5, 10), vec3(0, 0, 0), vec3(0, 1, 0));

    if (createEditor)
    {
        editor = std::make_unique<Editor>(window.get(), false, "");
        editor->engine = this;
    }

    physics = std::make_unique<ParallelPhysics>(); physics->engine = this;
    physics2D = std::make_unique<Physics2DWorld>();
    CSharpScriptSystem::SetPhysics(physics.get());

    scene->InitializeBaseGeometries(resource.get(), renderer.get());
    Input::Init(window.get());
    AudioEngine::Init();
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
    AudioEngine::Shutdown();

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

    if (window) { window->Destroy(); window.reset(); }
    Ditto::ShutdownWindowSystem();
}

void Engine::Run()
{
    while (state != Exit && window && !window->ShouldClose())
    {
        bool enteredPlay = BeginRuntimeFrame();
        if (state == Play)
            UpdatePlayModeFrame(enteredPlay);
        else
            UpdateRuntimeComponents();
        if (!BeginRenderFrame()) continue;
        RenderMainFrame();
        renderer->EndFrame();
    }
}

bool Engine::BeginRuntimeFrame()
{
    curTime = window ? window->TimeSeconds() : 0.0f;
    deltaTime = curTime - lastTime;
    lastTime = curTime;

    bool enteredPlay = (state == Play && previousFrameState != Play);
    previousFrameState = state;

    // Poll window events and snapshot input state BEFORE gameplay runs so
    // C# scripts see this frame's key edges (GetKeyDown/Up) correctly.
    if (window) window->PollEvents();
    Input::NewFrame();
    ProcessInput();
    if (gameMode)
        Input::SetGameViewport(0.0f, 0.0f, (float)window_width, (float)window_height);

    return enteredPlay;
}

void Engine::UpdatePlayModeFrame(bool enteredPlay)
{
    if (enteredPlay) CSharpScriptSystem::SetTime(0.0f);

    double physStart = window ? window->TimeSeconds() : 0.0;
    Ditto::EngineLifecycle::StepPlayModeFrame(scene.get(), physics.get(), physics2D.get(),
        static_cast<float>(deltaTime), physics2DAccumulator);
    physicsCnt++;
    physicsTime += static_cast<float>((window ? window->TimeSeconds() : 0.0) - physStart);

    DispatchCollisionEvents();
    UpdateUIButtonInteractions();
}

void Engine::StepPhysics2D()
{
    if (!physics2D) return;

    float cappedDelta = static_cast<float>(deltaTime);
    if (cappedDelta > 0.25f) cappedDelta = 0.25f;
    physics2DAccumulator += cappedDelta;
    int steps2D = 0;
    while (physics2DAccumulator >= physics2D->fixedDeltaTime && steps2D < 5)
    {
        float step2D = physics2D->fixedDeltaTime;
        CSharpScriptSystem::SetDeltaTime(step2D);
        ForEachGameObject(scene.get(), [](GameObject* obj)
        {
            ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
            {
                script->FixedUpdate();
            });
        });
        physics2D->StepFixed(scene.get(), step2D);
        physics2DAccumulator -= step2D;
        steps2D++;
    }
    CSharpScriptSystem::SetDeltaTime(deltaTime);
}

void Engine::DispatchCollisionEvents()
{
    // Dispatch collision/trigger Enter/Exit events to C# scripts
    // (before Update so scripts see the events for this frame).
    // kind: 0=CollisionEnter 1=CollisionExit 2=TriggerEnter 3=TriggerExit
    auto dispatch = [](GameObject* self, GameObject* other,
        const ContactEvent& ev, bool flipNormal, bool isEnter)
    {
        if (!self || !other) return;
        int kind = (ev.isTrigger ? 2 : 0) + (isEnter ? 0 : 1);
        glm::vec3 n = flipNormal ? -ev.normal : ev.normal;
        ForEachScriptComponent(self, [&](CSharpScriptComponent* script)
        {
            if (!script->enabled || !script->scriptInstance) return;
            MonoRuntime::CallDispatchCollision(script->scriptInstance, kind,
                other, ev.point.x, ev.point.y, ev.point.z,
                n.x, n.y, n.z, ev.depth);
        });
    };

    // ev.normal points from a towards b. Unity convention: the collision normal
    // points away from the other collider.
    for (const ContactEvent& ev : physics->enterEvents)
    {
        dispatch(ev.a, ev.b, ev, true, true);
        dispatch(ev.b, ev.a, ev, false, true);
    }
    for (const ContactEvent& ev : physics->exitEvents)
    {
        dispatch(ev.a, ev.b, ev, true, false);
        dispatch(ev.b, ev.a, ev, false, false);
    }
    physics->enterEvents.clear();
    physics->exitEvents.clear();

    if (!physics2D) return;

    auto dispatch2D = [&](GameObject* self, GameObject* other,
        const ContactEvent2D& ev, bool flipNormal, bool isEnter)
    {
        if (!self || !other) return;
        int kind = (ev.isTrigger ? 2 : 0) + (isEnter ? 0 : 1);
        glm::vec2 n = flipNormal ? -ev.normal : ev.normal;
        ForEachScriptComponent(self, [&](CSharpScriptComponent* script)
        {
            if (!script->enabled || !script->scriptInstance) return;
            MonoRuntime::CallDispatchCollision(script->scriptInstance, kind,
                other, ev.point.x, ev.point.y, 0.0f,
                n.x, n.y, 0.0f, ev.depth);
        });
    };

    for (const ContactEvent2D& ev : physics2D->enterEvents)
    {
        dispatch2D(ev.a, ev.b, ev, true, true);
        dispatch2D(ev.b, ev.a, ev, false, true);
    }
    for (const ContactEvent2D& ev : physics2D->exitEvents)
    {
        dispatch2D(ev.a, ev.b, ev, true, false);
        dispatch2D(ev.b, ev.a, ev, false, false);
    }
    physics2D->enterEvents.clear();
    physics2D->exitEvents.clear();
}

void Engine::UpdateUIButtonInteractions()
{
    // UI button interaction (hover/press/click), using the viewport-relative
    // mouse position. Runs before script Update so scripts observe clicks in
    // the same frame.
    glm::vec2 mouse = Input::GetMousePosition();
    glm::vec2 vp = Input::GetGameViewportSize();
    bool lmbDown = Input::GetMouseButton(0);
    bool lmbUp = Input::GetMouseButtonUp(0);

    ForEachGameObject(scene.get(), [&](GameObject* obj)
    {
        if (!obj->enabled) return;
        for (const auto& comp : obj->components)
        {
            if (!comp || comp->index != ComponentIndex::UIButton || !comp->enabled) continue;
            auto* btn = static_cast<UIButtonComponent*>(comp.get());
            if (!btn->interactable)
            {
                btn->hovered = false;
                btn->pressed = false;
                btn->wasClicked = false;
                continue;
            }
            glm::vec4 rect = obj->GetComponent<RectTransformComponent>()
                ? obj->GetComponent<RectTransformComponent>()->ComputeRect(vp.x, vp.y)
                : ComputeUIRect(btn->anchor, btn->offset, btn->size, vp.x, vp.y);
            btn->hovered = mouse.x >= rect.x && mouse.x < rect.x + rect.z &&
                mouse.y >= rect.y && mouse.y < rect.y + rect.w;
            btn->pressed = btn->hovered && lmbDown;
            if (btn->hovered && lmbUp) btn->wasClicked = true;
        }
    });
}

void Engine::UpdateScriptComponents()
{
    ForEachGameObject(scene.get(), [](GameObject* obj)
    {
        ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
        {
            script->Update();
        });
    });
}

void Engine::UpdateRuntimeComponents()
{
    // Animator runs only in Play mode; ParticleSystem also ticks in Edit mode
    // for Inspector preview playback.
    float frameDt = static_cast<float>(deltaTime);
    bool isPlay = (state == Play);
    ForEachGameObject(scene.get(), [frameDt, isPlay](GameObject* obj)
    {
        if (!obj->enabled) return;
        if (isPlay)
            if (auto* anim = obj->GetComponent<AnimatorComponent>())
                anim->Update(frameDt);
        if (auto* ps = obj->GetComponent<ParticleSystemComponent>())
            ps->Update(frameDt);
    });
}

bool Engine::BeginRenderFrame()
{
    if (window) window->GetFramebufferSize(window_width, window_height);
    if (window_width <= 0 || window_height <= 0) return false;

    renderer->BeginFrame();
    renderer->SetViewport(0, 0, window_width, window_height);
    return true;
}

void Engine::RenderMainFrame()
{
    if (gameMode)
    {
        Camera activeCamera = scene ? scene->GetMainCamera(*gameCamera) : *gameCamera;
        renderer->Clear(Ditto::ClearColor | Ditto::ClearDepth, activeCamera.backgroundColor);
        renderer->SetDepthState(true);

        mat4 view = activeCamera.GetViewMatrix();
        mat4 projection = activeCamera.GetProjectionMatrix((float)window_width / (float)window_height);

        if (scene)
            scene->Render(view, projection, activeCamera.position, window_width, window_height, true);
    }
    else
    {
        renderer->Clear(Ditto::ClearColor | Ditto::ClearDepth, sceneCamera->backgroundColor);
        if (editor)
            editor->Draw();
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
    renderer->SetViewport(0, 0, w, h);
    renderer->SetScissor(false);
    Camera fallbackCamera = isGameView ? *gameCamera : *sceneCamera;
    Camera activeCamera = (isGameView && scene) ? scene->GetMainCamera(fallbackCamera) : fallbackCamera;
    renderer->Clear(Ditto::ClearColor | Ditto::ClearDepth,
        activeCamera.backgroundColor);
    renderer->SetBlendState(false);
    renderer->SetDepthState(true);

    mat4 view = activeCamera.GetViewMatrix();
    mat4 projection = activeCamera.GetProjectionMatrix((float)w / (float)h);

    scene->Render(view, projection, activeCamera.position, w, h, isGameView);

    renderer->EndRenderTarget();

    return renderer->GetImGuiTextureID(renderer->GetColorTexture(rt));
}

void Engine::ProcessInput()
{
    if (!window) return;
    if (window->IsKeyPressed(Ditto::KeyCode::Escape)) state = Exit;

    static bool altPressedLastFrame = false;
    bool altPressedNow = window->IsKeyPressed(Ditto::KeyCode::LeftAlt);
    if (altPressedNow && !altPressedLastFrame) enableMouse = !enableMouse;
    altPressedLastFrame = altPressedNow;

    static bool deletePressedLastFrame = false;
    bool deletePressedNow = window->IsKeyPressed(Ditto::KeyCode::Delete);
    if (deletePressedNow && !deletePressedLastFrame && editor) {
        if (editor->selectedFile.IsValid())
            editor->DeleteSelectedFile();
        else
            editor->DeleteSelectedObject();
    }
    deletePressedLastFrame = deletePressedNow;

    static bool ctrlDPressedLastFrame = false;
    bool ctrlDPressedNow = window->IsKeyPressed(Ditto::KeyCode::LeftControl) && window->IsKeyPressed(Ditto::KeyCode::D);
    if (ctrlDPressedNow && !ctrlDPressedLastFrame && editor) {
        if (editor->selectedFile.IsValid())
            editor->DuplicateSelectedFile();
        else
            editor->CopySelectedObject();
    }
    ctrlDPressedLastFrame = ctrlDPressedNow;

    static bool ctrlRPressedLastFrame = false;
    bool ctrlRPressedNow = window->IsKeyPressed(Ditto::KeyCode::LeftControl) && window->IsKeyPressed(Ditto::KeyCode::R);
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
    bool ctrlDown = window->IsKeyPressed(Ditto::KeyCode::LeftControl) ||
                    window->IsKeyPressed(Ditto::KeyCode::RightControl);
    bool textInputActive = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;

    static bool ctrlZLastFrame = false;
    bool ctrlZNow = ctrlDown && window->IsKeyPressed(Ditto::KeyCode::Z);
    if (ctrlZNow && !ctrlZLastFrame && editor && state == Edit && !textInputActive)
        editor->Undo();
    ctrlZLastFrame = ctrlZNow;

    static bool ctrlYLastFrame = false;
    bool ctrlYNow = ctrlDown && window->IsKeyPressed(Ditto::KeyCode::Y);
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
                EnterPlayMode();
            break;
        }
        case Pause:
        {
            break;
        }
        case Stop:
        {
            ExitPlayMode();
            break;
        }
    }
    state = newState;
}

void Engine::RebuildRuntimePhysics()
{
    if (scene && scene->rootGameObject && physics)
    {
        std::vector<GameObject*> rootObjects;
        rootObjects.push_back(scene->rootGameObject.get());
        physics->GenerateColliders(rootObjects);
    }
    if (physics2D) physics2D->Rebuild(scene.get());
    physics2DAccumulator = 0.0f;
}

void Engine::PrepareScriptsForPlay()
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

void Engine::StartScriptsAndAwakeComponents()
{
    ForEachGameObject(scene.get(), [](GameObject* obj)
    {
        ForEachScriptComponent(obj, [](CSharpScriptComponent* script)
        {
            script->Start();
        });

        for (AudioSourceComponent* audio : obj->GetComponents<AudioSourceComponent>())
            if (audio->enabled && audio->playOnAwake)
                audio->Play();

        if (auto* anim = obj->GetComponent<AnimatorComponent>())
            if (anim->playOnAwake) anim->Play();
        if (auto* ps = obj->GetComponent<ParticleSystemComponent>())
            if (ps->playOnAwake) ps->Play();
    });
}

void Engine::EnterPlayMode()
{
    Ditto::EngineLifecycle::EnterPlayMode(scene.get(), physics.get(), physics2D.get(), physics2DAccumulator);
}

void Engine::ExitPlayMode()
{
    Ditto::EngineLifecycle::ExitPlayMode(scene.get(), physics.get(), physics2D.get(), physics2DAccumulator);
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
        GameConfig config;
        if (Ditto::JsonConfig::ReadGameConfig(configFile, config) && !config.startupScene.empty())
        {
            sceneName = config.startupScene;
            DITTO_LOG_INFO_STREAM("[Engine] Startup scene from config: " << sceneName);
        }
    }

    if (sceneName.empty()) sceneName = "Default";

    fs::path scenePath = fs::path(gameProjectPath) / "Assets" / "Scenes" / (sceneName + ".bin");
    DITTO_LOG_INFO_STREAM("[Engine] Loading scene: " << scenePath.string());
    
    bool loaded = false;
    
    if (scene && fs::exists(scenePath))
    {
        if (scene->LoadScene(scenePath.string().c_str()))
        {
            DITTO_LOG_INFO_STREAM("[Engine] Scene loaded: " << scene->name);
            loaded = true;
        }
        else
        {
            DITTO_LOG_ERROR_STREAM("[Engine] Failed to load scene: " << scenePath.string());
        }
    }
    
    if (!loaded)
    {
        fs::path scenesDir = fs::path(gameProjectPath) / "Assets" / "Scenes";
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

    EnterPlayMode();
    state = Play;
    DITTO_LOG_INFO("[Engine] Game mode active, state = Play");
}
