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
#include "../Engine/Core/JsonConfig.h"
#include "../Engine/Core/RuntimeContext.h"
#include "LayoutManager.h"
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "SceneWindow.h"
#include "BuildSystem.h"
#include "RenderTargetImageUV.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/PrefabAsset.h"
#include "../Engine/Core/Input.h"
#include "../Engine/Core/IWindow.h"
#include "../3rdParty/ImGuizmo/ImGuizmo.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/CSharpScript.h"
#include "../Engine/Core/Logger.h"
#include "../Engine/Resources/AssetDatabase.h"
#include "../Engine/Resources/AssetPath.h"
#include "../3rdParty/ImGui/imgui_internal.h"
#include "../3rdParty/GLM/ext/matrix_transform.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../3rdParty/stb_image.h"
using namespace glm;
namespace fs = std::filesystem;

static GameObject* CreateCameraObject(GameObject* parent, const std::string& name = "Camera")
{
    GameObject* cameraObj = parent->AddChild(std::make_unique<GameObject>(name));
    if (TransformComponent* transform = cameraObj->GetComponent<TransformComponent>())
    {
        transform->position = glm::vec3(0.0f, 2.0f, 5.0f);
        transform->rotation = glm::vec3(-21.8f, 0.0f, 0.0f);
        transform->localDirty = true;
        transform->UpdateTransform();
    }
    cameraObj->AddComponent<CameraComponent>();
    return cameraObj;
}

static RendererComponent* AddDefaultRenderer(GameObject* obj, const std::string& meshPath = "")
{
    RendererComponent* renderer = obj->AddComponent<RendererComponent>();
    renderer->meshPath = meshPath;
    renderer->materialPath = "Materials/Lit_Toon.mat";
    return renderer;
}

static GameObject* CreateSpriteObject(GameObject* parent, const std::string& spritePath = "")
{
    GameObject* sprite = parent->AddChild(std::make_unique<GameObject>("Sprite"));
    SpriteRendererComponent* renderer = sprite->AddComponent<SpriteRendererComponent>();
    renderer->materialPath = "Materials/Lit_Sprite.mat";
    renderer->spritePath = spritePath;
    renderer->color = glm::vec4(1.0f);
    if (TransformComponent* transform = sprite->GetComponent<TransformComponent>())
    {
        transform->scale = glm::vec3(1.0f);
        transform->localDirty = true;
        transform->UpdateTransform();
    }
    return sprite;
}

static GameObject* CreateCanvasObject(GameObject* parent)
{
    GameObject* canvas = parent->AddChild(std::make_unique<GameObject>("Canvas"));
    canvas->AddComponent<CanvasComponent>();
    RectTransformComponent* rect = canvas->AddComponent<RectTransformComponent>();
    rect->anchor = UIAnchor::Center;
    rect->anchoredPosition = glm::vec2(0.0f);
    rect->sizeDelta = glm::vec2(800.0f, 600.0f);
    return canvas;
}

static bool IsCanvasObject(GameObject* obj)
{
    return obj && (obj->GetComponent<CanvasComponent>() || obj->name == "Canvas");
}

static GameObject* FindCanvasInChildren(GameObject* parent)
{
    if (!parent) return nullptr;
    if (IsCanvasObject(parent)) return parent;
    for (const auto& child : parent->children)
        if (GameObject* canvas = FindCanvasInChildren(child.get()))
            return canvas;
    return nullptr;
}

static GameObject* FindOrCreateCanvas(GameObject* root, GameObject* preferredParent)
{
    if (IsCanvasObject(preferredParent)) return preferredParent;
    if (GameObject* canvas = FindCanvasInChildren(root))
        return canvas;
    return CreateCanvasObject(root ? root : preferredParent);
}

static GameObject* CreateUIImageObject(GameObject* parent)
{
    GameObject* image = parent->AddChild(std::make_unique<GameObject>("Image"));
    RectTransformComponent* rect = image->AddComponent<RectTransformComponent>();
    rect->anchor = UIAnchor::Center;
    rect->anchoredPosition = glm::vec2(0.0f);
    rect->sizeDelta = glm::vec2(100.0f, 100.0f);
    image->AddComponent<UIImageComponent>();
    return image;
}

static GameObject* CreateUITextObject(GameObject* parent)
{
    GameObject* text = parent->AddChild(std::make_unique<GameObject>("Text"));
    RectTransformComponent* rect = text->AddComponent<RectTransformComponent>();
    rect->anchor = UIAnchor::Center;
    rect->anchoredPosition = glm::vec2(0.0f);
    rect->sizeDelta = glm::vec2(160.0f, 30.0f);
    text->AddComponent<UITextComponent>();
    return text;
}

static GameObject* CreateUIButtonObject(GameObject* parent)
{
    GameObject* button = parent->AddChild(std::make_unique<GameObject>("Button"));
    RectTransformComponent* rect = button->AddComponent<RectTransformComponent>();
    rect->anchor = UIAnchor::Center;
    rect->anchoredPosition = glm::vec2(0.0f);
    rect->sizeDelta = glm::vec2(160.0f, 40.0f);
    button->AddComponent<UIButtonComponent>();
    return button;
}

static GameObject* CreateUIPanelObject(GameObject* parent)
{
    GameObject* panel = parent->AddChild(std::make_unique<GameObject>("Panel"));
    RectTransformComponent* rect = panel->AddComponent<RectTransformComponent>();
    rect->anchor = UIAnchor::Center;
    rect->anchoredPosition = glm::vec2(0.0f);
    rect->sizeDelta = glm::vec2(400.0f, 300.0f);
    UIImageComponent* image = panel->AddComponent<UIImageComponent>();
    image->color = glm::vec4(0.18f, 0.18f, 0.18f, 0.85f);
    return panel;
}

static void SelectCreatedObject(Editor* editor, GameObject* obj)
{
    if (!editor || !obj) return;
    editor->selectedObject = obj;
    editor->activeSelection = obj;
    editor->selectedComponent = nullptr;
    editor->selectedFile.Clear();
    if (editor->engine && editor->engine->scene)
        editor->engine->scene->MarkDirty();
}

static bool BuildGameObjectPath(GameObject* root, GameObject* object, std::vector<int>& outPath)
{
    outPath.clear();
    if (!root || !object) return false;
    if (object == root) return true;

    std::vector<int> reversedPath;
    for (GameObject* node = object; node && node != root; node = node->parent)
    {
        GameObject* parent = node->parent;
        if (!parent) return false;

        auto it = std::find_if(parent->children.begin(), parent->children.end(),
            [node](const std::unique_ptr<GameObject>& child) { return child.get() == node; });
        if (it == parent->children.end()) return false;

        reversedPath.push_back(static_cast<int>(std::distance(parent->children.begin(), it)));
    }

    outPath.assign(reversedPath.rbegin(), reversedPath.rend());
    return true;
}

static GameObject* ResolveGameObjectPath(GameObject* root, const std::vector<int>& path)
{
    GameObject* node = root;
    for (int childIndex : path)
    {
        if (!node || childIndex < 0 || childIndex >= static_cast<int>(node->children.size()))
            return nullptr;
        node = node->children[childIndex].get();
    }
    return node;
}

static std::vector<std::vector<int>> CaptureExpandedObjectPaths(
    GameObject* root,
    const std::set<GameObject*>& expandedObjects)
{
    std::vector<std::vector<int>> paths;
    for (GameObject* expanded : expandedObjects)
    {
        std::vector<int> path;
        if (BuildGameObjectPath(root, expanded, path))
            paths.push_back(std::move(path));
    }
    return paths;
}

static void RestoreExpandedObjectPaths(
    GameObject* root,
    const std::vector<std::vector<int>>& paths,
    std::set<GameObject*>& expandedObjects)
{
    expandedObjects.clear();
    if (!root) return;

    for (const std::vector<int>& path : paths)
        if (GameObject* expanded = ResolveGameObjectPath(root, path))
            expandedObjects.insert(expanded);
}

struct GameObjectPathKey
{
    std::vector<int> indices;
    std::vector<std::string> names;
    std::vector<size_t> siblingCounts;
};

static bool BuildGameObjectPathKey(GameObject* root, GameObject* object, GameObjectPathKey& outKey)
{
    outKey = {};
    if (!BuildGameObjectPath(root, object, outKey.indices)) return false;

    GameObject* node = root;
    for (int childIndex : outKey.indices)
    {
        if (!node || childIndex < 0 || childIndex >= static_cast<int>(node->children.size()))
            return false;

        outKey.siblingCounts.push_back(node->children.size());
        node = node->children[childIndex].get();
        outKey.names.push_back(node ? node->name : std::string());
    }

    return true;
}

