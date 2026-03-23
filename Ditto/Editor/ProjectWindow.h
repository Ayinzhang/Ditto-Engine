#pragma once
#include <string>
#include "../3rdParty/GLM/glm.hpp"

// 前向声明
struct Editor;
struct Project;

class ProjectWindow
{
public:
    ProjectWindow(Editor* editor);
    ~ProjectWindow() = default;

    void Draw();

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
    
    // 创建文件夹弹窗
    bool m_showCreateFolderPopup = false;
    char m_newFolderNameBuffer[64] = "NewFolder";
    
    // 创建场景弹窗
    bool m_showCreateScenePopup = false;
    char m_newSceneNameBuffer[64] = "NewScene";
    
    // 标签页状态
    int m_activeTab = 0;
    
    // Console消息
    std::vector<std::string> m_consoleMessages;
    
public:
    // 添加Console消息
    void AddConsoleMessage(const std::string& message);
    
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
    
    // 打开 C# 文件相关
    void OpenCSharpFile(const std::string& filePath);
    bool CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName);
    std::string GetVisualStudioPath();

    // 创建新脚本文件
    void CreateNewScript(const std::string& name);
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
