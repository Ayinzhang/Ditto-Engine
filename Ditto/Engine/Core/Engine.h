#pragma once
#define GLFW_INCLUDE_NONE
#include <memory>
#include "Scene.h"
#include "../../Editor/Editor.h"
#include "../../Engine/Graphics/RHI/IRenderer.h"
#include "../../Engine/Graphics/Camera.h"
#include "../Physics/ParallelPhysics.h"
#include "../../Engine/Resources/Resource.h"
#include "../../3rdParty/GLFW/glfw3.h"
#include "../../3rdParty/ImGui/imgui.h"
#include "../../3rdParty/ImGui/imgui_internal.h"

struct Engine
{
    enum State { Edit, Play, Pause, Stop, Exit } state = Edit;

    // Render backend. Priority: Vulkan (default) > DirectX (opt-in) > OpenGL
    // (fallback). Selected at startup via env DITTO_RHI (gl/vk/dx); a backend
    // that fails to initialize falls back to OpenGL. GL and Vulkan cannot share
    // a window, so this also drives window creation.
    enum class Backend { OpenGL, Vulkan, DirectX };
    Backend backend = Backend::Vulkan;

    GLFWwindow* window;   // owned by GLFW (glfwDestroyWindow in ~Engine)
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
    // RHI: all rendering goes through this. GLRenderer today; a Vulkan backend
    // can replace it without touching engine/editor code.
    std::unique_ptr<Ditto::IRenderer> renderer;
    Ditto::PipelineHandle shaderPipeline;   // main scene pipeline (owned by renderer)
    // Offscreen render targets for the editor's Scene/Game viewports (owned by
    // the renderer; recreated on viewport resize by RenderSceneToTexture).
    Ditto::RenderTargetHandle sceneViewRT, gameViewRT;
    int sceneViewW = 0, sceneViewH = 0, gameViewW = 0, gameViewH = 0;
    std::string gameProjectPath;
    std::string startupSceneName;
    bool gameMode = false;

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
    static void MouseCallBack(GLFWwindow* window, double xpos, double ypos);

private:
    // Shared construction path for both the editor and game-mode constructors.
    // `createEditor` builds the ImGui editor (edit mode only); `shaderBaseDir`
    // selects where shaders are resolved from ("" = executable-anchored).
    void InitCommon(bool createEditor, const std::string& shaderBaseDir);
};
