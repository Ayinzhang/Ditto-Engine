#define IMGUI_DEFINE_MATH_OPERATORS
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <shlobj.h>
#include <windows.h>
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/GLM/gtc/matrix_transform.hpp"
#include "Editor.h"
#include "LayoutManager.h"
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "SceneWindow.h"
#include "BuildSystem.h"
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
using namespace glm;
namespace fs = std::filesystem;

// Helper function to find editor assets directory
static std::string FindEditorAssetsPath()
{
    const std::vector<std::string> possiblePaths = {
        "Assets",
        "Ditto/Assets",
        "Ditto/Ditto/Assets",
        "../Ditto/Assets",
        "../../Ditto/Ditto/Assets",
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path + "/Settings") || fs::exists(path + "/Icon")) return path;
    }
    
    std::cerr << "[Editor] Warning: Editor assets not found, using default" << std::endl;
    return "Assets";
}

// Helper function to find VS vcvars64.bat
static std::string FindVCVarsInVSInstallDir()
{
    char* vsPath = nullptr;
    size_t vsPathLen = 0;
    if (_dupenv_s(&vsPath, &vsPathLen, "VSINSTALLDIR") == 0 && vsPath)
    {
        std::string vcvarsPath = std::string(vsPath) + "VC\\Auxiliary\\Build\\vcvars64.bat";
        free(vsPath);
        if (fs::exists(vcvarsPath))
            return vcvarsPath;
    }
    return "";
}

static std::string FindVCVarsViaVsWhere()
{
    const char* regKey = "SOFTWARE\\Microsoft\\VisualStudio\\Setup\\Instances";
    HKEY hKey = nullptr;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char name[MAX_PATH];
        DWORD index = 0;
        DWORD nameSize = MAX_PATH;

        while (RegEnumKeyExA(hKey, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            std::string instanceKey = std::string(regKey) + "\\" + name;
            HKEY hInstKey = nullptr;

            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, KEY_READ, &hInstKey) == ERROR_SUCCESS)
            {
                char installPath[MAX_PATH];
                DWORD size = MAX_PATH;
                DWORD type = REG_SZ;

                if (RegQueryValueExA(hInstKey, "InstallLocation", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS)
                {
                    std::string vcvarsPath = std::string(installPath) + "VC\\Auxiliary\\Build\\vcvars64.bat";
                    if (fs::exists(vcvarsPath))
                    {
                        RegCloseKey(hInstKey);
                        RegCloseKey(hKey);
                        return vcvarsPath;
                    }
                }
                RegCloseKey(hInstKey);
            }
            nameSize = MAX_PATH;
        }
        RegCloseKey(hKey);
    }
    return "";
}

static std::string FindVCVarsPath()
{
    std::string result = FindVCVarsInVSInstallDir();
    if (!result.empty())
        return result;

    result = FindVCVarsViaVsWhere();
    if (!result.empty())
        return result;

    std::cerr << "[Editor] Warning: vcvars64.bat not found, compilation may fail" << std::endl;
    return "";
}

// Global Editor pointer
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
    // Set global Editor pointer
    g_editor = this;
    
    // Initialize selection state
    activeSelection = nullptr;
    this->gameMode = gameMode;
    this->gameProjectPath = projectPath;
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Enable Docking
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Set transparent background for Docking system
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

    // Initialize ProjectManager
    ProjectManager::GetInstance().Initialize("../../Ditto/Ditto/Projects");
    
    // Initialize LayoutManager
    std::string editorAssetsPath = FindEditorAssetsPath();
    LayoutManager::GetInstance().Initialize(editorAssetsPath + "/Settings");
    
    // Show project selector (only when not in game mode)
    showProjectSelector = !gameMode;

    // NOTE: model preview + file icons are initialized lazily on the first
    // Draw(), NOT here: they create GPU resources through engine->renderer,
    // but `engine` is only assigned by the caller AFTER this constructor runs.

    // Initialize window components
    m_projectWindow = new ProjectWindow(this);
    m_inspectorWindow = new InspectorWindow(this);
    m_sceneWindow = new SceneWindow(this);
    
    // Set script log callback
    CSharpScriptSystem::SetEditor(this);

    // Set scene modified callback (auto mark dirty)
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

    // Cleanup window components
    delete m_projectWindow;
    delete m_inspectorWindow;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    isSceneActive = false;

    // Lazy init: by the first Draw, engine (and engine->renderer) is set, so the
    // icon textures can be created through the RHI. Idempotent.
    InitFileIcons();

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    
    // Global Ctrl+S shortcut - save current scene
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveCurrentScene();
    }

    // If project not loaded, show project selector
    if (showProjectSelector)
    {
        DrawProjectSelector();
        
        // New project popup
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
        
        // Rename project popup
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

    // Setup fullscreen DockSpace
    SetupDocking();

    DrawToolbar();
    DrawHierarchy();
    DrawScene();
    DrawGame();
    if (m_projectWindow) m_projectWindow->Draw();
    if (m_projectWindow) m_projectWindow->DrawConsoleWindow();
    if (m_inspectorWindow) m_inspectorWindow->Draw();
    DrawPopups();
    DrawBuildSettingsWindow();

    // DockSpace end
    ImGui::End();

    ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (isSceneActive) engine->sceneCamera = engine->sceneCamera;
}

