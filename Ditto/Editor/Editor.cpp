#define IMGUI_DEFINE_MATH_OPERATORS
#include <fstream>
#include <iostream>
#include <filesystem>
#include "Editor.h"
#include "LayoutManager.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../3rdParty/ImGui/imgui_impl_opengl3.h"
#include "../3rdParty/ImGui/imgui_internal.h"

namespace fs = std::filesystem;

static ImRect GetCurrentViewportRect()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImRect(ImVec2(0, 0), ImVec2(0, 0));
    ImVec2 min = window->InnerRect.Min;
    ImVec2 max = window->InnerRect.Max;
    return ImRect(min, max);
}

Editor::Editor(void* window)
{
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
    showProjectSelector = true;  // 启动时显示项目选择
    projectLoaded = false;
    dockingInitialized = false;
    frame = deltaTime = 0;
    
    // 初始化布局管理器
    LayoutManager::GetInstance().Initialize("../../Ditto/Ditto/Assets/Settings");
    
    // 初始化项目管理器
    ProjectManager::GetInstance().Initialize("../../Ditto/Ditto/Projects");
}

Editor::~Editor()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    isSceneActive = false;
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    // 如果项目未加载，显示项目选择界面
    if (showProjectSelector)
    {
        DrawProjectSelector();
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
    DrawProject();
    DrawInspector();
    DrawPopups();
    
    // DockSpace结束
    ImGui::End();

    ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if(isSceneActive) engine->sceneCamera = engine->sceneCamera;
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
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) showSavePopup = true;
            if (ImGui::MenuItem("Load Scene", "Ctrl+L")) showLoadPopup = true;
            ImGui::EndMenu();
        }
        // View菜单 - 包含Layout功能
        if (ImGui::BeginMenu("View"))
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

        ImGui::SameLine(ImGui::GetWindowWidth() * 0.4f);
        if (engine->state == Engine::State::Edit)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
            if (ImGui::Button("Play")) engine->SetEngineState(Engine::State::Play);
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Stop")) engine->SetEngineState(Engine::State::Edit);
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        if (engine->state != Engine::State::Stop)
        {
            if (ImGui::Button("Pause")) engine->SetEngineState(Engine::State::Stop);
        }
        else if (ImGui::Button("Conti"))
            engine->SetEngineState(Engine::State::Play);

        float windowWidth = ImGui::GetWindowWidth(), infoWidth = 300.0f;
        ImGui::SameLine(windowWidth - infoWidth);
        ImGui::Text("Scene:"); ImGui::SameLine();

        ImGui::PushItemWidth(150.0f);
        if (ImGui::InputText("##SceneName", sceneNameBuffer, sizeof(sceneNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
            if (engine && engine->scene) engine->scene->name = sceneNameBuffer;
        ImGui::PopItemWidth();

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

void Editor::DrawGameObjectNode(GameObject* obj)
{
    ImGui::PushID(obj);

    bool hasChildren = !obj->children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selectedObject == obj) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool nodeOpen = ImGui::TreeNodeEx(obj->name.c_str(), flags);

    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
        ImGui::Text("Move '%s'", obj->name.c_str());
        ImGui::EndDragDropSource();
    }

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
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        selectedObject = obj;

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Create Child"))
        {
            GameObject* newObj = new GameObject("New GameObject");
            obj->AddChild(newObj);
            selectedObject = newObj;
        }
        if (ImGui::MenuItem("Copy")) CopySelectedObject();
        if (ImGui::MenuItem("Delete")) DeleteSelectedObject();
        ImGui::EndPopup();
    }

    if (hasChildren && nodeOpen)
    {
        for (auto child : obj->children)
            DrawGameObjectNode(child);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void Editor::DrawHierarchy()
{
    ImGui::Begin("Hierarchy");

    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::MenuItem("Create Cube"))
        {
            GameObject* cube = new GameObject("Cube");
            cube->AddComponent<RendererComponent>(RendererComponent::Type::Cube);
            engine->scene->gameObjects.push_back(cube);
            selectedObject = cube;
        }
        if (ImGui::MenuItem("Create Sphere"))
        {
            GameObject* sphere = new GameObject("Sphere");
            sphere->AddComponent<RendererComponent>(RendererComponent::Type::Sphere);
            engine->scene->gameObjects.push_back(sphere);
            selectedObject = sphere;
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
                engine->scene->gameObjects.push_back(droppedObj);
                droppedObj->parent = nullptr;
            }
        }
        ImGui::EndDragDropTarget();
    }

    for (GameObject* obj : engine->scene->gameObjects) DrawGameObjectNode(obj);

    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Hierarchy", pos, size, true, collapsed);
    }

    ImGui::End();
}

