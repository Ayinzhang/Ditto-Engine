#define IMGUI_DEFINE_MATH_OPERATORS
#include "Editor.h"
#include "LayoutManager.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../3rdParty/ImGui/imgui_impl_opengl3.h"
#include "../3rdParty/ImGui/imgui_internal.h"
#include <filesystem>

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

    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    showSavePopup = false;
    showLoadPopup = false;
    showSaveLayoutPopup = false;
    dockingInitialized = false;
    frame = deltaTime = 0;
    
    // 初始化布局管理器
    LayoutManager::GetInstance().Initialize("../../Ditto/Ditto/Assets/Settings");
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
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    
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
        
        // 中间面板再分割为上下两个（上70%，下30%）
        ImGuiID dock_id_center_top, dock_id_center_bottom;
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Down, 0.3f, &dock_id_center_bottom, &dock_id_center_top);
        
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
    // 重置Docking以允许新布局生效
    dockingInitialized = false;
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
        LayoutManager::GetInstance().SaveCurrentWindowState("Hierarchy", pos, size, true, collapsed);
    }

    ImGui::End();
}

void Editor::DrawScene()
{
    ImGui::SetNextWindowBgAlpha(0.0f); // 背景完全透明，不遮挡OpenGL渲染
    ImGui::Begin("Scene");
    
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
        LayoutManager::GetInstance().SaveCurrentWindowState("Scene", pos, size, true, collapsed);
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
}

void Editor::DrawGame()
{
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Game");
    
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
        LayoutManager::GetInstance().SaveCurrentWindowState("Game", pos, size, true, collapsed);
    }

    ImGui::End();
}

void Editor::DrawProject()
{
    ImGui::Begin("Project");
    
    ImGui::Text("Project View");

    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        LayoutManager::GetInstance().SaveCurrentWindowState("Project", pos, size, true, collapsed);
    }

    ImGui::End();
}

void Editor::DrawInspector()
{
    ImGui::Begin("Inspector");

    if (!selectedObject) { ImGui::End(); return; }

    if (engine->state == Engine::State::Play) ImGui::BeginDisabled();
    selectedObject->OnInspectorGUI();
    if (engine->state == Engine::State::Play) ImGui::EndDisabled();
    
    // 保存窗口状态
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        LayoutManager::GetInstance().SaveCurrentWindowState("Inspector", pos, size, true, collapsed);
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
                // 收集当前窗口状态并保存
                LayoutManager& lm = LayoutManager::GetInstance();
                
                // 更新当前布局名称
                lm.GetCurrentLayout().name = layoutNameBuffer;
                
                // 保存到文件
                if (lm.SaveLayout(layoutNameBuffer))
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