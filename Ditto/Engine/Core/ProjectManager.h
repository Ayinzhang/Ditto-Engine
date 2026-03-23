#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "Scene.h"

struct Project
{
    std::string name;
    std::string path;
    std::string lastScene;
};

class ProjectManager
{
public:
    static ProjectManager& GetInstance()
    {
        static ProjectManager instance;
        return instance;
    }

    void Initialize(const std::string& projectsPath);
    
    // 获取所有项目
    std::vector<Project> GetAllProjects();
    
    // 创建新项目
    bool CreateProject(const std::string& name);
    
    // 打开项目
    bool OpenProject(const std::string& projectPath);
    
    // 关闭当前项目
    void CloseProject();
    
    // 获取当前项目
    Project* GetCurrentProject() { return currentProject; }
    
    // 获取项目资源路径
    std::string GetProjectAssetsPath() const;
    std::string GetProjectScenesPath() const;

private:
    ProjectManager() = default;
    ~ProjectManager() = default;
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    std::string projectsDirectory;
    Project* currentProject = nullptr;
    
    void EnsureDirectoryExists(const std::string& path);
    std::string GetProjectFilePath(const Project& project) const;
};