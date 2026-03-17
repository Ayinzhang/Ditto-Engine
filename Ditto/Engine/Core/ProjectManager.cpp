#include "ProjectManager.h"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

void ProjectManager::Initialize(const std::string& projectsPath)
{
    projectsDirectory = projectsPath;
    EnsureDirectoryExists(projectsDirectory);
}

void ProjectManager::EnsureDirectoryExists(const std::string& path)
{
    if (!fs::exists(path))
    {
        fs::create_directories(path);
    }
}

std::vector<Project> ProjectManager::GetAllProjects()
{
    std::vector<Project> projects;
    
    try
    {
        if (fs::exists(projectsDirectory))
        {
            for (const auto& entry : fs::directory_iterator(projectsDirectory))
            {
                if (entry.is_directory())
                {
                    Project project;
                    project.name = entry.path().filename().string();
                    project.path = entry.path().string();
                    
                    // 读取项目配置文件
                    std::string projectFile = project.path + "/project.json";
                    if (fs::exists(projectFile))
                    {
                        // 简单解析 - 读取lastScene
                        std::ifstream file(projectFile);
                        std::string line;
                        while (std::getline(file, line))
                        {
                            if (line.find("\"lastScene\"") != std::string::npos)
                            {
                                size_t start = line.find(": \"") + 3;
                                size_t end = line.find("\"", start);
                                if (start != std::string::npos && end != std::string::npos)
                                {
                                    project.lastScene = line.substr(start, end - start);
                                }
                            }
                        }
                    }
                    
                    projects.push_back(project);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading projects: " << e.what() << std::endl;
    }
    
    return projects;
}

bool ProjectManager::CreateProject(const std::string& name)
{
    if (name.empty()) return false;
    
    std::string projectPath = projectsDirectory + "/" + name;
    
    if (fs::exists(projectPath))
    {
        std::cerr << "Project already exists: " << projectPath << std::endl;
        return false;
    }
    
    // 创建项目目录结构
    EnsureDirectoryExists(projectPath + "/Assets");
    EnsureDirectoryExists(projectPath + "/Assets/Scenes");
    EnsureDirectoryExists(projectPath + "/Assets/Models");
    EnsureDirectoryExists(projectPath + "/Assets/Materials");
    EnsureDirectoryExists(projectPath + "/Assets/Prefabs");
    
    // 创建项目配置文件
    std::string projectFile = projectPath + "/project.json";
    std::ofstream file(projectFile);
    if (!file.is_open())
    {
        std::cerr << "Failed to create project file: " << projectFile << std::endl;
        return false;
    }
    
    file << "{\n";
    file << "  \"name\": \"" << name << "\",\n";
    file << "  \"version\": \"1.0\",\n";
    file << "  \"engineVersion\": \"1.0\",\n";
    file << "  \"lastScene\": \"\"\n";
    file << "}\n";
    file.close();
    
    std::cout << "Project created: " << projectPath << std::endl;
    return true;
}

bool ProjectManager::OpenProject(const std::string& projectPath)
{
    if (!fs::exists(projectPath))
    {
        std::cerr << "Project does not exist: " << projectPath << std::endl;
        return false;
    }
    
    // 关闭当前项目
    CloseProject();
    
    // 分配新项目
    currentProject = new Project();
    currentProject->path = projectPath;
    currentProject->name = fs::path(projectPath).filename().string();
    
    // 读取项目配置
    std::string projectFile = projectPath + "/project.json";
    if (fs::exists(projectFile))
    {
        std::ifstream file(projectFile);
        std::string line;
        while (std::getline(file, line))
        {
            if (line.find("\"lastScene\"") != std::string::npos)
            {
                size_t start = line.find(": \"") + 3;
                size_t end = line.find("\"", start);
                if (start != std::string::npos && end != std::string::npos)
                {
                    currentProject->lastScene = line.substr(start, end - start);
                }
            }
        }
    }
    
    std::cout << "Project opened: " << currentProject->name << std::endl;
    return true;
}

void ProjectManager::CloseProject()
{
    if (currentProject)
    {
        delete currentProject;
        currentProject = nullptr;
    }
}

std::string ProjectManager::GetProjectAssetsPath() const
{
    if (!currentProject) return "";
    return currentProject->path + "/Assets";
}

std::string ProjectManager::GetProjectScenesPath() const
{
    if (!currentProject) return "";
    return currentProject->path + "/Assets/Scenes";
}