static GameObject* ResolveGameObjectPathKey(GameObject* root, const GameObjectPathKey& key)
{
    GameObject* node = root;
    for (size_t depth = 0; depth < key.indices.size(); ++depth)
    {
        if (!node) return nullptr;

        const int childIndex = key.indices[depth];
        const std::string& childName = key.names[depth];
        const size_t oldSiblingCount = key.siblingCounts[depth];
        GameObject* next = nullptr;

        if (childIndex >= 0 && childIndex < static_cast<int>(node->children.size()) &&
            node->children[childIndex] && node->children[childIndex]->name == childName)
        {
            next = node->children[childIndex].get();
        }
        else
        {
            auto namedSibling = std::find_if(node->children.begin(), node->children.end(),
                [&childName](const std::unique_ptr<GameObject>& child) {
                    return child && child->name == childName;
                });
            if (namedSibling != node->children.end())
            {
                next = namedSibling->get();
            }
            else if (node->children.size() == oldSiblingCount &&
                     childIndex >= 0 && childIndex < static_cast<int>(node->children.size()))
            {
                next = node->children[childIndex].get();
            }
        }

        if (!next) return nullptr;
        node = next;
    }
    return node;
}

static std::vector<GameObjectPathKey> CaptureExpandedObjectPathKeys(
    GameObject* root,
    const std::set<GameObject*>& expandedObjects)
{
    std::vector<GameObjectPathKey> keys;
    for (GameObject* expanded : expandedObjects)
    {
        GameObjectPathKey key;
        if (BuildGameObjectPathKey(root, expanded, key))
            keys.push_back(std::move(key));
    }
    return keys;
}

static void RestoreExpandedObjectPathKeys(
    GameObject* root,
    const std::vector<GameObjectPathKey>& keys,
    std::set<GameObject*>& expandedObjects)
{
    expandedObjects.clear();
    if (!root) return;

    for (const GameObjectPathKey& key : keys)
        if (GameObject* expanded = ResolveGameObjectPathKey(root, key))
            expandedObjects.insert(expanded);
}

static void RemoveExpandedSubtree(GameObject* root, std::set<GameObject*>& expandedObjects)
{
    if (!root) return;

    expandedObjects.erase(root);
    for (const auto& child : root->children)
        RemoveExpandedSubtree(child.get(), expandedObjects);
}

static GameObject* CreateCubeObject(GameObject* parent)
{
    
    GameObject* obj = parent->AddChild(std::make_unique<GameObject>("Cube"));
    AddDefaultRenderer(obj, "Models/Cube.obj");
    return obj;
}

static GameObject* CreateSphereObject(GameObject* parent)
{
    GameObject* obj = parent->AddChild(std::make_unique<GameObject>("Sphere"));
    AddDefaultRenderer(obj, "Models/Sphere.obj");
    return obj;
}

static GameObject* CreateQuadObject(GameObject* parent)
{
    GameObject* obj = parent->AddChild(std::make_unique<GameObject>("Quad"));
    AddDefaultRenderer(obj, "Models/Quad.obj");
    return obj;
}

static GameObject* CreateDirectionalLightObject(GameObject* parent)
{
    GameObject* lightObj = parent->AddChild(std::make_unique<GameObject>("Directional Light"));
    lightObj->AddComponent<LightComponent>();
    if (TransformComponent* transform = lightObj->GetComponent<TransformComponent>())
    {
        transform->rotation[0] = -30.0f;
        transform->localDirty = true;
        transform->UpdateTransform();
    }
    return lightObj;
}


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
    
    DITTO_LOG_WARN("[Editor] Editor assets not found, using default");
    return "Assets";
}


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

    DITTO_LOG_WARN("[Editor] vcvars64.bat not found, compilation may fail");
    return "";
}

static ImRect GetCurrentViewportRect()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImRect(ImVec2(0, 0), ImVec2(0, 0));
    ImVec2 min = window->InnerRect.Min;
    ImVec2 max = window->InnerRect.Max;
    return ImRect(min, max);
}

Editor::Editor(Ditto::IWindow* window, bool gameMode, const std::string& projectPath)
{
    
    Ditto::RuntimeContext::SetCurrentEditor(this);
    
    
    activeSelection = nullptr;
    this->gameMode = gameMode;
    this->gameProjectPath = projectPath;
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle().Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);

    
    
    
    m_window = window;

    showSavePopup = false;
    showLoadPopup = false;
    showSaveLayoutPopup = false;
    projectLoaded = false;
    dockingInitialized = false;
    frame = deltaTime = 0;

    
    
    ProjectManager::GetInstance().Initialize("../../Ditto/Projects");
    
    
    std::string editorAssetsPath = FindEditorAssetsPath();
    LayoutManager::GetInstance().Initialize(editorAssetsPath + "/Settings");
    
    
    showProjectSelector = !gameMode;

    
    
    

    
    m_projectWindow = std::make_unique<ProjectWindow>(this);
    m_inspectorWindow = std::make_unique<InspectorWindow>(this);
    m_assetHealthWindow = std::make_unique<AssetHealthWindow>(this);
    m_sceneWindow = std::make_unique<SceneWindow>(this);
    if (m_window)
        m_window->SetDropCallback([this](const std::vector<std::string>& paths)
        {
            ImportExternalFilesToProject(paths);
        });
    
    
    CSharpScriptSystem::SetEditor(this);

    
    if (engine && engine->scene)
    {
        engine->scene->onModified = [this]() {
            this->sceneDirty = true;
        };
    }
}

void Editor::ImportExternalFilesToProject(const std::vector<std::string>& paths)
{
    if (m_projectWindow)
        m_projectWindow->ImportExternalFiles(paths);
}

Editor::~Editor()
{
    if (Ditto::RuntimeContext::CurrentEditor() == this)
        Ditto::RuntimeContext::SetCurrentEditor(nullptr);
    if (m_window)
        m_window->SetDropCallback(nullptr);

    CleanupModelPreview();
    CleanupFileIcons();

    
    
    m_projectWindow.reset();
    m_inspectorWindow.reset();
    m_sceneWindow.reset();

    if (engine && engine->renderer && m_imguiBackendInit) engine->renderer->ImGuiShutdown();
    ImGui::DestroyContext();
}

void Editor::Draw()
{
    isSceneActive = false;

    
    
    if (!m_imguiBackendInit && engine && engine->renderer)
    {
        engine->renderer->ImGuiInit(m_window);
        m_imguiBackendInit = true;
    }
    InitFileIcons();

    if (engine && engine->renderer) engine->renderer->ImGuiNewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    
    
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveCurrentScene();
    }

    
    if (showProjectSelector)
    {
        DrawProjectSelector();
        
        
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
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                strcpy_s(projectNameBuffer, "MyProject");
            }
            ImGui::SameLine();
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
            ImGui::EndPopup();
        }
        
        
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
                        DITTO_LOG_INFO_STREAM("[Editor] Renamed project: " << renameProjectOldPath << " -> " << newPath.string() );
                    }
                    catch (const std::exception& e)
                    {
                        DITTO_LOG_ERROR_STREAM("Failed to rename project: " << e.what() );
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
        if (engine && engine->renderer) engine->renderer->ImGuiRenderDrawData(ImGui::GetDrawData());
        return;
    }

    
    SetupDocking();

    DrawToolbar();
    DrawHierarchy();
    DrawScene();
    DrawGame();
    if (m_projectWindow) m_projectWindow->Draw();
    if (m_projectWindow) m_projectWindow->DrawConsoleWindow();
    if (m_inspectorWindow) m_inspectorWindow->Draw();
    if (m_assetHealthWindow) m_assetHealthWindow->Draw();
    DrawPopups();
    DrawBuildSettingsWindow();

    
    ImGui::End();

    ImGui::Render();
    if (engine && engine->renderer) engine->renderer->ImGuiRenderDrawData(ImGui::GetDrawData());
    
    
}

