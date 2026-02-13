#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../3rdParty/ImGui/imgui_impl_opengl3.h"
#include <algorithm>

Editor::Editor(void* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    showHierarchy = true;
    showScene = true;
    showInspector = true;
    showSavePopup = false;
    showLoadPopup = false;
    frame = deltaTime = 0;
}

Editor::~Editor()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    DrawToolbar();
    if (showHierarchy) DrawHierarchy();
    if (showScene) DrawScene();
    if (showInspector) DrawInspector();
    DrawPopups();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::DrawToolbar()
{
    // ... 完全保持原样，无改动 ...
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) showSavePopup = true;
            if (ImGui::MenuItem("Load Scene", "Ctrl+L")) showLoadPopup = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Toggle Hierarchy", NULL, showHierarchy)) showHierarchy = !showHierarchy;
            if (ImGui::MenuItem("Toggle Scene", NULL, showScene)) showScene = !showScene;
            if (ImGui::MenuItem("Toggle Inspector", NULL, showInspector)) showInspector = !showInspector;
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

// ========== 递归绘制节点 + 拖拽 ==========
void Editor::DrawGameObjectNode(GameObject* obj)
{
    ImGui::PushID(obj);

    bool hasChildren = !obj->children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selectedObject == obj) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool nodeOpen = ImGui::TreeNodeEx(obj->name.c_str(), flags);

    // --- 拖拽源 ---
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
        ImGui::Text("Move '%s'", obj->name.c_str());
        ImGui::EndDragDropSource();
    }

    // --- 拖拽目标（成为子物体）---
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
        {
            GameObject* droppedObj = *(GameObject**)payload->Data;
            if (droppedObj && droppedObj != obj && !droppedObj->IsDescendantOf(obj))
            {
                if (droppedObj->parent)
                    droppedObj->RemoveFromParent();
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

    // --- 右键菜单 ---
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
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125f * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(hierarchyWidth, windowHeight));
    ImGui::Begin("Hierarchy");

    // --- 空白处右键菜单（创建根物体）---
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

    // --- 空白处拖拽目标（将物体拖到空白处，成为根物体）---
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
        {
            GameObject* droppedObj = *(GameObject**)payload->Data;
            if (droppedObj)
            {
                // 从原父移除
                if (droppedObj->parent)
                    droppedObj->RemoveFromParent();
                else
                {
                    // 已在根列表，先移除（避免重复）
                    auto& rootList = engine->scene->gameObjects;
                    auto it = std::find(rootList.begin(), rootList.end(), droppedObj);
                    if (it != rootList.end()) rootList.erase(it);
                }
                // 添加到根列表
                engine->scene->gameObjects.push_back(droppedObj);
                droppedObj->parent = nullptr;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // 绘制所有根物体
    for (GameObject* obj : engine->scene->gameObjects)
        DrawGameObjectNode(obj);

    ImGui::End();
}

void Editor::DrawScene()
{
    // ... 完全保持原样 ...
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125f * windowWidth;
    float sceneWidth = 0.625f * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(hierarchyWidth, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(sceneWidth, windowHeight));
    ImGui::Begin("Scene");
    ImGui::Text("Scene View");

    frame++; deltaTime += engine->deltaTime;
    if (deltaTime > 1.0f)
    {
        fps = frame / deltaTime;
        ppf = 1e6f * engine->physicsTime / engine->physicsCnt;
        frame = 0; deltaTime = 0; engine->physicsCnt = 0; engine->physicsTime = 0;
    }
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

void Editor::DrawInspector()
{
    // ... 完全保持原样 ...
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125f * windowWidth;
    float sceneWidth = 0.625f * windowWidth;
    float inspectorWidth = 0.25f * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(hierarchyWidth + sceneWidth, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(inspectorWidth, windowHeight));
    ImGui::Begin("Inspector");

    if (!selectedObject)
    {
        ImGui::End();
        return;
    }

    if (engine->state == Engine::State::Play) ImGui::BeginDisabled();
    selectedObject->OnInspectorGUI();
    if (engine->state == Engine::State::Play) ImGui::EndDisabled();

    ImGui::End();
}

void Editor::DrawPopups()
{
    // ... 完全保持原样（已包含 selectedObject = nullptr 处理）...
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
        selectedObject = nullptr;   // 加载时清空选中
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

    delete selectedObject;   // 递归删除所有子物体

    if (parent)
        selectedObject = parent;
    else if (!engine->scene->gameObjects.empty())
        selectedObject = engine->scene->gameObjects.back();
    else
        selectedObject = nullptr;
}