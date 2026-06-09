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

    // Render backend (selected at startup via env DITTO_RHI=vulkan; default GL).
    // GL and Vulkan cannot share a window, so this drives window creation too.
    enum class Backend { OpenGL, Vulkan };
    Backend backend = Backend::OpenGL;

    GLFWwindow* window;
    int window_width, window_height;
    // Engine-internal subsystems with no external raw-pointer references are
    // owned via unique_ptr (RAII). `scene`, `sceneCamera`/`gameCamera` and
    // `editor` are intentionally left raw: they are referenced as raw pointers
    // across the editor UI, so migrating them would ripple widely.
    std::unique_ptr<Resource> resource;
    Scene* scene;
    Editor* editor;
    int physicsCnt;
    float deltaTime, lastTime, curTime, physicsTime;
    Camera* sceneCamera, *gameCamera;
    bool enableMouse;
    float keySpeed, mouseSpeed;
    double lastX, lastY;
    std::unique_ptr<Physics> physics;
    // RHI: all rendering goes through this. GLRenderer today; a Vulkan backend
    // can replace it without touching engine/editor code.
    std::unique_ptr<Ditto::IRenderer> renderer;
    Ditto::PipelineHandle shaderPipeline;   // main scene pipeline (owned by renderer)
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
    void RenderSceneToViewport(ImRect viewport, bool isGameView);
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