void Editor::SetupDocking()
{
    ImGuiIO& io = ImGui::GetIO();


    float menuBarHeight = ImGui::GetFrameHeight();
    ImVec2 displaySize = io.DisplaySize;

    // If the display size changed (window resized or DPI changed), rebuild the docking layout
    if (displaySize.x != lastDisplaySize.x || displaySize.y != lastDisplaySize.y)
    {
        dockingInitialized = false;
        lastDisplaySize = displaySize;
    }

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

    
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    dockSpaceID = dockspace_id;

    
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    
    if (!dockingInitialized)
    {
        
        LayoutManager& lm = LayoutManager::GetInstance();
        if (lm.GetNeedsReloadDock())
        {
            
            lm.ClearNeedsReloadDock();

            
            ImGui::DockBuilderFinish(dockspace_id);
            ImGui::End();
            return;
        }

        dockingInitialized = true;

        
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(displaySize.x, displaySize.y - menuBarHeight));

        
        ImGuiID dock_id_left, dock_id_right, dock_id_center;

        
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.3f, &dock_id_left, &dock_id_center);
        
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Right, 0.42f, &dock_id_right, &dock_id_center);
        

        
        ImGuiID dock_id_left_top, dock_id_left_bottom;
        ImGui::DockBuilderSplitNode(dock_id_left, ImGuiDir_Up, 0.5f, &dock_id_left_top, &dock_id_left_bottom);

        
        ImGuiID dock_id_center_top, dock_id_center_bottom;
        ImGui::DockBuilderSplitNode(dock_id_center, ImGuiDir_Down, 0.5f, &dock_id_center_bottom, &dock_id_center_top);

        
        ImGui::DockBuilderDockWindow("Scene", dock_id_left_top);
        ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left_bottom);
        ImGui::DockBuilderDockWindow("Game", dock_id_center_top);
        ImGui::DockBuilderDockWindow("Project", dock_id_center_bottom);
        
        
        ImGui::DockBuilderDockWindow("Console", dock_id_center_bottom);
        ImGui::DockBuilderDockWindow("Inspector", dock_id_right);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

bool Editor::ApplyGameViewportToInput() const
{
    if (!hasGameViewport)
        return false;

    Input::SetGameViewport(gameViewportX, gameViewportY, gameViewportW, gameViewportH,
        gameViewportContentW, gameViewportContentH);
    return true;
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
            GameObject* parent = engine && engine->scene ? engine->scene->rootGameObject.get() : nullptr;
            if (ImGui::MenuItem("Create Empty") && parent)
            {
                PushUndoSnapshot();
                GameObject* obj = parent->AddChild(std::make_unique<GameObject>("GameObject"));
                SelectCreatedObject(this, obj);
            }
            if (ImGui::MenuItem("Camera") && parent)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateCameraObject(parent));
            }
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional Light") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateDirectionalLightObject(parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("UI"))
            {
                if (ImGui::MenuItem("Canvas") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateCanvasObject(parent));
                }
                if (ImGui::MenuItem("Panel") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateUIPanelObject(FindOrCreateCanvas(parent, parent)));
                }
                if (ImGui::MenuItem("Image") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateUIImageObject(FindOrCreateCanvas(parent, parent)));
                }
                if (ImGui::MenuItem("Text") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateUITextObject(FindOrCreateCanvas(parent, parent)));
                }
                if (ImGui::MenuItem("Button") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateUIButtonObject(FindOrCreateCanvas(parent, parent)));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("2D Object"))
            {
                if (ImGui::MenuItem("Square") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateSpriteObject(parent, "Sprites/Square.png"));
                }
                if (ImGui::MenuItem("Circle") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateSpriteObject(parent, "Sprites/Circle.png"));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("3D Object"))
            {
                if (ImGui::MenuItem("Cube") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateCubeObject(parent));
                }
                if (ImGui::MenuItem("Sphere") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateSphereObject(parent));
                }
                if (ImGui::MenuItem("Quad") && parent)
                {
                    PushUndoSnapshot();
                    SelectCreatedObject(this, CreateQuadObject(parent));
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Component"))
        {
            if (ImGui::MenuItem("Add Component...")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem("Asset Health"))
            {
                if (m_assetHealthWindow)
                {
                    m_assetHealthWindow->Open();
                    m_assetHealthWindow->RefreshDiagnostics();
                }
            }
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

        
        
        
        
        
        const ImVec4 grey       = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
        const ImVec4 greyHover  = ImVec4(grey.x + 0.06f, grey.y + 0.06f, grey.z + 0.06f, 1.0f);
        const ImVec4 greyActive = ImVec4(grey.x + 0.03f, grey.y + 0.03f, grey.z + 0.03f, 1.0f);
        const ImVec4 blue       = ImVec4(0.26f, 0.59f, 1.00f, 1.00f);
        const ImVec4 blueHover  = ImVec4(0.31f, 0.65f, 1.00f, 1.00f);
        const ImVec4 blueActive = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);

        
        
        
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
                m_playModeEntrySnapshot = CaptureEditorSnapshot();
                m_hasPlayModeEntrySnapshot = !m_playModeEntrySnapshot.sceneData.empty();

                
                m_tempScenePath = "../../Ditto/Ditto/Temp/PlayModeScene.scene";
                std::filesystem::create_directories("../../Ditto/Ditto/Temp");
                engine->scene->SaveScene(m_tempScenePath);

                engine->SetEngineState(Engine::Play);
            }
            else if (engine->state == Engine::Play)
            {
                
                engine->SetEngineState(Engine::Stop);
                StopAndRestoreScene();
            }
            else if (engine->state == Engine::Pause)
            {
                
                
                engine->SetEngineState(Engine::Stop);
                StopAndRestoreScene();
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + buttonSize + spacing);

        
        
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
                
                engine->SetEngineState(Engine::Play);
            }
            
        }
        ImGui::PopStyleColor(3);

        ImGui::EndMainMenuBar();
    }
}