void Editor::DrawScene()
{
    // 设置透明背景 - 确保在Begin之前设置
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;
    
    if (!ImGui::Begin("Scene", nullptr, flags))
    {
        ImGui::End();
        return;
    }
    
    // 只有Scene窗口获得焦点时才激活Scene相机控制
    if (ImGui::IsWindowFocused()) isSceneActive = true;
    
    // 获取窗口渲染区域并调用引擎渲染Scene视图（编辑器视角）
    ImRect sceneViewportRect = GetCurrentViewportRect();
    ImGui::GetWindowDrawList()->PushClipRect(sceneViewportRect.Min, sceneViewportRect.Max, true);
    engine->RenderSceneToViewport(sceneViewportRect, false);
    ImGui::GetWindowDrawList()->PopClipRect();

    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Scene", pos, size, true, collapsed);
    }

    // 避免文字被遮挡，用ForegroundDrawList
    frame++; deltaTime += engine->deltaTime;
    if (deltaTime > 1.0f)
    {
        fps = frame / deltaTime;
        ppf = 1e6f * engine->physicsTime / engine->physicsCnt;
        frame = 0; deltaTime = 0; engine->physicsCnt = 0; engine->physicsTime = 0;
    }
    else if (deltaTime < 0) deltaTime = 0;
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();

    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(windowPos.x + windowSize.x - 80, windowPos.y + 20),
        IM_COL32(0, 255, 0, 255), ("FPS: " + std::to_string((int)fps)).c_str()
    );
    if (engine->state == Engine::State::Play)
        ImGui::GetForegroundDrawList()->AddText(
            ImVec2(windowPos.x + windowSize.x - 80, windowPos.y + 40),
            IM_COL32(0, 255, 0, 255), ("PPF: " + std::to_string((int)ppf)).c_str()
        );
    ImGui::End();
    ImGui::PopStyleColor();
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

