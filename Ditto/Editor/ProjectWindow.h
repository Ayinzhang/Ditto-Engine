#pragma once
#include <string>
#include <set>
#include <vector>
#include <unordered_map>
#include "../Engine/Graphics/RHI/IRenderer.h"
#include "../3rdParty/GLM/glm.hpp"


struct Editor;
struct Project;

class ProjectWindow
{
public:
    ProjectWindow(Editor* editor);
    ~ProjectWindow();

    void Draw();
    void ImportExternalFiles(const std::vector<std::string>& paths);

    
    void OnLoadScene(const std::string& scenePath);

    
    void OnFileSelected(const std::string& path, const std::string& name, 
                        const std::string& ext, const std::string& folder);

    
    void OnScriptDropped(const std::string& scriptPath);

    
    void NavigateToFile(const std::string& filePath);
    void OpenCSharpFile(const std::string& filePath, int line = 0, int column = 0);

private:
    Editor* m_editor = nullptr;

    
    std::string m_currentFolder = "Assets/Scenes";
    float m_splitterPos = 150.0f;

    
    bool m_showCreateScriptPopup = false;
    char m_newScriptNameBuffer[64] = "NewScript";

    bool m_showCreateMaterialPopup = false;
    char m_newMaterialNameBuffer[64] = "New Material";

    bool m_showCreatePhysicsMaterial2DPopup = false;
    char m_newPhysicsMaterial2DNameBuffer[64] = "New Physics Material 2D";

    bool m_showCreateShaderPopup = false;
    char m_newShaderNameBuffer[64] = "New Shader";

    bool m_showCreatePrefabPopup = false;
    char m_newPrefabNameBuffer[64] = "New Prefab";
    
    
    bool m_showCreateFolderPopup = false;
    char m_newFolderNameBuffer[64] = "NewFolder";
    
    
    bool m_showCreateScenePopup = false;
    char m_newSceneNameBuffer[64] = "NewScene";
    
    
    int m_activeTab = 0;

    
    std::vector<std::string> m_consoleMessages;

    
    
    unsigned int m_projectDockId = 0;
    bool m_consoleDockInitialized = false;

    
    bool m_consoleShowInfo = true;
    bool m_consoleShowWarning = true;
    bool m_consoleShowError = true;
    bool m_consoleCollapse = true;
    bool m_consoleAutoScroll = true;

public:
    
    void AddConsoleMessage(const std::string& message);

    
    void DrawConsoleWindow();
    
private:

    
    bool m_showRenamePopup = false;
    std::string m_renameTargetPath;
    std::string m_renameTargetOldName;
    char m_renameBuffer[64] = "";
    
    
    std::string m_lastClickedFilePath;
    std::string m_lastClickedFolderPath;
    double m_lastClickTime = 0.0;
    static constexpr double DOUBLE_CLICK_THRESHOLD = 0.5;  
    
    
    std::set<std::string> m_expandedFolders;
    std::unordered_map<std::string, Ditto::TextureHandle> m_thumbnailCache;
    bool IsFolderExpanded(const std::string& path) const;
    void ToggleFolderExpanded(const std::string& path);
    Ditto::TextureHandle GetOrCreateThumbnail(const std::string& filePath, const std::string& ext);
    
    
    bool CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName);
    std::string GetVisualStudioPath();

    
    void CreateNewScript(const std::string& name);
    void CreateNewMaterial(const std::string& name);
    void CreateNewPhysicsMaterial2D(const std::string& name);
    void CreateNewShader(const std::string& name);
    void CreateNewPrefab(const std::string& name);
    void SaveSelectedObjectAsPrefab(const std::string& name);
    
    void CreateNewFolder(const std::string& name);
    
    void CreateNewScene(const std::string& name);
    
    
    void RenameFile(const std::string& oldPath, const std::string& newName);
    
    
    void DrawPopups();
    
    
    void DrawConsole();
};