void Editor::DrawLayoutMenu()
{
    if (ImGui::BeginMenu("Layout"))
    {
        
        if (ImGui::MenuItem("Save Layout..."))
        {
            showSaveLayoutPopup = true;
        }

        ImGui::Separator();

        
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
    
    
}

void Editor::LoadLayout(const std::string& layoutName)
{
    
    if (LayoutManager::GetInstance().LoadLayout(layoutName))
    {
        
        ImGui::DockContextClearNodes(GImGui, 0, true); 
        
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
    
    void* icon = isRoot ? GetDittoIcon() : GetGameObjectIconForObject(obj);
    
    
    float indent = depth * 18.0f;
    
    if (hasChildren) {
        bool isExpanded = m_expandedGameObjects.find(obj) != m_expandedGameObjects.end();
        
        
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::ArrowButton(("##arrow_" + std::to_string((uintptr_t)obj)).c_str(), isExpanded ? ImGuiDir_Down : ImGuiDir_Right)) {
            if (isExpanded) m_expandedGameObjects.erase(obj);
            else m_expandedGameObjects.insert(obj);
        }
        ImGui::PopStyleVar();
        
        
        if (icon) {
            ImGui::SameLine();
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
        }
        
        
        ImGui::SameLine();
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        
        std::string displayName = obj->name;
        if (isRoot && sceneDirty) {
            displayName += " *";
        }
        
        if (ImGui::Selectable(displayName.c_str(), activeSelection == obj)) {
            activeSelection = obj;
            selectedComponent = nullptr;
            if (!lockingSelection) {
                selectedObject = obj;
                selectedFile.Clear();
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && m_sceneWindow)
            m_sceneWindow->FrameObject(obj);
        
        if (isRoot) {
            ImGui::PopStyleColor();
        }
        
        
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
                    
                    
                    
                    m_pendingReparentSource = droppedObj;
                    m_pendingReparentTarget = obj;
                }
            }
            ImGui::EndDragDropTarget();
        }

        
        if (isExpanded) {
            for (auto& child : obj->children)
                DrawGameObjectNode(child.get(), false, depth + 1);
        }
    } else {
        
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
        
        if (icon) {
            ImGui::Image((void*)(intptr_t)icon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SameLine();
        }
        
        if (isRoot) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        
        
        std::string displayName = obj->name;
        if (isRoot && sceneDirty) {
            displayName += " *";
        }
        
        if (ImGui::Selectable(displayName.c_str(), activeSelection == obj)) {
            activeSelection = obj;
            selectedComponent = nullptr;
            if (!lockingSelection) {
                selectedObject = obj;
                selectedFile.Clear();
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && m_sceneWindow)
            m_sceneWindow->FrameObject(obj);
        
        if (isRoot) {
            ImGui::PopStyleColor();
        }
        
        
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
                    
                    m_pendingReparentSource = droppedObj;
                    m_pendingReparentTarget = obj;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    
    
    if (ImGui::BeginPopupContextItem(("GameObjectContext_" + std::to_string((uintptr_t)obj)).c_str()))
    {
        
        bool isSelectedRoot = (selectedObject == engine->scene->rootGameObject.get());
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
            GameObject* newObj = obj->AddChild(std::make_unique<GameObject>("New GameObject"));
            SelectCreatedObject(this, newObj);
        }
        if (ImGui::MenuItem("Camera"))
        {
            PushUndoSnapshot();
            SelectCreatedObject(this, CreateCameraObject(obj));
        }
        if (ImGui::BeginMenu("Light"))
        {
            if (ImGui::MenuItem("Directional Light"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateDirectionalLightObject(obj));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI"))
        {
            if (ImGui::MenuItem("Canvas"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateCanvasObject(obj));
            }
            if (ImGui::MenuItem("Panel"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIPanelObject(FindOrCreateCanvas(engine->scene->rootGameObject.get(), obj)));
            }
            if (ImGui::MenuItem("Image"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIImageObject(FindOrCreateCanvas(engine->scene->rootGameObject.get(), obj)));
            }
            if (ImGui::MenuItem("Text"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUITextObject(FindOrCreateCanvas(engine->scene->rootGameObject.get(), obj)));
            }
            if (ImGui::MenuItem("Button"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIButtonObject(FindOrCreateCanvas(engine->scene->rootGameObject.get(), obj)));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("2D Object"))
        {
            if (ImGui::MenuItem("Square"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSpriteObject(obj, "Sprites/Square.png"));
            }
            if (ImGui::MenuItem("Circle"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSpriteObject(obj, "Sprites/Circle.png"));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("3D Object"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateCubeObject(obj));
            }
            if (ImGui::MenuItem("Sphere"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSphereObject(obj));
            }
            if (ImGui::MenuItem("Quad"))
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateQuadObject(obj));
            }
            ImGui::EndMenu();
        }
        
        if (!obj->prefabSourcePath.empty())
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Apply Prefab"))
                ApplySelectedPrefabInstance();
            if (ImGui::MenuItem("Revert Prefab"))
                RevertSelectedPrefabInstance();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy")) m_pendingCopy = true;
        if (ImGui::MenuItem("Delete")) m_pendingDelete = true;
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

void Editor::DrawHierarchy()
{
    ImGui::Begin("Hierarchy");
    
    ImGui::BeginChild("HierarchyContent", ImVec2(0, 0), true);

    
    if (ImGui::BeginPopupContextWindow("HierarchyContextWindow"))
    {
        GameObject* root = engine && engine->scene ? engine->scene->rootGameObject.get() : nullptr;
        if (ImGui::MenuItem("Create Empty") && root)
        {
            PushUndoSnapshot();
            SelectCreatedObject(this, root->AddChild(std::make_unique<GameObject>("GameObject")));
        }
        if (ImGui::MenuItem("Camera") && root)
        {
            PushUndoSnapshot();
            SelectCreatedObject(this, CreateCameraObject(root));
        }
        if (ImGui::BeginMenu("Light"))
        {
            if (ImGui::MenuItem("Directional Light") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateDirectionalLightObject(root));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI"))
        {
            if (ImGui::MenuItem("Canvas") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateCanvasObject(root));
            }
            if (ImGui::MenuItem("Panel") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIPanelObject(FindOrCreateCanvas(root, root)));
            }
            if (ImGui::MenuItem("Image") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIImageObject(FindOrCreateCanvas(root, root)));
            }
            if (ImGui::MenuItem("Text") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUITextObject(FindOrCreateCanvas(root, root)));
            }
            if (ImGui::MenuItem("Button") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateUIButtonObject(FindOrCreateCanvas(root, root)));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("2D Object"))
        {
            if (ImGui::MenuItem("Square") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSpriteObject(root, "Sprites/Square.png"));
            }
            if (ImGui::MenuItem("Circle") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSpriteObject(root, "Sprites/Circle.png"));
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("3D Object"))
        {
            if (ImGui::MenuItem("Cube") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateCubeObject(root));
            }
            if (ImGui::MenuItem("Sphere") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateSphereObject(root));
            }
            if (ImGui::MenuItem("Quad") && root)
            {
                PushUndoSnapshot();
                SelectCreatedObject(this, CreateQuadObject(root));
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
                
                
                m_pendingReparentSource = droppedObj;
                m_pendingReparentTarget = engine->scene->rootGameObject.get();
            }
        }
        ImGui::EndDragDropTarget();
    }

    
    
    
    DrawGameObjectNode(engine->scene->rootGameObject.get(), true);

    
    
    
    if (m_pendingReparentSource && m_pendingReparentTarget)
    {
        PushUndoSnapshot();
        m_pendingReparentTarget->AddChild(m_pendingReparentSource);   
        engine->scene->MarkDirty();
    }
    m_pendingReparentSource = nullptr;
    m_pendingReparentTarget = nullptr;

    if (m_pendingCopy)   { m_pendingCopy = false;   CopySelectedObject(); }
    if (m_pendingDelete) { m_pendingDelete = false; DeleteSelectedObject(); }

    
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        
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
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("Game", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    static const char* resolutionNames[] = { "Free Aspect", "16:9 1280x720", "16:9 1920x1080", "4:3 1024x768", "Portrait 1080x1920" };
    static const ImVec2 resolutionSizes[] = {
        ImVec2(0.0f, 0.0f),
        ImVec2(1280.0f, 720.0f),
        ImVec2(1920.0f, 1080.0f),
        ImVec2(1024.0f, 768.0f),
        ImVec2(1080.0f, 1920.0f),
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
    ImGui::SetNextItemWidth(78.0f);
    ImGui::Combo("##GameDisplay", &gameResolutionIndex, resolutionNames, IM_ARRAYSIZE(resolutionNames));
    ImGui::SameLine();
    ImGui::TextUnformatted("Display 1");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##GameAspect", &gameResolutionIndex, resolutionNames, IM_ARRAYSIZE(resolutionNames));
    ImGui::SameLine();
    ImGui::TextUnformatted("Scale");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##GameScale", &gameViewScale, 0.1f, 1.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Button("Play Focused");
    ImGui::SameLine();
    static bool gameStats = false;
    ImGui::Checkbox("Stats", &gameStats);
    ImGui::SameLine();
    static bool gameGizmos = true;
    ImGui::Checkbox("Gizmos", &gameGizmos);
    ImGui::PopStyleVar();

    
    
    ImVec2 contentMin = ImGui::GetCursorScreenPos();
    ImVec2 contentAvail = ImGui::GetContentRegionAvail();
    ImVec2 renderSize = contentAvail;
    if (gameResolutionIndex > 0)
    {
        ImVec2 target = resolutionSizes[gameResolutionIndex];
        float fit = glm::min(contentAvail.x / target.x, contentAvail.y / target.y);
        fit = glm::max(0.01f, fit) * glm::clamp(gameViewScale, 0.1f, 1.0f);
        renderSize = ImVec2(target.x * fit, target.y * fit);
    }
    else
    {
        renderSize = ImVec2(glm::max(1.0f, contentAvail.x), glm::max(1.0f, contentAvail.y));
    }
    ImVec2 renderMin(contentMin.x + glm::max(0.0f, (contentAvail.x - renderSize.x) * 0.5f),
        contentMin.y + glm::max(0.0f, (contentAvail.y - renderSize.y) * 0.5f));
    ImRect gameViewportRect(renderMin, ImVec2(renderMin.x + renderSize.x, renderMin.y + renderSize.y));
    int renderW = gameResolutionIndex > 0 ? (int)resolutionSizes[gameResolutionIndex].x : (int)renderSize.x;
    int renderH = gameResolutionIndex > 0 ? (int)resolutionSizes[gameResolutionIndex].y : (int)renderSize.y;
    hasGameViewport = true;
    gameViewportX = gameViewportRect.Min.x;
    gameViewportY = gameViewportRect.Min.y;
    gameViewportW = gameViewportRect.Max.x - gameViewportRect.Min.x;
    gameViewportH = gameViewportRect.Max.y - gameViewportRect.Min.y;
    gameViewportContentW = (float)glm::max(1, renderW);
    gameViewportContentH = (float)glm::max(1, renderH);
    
    
    ApplyGameViewportToInput();
    void* gameTex = engine->RenderSceneToTexture(glm::max(1, renderW), glm::max(1, renderH), true);
    if (gameTex)
    {
        ImVec2 uv0, uv1;
        EditorRHI::RenderTargetImageUV(engine ? engine->renderer.get() : nullptr, uv0, uv1);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)gameTex,
            gameViewportRect.Min, gameViewportRect.Max, uv0, uv1);
    }
    ImGui::GetWindowDrawList()->AddRect(gameViewportRect.Min, gameViewportRect.Max, IM_COL32(0, 0, 0, 180));
    if (gameStats)
    {
        glm::vec2 rawMouse = Input::GetRawMousePosition();
        glm::vec2 localMouse = Input::GetMousePosition();
        bool inside = Input::IsMouseInsideGameViewport();
        char stats[256];
        snprintf(stats, sizeof(stats), "%dx%d  %.1f FPS\nraw %.0f,%.0f  game %.0f,%.0f  %s\nrect %.0f,%.0f %.0fx%.0f",
            renderW, renderH, fps,
            rawMouse.x, rawMouse.y,
            localMouse.x, localMouse.y,
            inside ? "inside" : "outside",
            gameViewportX, gameViewportY, gameViewportW, gameViewportH);
        ImGui::GetWindowDrawList()->AddText(ImVec2(gameViewportRect.Min.x + 8.0f, gameViewportRect.Min.y + 8.0f),
            IM_COL32(230, 230, 230, 255), stats);
    }

    
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        bool collapsed = ImGui::IsWindowCollapsed();
        
    }

    ImGui::End();
    ImGui::PopStyleColor();
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
        selectedFile.Clear();  
        ImGui::Text("Path"); ImGui::SameLine();
        static char loadPathBuffer[256] = "Assets/Scenes/scene.bin";
        ImGui::InputText("##Path", loadPathBuffer, sizeof(loadPathBuffer));

        if (ImGui::Button("Load", ImVec2(120, 0)))
        {
            if (engine && engine->scene && engine->scene->LoadScene(loadPathBuffer))
            {
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    
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

    
    
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Settings", &showBuildSettingsWindow))
    {
        Project* proj = ProjectManager::GetInstance().GetCurrentProject();
        if (!proj)
        {
            ImGui::TextDisabled("No project loaded");
            ImGui::End();
            ImGui::PopStyleColor();
            return;
        }
        
        
        ImGui::Text("Platform:");
        const char* platforms[] = { "Windows", "Android" };
        int currentPlatform = (int)buildSettings.platform;
        if (ImGui::Combo("##Platform", &currentPlatform, platforms, IM_ARRAYSIZE(platforms)))
        {
            buildSettings.platform = (BuildPlatform)currentPlatform;
            buildSettings.outputPath = BuildSystem::GetDefaultOutputPath(proj->path, buildSettings.platform);
        }
        
        
        ImGui::Text("Configuration:");
        const char* configs[] = { "Debug", "Release" };
        int currentConfig = (int)buildSettings.configuration;
        if (ImGui::Combo("##Config", &currentConfig, configs, IM_ARRAYSIZE(configs)))
        {
            buildSettings.configuration = (BuildConfiguration)currentConfig;
        }
        
        ImGui::Separator();
        
        
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
        
        
        ImGui::Text("Scenes In Build:");
        
        
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
                
                
                ImGui::Text("%d", (int)i);
                ImGui::SameLine(30);
                
                if (ImGui::Selectable(sceneName.c_str(), isSelected))
                {
                    buildSettings.startupScene = sceneName;
                }
                
                
                if (isSelected)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Startup)");
                }
            }
        }
        
        ImGui::EndChild();
        
        
        ImGui::Text("Startup Scene: %s", buildSettings.startupScene.empty() ? "None" : buildSettings.startupScene.c_str());
        
        ImGui::Separator();
        
        
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
            
        }
        
        
        ImGui::Checkbox("Development Build", &buildSettings.developmentBuild);
        if (buildSettings.developmentBuild)
        {
            ImGui::Checkbox("Script Debugging", &buildSettings.enableScriptDebugging);
        }
        
        ImGui::Separator();
        
        
        if (isBuilding)
        {
            ImGui::Text("Building: %s", buildStatus.c_str());
            ImGui::ProgressBar(buildProgress, ImVec2(-1, 0));
        }
        
        
        ImGui::BeginDisabled(isBuilding);
        
        if (ImGui::Button("Build", ImVec2(120, 30)))
        {
            std::string error;
            if (BuildSystem::ValidateSettings(buildSettings, error))
            {
                isBuilding = true;
                buildProgress = 0.0f;
                buildStatus = "Starting...";
                
                
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
        
        
        if (!buildStatus.empty())
        {
            ImGui::Separator();
            ImGui::Text("Status: %s", buildStatus.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}



Editor::EditorSnapshot Editor::CaptureEditorSnapshot() const
{
    EditorSnapshot snapshot;
    if (!engine || !engine->scene) return snapshot;

    snapshot.sceneData = engine->scene->CaptureSnapshot();

    GameObject* root = engine->scene->rootGameObject.get();
    GameObject* current = selectedObject ? selectedObject : activeSelection;
    if (root && current)
    {
        snapshot.hasSelectedObject = BuildGameObjectPath(root, current, snapshot.selectedObjectPath);
    }

    snapshot.expandedObjectPaths = CaptureExpandedObjectPaths(root, m_expandedGameObjects);

    if (selectedComponent && selectedComponent->gameObject == current)
    {
        const auto& components = current->components;
        for (size_t i = 0; i < components.size(); ++i)
        {
            if (components[i].get() == selectedComponent)
            {
                snapshot.selectedComponentOrdinal = static_cast<int>(i);
                break;
            }
        }
    }

    return snapshot;
}

void Editor::RestoreEditorSelection(const EditorSnapshot& snapshot)
{
    selectedObject = nullptr;
    activeSelection = nullptr;
    selectedComponent = nullptr;
    selectedFile.Clear();
    m_expandedGameObjects.clear();

    if (!engine || !engine->scene || !engine->scene->rootGameObject) return;

    for (const std::vector<int>& path : snapshot.expandedObjectPaths)
        if (GameObject* expanded = ResolveGameObjectPath(engine->scene->rootGameObject.get(), path))
            m_expandedGameObjects.insert(expanded);

    if (!snapshot.hasSelectedObject) return;

    GameObject* node = ResolveGameObjectPath(engine->scene->rootGameObject.get(), snapshot.selectedObjectPath);
    if (!node) return;

    selectedObject = node;
    activeSelection = node;
    if (snapshot.selectedComponentOrdinal >= 0 &&
        snapshot.selectedComponentOrdinal < static_cast<int>(node->components.size()))
    {
        selectedComponent = node->components[snapshot.selectedComponentOrdinal].get();
    }
}

void Editor::PushUndoSnapshot()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;   
    EditorSnapshot snap = CaptureEditorSnapshot();
    if (snap.sceneData.empty()) return;
    if (!m_undoStack.empty() && m_undoStack.back().sceneData == snap.sceneData) return;  
    m_undoStack.push_back(std::move(snap));
    if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void Editor::BeginInspectorEdit()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_hasPendingEdit) return;                       
    m_pendingPreEdit = CaptureEditorSnapshot();
    m_hasPendingEdit = true;
}

void Editor::EndInspectorEdit()
{
    if (!m_hasPendingEdit) return;
    m_hasPendingEdit = false;
    if (!engine || !engine->scene) return;
    
    
    if (engine->scene->CaptureSnapshot() == m_pendingPreEdit.sceneData) return;
    m_undoStack.push_back(std::move(m_pendingPreEdit));
    if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void Editor::Undo()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_undoStack.empty()) return;

    std::vector<GameObjectPathKey> expandedPaths =
        CaptureExpandedObjectPathKeys(engine->scene->rootGameObject.get(), m_expandedGameObjects);
    EditorSnapshot current = CaptureEditorSnapshot();
    EditorSnapshot prev = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    if (engine->scene->RestoreSnapshot(prev.sceneData))
    {
        m_redoStack.push_back(std::move(current));
        if (m_redoStack.size() > kUndoDepth) m_redoStack.erase(m_redoStack.begin());
        RestoreEditorSelection(prev);
        RestoreExpandedObjectPathKeys(engine->scene->rootGameObject.get(), expandedPaths, m_expandedGameObjects);
        m_hasPendingEdit = false;
        sceneDirty = true;
    }
    else
    {
        m_undoStack.push_back(std::move(prev));  
    }
}