void Editor::DrawProject()
{
    ProjectManager& pm = ProjectManager::GetInstance();
    Project* proj = pm.GetCurrentProject();
    
    if (!proj)
    {
        ImGui::Begin("Project");
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        return;
    }
    
    std::string assetsPath = pm.GetProjectAssetsPath();
    static std::string currentFolder = "Assets/Scenes";
    static float splitterPos = 150.0f;
    
    ImGui::Begin("Project");
    
    // 顶部路径栏
    ImGui::Text("Project");
    ImGui::SameLine();
    ImGui::Text(" > ");
    ImGui::SameLine();
    ImGui::Text(currentFolder.c_str());
    ImGui::Separator();
    
    float panelWidth = ImGui::GetContentRegionAvail().x;
    float panelHeight = ImGui::GetContentRegionAvail().y;
    
    // 确保splitterPos在合理范围内
    if (splitterPos < 100) splitterPos = 100;
    if (splitterPos > panelWidth - 100) splitterPos = panelWidth - 100;
    
    // 左边 - 文件夹树（无边框）
    ImGui::BeginChild("Folders", ImVec2(splitterPos, panelHeight), false);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    
    // Assets
    if (ImGui::TreeNode("Assets"))
    {
        ImGui::Indent(0, 0.5f);   // 缩进一半
        
        // Scenes
        bool isSelected_Scenes = (currentFolder == "Assets/Scenes");
        if (ImGui::Selectable("Scenes", isSelected_Scenes)) {
            currentFolder = "Assets/Scenes";
            selectedFile.Clear();  // 切换文件夹时清除选中
        }
        
        // Models
        bool isSelected_Models = (currentFolder == "Assets/Models");
        if (ImGui::Selectable("Models", isSelected_Models)) {
            currentFolder = "Assets/Models";
            selectedFile.Clear();
        }
        
        // Materials
        bool isSelected_Materials = (currentFolder == "Assets/Materials");
        if (ImGui::Selectable("Materials", isSelected_Materials)) {
            currentFolder = "Assets/Materials";
            selectedFile.Clear();
        }
        
        // Prefabs
        bool isSelected_Prefabs = (currentFolder == "Assets/Prefabs");
        if (ImGui::Selectable("Prefabs", isSelected_Prefabs)) {
            currentFolder = "Assets/Prefabs";
            selectedFile.Clear();
        }
        
        ImGui::Unindent(0, 0.5f);
        ImGui::TreePop();
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    
    // 分隔条 - 细线
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.5f));
    ImGui::Button("##splitter", ImVec2(1, panelHeight));
    ImGui::PopStyleColor();
    
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    
    // 拖动分隔条
    if (ImGui::IsItemActive())
    {
        splitterPos += ImGui::GetIO().MouseDelta.x;
        if (splitterPos < 100) splitterPos = 100;
        if (splitterPos > panelWidth - 100) splitterPos = panelWidth - 100;
    }
    
    ImGui::SameLine();
    
    // 右边 - 文件视图（无边框）
    ImGui::BeginChild("View", ImVec2(0, panelHeight), false);
    
    // 获取当前文件夹路径
    std::string folderPath = assetsPath;
    size_t pos = currentFolder.find('/');
    if (pos != std::string::npos)
    {
        folderPath = assetsPath + "/" + currentFolder.substr(pos + 1);
    }
    
    // 右键菜单 - 未选中文件时
    if (ImGui::BeginPopupContextWindow("ProjectContext"))
    {
        if (!selectedFile.IsValid())
        {
            if (ImGui::MenuItem("Create New..."))
            {
                // TODO: 创建新资源的逻辑
            }
        }
        ImGui::EndPopup();
    }
    
    // 文件网格显示
    float itemWidth = 80;
    float itemHeight = 80;
    float currentX = 0;
    float availWidth = ImGui::GetContentRegionAvail().x;
    
    try
    {
        if (fs::exists(folderPath))
        {
            for (const auto& entry : fs::directory_iterator(folderPath))
            {
                if (entry.is_regular_file())
                {
                    std::string filename = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    
                    // 换行
                    if (currentX + itemWidth > availWidth)
                    {
                        ImGui::NewLine();
                        currentX = 0;
                    }
                    
                    // 文件项
                    ImGui::BeginGroup();
                    
                    // 判断是否选中
                    bool isSelected = selectedFile.IsValid() && (selectedFile.path == entry.path().string());
                    
                    // 文件图标区域
                    ImVec2 cursorPos = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - 40) / 2, cursorPos.y));
                    
                    // 根据选中状态改变颜色
                    if (isSelected)
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[]");
                    else if (ext == ".bin")
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "[]");
                    else if (ext == ".obj" || ext == ".fbx")
                        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[]");
                    else if (ext == ".mat")
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "[]");
                    else
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[]");
                    
                    // 文件名
                    std::string displayName = filename;
                    if (ext == ".bin") displayName = filename.substr(0, filename.size() - 4);
                    if (displayName.size() > 10) displayName = displayName.substr(0, 8) + "..";
                    
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - ImGui::CalcTextSize(displayName.c_str()).x) / 2, cursorPos.y + 50));
                    
                    // 选中时显示不同颜色
                    if (isSelected)
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), displayName.c_str());
                    else
                        ImGui::Text(displayName.c_str());
                    
                    // 点击处理
                    ImGui::SetCursorPos(cursorPos);
                    ImGui::InvisibleButton(("file_" + filename).c_str(), ImVec2(itemWidth, itemHeight));
                    
                    // 选中文件
                    if (ImGui::IsItemClicked(0))
                    {
                        selectedFile.path = entry.path().string();
                        selectedFile.name = filename.substr(0, filename.size() - ext.size());
                        selectedFile.extension = ext;
                        selectedFile.folder = currentFolder;
                    }
                    
                    // 右键菜单 - 选中文件时
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Delete"))
                        {
                            // 删除文件
                            try {
                                fs::remove(entry.path());
                                if (selectedFile.path == entry.path().string())
                                    selectedFile.Clear();
                            } catch (const std::exception& e) {
                                std::cerr << "Delete failed: " << e.what() << std::endl;
                            }
                        }
                        if (ImGui::MenuItem("Show in Explorer"))
                        {
                            // 在文件管理器中显示
                            std::string cmd = "explorer /select,\"" + entry.path().string() + "\"";
                            system(cmd.c_str());
                        }
                        ImGui::EndPopup();
                    }
                    
                    // 双击处理
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        if (ext == ".bin")
                        {
                            LoadSceneFromProject(entry.path().string());
                        }
                    }
                    
                    currentX += itemWidth;
                    ImGui::EndGroup();
                    ImGui::SameLine();
                }
            }
        }
    }
    catch (const std::exception&) {}
    
    ImGui::EndChild();

    ImGui::End();
}

// 辅助函数：解析OBJ文件获取模型信息
struct ModelInfo {
    int vertexCount = 0;
    int faceCount = 0;
    bool loaded = false;
};

static ModelInfo LoadModelInfo(const std::string& path)
{
    ModelInfo info;
    std::ifstream file(path);
    if (!file.is_open()) return info;
    
    std::string line;
    int vertexCount = 0;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;
        
        if (prefix == "v") vertexCount++;
        else if (prefix == "f") info.faceCount++;
    }
    
    info.vertexCount = vertexCount;
    info.loaded = true;
    return info;
}

