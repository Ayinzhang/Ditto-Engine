#pragma once
#include <string>
#include <vector>

// 文件图标管理器 - 负责加载和缓存文件类型图标
class FileIconManager
{
public:
    static FileIconManager& GetInstance();
    
    // 初始化图标资源目录
    void Initialize(const std::string& assetsPath);
    
    // 根据扩展名获取图标纹理ID
    unsigned int GetIconByExtension(const std::string& extension);
    
    // 获取文件夹图标
    unsigned int GetFolderIcon() { return m_folderIcon; }
    unsigned int GetFolderEmptyIcon() { return m_folderEmptyIcon; }
    
    // 加载更多文件夹图标
    void AddFolderIcon(const std::string& folderName, unsigned int icon) {}
    
    // 清理资源
    void Cleanup();

private:
    FileIconManager() = default;
    ~FileIconManager() = default;
    FileIconManager(const FileIconManager&) = delete;
    FileIconManager& operator=(const FileIconManager&) = delete;
    
    // 加载单个图标
    unsigned int LoadIcon(const std::string& iconPath);
    
    // 扩展名到图标索引的映射
    int GetIconIndex(const std::string& ext);
    
    std::string m_assetsPath;
    unsigned int m_icons[7] = {0};  // 0:Default, 1:Cpp, 2:Prefab, 3:Text, 4:Shader, 5:Scene, 6:Folder
    unsigned int m_folderIcon = 0;
    unsigned int m_folderEmptyIcon = 0;
    bool m_initialized = false;
    
    static const char* s_iconFiles[];
};