void Editor::Redo()
{
    if (!engine || !engine->scene) return;
    if (engine->state == Engine::State::Play) return;
    if (m_redoStack.empty()) return;

    std::vector<GameObjectPathKey> expandedPaths =
        CaptureExpandedObjectPathKeys(engine->scene->rootGameObject.get(), m_expandedGameObjects);
    EditorSnapshot current = CaptureEditorSnapshot();
    EditorSnapshot next = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    if (engine->scene->RestoreSnapshot(next.sceneData))
    {
        m_undoStack.push_back(std::move(current));
        if (m_undoStack.size() > kUndoDepth) m_undoStack.erase(m_undoStack.begin());
        RestoreEditorSelection(next);
        RestoreExpandedObjectPathKeys(engine->scene->rootGameObject.get(), expandedPaths, m_expandedGameObjects);
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
    auto clone = std::make_unique<GameObject>(selectedObject);
    
    
    GameObject* attachTo = selectedObject->parent
        ? selectedObject->parent
        : engine->scene->rootGameObject.get();
    GameObject* copiedObject = attachTo ? attachTo->AddChild(std::move(clone)) : nullptr;
    if (!copiedObject) return;

    engine->scene->RegisterSubtree(copiedObject);
    selectedObject = copiedObject;
    activeSelection = copiedObject;
    selectedComponent = nullptr;
    selectedFile.Clear();
    engine->scene->MarkDirty();  
}

void Editor::DeleteSelectedObject()
{
    if (!selectedObject || !engine || !engine->scene) return;

    if (selectedObject == engine->scene->rootGameObject.get())
    {
        return;
    }

    PushUndoSnapshot();   

    
    
    
    GameObject* objectToDelete = selectedObject;
    GameObject* parent = selectedObject->parent;
    if (!parent) return;

    auto siblingIt = std::find_if(parent->children.begin(), parent->children.end(),
        [objectToDelete](const std::unique_ptr<GameObject>& child) { return child.get() == objectToDelete; });
    size_t siblingIndex = siblingIt != parent->children.end()
        ? static_cast<size_t>(std::distance(parent->children.begin(), siblingIt))
        : parent->children.size();

    GameObject* nextSelection = nullptr;
    if (siblingIndex + 1 < parent->children.size())
        nextSelection = parent->children[siblingIndex + 1].get();
    else if (siblingIndex > 0 && siblingIndex - 1 < parent->children.size())
        nextSelection = parent->children[siblingIndex - 1].get();
    else
        nextSelection = parent;

    RemoveExpandedSubtree(objectToDelete, m_expandedGameObjects);
    std::unique_ptr<GameObject> owned = parent->DetachChild(objectToDelete);

    engine->scene->UnregisterSubtree(objectToDelete);
    owned.reset();

    selectedObject = nextSelection;
    activeSelection = nextSelection;
    selectedComponent = nullptr;
    selectedFile.Clear();

    engine->scene->MarkDirty();  
}

void Editor::DeleteSelectedFile()
{
    if (!selectedFile.IsValid()) return;

    try {
        fs::remove(selectedFile.path);
        selectedFile.Clear();
    }
    catch (const std::exception& e) {
        DITTO_LOG_ERROR_STREAM("Delete file failed: " << e.what() );
    }
}

void Editor::DuplicateSelectedFile()
{
    if (!selectedFile.IsValid()) return;

    
    std::string newName = selectedFile.name + "_copy" + selectedFile.extension;
    std::string newPath = selectedFile.path;
    size_t pos = newPath.rfind(selectedFile.name + selectedFile.extension);
    if (pos != std::string::npos) {
        newPath.replace(pos, selectedFile.name.size() + selectedFile.extension.size(), newName);
    }

    try {
        fs::copy_file(selectedFile.path, newPath, fs::copy_options::overwrite_existing);
        
        selectedFile.path = newPath;
        selectedFile.name = selectedFile.name + "_copy";
    }
    catch (const std::exception& e) {
        DITTO_LOG_ERROR_STREAM("Duplicate file failed: " << e.what() );
    }
}


void Editor::DrawProjectSelector()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground;

    ImGui::Begin("ProjectSelector", nullptr, flags);

    
    float windowWidth = ImGui::GetIO().DisplaySize.x;
    float windowHeight = ImGui::GetIO().DisplaySize.y;
    
    ImGui::SetCursorPosX((windowWidth - 200) * 0.5f);
    ImGui::SetCursorPosY(windowHeight * 0.15f);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::Text("Ditto Engine");
    ImGui::SetWindowFontScale(1.0f);

    
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
                DITTO_LOG_ERROR_STREAM("Failed to delete project: " << e.what() );
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

        
        Project* proj = pm.GetCurrentProject();
        if (proj && !proj->lastScene.empty())
        {
            std::string fullPath = proj->path + "/" + proj->lastScene;
            DITTO_LOG_INFO_STREAM("[Editor] Loading last scene: " << fullPath );
            
            if (engine && engine->scene)
            {
                if (engine->scene->LoadScene(fullPath.c_str()))
                {
                    DITTO_LOG_INFO_STREAM("[Editor] Scene loaded successfully: " << engine->scene->name );
                    DITTO_LOG_INFO_STREAM("[Editor] GameObject count: " << engine->scene->gameObjects.size() );
                    
                    DITTO_LOG_INFO_STREAM("[Editor] RootGameObject children: " << engine->scene->rootGameObject->children.size() );
                }
                else
                {
                    DITTO_LOG_ERROR_STREAM("[Editor] Failed to load scene: " << fullPath );
                    
                    
                    engine->scene->name = "Default";
                    engine->scene->ClearScene();
                }
                
                
                strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
                sceneDirty = false;  
                
                
                engine->scene->onModified = [this]() {
                    this->sceneDirty = true;
                };
            }
        }
        else
        {
            DITTO_LOG_INFO_STREAM("[Editor] No last scene to load, creating default scene" );
            
            if (engine && engine->scene)
            {
                
                
                engine->scene->name = "Default";
                engine->scene->ClearScene();
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
        
        
        strcpy_s(sceneNameBuffer, sizeof(sceneNameBuffer), engine->scene->name.c_str());
        sceneDirty = false;  
        
        
        engine->scene->onModified = [this]() {
            this->sceneDirty = true;
        };

        
        Project* proj = ProjectManager::GetInstance().GetCurrentProject();
        if (proj)
        {
            
            size_t pos = scenePath.find("/Assets/");
            if (pos != std::string::npos)
            {
                proj->lastScene = scenePath.substr(pos + 1);

                if (!Ditto::JsonConfig::UpdateProjectLastScene(fs::path(proj->path) / "project.json", proj->lastScene))
                    DITTO_LOG_WARN_STREAM("[Editor] Failed to update project lastScene: " << proj->lastScene);
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
        DITTO_LOG_ERROR_STREAM("Error reading scenes: " << e.what() );
    }

    return scenes;
}

void Editor::OnScriptComponentDropped(const std::string& scriptPath)
{
    if (!selectedObject)
    {
        DITTO_LOG_INFO_STREAM("[Editor] No object selected to add script" );
        return;
    }

    OnScriptComponentDroppedToObject(selectedObject, scriptPath);
}

void Editor::OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath)
{
    if (!obj)
    {
        DITTO_LOG_INFO_STREAM("[Editor] No object to add script" );
        return;
    }

    
    fs::path p(scriptPath);
    std::string ext = p.extension().string();
    std::string scriptName = p.stem().string();
    
    DITTO_LOG_INFO_STREAM("[Editor] Adding script: " << scriptName << " (" << ext << ") to " << obj->name );
    
    if (ext == ".cs")
    {
        PushUndoSnapshot();   
        
        
        CSharpScriptComponent* csScript = obj->AddComponent<CSharpScriptComponent>();
        csScript->scriptPath = scriptPath;
        csScript->scriptName = std::filesystem::path(scriptPath).filename().stem().string();
        csScript->ParseScriptFields();
        DITTO_LOG_INFO_STREAM("[Editor] C# script added: " << csScript->scriptName );
        
        
        sceneDirty = true;
    }
}

bool Editor::InstantiatePrefabToScene(const std::string& prefabPath)
{
    if (!engine || !engine->scene || prefabPath.empty())
        return false;

    std::unique_ptr<GameObject> instance = Ditto::PrefabAsset::Instantiate(prefabPath);
    if (!instance)
        return false;

    PushUndoSnapshot();
    GameObject* parent = selectedObject ? selectedObject : engine->scene->rootGameObject.get();
    if (!parent)
        return false;

    GameObject* created = parent->AddChild(std::move(instance));
    engine->scene->RegisterSubtree(created);
    SelectCreatedObject(this, created);
    sceneDirty = true;
    engine->scene->MarkDirty();
    DITTO_LOG_INFO_STREAM("[Editor] Instantiated prefab: " << prefabPath);
    return true;
}

bool Editor::SaveSelectedObjectAsPrefab(const std::string& prefabPath)
{
    if (!selectedObject || !engine || !engine->scene || prefabPath.empty())
        return false;

    if (selectedObject == engine->scene->rootGameObject.get())
    {
        DITTO_LOG_WARN("[Editor] Scene root cannot be saved as a prefab");
        return false;
    }

    if (!Ditto::PrefabAsset::Save(*selectedObject, prefabPath))
        return false;

    selectedObject->prefabSourcePath = Ditto::AssetPath::NormalizeAssetKey(prefabPath);
    selectedObject->prefabSourceGuid = Ditto::AssetDatabase::Get().EnsureMetaForAsset(prefabPath);
    DITTO_LOG_INFO_STREAM("[Editor] Saved prefab: " << prefabPath);
    return true;
}

bool Editor::ApplySelectedPrefabInstance()
{
    if (!selectedObject || selectedObject->prefabSourcePath.empty())
        return false;

    if (!Ditto::PrefabAsset::Apply(*selectedObject))
        return false;

    DITTO_LOG_INFO_STREAM("[Editor] Applied prefab instance: " << selectedObject->prefabSourcePath);
    return true;
}

bool Editor::RevertSelectedPrefabInstance()
{
    if (!selectedObject || !engine || !engine->scene || selectedObject->prefabSourcePath.empty())
        return false;

    PushUndoSnapshot();
    engine->scene->UnregisterSubtree(selectedObject);
    if (!Ditto::PrefabAsset::Revert(*selectedObject))
        return false;

    engine->scene->RegisterSubtree(selectedObject);
    activeSelection = selectedObject;
    selectedComponent = nullptr;
    sceneDirty = true;
    engine->scene->MarkDirty();
    DITTO_LOG_INFO_STREAM("[Editor] Reverted prefab instance: " << selectedObject->prefabSourcePath);
    return true;
}

void Editor::SaveCurrentScene()
{
    if (!engine || !engine->scene)
    {
        DITTO_LOG_ERROR_STREAM("[Editor] No scene to save" );
        return;
    }

    
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    std::string savePath;
    
    if (proj)
    {
        
        savePath = proj->path + "/Assets/Scenes/" + engine->scene->name + ".bin";
    }
    else
    {
        
        savePath = "Assets/Scenes/" + engine->scene->name + ".bin";
    }
    
    DITTO_LOG_INFO_STREAM("[Editor] Saving scene to: " << savePath );
    
    if (engine->scene->SaveScene(savePath.c_str()))
    {
        DITTO_LOG_INFO_STREAM("[Editor] Scene saved successfully" );
        sceneDirty = false;  
        
        
        if (proj)
        {
            
            proj->lastScene = "Assets/Scenes/" + engine->scene->name + ".bin";
        }
    }
    else
    {
        DITTO_LOG_ERROR_STREAM("[Editor] Failed to save scene" );
    }
}

void Editor::BuildProject()
{
    if (!engine || !engine->scene)
    {
        DITTO_LOG_ERROR_STREAM("[Editor] No scene to build" );
        return;
    }

    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        DITTO_LOG_ERROR_STREAM("[Editor] No project loaded" );
        return;
    }

    
    std::string projectPath = proj->path;
    std::replace(projectPath.begin(), projectPath.end(), '\\', '/');
    std::string outputDir = projectPath + "/Build/Windows";
    
    try
    {
        
        if (!fs::exists(outputDir))
        {
            fs::create_directories(outputDir);
        }

        
        std::string assetsSrc = projectPath + "/Assets";
        
        if (fs::exists(assetsSrc))
        {
            
            std::string assetsDst = outputDir;
            fs::remove_all(assetsDst + "/Assets");
            fs::copy(assetsSrc, assetsDst + "/Assets", fs::copy_options::recursive);
            DITTO_LOG_INFO_STREAM("[Editor] Copied Assets to " << assetsDst );
        }

        
        std::string sceneName = engine->scene->name;
        std::string sceneSrc = projectPath + "/Assets/Scenes/" + sceneName + ".bin";
        
        
        fs::create_directories(outputDir + "/Assets/Scenes");
        
        
        engine->scene->SaveScene(sceneSrc.c_str());
        if (fs::exists(sceneSrc))
        {
            std::string sceneDst = outputDir + "/Assets/Scenes/" + sceneName + ".bin";
            fs::copy(sceneSrc, sceneDst, fs::copy_options::overwrite_existing);
            DITTO_LOG_INFO_STREAM("[Editor] Copied scene: " << sceneDst );
        }

        
        std::string projectJsonSrc = projectPath + "/project.json";
        if (fs::exists(projectJsonSrc))
        {
            fs::copy(projectJsonSrc, outputDir + "/project.json", fs::copy_options::overwrite_existing);
            DITTO_LOG_INFO_STREAM("[Editor] Copied project.json to " << outputDir );
        }

        
        std::string exeSrc = projectPath + "/../../x64/Debug/Ditto.exe";
        std::string exeDst = outputDir + "/" + proj->name + ".exe";
        if (fs::exists(exeSrc))
        {
            fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
            DITTO_LOG_INFO_STREAM("[Editor] Copied executable: " << exeDst );
        }
        else
        {
            exeSrc = projectPath + "/../../../x64/Debug/Ditto.exe";
            if (fs::exists(exeSrc))
            {
                fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
                DITTO_LOG_INFO_STREAM("[Editor] Copied executable: " << exeDst );
            }
            else
            {
                exeSrc = projectPath + "/../../Ditto/x64/Debug/Ditto.exe";
                if (fs::exists(exeSrc))
                {
                    fs::copy(exeSrc, exeDst, fs::copy_options::overwrite_existing);
                    DITTO_LOG_INFO_STREAM("[Editor] Copied executable: " << exeDst );
                }
            }
        }

        
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
            
            for (const auto& entry : fs::directory_iterator(thirdPartySrc))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".dll")
                {
                    std::string dllDst = outputDir + "/" + entry.path().filename().string();
                    fs::copy(entry.path(), dllDst, fs::copy_options::overwrite_existing);
                    DITTO_LOG_INFO_STREAM("[Editor] Copied DLL: " << dllDst );
                }
            }
        }

        
        std::string batPath = outputDir + "/Run.bat";
        std::ofstream batFile(batPath);
        batFile << "@echo off\n";
        batFile << "echo Starting " << proj->name << "...\n";
        batFile << "cd /d \"%~dp0\"\n";
        batFile << "\"" << proj->name << ".exe\"\n";
        batFile << "pause\n";
        batFile.close();
        DITTO_LOG_INFO_STREAM("[Editor] Created startup script: " << batPath );
        
        DITTO_LOG_INFO_STREAM("[Editor] Build completed: " << outputDir );
        
        
        std::wstring outputDirW = fs::absolute(outputDir).wstring();
        ShellExecuteW(NULL, L"open", outputDirW.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Editor] Build failed: " << e.what() );
    }
}