void Editor::DrawInspector()
{
    ImGui::Begin("Inspector");

    // 优先显示文件信息（如果选中了文件）
    if (selectedFile.IsValid())
    {
        // 文件基本信息
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "File");
        ImGui::Separator();
        
        ImGui::Text("Name: "); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), selectedFile.name.c_str());
        
        ImGui::Text("Path: "); ImGui::SameLine();
        ImGui::TextDisabled(selectedFile.path.c_str());
        
        // 如果是模型文件，显示模型信息
        if (selectedFile.extension == ".obj" || selectedFile.extension == ".fbx")
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Model");
            ImGui::Separator();
            
            ModelInfo modelInfo = LoadModelInfo(selectedFile.path);
            if (modelInfo.loaded)
            {
                ImGui::Text("Vertices: "); ImGui::SameLine();
                ImGui::Text(std::to_string(modelInfo.vertexCount).c_str());
                
                ImGui::Text("Faces: "); ImGui::SameLine();
                ImGui::Text(std::to_string(modelInfo.faceCount).c_str());
            }
            
            // 预览图占位（可后续接入缩略图）
            float previewWidth = ImGui::GetContentRegionAvail().x;
            if (previewWidth > 0)
            {
                ImGui::Separator();
                ImGui::Text("Preview");
                ImVec2 cursor = ImGui::GetCursorPos();
                float btnWidth = std::min(previewWidth - 20, 150.0f);
                float btnHeight = btnWidth * 0.75f;
                ImGui::SetCursorPos(ImVec2(cursor.x + (previewWidth - btnWidth) / 2, cursor.y));
                ImGui::Button("##preview", ImVec2(btnWidth, btnHeight));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Preview not available");
            }
        }
        
        ImGui::End();
        return;
    }

    // 没有选中文件时，显示 GameObject 信息
    if (!selectedObject) {
        ImGui::TextDisabled("Select an object to view its properties");
        ImGui::End();
        return;
    }

    if (engine->state == Engine::State::Play) ImGui::BeginDisabled();
    selectedObject->OnInspectorGUI();
    if (engine->state == Engine::State::Play) ImGui::EndDisabled();
    
    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Inspector", pos, size, true, collapsed);
    }

    ImGui::End();
}

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
        ImGui::Text("Path"); ImGui::SameLine();
        static char loadPathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", loadPathBuffer, sizeof(loadPathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0)))
        {
            if (engine && engine->scene && engine->scene->LoadScene(loadPathBuffer))
            {
                strcpy_s(sceneNameBuffer, engine->scene->name.c_str());
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
}

void Editor::DeleteSelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;

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
}

void Editor::DeleteSelectedFile()
{
    if (!selectedFile.IsValid()) return;
    
    try {
        fs::remove(selectedFile.path);
        selectedFile.Clear();
    } catch (const std::exception& e) {
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
    } catch (const std::exception& e) {
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
    
    // 标题
    float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX((windowWidth - 200) * 0.5f);
    ImGui::SetCursorPosY(100);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("Ditto Engine");
    ImGui::SetWindowFontScale(1.0f);
    
    // 项目列表
    ProjectManager& pm = ProjectManager::GetInstance();
    auto projects = pm.GetAllProjects();
    static int selectedProject = -1;
    
    ImGui::SetCursorPosY(180);
    float listWidth = 400;
    float listHeight = 250;
    ImGui::SetCursorPosX((windowWidth - listWidth) * 0.5f);
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
    
    // 按钮 - 居中，与列表框对齐
    float buttonWidth = 150;
    float buttonSpacing = 20;
    float buttonsWidth = buttonWidth * 2 + buttonSpacing;
    float buttonsStartX = (windowWidth - buttonsWidth) * 0.5f;
    
    ImGui::SetCursorPosX(buttonsStartX);
    if (ImGui::Button("Create", ImVec2(buttonWidth, 40)))
    {
        showNewProjectPopup = true;
    }
    
    ImGui::SameLine();
    ImGui::SetCursorPosX(buttonsStartX + buttonWidth + buttonSpacing);
    if (ImGui::Button("Open", ImVec2(buttonWidth, 40)))
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
    
    // 新建项目弹窗
    if (showNewProjectPopup)
    {
        ImGui::OpenPopup("Create Project");
        showNewProjectPopup = false;
    }
    
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
                    // 打开新创建的项目
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
            if (engine && engine->scene)
            {
                engine->scene->LoadScene(fullPath.c_str());
            }
        }
    }
}

void Editor::LoadSceneFromProject(const std::string& scenePath)
{
    if (engine && engine->scene)
    {
        engine->scene->LoadScene(scenePath.c_str());
        
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