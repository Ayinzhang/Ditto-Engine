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

    
    
    
    
    enum class Backend { OpenGL, Vulkan, DirectX12 };
#ifdef DITTO_ENABLE_DX12
    Backend backend = Backend::DirectX12;
#elif defined(DITTO_ENABLE_VULKAN)
    Backend backend = Backend::Vulkan;
#else
    Backend backend = Backend::OpenGL;
#endif

    std::unique_ptr<Ditto::IWindow> window;
    int window_width, window_height;
    
    
    
    
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
    
    
    std::unique_ptr<Ditto::IRenderer> renderer;
    
    
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
    
    
    
    
    void* RenderSceneToTexture(int w, int h, bool isGameView);
    void SetEngineState(State state);
    void SetProjectPath(const std::string& path);
    void LoadGameScene();

private:
    State previousFrameState = Edit;

    
    
    
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
