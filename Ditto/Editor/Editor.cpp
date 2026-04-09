#define IMGUI_DEFINE_MATH_OPERATORS
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <shlobj.h>
#include <windows.h>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
using namespace glm;
#include "Editor.h"
#include "LayoutManager.h"
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "SceneWindow.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/CSharpScript.h"
#define GLFW_INCLUDE_NONE
#include "../3rdParty/GLFW/glfw3.h"
#include "../3rdParty/GLAD/glad.h"
#include "../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../3rdParty/ImGui/imgui_impl_opengl3.h"
#include "../3rdParty/ImGui/imgui_internal.h"
#include "../3rdParty/GLM/ext/matrix_transform.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../3rdParty/stb_image.h"

namespace fs = std::filesystem;

// Helper function to find editor assets directory
static std::string FindEditorAssetsPath()
{
    const std::vector<std::string> possiblePaths = {
        "Assets",
        "Ditto/Assets",
        "../../Ditto/Ditto/Assets",
        "../Ditto/Assets",
        "Ditto/Ditto/Assets",
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path + "/Settings") || fs::exists(path + "/Icon"))
            return path;
    }
    
    std::cerr << "[Editor] Warning: Editor assets not found, using default" << std::endl;
    return "Assets";
}

// Helper function to find VS vcvars64.bat
static std::string FindVCVarsPath()
{
    // Check environment variable first
    char* vsPath = nullptr;
    size_t vsPathLen = 0;
    _dupenv_s(&vsPath, &vsPathLen, "VSINSTALLDIR");
    if (vsPath)
    {
        std::string vcvarsPath = std::string(vsPath) + "VC\\Auxiliary\\Build\\vcvars64.bat";
        free(vsPath);
        if (fs::exists(vcvarsPath))
            return vcvarsPath;
    }
    
    // Check common installation paths
    const std::vector<std::string> possiblePaths = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvars64.bat",
        "D:/Visual Studio 2022/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Auxiliary/Build/vcvars64.bat",
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path))
            return path;
    }
    
    std::cerr << "[Editor] Warning: vcvars64.bat not found, compilation may fail" << std::endl;
    return "";
}

// 全局 Editor 指针定义
Editor* g_editor = nullptr;

static ImRect GetCurrentViewportRect()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImRect(ImVec2(0, 0), ImVec2(0, 0));
    ImVec2 min = window->InnerRect.Min;
    ImVec2 max = window->InnerRect.Max;
    return ImRect(min, max);
}

Editor::Editor(void* window, bool gameMode, const std::string& projectPath)
{
    // 设置全局 Editor 指针
    g_editor = this;
    
    // 初始化选择状态
    activeSelection = nullptr;
    this->gameMode = gameMode;
    this->gameProjectPath = projectPath;
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // 启用Docking功能
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // 设置全局透明背景 - 针对Docking系统
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle().Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);

    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    showSavePopup = false;
    showLoadPopup = false;
    showSaveLayoutPopup = false;
    projectLoaded = false;
    dockingInitialized = false;
    frame = deltaTime = 0;

    // 初始化项目管理器
    ProjectManager::GetInstance().Initialize("../../Ditto/Ditto/Projects");
    
    // 初始化布局管理器
    std::string editorAssetsPath = FindEditorAssetsPath();
    LayoutManager::GetInstance().Initialize(editorAssetsPath + "/Settings");
    
    // 显示项目选择界面
    showProjectSelector = true;

    // 初始化 3D 模型预览
    InitModelPreview();
    
    // 初始化文件图标
    InitFileIcons();

    // 初始化窗口组件
    m_projectWindow = new ProjectWindow(this);
    m_inspectorWindow = new InspectorWindow(this);
    m_sceneWindow = new SceneWindow(this);
    
    // 设置脚本日志回调
    CSharpScriptSystem::SetEditor(this);

    // 设置场景修改回调（自动标记 dirty）
    if (engine && engine->scene)
    {
        engine->scene->onModified = [this]() {
            this->sceneDirty = true;
        };
    }
}

