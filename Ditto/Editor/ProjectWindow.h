#pragma once
#include <string>
#include <set>
#include <vector>
#include <unordered_map>
#include "../Engine/Graphics/RHI/IRenderer.h"
#include "../3rdParty/GLM/glm.hpp"

// 前向声明
struct Editor;
struct Project;

class ProjectWindow
{
public:
    ProjectWindow(Editor* editor);
    ~ProjectWindow();

    void Draw();
    void ImportExternalFiles(const std::vector<std::string>& paths);

    // 回调：加载场景
    void OnLoadScene(const std::string& scenePath);

    // 回调：选中了文件（用于Inspector显示文件信息）
    void OnFileSelected(const std::string& path, const std::string& name, 
                        const std::string& ext, const std::string& folder);

    // 拖拽脚本到 Inspector 时调用
    void OnScriptDropped(const std::string& scriptPath);

private:
    Editor* m_editor = nullptr;

    // 静态状态（对应原 DrawProject 中的 static 变量）
    std::string m_currentFolder = "Assets/Scenes";
    float m_splitterPos = 150.0f;

    // 创建脚本弹窗
    bool m_showCreateScriptPopup = false;
    char m_newScriptNameBuffer[64] = "NewScript";

    bool m_showCreateMaterialPopup = false;
    char m_newMaterialNameBuffer[64] = "New Material";
    
    // 创建文件夹弹窗
    bool m_showCreateFolderPopup = false;
    char m_newFolderNameBuffer[64] = "NewFolder";
    
    // 创建场景弹窗
    bool m_showCreateScenePopup = false;
    char m_newSceneNameBuffer[64] = "NewScene";
    
    // 标签页状态
    int m_activeTab = 0;

    // Console消息（旧接口保留；显示数据现统一来自 Ditto::Logger）
    std::vector<std::string> m_consoleMessages;

    // Console 停靠：首帧把 Console dock 到 Project 所在节点（之后完全自由拖动）。
    // ImGuiID 即 unsigned int，避免在头文件引入 imgui.h。
    unsigned int m_projectDockId = 0;
    bool m_consoleDockInitialized = false;

    // Console 视图状态（Unity 风格：分级过滤 / 折叠 / 自动滚动）
    bool m_consoleShowInfo = true;
    bool m_consoleShowWarning = true;
    bool m_consoleShowError = true;
    bool m_consoleCollapse = true;
    bool m_consoleAutoScroll = true;

public:
    // 添加Console消息
    void AddConsoleMessage(const std::string& message);

    // 绘制独立的 Console 窗口（与 Project 同级、可拖动停靠）
    void DrawConsoleWindow();
    
private:

    // 重命名弹窗
    bool m_showRenamePopup = false;
    std::string m_renameTargetPath;
    std::string m_renameTargetOldName;
    char m_renameBuffer[64] = "";
    
    // 单击/双击判断
    std::string m_lastClickedFilePath;
    std::string m_lastClickedFolderPath;
    double m_lastClickTime = 0.0;
    static constexpr double DOUBLE_CLICK_THRESHOLD = 0.5;  // 500ms
    
    // 文件夹展开状态
    std::set<std::string> m_expandedFolders;
    std::unordered_map<std::string, Ditto::TextureHandle> m_thumbnailCache;
    bool IsFolderExpanded(const std::string& path) const;
    void ToggleFolderExpanded(const std::string& path);
    Ditto::TextureHandle GetOrCreateThumbnail(const std::string& filePath, const std::string& ext);
    
    // 打开 C# 文件相关
    void OpenCSharpFile(const std::string& filePath);
    bool CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName);
    std::string GetVisualStudioPath();

    // 创建新脚本文件
    void CreateNewScript(const std::string& name);
    void CreateNewMaterial(const std::string& name);
    // 创建新文件夹
    void CreateNewFolder(const std::string& name);
    // 创建新场景
    void CreateNewScene(const std::string& name);
    
    // 重命名文件
    void RenameFile(const std::string& oldPath, const std::string& newName);
    
    // 绘制弹窗
    void DrawPopups();
    
    // 绘制Console
    void DrawConsole();
};
