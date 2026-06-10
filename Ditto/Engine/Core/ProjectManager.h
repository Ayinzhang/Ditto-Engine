#pragma once
#include <string>
#include <vector>
#include <memory>
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
    
    std::vector<Project> GetAllProjects();
    
    bool CreateProject(const std::string& name);
    
    bool OpenProject(const std::string& projectPath);
    
    void CloseProject();
    
    Project* GetCurrentProject() { return currentProject.get(); }
    
    std::string GetProjectAssetsPath() const;
    std::string GetProjectScenesPath() const;

private:
    ProjectManager() = default;
    ~ProjectManager() = default;
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    std::string projectsDirectory;
    std::unique_ptr<Project> currentProject;
    
    void EnsureDirectoryExists(const std::string& path);
    std::string GetProjectFilePath(const Project& project) const;
};