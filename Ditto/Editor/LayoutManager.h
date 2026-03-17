#pragma once
#include <string>
#include <vector>
#include <map>
#include "../3rdParty/GLM/glm.hpp"

// 前向声明ImVec2
struct ImVec2;

struct WindowLayout
{
    std::string name;
    glm::vec2 pos;
    glm::vec2 size;
    bool visible;
    bool collapsed;
};

struct EditorLayout
{
    std::string name;
    std::map<std::string, WindowLayout> windows;
    float menuBarHeight;
};

class LayoutManager
{
public:
    static LayoutManager& GetInstance()
    {
        static LayoutManager instance;
        return instance;
    }

    void Initialize(const std::string& settingsPath);
    
    // 保存布局
    bool SaveLayout(const std::string& layoutName);
    
    // 加载布局
    bool LoadLayout(const std::string& layoutName);
    
    // 删除布局
    bool DeleteLayout(const std::string& layoutName);
    
    // 获取所有布局名称
    std::vector<std::string> GetAllLayoutNames();
    
    // 保存当前窗口状态
    void SaveCurrentWindowState(const std::string& windowName, const ImVec2& pos, const ImVec2& size, bool visible, bool collapsed);
    
    // 获取当前布局
    EditorLayout& GetCurrentLayout() { return currentLayout; }

private:
    LayoutManager() = default;
    ~LayoutManager() = default;
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    std::string settingsDirectory;
    std::string GetLayoutFilePath(const std::string& layoutName);
    EditorLayout currentLayout;
    
    void EnsureDirectoryExists();
};