void Editor::SetupDocking()
{
    ImGuiIO& io = ImGui::GetIO();

    // Get current window size
    float menuBarHeight = ImGui::GetFrameHeight();
    ImVec2 displaySize = io.DisplaySize;

    // Fullscreen window as DockSpace host - dynamically adapt to window size changes
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

    // Create DockSpace
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    dockSpaceID = dockspace_id;

    // Use NoSplit flag to prevent manual splitting, or use DockSpace default behavior
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // Initialize default layout only on first run
    if (!dockingInitialized)
    {
        // After loading INI, need to apply Ini settings before building Dock
        LayoutManager& lm = LayoutManager::GetInstance();
        if (lm.GetNeedsReloadDock())
        {
            // Loaded new layout, no need to rebuild Dock, ImGui has already restored state
            lm.ClearNeedsReloadDock();

            // Must Finish DockSpace
            ImGui::DockBuilderFinish(dockspace_id);
            ImGui::End();
            return;
        }

        dockingInitialized = true;

        // Clear existing dock layout to rebuild
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(displaySize.x, displaySize.y - menuBarHeight));

        // Split DockSpace - use relative ratios instead of fixed sizes
        ImGuiID dock_id_left, dock_id_right, dock_id_center;

        // Left panel 30%
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, &dock_id_left, &dock_id_center);
        // Right panel 30% (calculated from remaining space)
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Right, 0.42f, &dock_id_right, &dock_id_center);
        // Note: now dock_id_center is the middle 40% region

        // Left panel split into top and bottom (50% each)
        ImGuiID dock_id_left_top, dock_id_left_bottom;
        ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Up, 0.5f, &dock_id_left_top, &dock_id_left_bottom);

        // Center panel split into top and bottom (50% each)
        ImGuiID dock_id_center_top, dock_id_center_bottom;
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Down, 0.5f, &dock_id_center_bottom, &dock_id_center_top);

        // Attach windows to Dock nodes
        ImGui::DockBuilderDockWindow("Scene", dock_id_left_top);
        ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left_bottom);
        ImGui::DockBuilderDockWindow("Game", dock_id_center_top);
        ImGui::DockBuilderDockWindow("Project", dock_id_center_bottom);
        // Console shares Project's dock node by default: appears as a tab next to
        // Project but is an independent, draggable window.
        ImGui::DockBuilderDockWindow("Console", dock_id_center_bottom);
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
            if (ImGui::MenuItem("Build Settings..."))
            {
                showBuildSettingsWindow = true;
                // Initialize build settings
                Project* proj = ProjectManager::GetInstance().GetCurrentProject();
                if (proj)
                {
                    buildSettings.productName = proj->name;
                    buildSettings.outputPath = BuildSystem::GetDefaultOutputPath(proj->path);
                    buildSettings.scenes = ::BuildSystem::GetProjectScenes(proj->path);
                    if (!buildSettings.scenes.empty())
                    {
                        buildSettings.startupScene = fs::path(buildSettings.scenes[0]).stem().string();
                    }
                }
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
                // TODO: Implement light creation
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
        float buttonSize = 32.0f;
        float iconSize = 20.0f;
        float spacing = 10.0f;
        float totalWidth = buttonSize * 2 + spacing;
        float startX = (windowWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);

        // Button palette. Grey variants match the menu bar; the two blues are
        // the standard ImGui accent colors used for an "active" button feel.
        // We always override the three Button* colors to neutralize ImGui's
        // translucent light-blue Button default; the decision of "grey vs blue"
        // is made per-button below.
        const ImVec4 grey       = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
        const ImVec4 greyHover  = ImVec4(grey.x + 0.06f, grey.y + 0.06f, grey.z + 0.06f, 1.0f);
        const ImVec4 greyActive = ImVec4(grey.x + 0.03f, grey.y + 0.03f, grey.z + 0.03f, 1.0f);
        const ImVec4 blue       = ImVec4(0.26f, 0.59f, 1.00f, 1.00f);
        const ImVec4 blueHover  = ImVec4(0.31f, 0.65f, 1.00f, 1.00f);
        const ImVec4 blueActive = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);

        // ---- Left button: Play ----
        // Blue when the engine is currently Play OR Pause (i.e. any "in-session"
        // state); grey otherwise.
        const bool playOn = (engine->state == Engine::Play || engine->state == Engine::Pause);
        if (playOn)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        blue);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blueHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  blueActive);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        grey);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, greyHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  greyActive);
        }
        if (ImGui::ImageButton("##PlayBtn", (ImTextureID)GetPlayIcon(),
                               ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
        {
            if (engine->state == Engine::Edit)
            {
                // Save current scene to temp file before entering Play mode
                m_tempScenePath = "../../Ditto/Ditto/Temp/PlayModeScene.scene";
                std::filesystem::create_directories("../../Ditto/Ditto/Temp");
                engine->scene->SaveScene(m_tempScenePath);

                engine->SetEngineState(Engine::Play);
            }
            else if (engine->state == Engine::Play)
            {
                // Already playing — clicking Play returns to Edit (acts as Stop)
                engine->SetEngineState(Engine::Stop);
                StopAndRestoreScene();
            }
            else if (engine->state == Engine::Pause)
            {
                // Paused — clicking Play also returns to Edit (acts as Stop);
                // both buttons should go grey to signal "session ended".
                engine->SetEngineState(Engine::Stop);
                StopAndRestoreScene();
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + buttonSize + spacing);

        // ---- Right button: Pause ----
        // Blue ONLY when the engine is currently Paused; grey otherwise.
        const bool pauseOn = (engine->state == Engine::Pause);
        if (pauseOn)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        blue);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blueHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  blueActive);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        grey);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, greyHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  greyActive);
        }
        if (ImGui::ImageButton("##PauseBtn", (ImTextureID)GetPauseIcon(),
                               ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
        {
            if (engine->state == Engine::Play)
            {
                engine->SetEngineState(Engine::Pause);
            }
            else if (engine->state == Engine::Pause)
            {
                // Already paused — clicking Pause resumes Play
                engine->SetEngineState(Engine::Play);
            }
            // In Edit state: do nothing (Pause has no meaning before Play)
        }
        ImGui::PopStyleColor(3);

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

        // Load Layout submenu
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
    // Save current window state to LayoutManager
    // We only need to trigger save, the actual content has been updated in Draw function
}

void Editor::LoadLayout(const std::string& layoutName)
{
    // Load layout - use ImGui built-in INI mechanism
    if (LayoutManager::GetInstance().LoadLayout(layoutName))
    {
        // Clear all Dock nodes so ImGui can reapply layout from INI
        ImGui::DockContextClearNodes(GImGui, 0, true); // root_id==0 means clear all nodes, true clears settings references
        // Mark dock for rebuild
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
    
    void* icon = isRoot ? GetDittoIcon() : GetGameObjectIcon();
    
    // Calculate indent: 18px per level
    float indent = depth * 18.0f;
    
    if (hasChildren) {
        bool isExpanded = m_expandedGameObjects.find(obj) != m_expandedGameObjects.end();
        
        // Arrow button (12px)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::ArrowButton(("##arrow_" + std::to_string((uintptr_t)obj)).c_str(), isExpanded ? ImGuiDir_Down : ImGuiDir_Right)) {
            if (isExpanded) m_expandedGameObjects.erase(obj);
            else m_expandedGameObjects.insert(obj);
        }
        ImGui::PopStyleVar();
        
        // Icon
        if (icon) {
            ImGui::SameLine();
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
        }
        
        // Name
        ImGui::SameLine();
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        // Build display name: add asterisk for root node when modified
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
        
        // Drag source (must be after Selectable)
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
            ImGui::Text("Move '%s'", obj->name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag target
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
            {
                GameObject* droppedObj = *(GameObject**)payload->Data;
                if (droppedObj && droppedObj != obj && !droppedObj->IsDescendantOf(obj))
                {
                    PushUndoSnapshot();
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
        
        // Children
        if (isExpanded) {
            for (auto child : obj->children)
                DrawGameObjectNode(child, false, depth + 1);
        }
    } else {
        // Leaf node: leave space for arrow (20px = 12px arrow + 8px spacing) + depth indent
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
        
        if (icon) {
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SameLine();
        }
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        // Build display name: add asterisk for root node when modified
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
        
        // Drag source (must be after Selectable)
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("GAMEOBJECT", &obj, sizeof(GameObject*));
            ImGui::Text("Move '%s'", obj->name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drag target
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT"))
            {
                GameObject* droppedObj = *(GameObject**)payload->Data;
                if (droppedObj && droppedObj != obj && !droppedObj->IsDescendantOf(obj))
                {
                    PushUndoSnapshot();
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
    
    // Object right-click menu - show different options based on current selection
    if (ImGui::BeginPopupContextItem(("GameObjectContext_" + std::to_string((uintptr_t)obj)).c_str()))
    {
        // Only show save option when scene root object is selected
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
            PushUndoSnapshot();
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

    // Right-click context menu on empty space - create objects
    if (ImGui::BeginPopupContextWindow("HierarchyContextWindow"))
    {
        if (ImGui::MenuItem("Create Directional Light"))
        {
            PushUndoSnapshot();
            GameObject* lightObj = new GameObject("DirLight");
            lightObj->AddComponent<LightComponent>();
            lightObj->GetComponent<TransformComponent>()->rotation[0] = -30.0f;
            lightObj->GetComponent<TransformComponent>()->UpdateTransform();
            // Single-ownership: always attach to rootGameObject.
            engine->scene->rootGameObject->AddChild(lightObj);
            selectedObject = lightObj;
            selectedFile.Clear();
            engine->scene->MarkDirty();
        }
        
        if (ImGui::BeginMenu("Create Geometry"))
        {
            if (ImGui::MenuItem("Create Cube"))
            {
                PushUndoSnapshot();
                GameObject* cube = new GameObject("Cube");
                cube->AddComponent<RendererComponent>(RendererComponent::Type::Cube);
                // Single-ownership: always attach to rootGameObject.
                engine->scene->rootGameObject->AddChild(cube);
                selectedObject = cube;
                selectedFile.Clear();
                engine->scene->MarkDirty();
            }
            if (ImGui::MenuItem("Create Sphere"))
            {
                PushUndoSnapshot();
                GameObject* sphere = new GameObject("Sphere");
                sphere->AddComponent<RendererComponent>(RendererComponent::Type::Sphere);
                // Single-ownership: always attach to rootGameObject.
                engine->scene->rootGameObject->AddChild(sphere);
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
                PushUndoSnapshot();
                // Single-ownership: AddChild() already rejects self-loops and
                // handles reparenting (RemoveFromParent internally).
                engine->scene->rootGameObject->AddChild(droppedObj);
                engine->scene->MarkDirty();
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Single-ownership: the entire hierarchy starts from rootGameObject.
    // The root itself is shown at depth 0 (isRoot=true) so the user can
    // select it to perform scene-wide operations.
    DrawGameObjectNode(engine->scene->rootGameObject, true);

    // Save window state
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
    // Set transparent background - ensure this is set before Begin
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("Game", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // Get window render area and call engine to render Game view (game running perspective)
    ImRect gameViewportRect = GetCurrentViewportRect();
    ImGui::GetWindowDrawList()->PushClipRect(gameViewportRect.Min, gameViewportRect.Max, true);
    engine->RenderSceneToViewport(gameViewportRect, true);
    ImGui::GetWindowDrawList()->PopClipRect();

    // Save window state
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        //LayoutManager::GetInstance().SaveCurrentWindowState("Game", pos, size, true, collapsed);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

// DrawProject and DrawInspector have been moved to ProjectWindow.cpp and InspectorWindow.cpp
// Keep empty implementation to maintain API compatibility

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
        selectedFile.Clear();  // Clear file selection
        ImGui::Text("Path"); ImGui::SameLine();
        static char loadPathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", loadPathBuffer, sizeof(loadPathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0)))
        {
            if (engine && engine->scene && engine->scene->LoadScene(loadPathBuffer))
            {
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  // Newly loaded scene has no modifications
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
                // Save to file - use ImGui built-in INI save
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

    // Build Popup - Build and publish
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

void Editor::DrawBuildSettingsWindow()
{
    if (!showBuildSettingsWindow) return;
    
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Settings", &showBuildSettingsWindow))
    {
        Project* proj = ProjectManager::GetInstance().GetCurrentProject();
        if (!proj)
        {
            ImGui::TextDisabled("No project loaded");
            ImGui::End();
            return;
        }
        
        // Platform selection
        ImGui::Text("Platform:");
        const char* platforms[] = { "Windows" };
        int currentPlatform = (int)buildSettings.platform;
        if (ImGui::Combo("##Platform", &currentPlatform, platforms, IM_ARRAYSIZE(platforms)))
        {
            buildSettings.platform = (BuildPlatform)currentPlatform;
        }
        
        // Configuration selection
        ImGui::Text("Configuration:");
        const char* configs[] = { "Debug", "Release" };
        int currentConfig = (int)buildSettings.configuration;
        if (ImGui::Combo("##Config", &currentConfig, configs, IM_ARRAYSIZE(configs)))
        {
            buildSettings.configuration = (BuildConfiguration)currentConfig;
        }
        
        ImGui::Separator();
        
        // Product info
        ImGui::Text("Product Settings:");
        
        char productName[256];
        strcpy_s(productName, buildSettings.productName.c_str());
        ImGui::Text("Product Name:");
        if (ImGui::InputText("##ProductName", productName, sizeof(productName)))
        {
            buildSettings.productName = productName;
        }
        
        char companyName[256];
        strcpy_s(companyName, buildSettings.companyName.c_str());
        ImGui::Text("Company Name:");
        if (ImGui::InputText("##CompanyName", companyName, sizeof(companyName)))
        {
            buildSettings.companyName = companyName;
        }
        
        char version[64];
        strcpy_s(version, buildSettings.version.c_str());
        ImGui::Text("Version:");
        if (ImGui::InputText("##Version", version, sizeof(version)))
        {
            buildSettings.version = version;
        }
        
        ImGui::Separator();
        
        // Scene list
        ImGui::Text("Scenes In Build:");
        
        // Refresh scene list button
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            buildSettings.scenes = ::BuildSystem::GetProjectScenes(proj->path);
        }
        
        ImGui::BeginChild("ScenesList", ImVec2(0, 150), true);
        
        if (buildSettings.scenes.empty())
        {
            ImGui::TextDisabled("No scenes found in project");
        }
        else
        {
            for (size_t i = 0; i < buildSettings.scenes.size(); i++)
            {
                std::string sceneName = fs::path(buildSettings.scenes[i]).stem().string();
                bool isSelected = (buildSettings.startupScene == sceneName);
                
                // Show scene index and name
                ImGui::Text("%d", (int)i);
                ImGui::SameLine(30);
                
                if (ImGui::Selectable(sceneName.c_str(), isSelected))
                {
                    buildSettings.startupScene = sceneName;
                }
                
                // Show if it's the startup scene
                if (isSelected)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Startup)");
                }
            }
        }
        
        ImGui::EndChild();
        
        // Startup scene display
        ImGui::Text("Startup Scene: %s", buildSettings.startupScene.empty() ? "None" : buildSettings.startupScene.c_str());
        
        ImGui::Separator();
        
        // Output path
        ImGui::Text("Output Path:");
        char outputPath[512];
        strcpy_s(outputPath, buildSettings.outputPath.c_str());
        if (ImGui::InputText("##OutputPath", outputPath, sizeof(outputPath)))
        {
            buildSettings.outputPath = outputPath;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse##Output"))
        {
            // TODO: Open folder selection dialog
        }
        
        // Development build settings
        ImGui::Checkbox("Development Build", &buildSettings.developmentBuild);
        if (buildSettings.developmentBuild)
        {
            ImGui::Checkbox("Script Debugging", &buildSettings.enableScriptDebugging);
        }
        
        ImGui::Separator();
        
        // Build progress
        if (isBuilding)
        {
            ImGui::Text("Building: %s", buildStatus.c_str());
            ImGui::ProgressBar(buildProgress, ImVec2(-1, 0));
        }
        
        // Build button
        ImGui::BeginDisabled(isBuilding);
        
        if (ImGui::Button("Build", ImVec2(120, 30)))
        {
            std::string error;
            if (BuildSystem::ValidateSettings(buildSettings, error))
            {
                isBuilding = true;
                buildProgress = 0.0f;
                buildStatus = "Starting...";
                
                // Execute build
                bool success = BuildSystem::Build(buildSettings, 
                    [this](const std::string& stage, float progress)
                    {
                        buildStatus = stage;
                        buildProgress = progress;
                    });
                
                isBuilding = false;
                if (success)
                {
                    buildStatus = "Build completed successfully!";
                    // Open output directory - use absolute path
                    std::wstring outputDirW = fs::absolute(buildSettings.outputPath).wstring();
                    ShellExecuteW(NULL, L"open", outputDirW.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                else
                {
                    buildStatus = "Build failed!";
                }
            }
            else
            {
                buildStatus = "Error: " + error;
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::SameLine();
        
        if (ImGui::Button("Close", ImVec2(120, 30)))
        {
            showBuildSettingsWindow = false;
        }
        
        // Display status info
        if (!buildStatus.empty())
        {
            ImGui::Separator();
            ImGui::Text("Status: %s", buildStatus.c_str());
        }
    }
    ImGui::End();
}

// ---- Undo / Redo ----------------------------------------------------------

void Editor::PushUndoSnapshot()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;   // edit-mode only
    std::string snap = engine->scene->CaptureSnapshot();
    if (snap.empty()) return;
    if (!m_undoStack.empty() && m_undoStack.back() == snap) return;  // dedup
    m_undoStack.push_back(std::move(snap));
    if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void Editor::BeginInspectorEdit()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_hasPendingEdit) return;                       // one capture per interaction
    m_pendingPreEdit = engine->scene->CaptureSnapshot();
    m_hasPendingEdit = true;
}

void Editor::EndInspectorEdit()
{
    if (!m_hasPendingEdit) return;
    m_hasPendingEdit = false;
    if (!engine || !engine->scene) return;
    // Commit the pre-edit snapshot only if the scene actually changed -> no
    // spurious undo steps from clicking a control without moving it.
    if (engine->scene->CaptureSnapshot() == m_pendingPreEdit) return;
    m_undoStack.push_back(std::move(m_pendingPreEdit));
    if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void Editor::Undo()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_undoStack.empty()) return;

    std::string current = engine->scene->CaptureSnapshot();
    std::string prev = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    if (engine->scene->RestoreSnapshot(prev))
    {
        m_redoStack.push_back(std::move(current));
        if (m_redoStack.size() > kUndoDepth) m_redoStack.erase(m_redoStack.begin());
        // All GameObject pointers were rebuilt -> drop stale references.
        selectedObject = nullptr; activeSelection = nullptr;
        selectedFile.Clear();
        m_expandedGameObjects.clear();
        m_hasPendingEdit = false;
        sceneDirty = true;
    }
    else
    {
        m_undoStack.push_back(std::move(prev));  // restore failed: put it back
    }
}

void Editor::Redo()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_redoStack.empty()) return;

    std::string current = engine->scene->CaptureSnapshot();
    std::string next = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    if (engine->scene->RestoreSnapshot(next))
    {
        m_undoStack.push_back(std::move(current));
        if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
        selectedObject = nullptr; activeSelection = nullptr;
        selectedFile.Clear();
        m_expandedGameObjects.clear();
        m_hasPendingEdit = false;
        sceneDirty = true;
    }
    else
    {
        m_redoStack.push_back(std::move(next));
    }
}

void Editor::CopySelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;
    PushUndoSnapshot();
    GameObject* newObj = new GameObject(selectedObject);
    newObj->parent = nullptr;
    if (selectedObject->parent)
        selectedObject->parent->AddChild(newObj);
    else
        engine->scene->gameObjects.push_back(newObj);
    selectedObject = newObj;
    engine->scene->MarkDirty();  // Mark scene as modified
}

void Editor::DeleteSelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;

    if (selectedObject == engine->scene->rootGameObject)
    {
        return;
    }

    PushUndoSnapshot();   // capture pre-delete state (covers menu + Delete key)

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

    // Remove the whole subtree from the non-owning observer list before the
    // recursive delete, otherwise gameObjects would keep dangling pointers to
    // the freed descendants.
    engine->scene->UnregisterSubtree(selectedObject);

    delete selectedObject;

    if (parent)
        selectedObject = parent;
    else if (!engine->scene->gameObjects.empty())
        selectedObject = engine->scene->gameObjects.back();
    else
        selectedObject = nullptr;
    
    engine->scene->MarkDirty();  // Mark scene as modified
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

    // Build new filename: name_copy.ext
    std::string newName = selectedFile.name + "_copy" + selectedFile.extension;
    std::string newPath = selectedFile.path;
    size_t pos = newPath.rfind(selectedFile.name + selectedFile.extension);
    if (pos != std::string::npos) {
        newPath.replace(pos, selectedFile.name.size() + selectedFile.extension.size(), newName);
    }

    try {
        fs::copy_file(selectedFile.path, newPath, fs::copy_options::overwrite_existing);
        // Select newly copied file
        selectedFile.path = newPath;
        selectedFile.name = selectedFile.name + "_copy";
    }
    catch (const std::exception& e) {
        std::cerr << "Duplicate file failed: " << e.what() << std::endl;
    }
}

// Project selection interface
void Editor::DrawProjectSelector()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("ProjectSelector", nullptr, flags);

    // Title - centered
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y;
    
    ImGui::SetCursorPosX((windowWidth - 200) * 0.5f);
    ImGui::SetCursorPosY(windowHeight * 0.15f);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("Ditto Engine");
    ImGui::SetWindowFontScale(1.0f);

    // Project list - centered
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

    // Buttons - centered, in a row, aligned with list box
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

    // Save selected project index
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

        // Load last scene (if any)
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
                    // Single-ownership: rootGameObject is always present.
                    std::cout << "[Editor] RootGameObject children: " << engine->scene->rootGameObject->children.size() << std::endl;
                }
                else
                {
                    std::cerr << "[Editor] Failed to load scene: " << fullPath << std::endl;
                    // Create default scene. ClearScene() rebuilds the root
                    // using the current scene name, so set the name first.
                    engine->scene->name = "Default";
                    engine->scene->ClearScene();
                }
                
                // Update UI
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  // Newly loaded scene has no modifications
                
                // Re-setup scene modified callback
                engine->scene->onModified = [this]() {
                    this->sceneDirty = true;
                };
            }
        }
        else
        {
            std::cout << "[Editor] No last scene to load, creating default scene" << std::endl;
            // Create default scene
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
        
        // Update UI
        strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
        sceneDirty = false;  // Newly loaded scene has no modifications
        
        // Re-setup scene modified callback
        engine->scene->onModified = [this]() {
            this->sceneDirty = true;
        };

        // Save to project config
        Project* proj = ProjectManager::GetInstance().GetCurrentProject();
        if (proj)
        {
            // Extract relative path
            size_t pos = scenePath.find("/Assets/");
            if (pos != std::string::npos)
            {
                proj->lastScene = scenePath.substr(pos + 1);

                // Save to project.json
                std::string projectFile = proj->path + "/project.json";
                // TODO: Update lastScene in project.json
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

    // Check file type
    fs::path p(scriptPath);
    std::string ext = p.extension().string();
    std::string scriptName = p.stem().string();
    
    std::cout << "[Editor] Adding script: " << scriptName << " (" << ext << ") to " << obj->name << std::endl;
    
    if (ext == ".cs")
    {
        PushUndoSnapshot();   // adding a script component is undoable
        // C# script - create CSharpScriptComponent
        CSharpScriptComponent* csScript = new CSharpScriptComponent();
        csScript->scriptPath = scriptPath;
        csScript->scriptName = std::filesystem::path(scriptPath).filename().stem().string();
        csScript->gameObject = obj;
        csScript->ParseScriptFields();
        
        obj->components.push_back(csScript);
        obj->compMask |= csScript->index;   // bitwise OR, consistent with GameObject::AddComponent
        std::cout << "[Editor] C# script added: " << csScript->scriptName << std::endl;
        
        // Mark scene as modified
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

    // Get current project path
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    std::string savePath;
    
    if (proj)
    {
        // Save to project directory under Assets/Scenes/
        savePath = proj->path + "/Assets/Scenes/" + engine->scene->name + ".bin";
    }
    else
    {
        // When no project, use relative path
        savePath = "Assets/Scenes/" + engine->scene->name + ".bin";
    }
    
    std::cout << "[Editor] Saving scene to: " << savePath << std::endl;
    
    if (engine->scene->SaveScene(savePath.c_str()))
    {
        std::cout << "[Editor] Scene saved successfully" << std::endl;
        sceneDirty = false;  // Clear modification flag
        
        // Update project config
        if (proj)
        {
            // Save relative path to project config
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

    // Output directory: Build/Windows under project directory
    std::string projectPath = proj->path;
    std::replace(projectPath.begin(), projectPath.end(), '\\', '/');
    std::string outputDir = projectPath + "/Build/Windows";
    
    try
    {
        // Create output directory
        if (!fs::exists(outputDir))
        {
            fs::create_directories(outputDir);
        }

        // 1. Copy Assets directory contents to root directory
        std::string assetsSrc = projectPath + "/Assets";
        
        if (fs::exists(assetsSrc))
        {
            // Copy entire Assets directory
            std::string assetsDst = outputDir;
            fs::remove_all(assetsDst + "/Assets");
            fs::copy(assetsSrc, assetsDst + "/Assets", fs::copy_options::recursive);
            std::cout << "[Editor] Copied Assets to " << assetsDst << std::endl;
        }

        // 2. Save current scene
        std::string sceneName = engine->scene->name;
        std::string sceneSrc = projectPath + "/Assets/Scenes/" + sceneName + ".bin";
        
        // Ensure directory exists
        fs::create_directories(outputDir + "/Assets/Scenes");
        
        // Save and copy scene
        engine->scene->SaveScene(sceneSrc.c_str());
        if (fs::exists(sceneSrc))
        {
            std::string sceneDst = outputDir + "/Assets/Scenes/" + sceneName + ".bin";
            fs::copy(sceneSrc, sceneDst, fs::copy_options::overwrite_existing);
            std::cout << "[Editor] Copied scene: " << sceneDst << std::endl;
        }

        // Copy project.json to root directory
        std::string projectJsonSrc = projectPath + "/project.json";
        if (fs::exists(projectJsonSrc))
        {
            fs::copy(projectJsonSrc, outputDir + "/project.json", fs::copy_options::overwrite_existing);
            std::cout << "[Editor] Copied project.json to " << outputDir << std::endl;
        }

        // 3. Copy executable
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

        // 4. Copy 3rdParty DLL
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
            // Copy glfw3.dll
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

        // 5. Create startup script (Run.bat)
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
        
        // 6. Open output directory in Explorer
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
    
    // Script directory
    std::string scriptsDir = proj->path + "/Assets/Scripts";
    std::string outputDll = proj->path + "/Scripts.dll";
    
    if (!fs::exists(scriptsDir))
    {
        std::cout << "[Editor] Scripts directory does not exist: " << scriptsDir << std::endl;
        return;
    }
    
    // Collect all .cpp files
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
    
    // Convert to absolute paths
    fs::path projPathAbs = fs::absolute(proj->path);
    // E:\Engine Source\Ditto\Ditto\Projects\MyProject -> ../.. = E:\Engine Source\Ditto\Ditto
    fs::path enginePathAbs = fs::absolute(proj->path + "/../..");
    fs::path outputDllAbs = fs::absolute(outputDll);
    
    // Use raw script files directly (user needs to manually modify include paths)
    std::string compileFiles;
    for (const auto& f : cppFiles)
    {
        compileFiles += "\"" + fs::absolute(f).string() + "\" ";
    }
    
    // Create compilation script
    std::string batPath = proj->path + "/build_scripts.bat";
    std::ofstream batFile(batPath);
    
    batFile << "@echo off\n";
    batFile << "cd /d \"" << projPathAbs.string() << "\"\n";
    std::string vcvarsPath = FindVCVarsPath();
    if (!vcvarsPath.empty())
        batFile << "call \"" << vcvarsPath << "\"\n";
    else
        batFile << "echo Warning: vcvars64.bat not found, compilation may fail\n";
    
    // Use absolute paths, add C++20 and more header paths
    std::string clCmd = "cl /LD /EHsc /std:c++latest /I\"" + enginePathAbs.string() + "\\3rdParty\\GLM\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\\include\" /I\"" + enginePathAbs.string() + "\\3rdParty\\ImGui\" /I\"" + enginePathAbs.string() + "\\Engine\\Core\" /I\"" + enginePathAbs.string() + "\\Engine\\Graphics\" /I\"" + enginePathAbs.string() + "\\Engine\\Physics\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\" /I\"" + enginePathAbs.string() + "\" /D\"SCRIPT_DLL\" /O2 /MD " + compileFiles + "/Fe:\"" + outputDllAbs.string() + "\"";
    
    batFile << clCmd << "\n";
    batFile << "pause\n";
    
    batFile.close();
    
    std::cout << "[Editor] Please run: " << batPath << std::endl;
    std::cout << "[Editor] Or manually compile your scripts and place DLL at: " << outputDll << std::endl;
    
    // Try to execute directly
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
        
        // Check if DLL was generated
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


// Model preview has been moved to InspectorWindow.cpp
// File icon related functions
static const char* s_iconFiles[] = {
    "Default.png", "Cs.png", "Model.png", "Prefab.png", "Shader.png", "Scene.png", "Folder.png"
};

void Editor::StopAndRestoreScene()
{
    // Reload the temp scene snapshot (captured when Play started) and reset
    // editor selection state. After this returns the engine state should be
    // Engine::Edit; the caller is expected to set that.
    if (!m_tempScenePath.empty() && std::filesystem::exists(m_tempScenePath))
    {
        engine->scene->LoadScene(m_tempScenePath);
    }

    // After scene reload, all old GameObject pointers are invalidated.
    selectedObject = nullptr;
    activeSelection = nullptr;
    selectedFile.Clear();
    m_expandedGameObjects.clear();

    engine->state = Engine::Edit;
}

void Editor::InitFileIcons()
{
    if (m_fileIconsInitialized) return;
    
    // Get icon directory path
    m_assetsPath = FindEditorAssetsPath() + "/Icon";
    std::cout << "[FileIcon] Initializing from: " << m_assetsPath << std::endl;
    
    // Load file icons
    for (int i = 0; i < 7; i++) {
        std::string path = m_assetsPath + "/" + s_iconFiles[i];
        m_icons[i] = LoadIcon(path);
    }
    
    // Load folder icons
    m_folderIcon = LoadIcon(m_assetsPath + "/Folder.png");
    m_folderEmptyIcon = LoadIcon(m_assetsPath + "/FolderEmpty.png");
    m_folderOpenedIcon = LoadIcon(m_assetsPath + "/FolderOpened Icon.png");
    
    // Load special icons
    m_dittoIcon = LoadIcon(m_assetsPath + "/Scene.png");
    m_gameObjectIcon = LoadIcon(m_assetsPath + "/GameObject.png");
    
    // Load lock icons
    m_lockIcon = LoadIcon(m_assetsPath + "/Lock.png");
    m_unlockIcon = LoadIcon(m_assetsPath + "/UnLock.png");

    // Load toolbar icons
    m_playIcon = LoadIcon(m_assetsPath + "/Play.png");
    m_pauseIcon = LoadIcon(m_assetsPath + "/Pause.png");
    m_stopIcon = LoadIcon(m_assetsPath + "/Scene.png"); // placeholder; swap to "Stop.png" when available

    m_fileIconsInitialized = true;
    std::cout << "[FileIcon] Initialized successfully" << std::endl;
}

Ditto::TextureHandle Editor::LoadIcon(const std::string& iconPath)
{
    namespace fs = std::filesystem;

    if (!fs::exists(iconPath)) {
        std::cerr << "[FileIcon] File not found: " << iconPath << std::endl;
        return {};
    }
    if (!engine || !engine->renderer) return {};

    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);

    if (!data) {
        std::cerr << "[FileIcon] Failed to load: " << iconPath << std::endl;
        return {};
    }

    Ditto::TextureHandle tex = engine->renderer->CreateTexture(data, width, height, 4);
    stbi_image_free(data);

    std::cout << "[FileIcon] Loaded: " << iconPath << " (" << width << "x" << height << ")" << std::endl;
    return tex;
}

// Resolve an icon handle to an ImGui texture id (backend-specific). Central
// helper used by all the icon getters.
void* Editor::IconTexID(Ditto::TextureHandle h)
{
    return (engine && engine->renderer) ? engine->renderer->GetImGuiTextureID(h) : nullptr;
}

int Editor::GetIconIndex(const std::string& ext)
{
    if (ext == ".cs") return 1;  // Cs.png
    if (ext == ".obj") return 2;  // Prefab.png (model)
    if (ext == ".prefab") return 3;  // Text.png (material)
    if (ext == ".shader") return 4;  // Shader.png
    if (ext == ".bin") return 5;  // Scene.png
    if (ext == ".tga") return 6;  // Folder.png (texture)
    return 0;  // Default Default.png
}

void* Editor::GetIconByExtension(const std::string& extension)
{
    if (!m_fileIconsInitialized) return nullptr;
    int idx = GetIconIndex(extension);   // already 0-6 range
    return IconTexID(m_icons[idx]);
}

// Icon getters resolve a stored handle to an ImGui texture id on demand.
void* Editor::GetFolderIcon()       { return IconTexID(m_folderIcon); }
void* Editor::GetFolderEmptyIcon()  { return IconTexID(m_folderEmptyIcon); }
void* Editor::GetFolderOpenedIcon() { return IconTexID(m_folderOpenedIcon); }
void* Editor::GetDittoIcon()        { return IconTexID(m_dittoIcon); }
void* Editor::GetGameObjectIcon()   { return IconTexID(m_gameObjectIcon); }
void* Editor::GetLockIcon()         { return IconTexID(m_lockIcon); }
void* Editor::GetUnlockIcon()       { return IconTexID(m_unlockIcon); }
void* Editor::GetPlayIcon()         { return IconTexID(m_playIcon); }
void* Editor::GetPauseIcon()        { return IconTexID(m_pauseIcon); }
void* Editor::GetStopIcon()         { return IconTexID(m_stopIcon); }

void Editor::CleanupFileIcons()
{
    if (!m_fileIconsInitialized) return;

    if (engine && engine->renderer)
    {
        Ditto::IRenderer* r = engine->renderer.get();
        for (auto& h : m_icons) r->DestroyTexture(h);
        r->DestroyTexture(m_folderIcon);
        r->DestroyTexture(m_folderEmptyIcon);
        r->DestroyTexture(m_folderOpenedIcon);
        r->DestroyTexture(m_dittoIcon);
        r->DestroyTexture(m_gameObjectIcon);
        r->DestroyTexture(m_lockIcon);
        r->DestroyTexture(m_unlockIcon);
        r->DestroyTexture(m_playIcon);
        r->DestroyTexture(m_pauseIcon);
        r->DestroyTexture(m_stopIcon);
    }

    for (auto& h : m_icons) h = {};
    m_folderIcon = m_folderEmptyIcon = m_folderOpenedIcon = {};
    m_dittoIcon = m_gameObjectIcon = m_lockIcon = m_unlockIcon = {};
    m_playIcon = m_pauseIcon = m_stopIcon = {};

    m_fileIconsInitialized = false;
    std::cout << "[FileIcon] Cleaned up" << std::endl;
}