void Editor::BuildScripts()
{
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        DITTO_LOG_ERROR_STREAM("[Editor] No project loaded" );
        return;
    }
    
    
    std::string scriptsDir = proj->path + "/Assets/Scripts";
    std::string outputDll = proj->path + "/Scripts.dll";
    
    if (!fs::exists(scriptsDir))
    {
        DITTO_LOG_INFO_STREAM("[Editor] Scripts directory does not exist: " << scriptsDir );
        return;
    }
    
    
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
        DITTO_LOG_INFO_STREAM("[Editor] No script files found" );
        return;
    }
    
    DITTO_LOG_INFO_STREAM("[Editor] Building " << cppFiles.size() << " script(s)..." );
    
    
    fs::path projPathAbs = fs::absolute(proj->path);
    
    fs::path enginePathAbs = fs::absolute(proj->path + "/../..");
    fs::path outputDllAbs = fs::absolute(outputDll);
    
    
    std::string compileFiles;
    for (const auto& f : cppFiles)
    {
        compileFiles += "\"" + fs::absolute(f).string() + "\" ";
    }
    
    
    std::string batPath = proj->path + "/build_scripts.bat";
    std::ofstream batFile(batPath);
    
    batFile << "@echo off\n";
    batFile << "cd /d \"" << projPathAbs.string() << "\"\n";
    std::string vcvarsPath = FindVCVarsPath();
    if (!vcvarsPath.empty())
        batFile << "call \"" << vcvarsPath << "\"\n";
    else
        batFile << "echo Warning: vcvars64.bat not found, compilation may fail\n";
    
    
    std::string clCmd = "cl /LD /EHsc /std:c++latest /I\"" + enginePathAbs.string() + "\\3rdParty\\GLM\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\\include\" /I\"" + enginePathAbs.string() + "\\3rdParty\\ImGui\" /I\"" + enginePathAbs.string() + "\\Engine\\Core\" /I\"" + enginePathAbs.string() + "\\Engine\\Graphics\" /I\"" + enginePathAbs.string() + "\\Engine\\Physics\" /I\"" + enginePathAbs.string() + "\\3rdParty\\GLFW\" /I\"" + enginePathAbs.string() + "\" /D\"SCRIPT_DLL\" /O2 /MD " + compileFiles + "/Fe:\"" + outputDllAbs.string() + "\"";
    
    batFile << clCmd << "\n";
    batFile << "pause\n";
    
    batFile.close();
    
    DITTO_LOG_INFO_STREAM("[Editor] Please run: " << batPath );
    DITTO_LOG_INFO_STREAM("[Editor] Or manually compile your scripts and place DLL at: " << outputDll );
    
    
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
        
        
        if (fs::exists(outputDll))
        {
            DITTO_LOG_INFO_STREAM("[Editor] DLL built: " << outputDll );
        }
        else
        {
            DITTO_LOG_INFO_STREAM("[Editor] DLL not found after build. Check the console window for errors." );
        }
    }
}




