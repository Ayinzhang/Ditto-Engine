#pragma once
#include <string>
#include <vector>
#include <set>
#include <memory>
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "BuildSystem.h"
#include "../Engine/Graphics/RHI/IRenderer.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui.h"
#include "../Engine/Core/CSharpScript.h"

struct Engine; struct GameObject; struct Project; struct Camera; struct Shader; struct SceneWindow;

struct SelectedFile 
{
	std::string path, name, extension, folder; // full path, file name with extension, file extension, parent folder

    bool IsValid() const { return !path.empty(); }
    void Clear() { path.clear(); name.clear(); extension.clear(); folder.clear(); }
};

struct Editor
{
    struct EditorSnapshot
    {
        std::string sceneData;
        bool hasSelectedObject = false;
        std::vector<int> selectedObjectPath;
        int selectedComponentOrdinal = -1;
        std::vector<std::vector<int>> expandedObjectPaths;
    };

    Engine* engine = nullptr;
    GameObject* selectedObject = nullptr, *activeSelection = nullptr;
    Component* selectedComponent = nullptr;
    std::set<GameObject*> m_expandedGameObjects;
    SelectedFile selectedFile;
    char sceneNameBuffer[16] = "Default";
    char layoutNameBuffer[32] = "Default";
    char projectNameBuffer[32] = "MyProject";
    bool isSceneActive;
    bool showSavePopup, showLoadPopup, showSaveLayoutPopup;
    bool showBuildPopup = false;
    bool showBuildSettingsWindow = false;
    bool isBuilding = false;
    float buildProgress = 0.0f;
    std::string buildStatus;
    BuildSettings buildSettings;
    bool showProjectManager = false;
    bool showNewProjectPopup = false;
    bool showRenameProjectPopup = false;
    std::string renameProjectOldPath;
    char renameProjectBuffer[64] = "";
    bool showProjectSelector = false;
    bool projectLoaded = false;
    bool sceneDirty = false;
    bool lockingSelection = false;

    // ---- Undo / Redo (memento: full-scene snapshots) ----
    std::vector<EditorSnapshot> m_undoStack;   // snapshots of pre-change scene + selection states
    std::vector<EditorSnapshot> m_redoStack;
    EditorSnapshot m_pendingPreEdit;           // pre-edit snapshot captured at drag start
    bool m_hasPendingEdit = false;
    static constexpr size_t kUndoDepth = 64;
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    void* m_glfwWindow = nullptr;       // GLFWwindow* (for lazy ImGui backend init)
    bool m_imguiBackendInit = false;    // ImGui platform+renderer backend initialized
    int frame; float fps, ppf, deltaTime;
    
    std::string m_tempScenePath;

    Editor(void* window, bool gameMode = false, const std::string& projectPath = "");
    ~Editor();
    void Draw();
    void DrawProjectSelector();
    

    bool gameMode = false;
    std::string gameProjectPath;
    void DrawProjectManager();
    void DrawToolbar();
    void DrawHierarchy();
    void DrawScene();
    void DrawGame();
    void DrawProject();
    void DrawInspector();
    void DrawPopups();
    void DrawBuildSettingsWindow();
    void DrawLayoutMenu();

    void SetupDocking();
    void SaveCurrentLayout();
    void LoadLayout(const std::string& layoutName);
    std::vector<std::string> GetSavedLayouts();

    void OpenProject(const std::string& projectPath);
    void LoadSceneFromProject(const std::string& scenePath);
    std::vector<std::string> GetProjectScenes();

    // Save current scene (with dirty check)
    void SaveCurrentScene();
    
    // Mark scene as modified
    void MarkSceneDirty() { sceneDirty = true; }

    // Undo / Redo. PushUndoSnapshot is called immediately BEFORE a discrete
    // mutation; Begin/EndInspectorEdit bracket continuous edits (drags) so each
    // drag is one undo step committed only if the value actually changed.
    void PushUndoSnapshot();
    void BeginInspectorEdit();
    void EndInspectorEdit();
    void Undo();
    void Redo();
    EditorSnapshot CaptureEditorSnapshot() const;
    void RestoreEditorSelection(const EditorSnapshot& snapshot);
    
    // Build and package for release
    void BuildProject();
    
    // Compile scripts to DLL
    void BuildScripts();