Editor::~Editor()
{
    CleanupModelPreview();
    CleanupFileIcons();

    // 清理窗口组件
    delete m_projectWindow;
    delete m_inspectorWindow;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    isSceneActive = false;
    
    // 游戏模式：直接渲染场景，不走ImGui窗口
    if (gameMode)
    {
        // 如果项目未加载，尝试加载项目
        if (!projectLoaded && !gameProjectPath.empty())
        {
            std::filesystem::path projectFile = std::filesystem::path(gameProjectPath) / "project.bin";
            if (std::filesystem::exists(projectFile))
            {
                std::cout << "[Editor] Loading project from: " << projectFile.string() << std::endl;
                OpenProject(gameProjectPath);
                projectLoaded = true;
            }
            else
            {
                std::cerr << "[Editor] Project file not found: " << projectFile.string() << std::endl;
            }
        }
        
        if (engine)
            engine->state = Engine::Play;
        isSceneActive = true;
        
        // 游戏模式直接渲染，不使用ImGui
        int w = 0, h = 0;
        glfwGetFramebufferSize(engine->window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        
        if (engine && engine->scene && engine->shader)
        {
            Camera* cam = engine->gameCamera;
            mat4 view = cam->GetViewMatrix();
            mat4 projection = perspective(radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);
            engine->scene->Render(engine->shader, view, projection, cam->position, w, h);
        }
        
        return;
    }
    
    // 编辑器模式
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    
    // 全局 Ctrl+S 快捷键 - 保存当前场景
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveCurrentScene();
    }

    // 如果项目未加载，显示项目选择界面
    if (showProjectSelector)
    {
        DrawProjectSelector();
        
        // 新建项目弹窗
        if (showNewProjectPopup)
        {
            ImGui::OpenPopup("Create Project");
            showNewProjectPopup = false;
        }
        ProjectManager& pm = ProjectManager::GetInstance();
        if (ImGui::BeginPopupModal("Create Project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Project Name:"); ImGui::SameLine();
            ImGui::InputText("##ProjectName", projectNameBuffer, sizeof(projectNameBuffer));
            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (strlen(projectNameBuffer) > 0)
                {
                    if (pm.CreateProject(projectNameBuffer))
                    {
                        std::string newProjectPath = pm.GetAllProjects().back().path;
                        OpenProject(newProjectPath);
                        strcpy_s(projectNameBuffer, "MyProject");
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                strcpy_s(projectNameBuffer, "MyProject");
            }
            ImGui::EndPopup();
        }
        
        // 重命名项目弹窗
        if (showRenameProjectPopup)
        {
            ImGui::OpenPopup("Rename Project");
            showRenameProjectPopup = false;
        }
        
        if (ImGui::BeginPopupModal("Rename Project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("New Name:"); ImGui::SameLine();
            ImGui::InputText("##RenameProject", renameProjectBuffer, sizeof(renameProjectBuffer));
            
            if (ImGui::Button("Confirm", ImVec2(120, 0)))
            {
                if (strlen(renameProjectBuffer) > 0)
                {
                    try
                    {
                        fs::path oldPath(renameProjectOldPath);
                        fs::path newPath = oldPath.parent_path() / renameProjectBuffer;
                        fs::rename(oldPath, newPath);
                        std::cout << "[Editor] Renamed project: " << renameProjectOldPath << " -> " << newPath.string() << std::endl;
                    }
                    catch (const std::exception& e)
                    {
                        std::cerr << "Failed to rename project: " << e.what() << std::endl;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    // 设置全屏DockSpace
    SetupDocking();

    DrawToolbar();
    DrawHierarchy();
    DrawScene();
    DrawGame();
    if (m_projectWindow) m_projectWindow->Draw();
    if (m_inspectorWindow) m_inspectorWindow->Draw();
    DrawPopups();

    // DockSpace结束
    ImGui::End();

    ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (isSceneActive) engine->sceneCamera = engine->sceneCamera;
}

void Editor::SetupDocking()
{
    ImGuiIO& io = ImGui::GetIO();

    // 获取当前窗口大小
    float menuBarHeight = ImGui::GetFrameHeight();
    ImVec2 displaySize = io.DisplaySize;

    // 全屏窗口作为DockSpace宿主 - 动态适应窗口大小变化
    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, displaySize.y - menuBarHeight));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    bool open = true;
    ImGui::Begin("DockSpace", &open, window_flags);

    ImGui::PopStyleVar(3);

    // 创建DockSpace
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    dockSpaceID = dockspace_id;

    // 使用NoSplit标志防止手动分割，或使用Dockspace的默认行为
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // 只在首次运行时初始化默认布局
    if (!dockingInitialized)
    {
        // 加载INI后需要先应用Ini设置再构建Dock
        LayoutManager& lm = LayoutManager::GetInstance();
        if (lm.GetNeedsReloadDock())
        {
            // 加载了新的布局，不需要重建Dock，ImGui已经恢复了状态
            lm.ClearNeedsReloadDock();

            // 必须Finish DockSpace
            ImGui::DockBuilderFinish(dockspace_id);
            ImGui::End();
            return;
        }

        dockingInitialized = true;

        // 清除现有的dock布局以重新构建
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(displaySize.x, displaySize.y - menuBarHeight));

        // 分割DockSpace - 使用相对比例而不是固定大小
        ImGuiID dock_id_left, dock_id_right, dock_id_center;

        // 左侧面板占30%
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, &dock_id_left, &dock_id_center);
        // 右侧面板占30%（从剩余空间计算）
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Right, 0.42f, &dock_id_right, &dock_id_center);
        // 注意：现在dock_id_center是中间40%的区域

        // 左侧面板再分割为上下两个（各50%）
        ImGuiID dock_id_left_top, dock_id_left_bottom;
        ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Up, 0.5f, &dock_id_left_top, &dock_id_left_bottom);

        // 中间面板再分割为上下两个（各50%）
        ImGuiID dock_id_center_top, dock_id_center_bottom;
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Down, 0.5f, &dock_id_center_bottom, &dock_id_center_top);

        // 将窗口附加到Dock节点
        ImGui::DockBuilderDockWindow("Scene", dock_id_left_top);
        ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left_bottom);
        ImGui::DockBuilderDockWindow("Game", dock_id_center_top);
        ImGui::DockBuilderDockWindow("Project", dock_id_center_bottom);
        ImGui::DockBuilderDockWindow("Inspector", dock_id_right);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

void Editor::DrawToolbar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Build..."))
            {
                showBuildPopup = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Create Empty")) {}
            if (ImGui::BeginMenu("Create Geometry"))
            {
                if (ImGui::MenuItem("Cube")) {}
                if (ImGui::MenuItem("Sphere")) {}
                if (ImGui::MenuItem("Plane")) {}
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Create Light"))
            {
                // TODO: 实现创建光源
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Component"))
        {
            if (ImGui::MenuItem("Add Component...")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About Ditto Engine")) {}
            ImGui::EndMenu();
        }

        ImGui::Separator();

        float windowWidth = ImGui::GetWindowWidth();
        float buttonWidth = 60.0f;
        float spacing = 10.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        float startX = (windowWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);

        // Play/Pause 按钮
        if (engine->state == Engine::Edit)
        {
            // Edit 模式：显示绿色 Play 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            if (ImGui::Button("Play", ImVec2(buttonWidth, 0)))
            {
                // 保存当前场景到临时文件
                m_tempScenePath = "../../Ditto/Ditto/Temp/PlayModeScene.scene";
                std::filesystem::create_directories("../../Ditto/Ditto/Temp");
                engine->scene->SaveScene(m_tempScenePath);
                
                // 开始 Play 模式
                m_isPlaying = true;
                engine->SetEngineState(Engine::Play);
            }
            ImGui::PopStyleColor(2);
        }
        else if (engine->state == Engine::Play)
        {
            // Play 模式：显示蓝色 Pause 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
            if (ImGui::Button("Pause", ImVec2(buttonWidth, 0)))
            {
                engine->SetEngineState(Engine::Pause);
            }
            ImGui::PopStyleColor(2);
        }
        else if (engine->state == Engine::Pause)
        {
            // Pause 模式：显示绿色 Play 按钮（继续）
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            if (ImGui::Button("Play", ImVec2(buttonWidth, 0)))
            {
                engine->SetEngineState(Engine::Play);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + buttonWidth + spacing);

        // Stop 按钮（仅在 Play 或 Pause 模式下可用）
        if (engine->state == Engine::Edit)
        {
            // Edit 模式：Stop 按钮灰色不可用
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            ImGui::Button("Stop", ImVec2(buttonWidth, 0));
            ImGui::PopStyleColor(2);
        }
        else
        {
            // Play/Pause 模式：显示红色 Stop 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Stop", ImVec2(buttonWidth, 0)))
            {
                // 先停止物理模拟
                engine->SetEngineState(Engine::Stop);
                
                // 加载临时场景文件（恢复到 Play 前的状态）
                if (!m_tempScenePath.empty() && std::filesystem::exists(m_tempScenePath))
                {
                    engine->scene->LoadScene(m_tempScenePath);
                    
                    // 场景重新加载后，所有旧的 GameObject 指针都失效了
                    // 重置选择状态
                    selectedObject = nullptr;
                    activeSelection = nullptr;
                    selectedFile.Clear();
                    m_expandedGameObjects.clear();
                }
                
                // 结束 Play 模式，回到 Edit 状态
                m_isPlaying = false;
                engine->state = Engine::Edit;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::EndMainMenuBar();
    }
}

void Editor::DrawLayoutMenu()
{
    if (ImGui::BeginMenu("Layout"))
    {
        // Save Layout
        if (ImGui::MenuItem("Save Layout..."))
        {
            showSaveLayoutPopup = true;
        }

        ImGui::Separator();

        // Load Layout 子菜单
        if (ImGui::BeginMenu("Load Layout"))
        {
            std::vector<std::string> layouts = GetSavedLayouts();

            if (layouts.empty())
            {
                ImGui::TextDisabled("No saved layouts");
            }
            else
            {
                for (const auto& layoutName : layouts)
                {
                    if (ImGui::MenuItem(layoutName.c_str()))
                    {
                        LoadLayout(layoutName);
                    }
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

void Editor::SaveCurrentLayout()
{
    // 将当前窗口状态保存到LayoutManager
    // 这里我们只需要触发保存，实际内容在Draw函数中已经更新
}

void Editor::LoadLayout(const std::string& layoutName)
{
    // 加载布局 - 使用ImGui内置INI机制
    if (LayoutManager::GetInstance().LoadLayout(layoutName))
    {
        // 清除所有Dock节点，让ImGui可以重新应用INI中的布局
        ImGui::DockContextClearNodes(GImGui, 0, true); // root_id==0 表示清除所有节点，true 清除 settings 引用
        // 标记需要重建Dock
        dockingInitialized = false;
    }
}

std::vector<std::string> Editor::GetSavedLayouts()
{
    return LayoutManager::GetInstance().GetAllLayoutNames();
}

void Editor::DrawGameObjectNode(GameObject* obj, bool isRoot, int depth)
{
    ImGui::PushID(obj);

    bool hasChildren = !obj->children.empty();
    
    unsigned int icon = isRoot ? GetDittoIcon() : GetGameObjectIcon();
    
    // 计算缩进：每层 18px
    float indent = depth * 18.0f;
    
    if (hasChildren) {
        bool isExpanded = m_expandedGameObjects.find(obj) != m_expandedGameObjects.end();
        
        // 箭头按钮（12px）
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::ArrowButton(("##arrow_" + std::to_string((uintptr_t)obj)).c_str(), isExpanded ? ImGuiDir_Down : ImGuiDir_Right)) {
            if (isExpanded) m_expandedGameObjects.erase(obj);
            else m_expandedGameObjects.insert(obj);
        }
        ImGui::PopStyleVar();
        
        // 图标
        if (icon) {
            ImGui::SameLine();
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
        }
        
        // 名称
        ImGui::SameLine();
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        // 构建显示名称：根节点且有修改时添加星号
        std::string displayName = obj->name;
        if (isRoot && sceneDirty) {
            displayName += " *";
        }
        
        if (ImGui::Selectable(displayName.c_str(), activeSelection == obj)) {
            activeSelection = obj;
            if (!lockingSelection) {
                selectedObject = obj;
                selectedFile.Clear();
            }
        }
        
        if (isRoot) {
            ImGui::PopStyleColor();
        }
        
        // 拖动源（必须在Selectable之后）
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
            ImGui::Text("Move '%s'", obj->name.c_str());
            ImGui::EndDragDropSource();
        }

        // 拖动目标
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
            {
                GameObject* droppedObj = *(GameObject**)payload->Data;
                if (droppedObj && droppedObj != obj && !droppedObj->IsDescendantOf(obj))
                {
                    if (droppedObj->parent) droppedObj->RemoveFromParent();
                    else
                    {
                        auto& rootList = engine->scene->gameObjects;
                        auto it = std::find(rootList.begin(), rootList.end(), droppedObj);
                        if (it != rootList.end()) rootList.erase(it);
                    }
                    obj->AddChild(droppedObj);
                    engine->scene->MarkDirty();
                }
            }
            ImGui::EndDragDropTarget();
        }
        
        // 子物体
        if (isExpanded) {
            for (auto child : obj->children)
                DrawGameObjectNode(child, false, depth + 1);
        }
    } else {
        // 叶子节点：留出箭头位置（20px = 12px箭头 + 8px间距）+ 深度缩进
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
        
        if (icon) {
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SameLine();
        }
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        // 构建显示名称：根节点且有修改时添加星号
        std::string displayName = obj->name;
        if (isRoot && sceneDirty) {
            displayName += " *";
        }
        
        if (ImGui::Selectable(displayName.c_str(), activeSelection == obj)) {
            activeSelection = obj;
            if (!lockingSelection) {
                selectedObject = obj;
                selectedFile.Clear();
            }
        }
        
        if (isRoot) {
            ImGui::PopStyleColor();
        }
        
        // 拖动源（必须在Selectable之后）
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
            ImGui::Text("Move '%s'", obj->name.c_str());
            ImGui::EndDragDropSource();
        }

        // 拖动目标
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
            {
                GameObject* droppedObj = *(GameObject**)payload->Data;
                if (droppedObj && droppedObj != obj && !droppedObj->IsDescendantOf(obj))
                {
                    if (droppedObj->parent) droppedObj->RemoveFromParent();
                    else
                    {
                        auto& rootList = engine->scene->gameObjects;
                        auto it = std::find(rootList.begin(), rootList.end(), droppedObj);
                        if (it != rootList.end()) rootList.erase(it);
                    }
                    obj->AddChild(droppedObj);
                    engine->scene->MarkDirty();
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    
    // 对象右键菜单 - 根据当前选中的对象显示不同选项
    if (ImGui::BeginPopupContextItem(("GameObjectContext_" + std::to_string((uintptr_t)obj)).c_str()))
    {
        // 只有当选中的是场景根物体时才显示保存选项
        bool isSelectedRoot = (selectedObject == engine->scene->rootGameObject);
        if (isSelectedRoot)
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                SaveCurrentScene();
            }
            ImGui::Separator();
        }
        
        if (ImGui::MenuItem("Create Child"))
        {
            GameObject* newObj = new GameObject("New GameObject");
            obj->AddChild(newObj);
            selectedObject = newObj;
            selectedFile.Clear();
            engine->scene->MarkDirty();
        }
        if (ImGui::MenuItem("Copy")) CopySelectedObject();
        if (ImGui::MenuItem("Delete")) DeleteSelectedObject();
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void Editor::DrawHierarchy()
{
    ImGui::Begin("Hierarchy");
    
    ImGui::BeginChild("HierarchyContent", ImVec2(0, 0), true);

    // 空白处右键菜单 - 创建物体
    if (ImGui::BeginPopupContextWindow("HierarchyContextWindow"))
    {
        if (ImGui::MenuItem("Create Directional Light"))
        {
            GameObject* lightObj = new GameObject("DirLight");
            lightObj->AddComponent<LightComponent>();
            lightObj->GetComponent<TransformComponent>()->rotation[0] = -30.0f;
            lightObj->GetComponent<TransformComponent>()->UpdateTransform();
            if (engine->scene->rootGameObject)
            {
                engine->scene->rootGameObject->AddChild(lightObj);
            }
            else
            {
                engine->scene->gameObjects.push_back(lightObj);
            }
            selectedObject = lightObj;
            selectedFile.Clear();
            engine->scene->MarkDirty();
        }
        
        if (ImGui::BeginMenu("Create Geometry"))
        {
            if (ImGui::MenuItem("Create Cube"))
            {
                GameObject* cube = new GameObject("Cube");
                cube->AddComponent<RendererComponent>(RendererComponent::Type::Cube);
                if (engine->scene->rootGameObject)
                {
                    engine->scene->rootGameObject->AddChild(cube);
                }
                else
                {
                    engine->scene->gameObjects.push_back(cube);
                }
                selectedObject = cube;
                selectedFile.Clear();
                engine->scene->MarkDirty();
            }
            if (ImGui::MenuItem("Create Sphere"))
            {
                GameObject* sphere = new GameObject("Sphere");
                sphere->AddComponent<RendererComponent>(RendererComponent::Type::Sphere);
                if (engine->scene->rootGameObject)
                {
                    engine->scene->rootGameObject->AddChild(sphere);
                }
                else
                {
                    engine->scene->gameObjects.push_back(sphere);
                }
                selectedObject = sphere;
                selectedFile.Clear();
                engine->scene->MarkDirty();
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
        {
            GameObject* droppedObj = *(GameObject**)payload->Data;
            if (droppedObj)
            {
                if (droppedObj->parent) droppedObj->RemoveFromParent();
                else
                {
                    auto& rootList = engine->scene->gameObjects;
                    auto it = std::find(rootList.begin(), rootList.end(), droppedObj);
                    if (it != rootList.end()) rootList.erase(it);
                }
                if (engine->scene->rootGameObject && droppedObj != engine->scene->rootGameObject)
                {
                    engine->scene->rootGameObject->AddChild(droppedObj);
                }
                else
                {
                    engine->scene->gameObjects.push_back(droppedObj);
                    droppedObj->parent = nullptr;
                }
                engine->scene->MarkDirty();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (engine->scene->rootGameObject)
    {
        DrawGameObjectNode(engine->scene->rootGameObject, true);
    }
    else
    {
        for (GameObject* obj : engine->scene->gameObjects) DrawGameObjectNode(obj, false);
    }

    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Hierarchy", pos, size, true, collapsed);
    }

    ImGui::EndChild();
    ImGui::End();
}

void Editor::DrawScene()
{
    if (m_sceneWindow)
        m_sceneWindow->Draw();
}

void Editor::DrawGame()
{
    // 设置透明背景 - 确保在Begin之前设置
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("Game", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // 获取窗口渲染区域并调用引擎渲染Game视图（游戏运行视角）
    ImRect gameViewportRect = GetCurrentViewportRect();
    ImGui::GetWindowDrawList()->PushClipRect(gameViewportRect.Min, gameViewportRect.Max, true);
    engine->RenderSceneToViewport(gameViewportRect, true);
    ImGui::GetWindowDrawList()->PopClipRect();

    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Game", pos, size, true, collapsed);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

// DrawProject 和 DrawInspector 已移至 ProjectWindow.cpp 和 InspectorWindow.cpp
// 保留空实现以保持 API 兼容

void Editor::DrawPopups()
{
    if (showSavePopup)
    {
        ImGui::OpenPopup("Save Scene");
        showSavePopup = false;
    }

    if (ImGui::BeginPopupModal("Save Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Path"); ImGui::SameLine();
        static char savePathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", savePathBuffer, sizeof(savePathBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            if (engine && engine->scene && engine->scene->SaveScene(savePathBuffer))
                ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (showLoadPopup)
    {
        ImGui::OpenPopup("Load Scene");
        showLoadPopup = false;
    }

    if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        selectedObject = nullptr;
        selectedFile.Clear();  // 清除文件选中
        ImGui::Text("Path"); ImGui::SameLine();
        static char loadPathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", loadPathBuffer, sizeof(loadPathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0)))
        {
            if (engine && engine->scene && engine->scene->LoadScene(loadPathBuffer))
            {
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  // 新加载的场景没有修改
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Save Layout Popup
    if (showSaveLayoutPopup)
    {
        ImGui::OpenPopup("Save Layout");
        showSaveLayoutPopup = false;
    }

    if (ImGui::BeginPopupModal("Save Layout", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Layout Name:"); ImGui::SameLine();
        ImGui::InputText("##LayoutName", layoutNameBuffer, sizeof(layoutNameBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            if (strlen(layoutNameBuffer) > 0)
            {
                // 保存到文件 - 使用ImGui内置INI保存
                if (LayoutManager::GetInstance().SaveLayout(layoutNameBuffer))
                {
                    ImGui::CloseCurrentPopup();
                    strcpy_s(layoutNameBuffer, "Default");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            strcpy_s(layoutNameBuffer, "Default");
        }
        ImGui::EndPopup();
    }

    // Build Popup - 打包发布
    if (showBuildPopup)
    {
        ImGui::OpenPopup("Build Project");
        showBuildPopup = false;
    }

    if (ImGui::BeginPopupModal("Build Project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Select scene to build:");
        ImGui::Separator();
        
        std::vector<std::string> scenes = GetProjectScenes();
        static int selectedScene = 0;
        
        if (scenes.empty())
        {
            ImGui::TextDisabled("No scenes found");
        }
        else
        {
            for (int i = 0; i < scenes.size(); i++)
            {
                fs::path p(scenes[i]);
                std::string sceneName = p.stem().string();
                if (ImGui::Selectable(sceneName.c_str(), selectedScene == i))
                {
                    selectedScene = i;
                }
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Build", ImVec2(120, 0)))
        {
            if (!scenes.empty())
            {
                BuildProject();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::CopySelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;
    GameObject* newObj = new GameObject(selectedObject);
    newObj->parent = nullptr;
    if (selectedObject->parent)
        selectedObject->parent->AddChild(newObj);
    else
        engine->scene->gameObjects.push_back(newObj);
    selectedObject = newObj;
    engine->scene->MarkDirty();  // 标记场景已修改
}

void Editor::DeleteSelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;

    if (selectedObject == engine->scene->rootGameObject)
    {
        return;
    }

    GameObject* parent = selectedObject->parent;
    bool wasRoot = (parent == nullptr);

    if (parent)
        parent->RemoveChild(selectedObject);
    else
    {
        auto& rootList = engine->scene->gameObjects;
        auto it = std::find(rootList.begin(), rootList.end(), selectedObject);
        if (it != rootList.end()) rootList.erase(it);
    }

    delete selectedObject;

    if (parent)
        selectedObject = parent;
    else if (!engine->scene->gameObjects.empty())
        selectedObject = engine->scene->gameObjects.back();
    else
        selectedObject = nullptr;
    
    engine->scene->MarkDirty();  // 标记场景已修改
}

void Editor::DeleteSelectedFile()
{
    if (!selectedFile.IsValid()) return;

    try {
        fs::remove(selectedFile.path);
        selectedFile.Clear();
    }
    catch (const std::exception& e) {
        std::cerr << "Delete file failed: " << e.what() << std::endl;
    }
}

void Editor::DuplicateSelectedFile()
{
    if (!selectedFile.IsValid()) return;

    // 构建新文件名：name_copy.ext
    std::string newName = selectedFile.name + "_copy" + selectedFile.extension;
    std::string newPath = selectedFile.path;
    size_t pos = newPath.rfind(selectedFile.name + selectedFile.extension);
    if (pos != std::string::npos) {
        newPath.replace(pos, selectedFile.name.size() + selectedFile.extension.size(), newName);
    }

    try {
        fs::copy_file(selectedFile.path, newPath, fs::copy_options::overwrite_existing);
        // 选中新复制的文件
        selectedFile.path = newPath;
        selectedFile.name = selectedFile.name + "_copy";
    }
    catch (const std::exception& e) {
        std::cerr << "Duplicate file failed: " << e.what() << std::endl;
    }
}

// 项目选择界面
void Editor::DrawProjectSelector()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("ProjectSelector", nullptr, flags);

    // 标题 - 居中
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y;
    
    ImGui::SetCursorPosX((windowWidth - 200) * 0.5f);
    ImGui::SetCursorPosY(windowHeight * 0.15f);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("Ditto Engine");
    ImGui::SetWindowFontScale(1.0f);

    // 项目列表 - 居中
    ProjectManager& pm = ProjectManager::GetInstance();
    auto projects = pm.GetAllProjects();
    static int selectedProject = -1;

    float listWidth = windowWidth * 0.4f;
    if (listWidth < 300) listWidth = 300;
    if (listWidth > 600) listWidth = 600;
    float listHeight = windowHeight * 0.4f;
    
    ImGui::SetCursorPosX((windowWidth - listWidth) * 0.5f);
    ImGui::SetCursorPosY(windowHeight * 0.3f);
    ImGui::BeginChild("ProjectList", ImVec2(listWidth, listHeight), true);

    if (projects.empty())
    {
        ImGui::TextDisabled("No projects yet.");
    }
    else
    {
        for (int i = 0; i < projects.size(); i++)
        {
            if (ImGui::Selectable(projects[i].name.c_str(), selectedProject == i))
            {
                selectedProject = i;
            }
        }
    }
    
    ImGui::EndChild();

    // 按钮 - 居中，一字排列，与列表框对齐
    float buttonWidth = listWidth / 4 - 10;
    float buttonSpacing = 10;
    float buttonsWidth = buttonWidth * 4 + buttonSpacing * 3;
    float buttonsStartX = (windowWidth - buttonsWidth) * 0.5f;

    ImGui::SetCursorPosX(buttonsStartX);
    ImGui::SetCursorPosY(windowHeight * 0.75f);
    if (ImGui::Button("Create", ImVec2(buttonWidth, 35)))
    {
        showNewProjectPopup = true;
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttonsStartX + buttonWidth + buttonSpacing);
    if (ImGui::Button("Delete", ImVec2(buttonWidth, 35)))
    {
        if (selectedProject >= 0 && selectedProject < projects.size())
        {
            try
            {
                fs::remove_all(projects[selectedProject].path);
                selectedProject = -1;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Failed to delete project: " << e.what() << std::endl;
            }
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttonsStartX + (buttonWidth + buttonSpacing) * 2);
    if (ImGui::Button("Rename", ImVec2(buttonWidth, 35)))
    {
        if (selectedProject >= 0 && selectedProject < projects.size())
        {
            renameProjectOldPath = projects[selectedProject].path;
            strcpy_s(renameProjectBuffer, sizeof(renameProjectBuffer), projects[selectedProject].name.c_str());
            showRenameProjectPopup = true;
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttonsStartX + (buttonWidth + buttonSpacing) * 3);
    if (ImGui::Button("Open", ImVec2(buttonWidth, 35)))
    {
        if (selectedProject >= 0 && selectedProject < projects.size())
        {
            OpenProject(projects[selectedProject].path);
        }
    }

    // 保存选中的项目索引
    static int lastSelectedProject = -1;
    if (lastSelectedProject != selectedProject)
    {
        lastSelectedProject = selectedProject;
    }

    ImGui::End();
}

void Editor::OpenProject(const std::string& projectPath)
{
    ProjectManager& pm = ProjectManager::GetInstance();
    if (pm.OpenProject(projectPath))
    {
        projectLoaded = true;
        showProjectSelector = false;

        // 加载上次场景（如果有）
        Project* proj = pm.GetCurrentProject();
        if (proj && !proj->lastScene.empty())
        {
            std::string fullPath = proj->path + "/" + proj->lastScene;
            std::cout << "[Editor] Loading last scene: " << fullPath << std::endl;
            
            if (engine && engine->scene)
            {
                if (engine->scene->LoadScene(fullPath.c_str()))
                {
                    std::cout << "[Editor] Scene loaded successfully: " << engine->scene->name << std::endl;
                    std::cout << "[Editor] GameObject count: " << engine->scene->gameObjects.size() << std::endl;
                    if (engine->scene->rootGameObject)
                    {
                        std::cout << "[Editor] RootGameObject children: " << engine->scene->rootGameObject->children.size() << std::endl;
                    }
                }
                else
                {
                    std::cerr << "[Editor] Failed to load scene: " << fullPath << std::endl;
                    // 创建默认场景
                    engine->scene->ClearScene();
                    engine->scene->name = "Default";
                    engine->scene->rootGameObject = new GameObject("Default");
                }
                
                // 更新 UI
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  // 新加载的场景没有修改
                
                // 重新设置场景修改回调
                engine->scene->onModified = [this]() {
                    this->sceneDirty = true;
                };
            }
        }
        else
        {
            std::cout << "[Editor] No last scene to load, creating default scene" << std::endl;
            // 创建默认场景
            if (engine && engine->scene)
            {
                engine->scene->ClearScene();
                engine->scene->name = "Default";
                engine->scene->rootGameObject = new GameObject("Default");
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;
                engine->scene->onModified = [this]() {
                    this->sceneDirty = true;
                };
            }
        }
    }
}

void Editor::LoadSceneFromProject(const std::string& scenePath)
{
    if (engine && engine->scene)
    {
        engine->scene->LoadScene(scenePath.c_str());
        
        // 更新 UI
        strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
        sceneDirty = false;  // 新加载的场景没有修改
        
        // 重新设置场景修改回调
        engine->scene->onModified = [this]() {
            this->sceneDirty = true;
        };

        // 保存到项目配置
        Project* proj = ProjectManager::GetInstance().GetCurrentProject();
        if (proj)
        {
            // 提取相对路径
            size_t pos = scenePath.find("/Assets/");
            if (pos != std::string::npos)
            {
                proj->lastScene = scenePath.substr(pos + 1);

                // 保存到project.json
                std::string projectFile = proj->path + "/project.json";
                // TODO: 更新project.json中的lastScene
            }
        }
    }
}

std::vector<std::string> Editor::GetProjectScenes()
{
    std::vector<std::string> scenes;
    ProjectManager& pm = ProjectManager::GetInstance();
    std::string scenesPath = pm.GetProjectScenesPath();

    try
    {
        if (fs::exists(scenesPath))
        {
            for (const auto& entry : fs::directory_iterator(scenesPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".bin")
                {
                    scenes.push_back(entry.path().string());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading scenes: " << e.what() << std::endl;
    }

    return scenes;
}

void Editor::OnScriptComponentDropped(const std::string& scriptPath)
{
    if (!selectedObject)
    {
        std::cout << "[Editor] No object selected to add script" << std::endl;
        return;
    }

    OnScriptComponentDroppedToObject(selectedObject, scriptPath);
}

void Editor::OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath)
{
    if (!obj)
    {
        std::cout << "[Editor] No object to add script" << std::endl;
        return;
    }

    // 检查文件类型
    fs::path p(scriptPath);
    std::string ext = p.extension().string();
    std::string scriptName = p.stem().string();
    
    std::cout << "[Editor] Adding script: " << scriptName << " (" << ext << ") to " << obj->name << std::endl;
    
    if (ext == ".cs")
    {
        // C# 脚本 - 创建 CSharpScriptComponent
        CSharpScriptComponent* csScript = new CSharpScriptComponent();
        csScript->scriptPath = scriptPath;
        csScript->scriptName = std::filesystem::path(scriptPath).filename().stem().string();
        csScript->gameObject = obj;
        csScript->ParseScriptFields();
        
        obj->components.push_back(csScript);
        obj->compMask += csScript->index;
        std::cout << "[Editor] C# script added: " << csScript->scriptName << std::endl;
        
        // 标记场景为已修改
        sceneDirty = true;
    }
}

void Editor::SaveCurrentScene()
{
    if (!engine || !engine->scene)
    {
        std::cerr << "[Editor] No scene to save" << std::endl;
        return;
    }

    // 获取当前项目路径
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    std::string savePath;
    
    if (proj)
    {
        // 保存到项目目录下的 Assets/Scenes/
        savePath = proj->path + "/Assets/Scenes/" + engine->scene->name + ".bin";
    }
    else
    {
        // 没有项目时，使用相对路径
        savePath = "Assets/Scenes/" + engine->scene->name + ".bin";
    }
    
    std::cout << "[Editor] Saving scene to: " << savePath << std::endl;
    
    if (engine->scene->SaveScene(savePath.c_str()))
    {
        std::cout << "[Editor] Scene saved successfully" << std::endl;
        sceneDirty = false;  // 清除修改标记
        
        // 更新项目配置
        if (proj)
        {
            // 保存相对路径到项目配置
            proj->lastScene = "Assets/Scenes/" + engine->scene->name + ".bin";
        }
    }
    else
    {
        std::cerr << "[Editor] Failed to save scene" << std::endl;
    }
}

void Editor::BuildProject()
{
    if (!engine || !engine->scene)
    {
        std::cerr << "[Editor] No scene to build" << std::endl;
        return;
    }

    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        std::cerr << "[Editor] No project loaded" << std::endl;
        return;
    }

    // 输出目录：项目目录下的 Build/Windows
    std::string projectPath = proj->path;
    std::replace(projectPath.begin(), projectPath.end(), '\\', '/');
    std::string outputDir = projectPath + "/Build/Windows";
    
    try
    {
        // 创建输出目录
        if (!fs::exists(outputDir))
        {
            fs::create_directories(outputDir);
        }

        // 1. 复制 Assets 目录内容到根目录
        std::string assetsSrc = projectPath + "/Assets";
        
        if (fs::exists(assetsSrc))
        {
            // 复制整个 Assets 目录
            std::string assetsDst = outputDir;
            fs::remove_all(assetsDst + "/Assets");
            fs::copy(assetsSrc, assetsDst + "/Assets", fs::copy_options::recursive);
            std::cout << "[Editor] Copied Assets to " << assetsDst << std::endl;
        }

        // 2. 保存当前场景
        std::string sceneName = engine->scene->name;
        std::string sceneSrc = projectPath + "/Assets/Scenes/" + sceneName + ".bin";
        
        // 确保目录存在
        fs::create_directories(outputDir + "/Assets/Scenes");
        
        // 保存并复制场景
        engine->scene->SaveScene(sceneSrc.c_str());
        if (fs::exists(sceneSrc))
        {
            std::string sceneDst = outputDir + "/Assets/Scenes/" + sceneName + ".bin";
            fs::copy(sceneSrc, sceneDst, fs::copy_options::overwrite_existing);
            std::cout << "[Editor] Copied scene: " << sceneDst << std::endl;
        }

        // 复制 project.json 到根目录
        std::string projectJsonSrc = projectPath + "/project.json";
        if (fs::exists(projectJsonSrc))
        {
            fs::copy(projectJsonSrc, outputDir + "/project.json", fs::copy_options::overwrite_existing);
            std::cout << "[Editor] Copied project.json to " << outputDir << std::endl;
        }

        // 3. 复制可执行文件
        std::string exeSrc = projectPath + "/../../x64/Debug/Ditto.exe";
        std::string exeDst = outputDir + "/" + proj->name + ".exe";
        if (fs::exists(exeSrc))
        {
            fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
            std::cout << "[Editor] Copied executable: " << exeDst << std::endl;
        }
        else
        {
            exeSrc = projectPath + "/../../../x64/Debug/Ditto.exe";
            if (fs::exists(exeSrc))
            {
                fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
                std::cout << "[Editor] Copied executable: " << exeDst << std::endl;
            }
            else
            {
                exeSrc = projectPath + "/../../Ditto/x64/Debug/Ditto.exe";
                if (fs::exists(exeSrc))
                {
                    fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
                    std::cout << "[Editor] Copied executable: " << exeDst << std::endl;
                }
            }
        }

        // 4. 复制 3rdParty DLL
        std::string thirdPartySrc = projectPath + "/../../Ditto/3rdParty/GLFW";
        if (!fs::exists(thirdPartySrc))
        {
            thirdPartySrc = projectPath + "/../../../Ditto/3rdParty/GLFW";
        }
        if (!fs::exists(thirdPartySrc))
        {
            thirdPartySrc = projectPath + "/../../3rdParty/GLFW";
        }
        
        if (fs::exists(thirdPartySrc))
        {
            // 复制 glfw3.dll
            for (const auto& entry : fs::directory_iterator(thirdPartySrc))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".dll")
                {
                    std::string dllDst = outputDir + "/" + entry.path().filename().string();
                    fs::copy(entry.path(), dllDst, fs::copy_options::overwrite_existing);
                    std::cout << "[Editor] Copied DLL: " << dllDst << std::endl;
                }
            }
        }

        // 5. 创建启动脚本（Run.bat）
        std::string batPath = outputDir + "/Run.bat";
        std::ofstream batFile(batPath);
        batFile << "@echo off\n";
        batFile << "echo Starting " << proj->name << "...\n";
        batFile << "cd /d \"%~dp0\"\n";
        batFile << "\"" << proj->name << ".exe\"\n";
        batFile << "pause\n";
        batFile.close();
        std::cout << "[Editor] Created startup script: " << batPath << std::endl;
        
        std::cout << "[Editor] Build completed: " << outputDir << std::endl;
        
        // 6. 在资源管理器中打开输出目录
        std::wstring outputDirW = fs::absolute(outputDir).wstring();
        ShellExecuteW(NULL, L"open", outputDirW.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Editor] Build failed: " << e.what() << std::endl;
    }
}

void Editor::BuildScripts()
{
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        std::cerr << "[Editor] No project loaded" << std::endl;
        return;
    }
    
    // 脚本目录
    std::string scriptsDir = proj->path + "/Assets/Scripts";
    std::string outputDll = proj->path + "/Scripts.dll";
    
    if (!fs::exists(scriptsDir))
    {
        std::cout << "[Editor] Scripts directory does not exist: " << scriptsDir << std::endl;
        return;
    }
    
    // 收集所有 .cpp 文件
    std::vector<std::string> cppFiles;
    for (const auto& entry : fs::directory_iterator(scriptsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp")
        {
            cppFiles.push_back(entry.path().string());
        }
    }
    
    if (cppFiles.empty())
    {
        std::cout << "[Editor] No script files found" << std::endl;
        return;
    }
    
    std::cout << "[Editor] Building " << cppFiles.size() << " script(s)..." << std::endl;
    
    // 转换为绝对路径
    fs::path projPathAbs = fs::absolute(proj->path);
    // E:\Engine Source\Ditto\Ditto\Projects\MyProject -> ../.. = E:\Engine Source\Ditto\Ditto
    fs::path enginePathAbs = fs::absolute(proj->path + "/../..");
    fs::path outputDllAbs = fs::absolute(outputDll);
    
    // 直接使用原始脚本文件（用户需要手动修改 include 路径）
    std::string compileFiles;
    for (const auto& f : cppFiles)
    {
        compileFiles += "\"" + fs::absolute(f).string() + "\" ";
    }
    
    // 创建编译脚本
    std::string batPath = proj->path + "/build_scripts.bat";
    std::ofstream batFile(batPath);
    
    batFile << "@echo off\n";
    batFile << "cd /d \"" << projPathAbs.string() << "\"\n";
    std::string vcvarsPath = FindVCVarsPath();
    if (!vcvarsPath.empty())
        batFile << "call \"" << vcvarsPath << "\"\n";
    else
        batFile << "echo Warning: vcvars64.bat not found, compilation may fail\n";
    
    // 使用绝对路径，添加 C++20 和更多头文件路径
    std::string clCmd = "cl /LD /EHsc /std:c++latest /I\"" + enginePathAbs.string() + "\\3rdParty\\GLM\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\\include\" /I\"" + enginePathAbs.string() + "\\3rdParty\\ImGui\" /I\"" + enginePathAbs.string() + "\\Engine\\Core\" /I\"" + enginePathAbs.string() + "\\Engine\\Graphics\" /I\"" + enginePathAbs.string() + "\\Engine\\Physics\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\" /I\"" + enginePathAbs.string() + "\" /D\"SCRIPT_DLL\" /O2 /MD " + compileFiles + "/Fe:\"" + outputDllAbs.string() + "\"";
    
    batFile << clCmd << "\n";
    batFile << "pause\n";
    
    batFile.close();
    
    std::cout << "[Editor] Please run: " << batPath << std::endl;
    std::cout << "[Editor] Or manually compile your scripts and place DLL at: " << outputDll << std::endl;
    
    // 尝试直接执行
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    PROCESS_INFORMATION pi;
    
    std::string cmd = "cmd /c \"" + batPath + "\"";
    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        // 检查 DLL 是否生成
        if (fs::exists(outputDll))
        {
            std::cout << "[Editor] DLL built: " << outputDll << std::endl;
        }
        else
        {
            std::cout << "[Editor] DLL not found after build. Check the console window for errors." << std::endl;
        }
    }
}


// 模型预览已移至 InspectorWindow.cpp
// 文件图标相关函数
static const char* s_iconFiles[] = {
    "Default.png", "Cs.png", "Model.png", "Prefab.png", "Shader.png", "Scene.png", "Folder.png"
};

void Editor::InitFileIcons()
{
    if (m_fileIconsInitialized) return;
    
    // 获取图标目录路径
    m_assetsPath = FindEditorAssetsPath() + "/Icon";
    std::cout << "[FileIcon] Initializing from: " << m_assetsPath << std::endl;
    
    // 加载文件图标
    for (int i = 0; i < 7; i++) {
        std::string path = m_assetsPath + "/" + s_iconFiles[i];
        m_icons[i] = LoadIcon(path);
    }
    
    // 加载文件夹图标
    m_folderIcon = LoadIcon(m_assetsPath + "/Folder.png");
    m_folderEmptyIcon = LoadIcon(m_assetsPath + "/FolderEmpty.png");
    m_folderOpenedIcon = LoadIcon(m_assetsPath + "/FolderOpened Icon.png");
    
    // 加载特殊图标
    m_dittoIcon = LoadIcon(m_assetsPath + "/Scene.png");
    m_gameObjectIcon = LoadIcon(m_assetsPath + "/GameObject.png");
    
    // 加载锁定图标
    m_lockIcon = LoadIcon(m_assetsPath + "/Lock.png");
    m_unlockIcon = LoadIcon(m_assetsPath + "/UnLock.png");
    
    m_fileIconsInitialized = true;
    std::cout << "[FileIcon] Initialized successfully" << std::endl;
}

unsigned int Editor::LoadIcon(const std::string& iconPath)
{
    namespace fs = std::filesystem;
    
    if (!fs::exists(iconPath)) {
        std::cerr << "[FileIcon] File not found: " << iconPath << std::endl;
        return 0;
    }
    
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        std::cerr << "[FileIcon] Failed to load: " << iconPath << std::endl;
        return 0;
    }
    
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    stbi_image_free(data);
    
    std::cout << "[FileIcon] Loaded: " << iconPath << " (" << width << "x" << height << ")" << std::endl;
    return textureID;
}

int Editor::GetIconIndex(const std::string& ext)
{
    if (ext == ".cs") return 1;  // Cs.png
    if (ext == ".obj") return 2;  // Prefab.png (模型)
    if (ext == ".prefab") return 3;  // Text.png (材质)
    if (ext == ".shader") return 4;  // Shader.png
    if (ext == ".bin") return 5;  // Scene.png
    if (ext == ".tga") return 6;  // Folder.png (纹理)
    return 0;  // 默认 Default.png
}

unsigned int Editor::GetIconByExtension(const std::string& extension)
{
    if (!m_fileIconsInitialized) {
        std::cerr << "[FileIcon] Not initialized!" << std::endl;
        return 0;
    }
    
    int idx = GetIconIndex(extension);
    return m_icons[idx];  // GetIconIndex 已经返回 0-6 范围
}

void Editor::CleanupFileIcons()
{
    if (!m_fileIconsInitialized) return;
    
    for (int i = 0; i < 7; i++) {
        if (m_icons[i]) {
            glDeleteTextures(1, &m_icons[i]);
            m_icons[i] = 0;
        }
    }
    
    if (m_folderIcon) {
        glDeleteTextures(1, &m_folderIcon);
        m_folderIcon = 0;
    }
    
    if (m_folderEmptyIcon) {
        glDeleteTextures(1, &m_folderEmptyIcon);
        m_folderEmptyIcon = 0;
    }
    
    m_fileIconsInitialized = false;
    std::cout << "[FileIcon] Cleaned up" << std::endl;
}