static const char* s_iconFiles[] = {
    "Default.png", "Cs.png", "Model.png", "Prefab.png", "Shader.png", "Scene.png", "Texture2D.png", "Material.png"
};

static const char* s_sceneToolbarIconFiles[] = {
    "SceneView2D.png",
    "SceneViewLighting.png",
    "SceneViewLightingOff.png",
    "SceneViewAudio.png",
    "SceneViewAudioOff.png",
    "SceneViewFx.png",
    "SceneViewCamera.png",
    "SceneViewVisibility.png",
    "SceneGrid.png",
    "SceneHandTool.png",
    "SceneMoveTool.png",
    "SceneRotateTool.png",
    "SceneScaleTool.png",
    "SceneTransformTool.png",
    "SceneRectTool.png",
    "SceneViewTools.png",
    "ScenePivot.png",
    "SceneCenter.png",
    "SceneLocal.png",
    "SceneGlobal.png",
};

void Editor::StopAndRestoreScene()
{
    
    
    if (!m_tempScenePath.empty() && std::filesystem::exists(m_tempScenePath))
    {
        engine->scene->LoadScene(m_tempScenePath);
    }

    if (m_hasPlayModeEntrySnapshot)
        RestoreEditorSelection(m_playModeEntrySnapshot);
    else
    {
        selectedObject = nullptr;
        activeSelection = nullptr;
        selectedComponent = nullptr;
        selectedFile.Clear();
        m_expandedGameObjects.clear();
    }
    m_hasPlayModeEntrySnapshot = false;

    engine->state = Engine::Edit;
}

