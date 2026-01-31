#include "Editor.h"
#include "../Engine/Core/Engine.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui_impl_glfw.h"
#include "../3rdParty/ImGui/imgui_impl_opengl3.h"

Editor::Editor(void* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow*)window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    showHierarchy = true; showScene = true; showInspector = true; showSavePopup = false; showLoadPopup = false;
}

Editor::~Editor()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    // Begin Frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 绘制 Toolbar
    DrawToolbar();
    if (showHierarchy) DrawHierarchy();
    if (showScene) DrawScene();
    if (showInspector) DrawInspector();

    DrawPopups();
    // End Frame
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::DrawToolbar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                showSavePopup = true;
            }
            if (ImGui::MenuItem("Load Scene", "Ctrl+L"))
            {
                showLoadPopup = true;
            }
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
            if (ImGui::Button("Play"))
            {
                engine->state = Engine::State::Play;
            }
        }
        else if (engine->state == Engine::State::Play) 
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Stop")) 
            {
                engine->state = Engine::State::Edit;
            }
		}
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Pause")) 
        {
            engine->state = Engine::State::Stop;
        }

        float windowWidth = ImGui::GetWindowWidth(), infoWidth = 300.0f;
        ImGui::SameLine(windowWidth - infoWidth); ImGui::Text("Scene:"); ImGui::SameLine();

        ImGui::PushItemWidth(150.0f);
        if (ImGui::InputText("##SceneName", sceneNameBuffer, sizeof(sceneNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
            if (engine && engine->scene) engine->scene->name = sceneNameBuffer;
        ImGui::PopItemWidth();

        ImGui::EndMainMenuBar();
    }
}

void Editor::DrawHierarchy()
{
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125 * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(hierarchyWidth, windowHeight));
    ImGui::Begin("Hierarchy");

    if (ImGui::BeginPopupContextWindow())
    {
        if (ImGui::MenuItem("Create Cube"))
        {
            GameObject* cube = new GameObject("Cube");
            cube->AddComponent<RendererComponent>(RendererComponent::Type::Cube);
            engine->scene->gameObjects.push_back(cube); selectedObject = cube;
        }
        if (ImGui::MenuItem("Create Sphere"))
        {
            GameObject* sphere = new GameObject("Sphere");
            sphere->AddComponent<RendererComponent>(RendererComponent::Type::Sphere);
            engine->scene->gameObjects.push_back(sphere); selectedObject = sphere;
        }
        if (ImGui::MenuItem("Create Plane"))
        {
            GameObject* plane = new GameObject("Plane");
            plane->AddComponent<RendererComponent>(RendererComponent::Type::Plane);
            engine->scene->gameObjects.push_back(plane); selectedObject = plane;
        }
        ImGui::EndPopup();
    }

    for (auto obj : engine->scene->gameObjects)
    {
        bool isSelected = (selectedObject == obj);

        ImGui::PushID(obj);
        if (ImGui::Selectable(obj->name.c_str(), isSelected)) selectedObject = obj;
        ImGui::PopID();

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelectedObject();
            if (ImGui::MenuItem("Delete", "Delete")) DeleteSelectedObject();
        }
    }

    ImGui::End();
}

void Editor::DrawScene()
{
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125 * windowWidth;
    float sceneWidth = 0.625 * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(hierarchyWidth, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(sceneWidth, windowHeight));
    ImGui::Begin("Scene");
    ImGui::Text("Scene View");
    ImGui::End();
}

void Editor::DrawInspector()
{
    float menuBarHeight = ImGui::GetFrameHeight();
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y - menuBarHeight;
    float hierarchyWidth = 0.125 * windowWidth;
    float sceneWidth = 0.625 * windowWidth;
    float inspectorWidth = 0.25 * windowWidth;

    ImGui::SetNextWindowPos(ImVec2(hierarchyWidth + sceneWidth, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(inspectorWidth, windowHeight));
    ImGui::Begin("Inspector");

    if (!selectedObject) { ImGui::End(); return; }
    else
    {
        if (engine->state == Engine::State::Play) ImGui::BeginDisabled();
        selectedObject->OnInspectorGUI();
        if (engine->state == Engine::State::Play) ImGui::EndDisabled(); ImGui::End();
    }
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
            if (engine && engine->scene)
            {
                if (engine->scene->SaveScene(savePathBuffer))
                {
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showLoadPopup)
    {
        ImGui::OpenPopup("Load Scene");
        showLoadPopup = false;
    }

    if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Path"); ImGui::SameLine();
        static char loadPathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", loadPathBuffer, sizeof(loadPathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0)))
        {
            if (engine && engine->scene)
            {
                if (engine->scene->LoadScene(loadPathBuffer))
                {
                    strcpy_s(sceneNameBuffer, engine->scene->name.c_str());
                    ImGui::CloseCurrentPopup();
                }
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
    if (selectedObject && engine && engine->scene)
    {
        GameObject* newObj = new GameObject(selectedObject);
        engine->scene->gameObjects.push_back(newObj); selectedObject = newObj;
    }
}

void Editor::DeleteSelectedObject()
{
    if (selectedObject && engine && engine->scene)
    {
        auto& gameObjects = engine->scene->gameObjects;
        gameObjects.erase(std::remove(gameObjects.begin(), gameObjects.end(), selectedObject), gameObjects.end());
        delete selectedObject; selectedObject = engine->scene->gameObjects.empty() ? nullptr : engine->scene->gameObjects.back();
    }
}