    // Stops the play session, reloads the temp scene snapshot, and resets
    // editor selection state. Leaves the engine in Engine::Edit.
    void StopAndRestoreScene();

    // Script drag-drop handling
    void OnScriptComponentDropped(const std::string& scriptPath);
    void OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath);

    // 3D model preview related (delegated to InspectorWindow)
    void InitModelPreview() { if (m_inspectorWindow) m_inspectorWindow->InitModelPreview(); }
    void LoadPreviewModel(const std::string& modelPath) { if (m_inspectorWindow) m_inspectorWindow->LoadPreviewModel(modelPath); }
    void CleanupModelPreview() { if (m_inspectorWindow) m_inspectorWindow->CleanupModelPreview(); }
    void AddConsoleMessage(const std::string& message) { if (m_projectWindow) m_projectWindow->AddConsoleMessage(message); }
    void ImportExternalFilesToProject(const std::vector<std::string>& paths);

    void DrawGameObjectNode(GameObject* obj, bool isRoot = false, int depth = 0);
    void CopySelectedObject();
    void DeleteSelectedObject();

    // Deferred hierarchy mutations. Structural changes (reparent / delete /
    // duplicate) requested from inside DrawGameObjectNode would invalidate the
    // `children` iterators of every ancestor draw frame (the vectors hold
    // unique_ptr now, so an erase destroys the object on the spot). The
    // handlers only RECORD the request here; DrawHierarchy applies it after
    // the whole tree has been drawn.
    GameObject* m_pendingReparentSource = nullptr;
    GameObject* m_pendingReparentTarget = nullptr;
    bool m_pendingCopy = false;
    bool m_pendingDelete = false;
    void DeleteSelectedFile();
    void DuplicateSelectedFile();

    // File icon related
    void InitFileIcons();
    void CleanupFileIcons();
    // Icon getters return an ImGui texture id (backend-specific void*), resolved
    // from a stored RHI TextureHandle. Out-of-line because they need engine->renderer.
    void* GetIconByExtension(const std::string& extension);
    void* GetFolderIcon();
    void* GetFolderEmptyIcon();
    void* GetFolderOpenedIcon();
    void* GetDittoIcon();
    void* GetGameObjectIcon();
    void* GetGameObjectIconForObject(GameObject* obj);
    void* GetCameraIcon();
    void* GetSpriteIcon();
    void* GetRectTransformIcon();
    void* GetLockIcon();
    void* GetUnlockIcon();
    void* GetPlayIcon();
    void* GetPauseIcon();
    // m_stopIcon reserved for a future Stop.png asset; not currently used in the toolbar.
    void* GetStopIcon();

private:
    // File icons (RHI texture handles; GPU textures owned by engine->renderer).
    Ditto::TextureHandle m_icons[7];  // 0:Default, 1:Cpp, 2:Prefab, 3:Text, 4:Shader, 5:Scene, 6:Folder
    Ditto::TextureHandle m_folderIcon;
    Ditto::TextureHandle m_folderEmptyIcon;
    Ditto::TextureHandle m_folderOpenedIcon;
    Ditto::TextureHandle m_dittoIcon;
    Ditto::TextureHandle m_gameObjectIcon;
    Ditto::TextureHandle m_cameraIcon;
    Ditto::TextureHandle m_spriteIcon;
    Ditto::TextureHandle m_rectTransformIcon;
    Ditto::TextureHandle m_lockIcon;
    Ditto::TextureHandle m_unlockIcon;
    Ditto::TextureHandle m_playIcon;
    Ditto::TextureHandle m_pauseIcon;
    Ditto::TextureHandle m_stopIcon;
    bool m_fileIconsInitialized = false;
    std::string m_assetsPath;

    // Load single icon into an RHI texture; resolve a handle to an ImGui id.
    Ditto::TextureHandle LoadIcon(const std::string& iconPath);
    void* IconTexID(Ditto::TextureHandle h);
    int GetIconIndex(const std::string& ext);

    // Window components (owned; SceneWindow is forward-declared, so ~Editor
    // must stay defined in Editor.cpp where SceneWindow.h is included).
    std::unique_ptr<ProjectWindow> m_projectWindow;
    std::unique_ptr<InspectorWindow> m_inspectorWindow;
    std::unique_ptr<SceneWindow> m_sceneWindow;
};
