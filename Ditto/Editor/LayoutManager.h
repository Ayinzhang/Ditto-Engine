#pragma once
#include <string>
#include <vector>
#include <map>

class LayoutManager
{
public:
    static LayoutManager& GetInstance()
    {
        static LayoutManager instance;
        return instance;
    }

    void Initialize(const std::string& settingsPath);
    
    
    bool SaveLayout(const std::string& layoutName);
    bool LoadLayout(const std::string& layoutName);
    bool DeleteLayout(const std::string& layoutName);
    std::vector<std::string> GetAllLayoutNames();
    
    
    void SetNeedsReloadDock() { needsReloadDock = true; }
    bool GetNeedsReloadDock() const { return needsReloadDock; }
    void ClearNeedsReloadDock() { needsReloadDock = false; }

private:
    LayoutManager() = default;
    ~LayoutManager() = default;
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    std::string settingsDirectory;
    std::string GetLayoutFilePath(const std::string& layoutName);
    void EnsureDirectoryExists();
    
    bool needsReloadDock = false;
};