void Editor::InitFileIcons()
{
    if (m_fileIconsInitialized) return;
    
    
    m_assetsPath = FindEditorAssetsPath() + "/Icons";
    DITTO_LOG_INFO_STREAM("[FileIcon] Initializing from: " << m_assetsPath );
    
    
    for (int i = 0; i < 8; i++) {
        std::string path = m_assetsPath + "/" + s_iconFiles[i];
        m_icons[i] = LoadIcon(path);
    }
    
    
    m_folderIcon = LoadIcon(m_assetsPath + "/Folder.png");
    m_folderEmptyIcon = LoadIcon(m_assetsPath + "/FolderEmpty.png");
    m_folderOpenedIcon = LoadIcon(m_assetsPath + "/FolderOpened Icon.png");
    
    
    m_dittoIcon = LoadIcon(m_assetsPath + "/Scene.png");
    m_gameObjectIcon = LoadIcon(m_assetsPath + "/GameObject.png");
    m_cameraIcon = LoadIcon(m_assetsPath + "/Camera.png");
    m_spriteIcon = LoadIcon(m_assetsPath + "/Sprite.png");
    m_spriteRendererIcon = LoadIcon(m_assetsPath + "/SpriteRenderer.png");
    m_rectTransformIcon = LoadIcon(m_assetsPath + "/RectTransform.png");
    
    
    m_lockIcon = LoadIcon(m_assetsPath + "/Lock.png");
    m_unlockIcon = LoadIcon(m_assetsPath + "/UnLock.png");

    
    m_playIcon = LoadIcon(m_assetsPath + "/Play.png");
    m_pauseIcon = LoadIcon(m_assetsPath + "/Pause.png");
    m_stopIcon = LoadIcon(m_assetsPath + "/Scene.png"); 
    m_objectPickerIcon = LoadIcon(m_assetsPath + "/ObjectPicker.png");
    for (int i = 0; i < static_cast<int>(SceneToolbarIcon::Count); ++i)
        m_sceneToolbarIcons[i] = LoadIcon(m_assetsPath + "/" + s_sceneToolbarIconFiles[i]);

    m_fileIconsInitialized = true;
    DITTO_LOG_INFO_STREAM("[FileIcon] Initialized successfully" );
}

Ditto::TextureHandle Editor::LoadIcon(const std::string& iconPath)
{
    namespace fs = std::filesystem;

    if (!fs::exists(iconPath)) {
        DITTO_LOG_ERROR_STREAM("[FileIcon] File not found: " << iconPath );
        return {};
    }
    if (!engine || !engine->renderer) return {};

    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);

    if (!data) {
        DITTO_LOG_ERROR_STREAM("[FileIcon] Failed to load: " << iconPath );
        return {};
    }

    Ditto::TextureHandle tex = engine->renderer->CreateTexture(data, width, height, 4);
    stbi_image_free(data);

    DITTO_LOG_INFO_STREAM("[FileIcon] Loaded: " << iconPath << " (" << width << "x" << height << ")" );
    return tex;
}



void* Editor::IconTexID(Ditto::TextureHandle h)
{
    return (engine && engine->renderer) ? engine->renderer->GetImGuiTextureID(h) : nullptr;
}

int Editor::GetIconIndex(const std::string& ext)
{
    if (ext == ".cs") return 1;  
    if (ext == ".obj" || ext == ".fbx" || ext == ".mesh") return 2;  
    if (ext == ".prefab") return 3;  
    if (ext == ".shader" || ext == ".hlsl" || ext == ".glsl" || ext == ".vert" || ext == ".frag") return 4;  
    if (ext == ".bin") return 5;  
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") return 6;  
    if (ext == ".mat" || ext == ".physmat2d") return 7;  
    return 0;  
}

void* Editor::GetIconByExtension(const std::string& extension)
{
    if (!m_fileIconsInitialized) return nullptr;

    
    if (extension == ".objectpicker")
        return IconTexID(m_objectPickerIcon);

    int idx = GetIconIndex(extension);   
    return IconTexID(m_icons[idx]);
}


void* Editor::GetFolderIcon()       { return IconTexID(m_folderIcon); }
void* Editor::GetFolderEmptyIcon()  { return IconTexID(m_folderEmptyIcon); }
void* Editor::GetFolderOpenedIcon() { return IconTexID(m_folderOpenedIcon); }
void* Editor::GetDittoIcon()        { return IconTexID(m_dittoIcon); }
void* Editor::GetGameObjectIcon()   { return IconTexID(m_gameObjectIcon); }
void* Editor::GetCameraIcon()       { return IconTexID(m_cameraIcon); }
void* Editor::GetSpriteIcon()       { return IconTexID(m_spriteIcon); }
void* Editor::GetSpriteRendererIcon(){ return IconTexID(m_spriteRendererIcon ? m_spriteRendererIcon : m_spriteIcon); }
void* Editor::GetRectTransformIcon(){ return IconTexID(m_rectTransformIcon); }
void* Editor::GetGameObjectIconForObject(GameObject* obj)
{
    if (!obj) return GetGameObjectIcon();
    if (obj->GetComponent<CameraComponent>()) return GetCameraIcon();
    if (obj->GetComponent<CanvasComponent>() || obj->GetComponent<RectTransformComponent>() ||
        obj->GetComponent<UIImageComponent>() || obj->GetComponent<UITextComponent>() ||
        obj->GetComponent<UIButtonComponent>() || obj->name == "Canvas")
        return GetRectTransformIcon();
    if (obj->GetComponent<SpriteRendererComponent>())
        return GetSpriteRendererIcon();
    if (RendererComponent* renderer = obj->GetComponent<RendererComponent>())
        if (renderer->meshPath == "__sprite_quad__")
            return GetSpriteIcon();
    return GetGameObjectIcon();
}
void* Editor::GetLockIcon()         { return IconTexID(m_lockIcon); }
void* Editor::GetUnlockIcon()       { return IconTexID(m_unlockIcon); }
void* Editor::GetPlayIcon()         { return IconTexID(m_playIcon); }
void* Editor::GetPauseIcon()        { return IconTexID(m_pauseIcon); }
void* Editor::GetStopIcon()         { return IconTexID(m_stopIcon); }
void* Editor::GetSceneIcon(SceneToolbarIcon icon)
{
    int idx = static_cast<int>(icon);
    if (idx < 0 || idx >= static_cast<int>(SceneToolbarIcon::Count)) return nullptr;
    return IconTexID(m_sceneToolbarIcons[idx]);
}

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
        r->DestroyTexture(m_cameraIcon);
        r->DestroyTexture(m_spriteIcon);
        r->DestroyTexture(m_spriteRendererIcon);
        r->DestroyTexture(m_rectTransformIcon);
        r->DestroyTexture(m_lockIcon);
        r->DestroyTexture(m_unlockIcon);
        r->DestroyTexture(m_playIcon);
        r->DestroyTexture(m_pauseIcon);
        r->DestroyTexture(m_stopIcon);
        for (auto& h : m_sceneToolbarIcons) r->DestroyTexture(h);
    }

    for (auto& h : m_icons) h = {};
    m_folderIcon = m_folderEmptyIcon = m_folderOpenedIcon = {};
    m_dittoIcon = m_gameObjectIcon = m_lockIcon = m_unlockIcon = {};
    m_cameraIcon = m_spriteIcon = m_spriteRendererIcon = m_rectTransformIcon = {};
    m_playIcon = m_pauseIcon = m_stopIcon = {};
    for (auto& h : m_sceneToolbarIcons) h = {};

    m_fileIconsInitialized = false;
    DITTO_LOG_INFO_STREAM("[FileIcon] Cleaned up" );
}
