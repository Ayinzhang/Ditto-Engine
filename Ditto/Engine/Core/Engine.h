#pragma once
#include <memory>
#include "Scene.h"
#include "../../Engine/Graphics/RHI/IRenderer.h"
#include "../../Engine/Graphics/Camera.h"
#include "../Physics/ParallelPhysics.h"
#include "../Physics/Physics2D.h"
#include "../../Engine/Resources/Resource.h"

struct Editor;

namespace Ditto { class IWindow; }

struct Engine
{
    enum State { Edit, Play, Pause, Stop, Exit } state = Edit;

    // Render backend. Scope is intentionally limited to Vulkan + OpenGL:
    // Vulkan is the default when compiled in, OpenGL is the fallback.
    // DITTO_RHI=opengl forces GL; DITTO_RHI=vulkan requests Vulkan.
    enum class Backend { OpenGL, Vulkan };
#ifdef DITTO_ENABLE_VULKAN
    Backend backend = Backend::Vulkan;
#else
    Backend backend = Backend::OpenGL;
#endif

    std::unique_ptr<Ditto::IWindow> window;
    int window_width, window_height;
    // All subsystems are owned via unique_ptr (RAII). The editor UI keeps
    // working with raw observer pointers obtained via .get()/->.
    // NOTE: ~Engine still resets these in an explicit order (scene must free
    // its GPU handles through `renderer` before the renderer dies).
    std::unique_ptr<Resource> resource;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<Editor> editor;
    int physicsCnt;
    float deltaTime, lastTime, curTime, physicsTime;
    std::unique_ptr<Camera> sceneCamera, gameCamera;
    bool enableMouse;
    float keySpeed, mouseSpeed;
    double lastX, lastY;
    std::unique_ptr<Physics> physics;
    std::unique_ptr<Physics2DWorld> physics2D;
    // RHI: all rendering goes through this. Startup selects Vulkan first when
    // available, then falls back to OpenGL.
    std::unique_ptr<Ditto::IRenderer> renderer;
    // Offscreen render targets for the editor's Scene/Game viewports (owned by
    // the renderer; recreated on viewport resize by RenderSceneToTexture).
    Ditto::RenderTargetHandle sceneViewRT, gameViewRT;
    int sceneViewW = 0, sceneViewH = 0, gameViewW = 0, gameViewH = 0;
    std::string gameProjectPath;
    std::string startupSceneName;
    bool gameMode = false;
    float physics2DAccumulator = 0.0f;

    Engine();
    Engine(bool isGameMode, const std::string& projectPath, const std::string& startupScene = "");
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Run();
    void ProcessInput();
    // Render the scene (scene or game camera) into an offscreen render target of
    // the given size and return its ImGui texture id (nullptr on failure). The
    // editor displays the result with flipped V (uv0=(0,1), uv1=(1,0)) -- both
    // backends produce GL bottom-up memory order.
    void* RenderSceneToTexture(int w, int h, bool isGameView);
    void SetEngineState(State state);
    void SetProjectPath(const std::string& path);
    void LoadGameScene();

private:
    State previousFrameState = Edit;

    // Shared construction path for both the editor and game-mode constructors.
    // `createEditor` builds the ImGui editor (edit mode only); `shaderBaseDir`
    // selects where shaders are resolved from ("" = executable-anchored).
    void InitCommon(bool createEditor, const std::string& shaderBaseDir);
    bool BeginRuntimeFrame();
    void UpdatePlayModeFrame(bool enteredPlay);
    void StepPhysics2D();
    void DispatchCollisionEvents();
    void UpdateUIButtonInteractions();
    void UpdateScriptComponents();
    void UpdateRuntimeComponents();
    bool BeginRenderFrame();
    void RenderMainFrame();
    void EnterPlayMode();
    void ExitPlayMode();
    void RebuildRuntimePhysics();
    void PrepareScriptsForPlay();
    void StartScriptsAndAwakeComponents();
};
