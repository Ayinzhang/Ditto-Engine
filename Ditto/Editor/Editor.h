#pragma once
#include <string>
#include <vector>
#include <set>
#include <memory>
#include "ProjectWindow.h"
#include "InspectorWindow.h"
#include "BuildSystem.h"
#include "AssetHealthWindow.h"
#include "../Engine/Graphics/RHI/IRenderer.h"
#include "../3rdParty/GLM/glm.hpp"
#include "../3rdParty/ImGui/imgui.h"
#include "../Engine/Core/CSharpScript.h"

struct Engine; struct GameObject; struct Project; struct Camera; struct Shader; struct SceneWindow;
namespace Ditto { class IWindow; }

enum class SceneToolbarIcon
{
    View2D,
    Lighting,
    LightingOff,
    Audio,
    AudioOff,
    Fx,
    Camera,
    Visibility,
    Grid,
    Hand,
    Move,
    Rotate,
    Scale,
    Transform,
    Rect,
    Tools,
    Pivot,
    Center,
    Local,
    Global,
    Count
};

struct SelectedFile 
{
	std::string path, name, extension, folder; 

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
    int gameResolutionIndex = 0;
    float gameViewScale = 1.0f;
    bool hasGameViewport = false;
    float gameViewportX = 0.0f, gameViewportY = 0.0f;
    float gameViewportW = 1.0f, gameViewportH = 1.0f;
    float gameViewportContentW = 1.0f, gameViewportContentH = 1.0f;

    
    std::vector<EditorSnapshot> m_undoStack;   
    std::vector<EditorSnapshot> m_redoStack;
    EditorSnapshot m_pendingPreEdit;           
    EditorSnapshot m_playModeEntrySnapshot;
    bool m_hasPendingEdit = false;
    bool m_hasPlayModeEntrySnapshot = false;
    static constexpr size_t kUndoDepth = 64;
    bool dockingInitialized = false;
    ImGuiID dockSpaceID = 0;
    ImVec2 lastDisplaySize = ImVec2(0.0f, 0.0f);
    Ditto::IWindow* m_window = nullptr; 
    bool m_imguiBackendInit = false;    
    int frame; float fps, ppf, deltaTime;
    
    std::string m_tempScenePath;

    Editor(Ditto::IWindow* window, bool gameMode = false, const std::string& projectPath = "");
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
    bool ApplyGameViewportToInput() const;
    void SaveCurrentLayout();
    void LoadLayout(const std::string& layoutName);
    std::vector<std::string> GetSavedLayouts();

    void OpenProject(const std::string& projectPath);
    void LoadSceneFromProject(const std::string& scenePath);
    std::vector<std::string> GetProjectScenes();

    
    void SaveCurrentScene();
    
    
    void MarkSceneDirty() { sceneDirty = true; }

    
    
    
    void PushUndoSnapshot();
    void BeginInspectorEdit();
    void EndInspectorEdit();
    void Undo();
    void Redo();
    EditorSnapshot CaptureEditorSnapshot() const;
    void RestoreEditorSelection(const EditorSnapshot& snapshot);
    
    
    void BuildProject();
    
    
    void BuildScripts();

    
    
    void StopAndRestoreScene();

    
    void OnScriptComponentDropped(const std::string& scriptPath);
    void OnScriptComponentDroppedToObject(GameObject* obj, const std::string& scriptPath);
    bool InstantiatePrefabToScene(const std::string& prefabPath);
    bool SaveSelectedObjectAsPrefab(const std::string& prefabPath);
    bool ApplySelectedPrefabInstance();
    bool RevertSelectedPrefabInstance();

    
    void InitModelPreview() { if (m_inspectorWindow) m_inspectorWindow->InitModelPreview(); }
    void LoadPreviewModel(const std::string& modelPath) { if (m_inspectorWindow) m_inspectorWindow->LoadPreviewModel(modelPath); }
    void CleanupModelPreview() { if (m_inspectorWindow) m_inspectorWindow->CleanupModelPreview(); }
    void AddConsoleMessage(const std::string& message) { if (m_projectWindow) m_projectWindow->AddConsoleMessage(message); }
    void ImportExternalFilesToProject(const std::vector<std::string>& paths);

    
    ProjectWindow* GetProjectWindow() { return m_projectWindow.get(); }

    void DrawGameObjectNode(GameObject* obj, bool isRoot = false, int depth = 0);
    void CopySelectedObject();
    void DeleteSelectedObject();

    
    
    
    
    
    
    GameObject* m_pendingReparentSource = nullptr;
    GameObject* m_pendingReparentTarget = nullptr;
    bool m_pendingCopy = false;
    bool m_pendingDelete = false;
    void DeleteSelectedFile();
    void DuplicateSelectedFile();

    
    void InitFileIcons();
    void CleanupFileIcons();
    
    
    void* GetIconByExtension(const std::string& extension);
    void* GetFolderIcon();
    void* GetFolderEmptyIcon();
    void* GetFolderOpenedIcon();
    void* GetDittoIcon();
    void* GetGameObjectIcon();
    void* GetGameObjectIconForObject(GameObject* obj);
    void* GetCameraIcon();
    void* GetSpriteIcon();
    void* GetSpriteRendererIcon();
    void* GetRectTransformIcon();
    void* GetLockIcon();
    void* GetUnlockIcon();
    void* GetPlayIcon();
    void* GetPauseIcon();
    
    void* GetStopIcon();
    void* GetSceneIcon(SceneToolbarIcon icon);

private:
    
    Ditto::TextureHandle m_icons[8];  
    Ditto::TextureHandle m_folderIcon;
    Ditto::TextureHandle m_folderEmptyIcon;
    Ditto::TextureHandle m_folderOpenedIcon;
    Ditto::TextureHandle m_dittoIcon;
    Ditto::TextureHandle m_gameObjectIcon;
    Ditto::TextureHandle m_cameraIcon;
    Ditto::TextureHandle m_spriteIcon;
    Ditto::TextureHandle m_spriteRendererIcon;
    Ditto::TextureHandle m_rectTransformIcon;
    Ditto::TextureHandle m_lockIcon;
    Ditto::TextureHandle m_unlockIcon;
    Ditto::TextureHandle m_playIcon;
    Ditto::TextureHandle m_pauseIcon;
    Ditto::TextureHandle m_stopIcon;
    Ditto::TextureHandle m_objectPickerIcon;
    Ditto::TextureHandle m_sceneToolbarIcons[static_cast<int>(SceneToolbarIcon::Count)];
    bool m_fileIconsInitialized = false;
    std::string m_assetsPath;

    
    Ditto::TextureHandle LoadIcon(const std::string& iconPath);
    void* IconTexID(Ditto::TextureHandle h);
    int GetIconIndex(const std::string& ext);

    
    
    std::unique_ptr<ProjectWindow> m_projectWindow;
    std::unique_ptr<InspectorWindow> m_inspectorWindow;
    std::unique_ptr<SceneWindow> m_sceneWindow;
    std::unique_ptr<AssetHealthWindow> m_assetHealthWindow;
};
