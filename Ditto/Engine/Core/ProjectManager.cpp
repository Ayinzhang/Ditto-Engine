#include "ProjectManager.h"
#include "Logger.h"
#include "PathUtils.h"
#include "JsonConfig.h"
#include "../Resources/AssetDatabase.h"
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

static void CopyDefaultAsset(const std::string& assetRelativePath, const fs::path& projectAssetsPath)
{
    try
    {
        fs::path src = PathUtils::ResolveAsset(assetRelativePath);
        if (!fs::exists(src))
        {
            DITTO_LOG_WARN_STREAM("[Project] Default asset not found: " << src.string());
            return;
        }

        fs::path dst = projectAssetsPath / fs::path(assetRelativePath);
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        DITTO_LOG_VERBOSE_STREAM("[Project] Copied default asset: " << dst.string());
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_WARN_STREAM("[Project] Failed to copy default asset " << assetRelativePath << ": " << e.what());
    }
}

static void EnsureDefaultAsset(const std::string& assetRelativePath, const fs::path& projectAssetsPath)
{
    fs::path dst = projectAssetsPath / fs::path(assetRelativePath);
    if (fs::exists(dst)) return;
    CopyDefaultAsset(assetRelativePath, projectAssetsPath);
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
                    
                    ProjectConfig config;
                    if (Ditto::JsonConfig::ReadProjectConfig(fs::path(project.path) / "project.json", config))
                    {
                        if (!config.name.empty())
                            project.name = config.name;
                        project.lastScene = config.lastScene;
                    }
                    
                    projects.push_back(project);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("Error reading projects: " << e.what() );
    }
    
    return projects;
}

bool ProjectManager::CreateProject(const std::string& name)
{
    if (name.empty()) return false;
    
    std::string projectPath = projectsDirectory + "/" + name;
    
    if (fs::exists(projectPath))
    {
        DITTO_LOG_ERROR_STREAM("Project already exists: " << projectPath );
        return false;
    }
    
    // Create project directory structure
    EnsureDirectoryExists(projectPath + "/Assets");
    EnsureDirectoryExists(projectPath + "/Assets/Scenes");
    EnsureDirectoryExists(projectPath + "/Assets/Models");
    EnsureDirectoryExists(projectPath + "/Assets/Materials");
    EnsureDirectoryExists(projectPath + "/Assets/Textures");
    EnsureDirectoryExists(projectPath + "/Assets/Sprites");
    EnsureDirectoryExists(projectPath + "/Assets/PhysicsMaterials2D");
    EnsureDirectoryExists(projectPath + "/Assets/Prefabs");
    EnsureDirectoryExists(projectPath + "/Assets/Scripts");
    EnsureDirectoryExists(projectPath + "/Assets/Shaders");

    fs::path projectAssetsPath = fs::path(projectPath) / "Assets";
    CopyDefaultAsset("Models/Cube.obj", projectAssetsPath);
    CopyDefaultAsset("Models/Sphere.obj", projectAssetsPath);
    CopyDefaultAsset("Shaders/Lit_Toon.shader", projectAssetsPath);
    CopyDefaultAsset("Shaders/Lit_Sprite.shader", projectAssetsPath);
    CopyDefaultAsset("Materials/Lit_Toon.mat", projectAssetsPath);
    CopyDefaultAsset("Materials/Lit_Sprite.mat", projectAssetsPath);
    CopyDefaultAsset("Sprites/Square.png", projectAssetsPath);
    CopyDefaultAsset("Sprites/Circle.png", projectAssetsPath);
    CopyDefaultAsset("PhysicsMaterials2D/Default.physmat2d", projectAssetsPath);
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    
    // Create default scene file
    std::string defaultScenePath = projectPath + "/Assets/Scenes/Default.bin";
    {
        Scene tempScene;
        tempScene.name = "Default";
        tempScene.SaveScene(defaultScenePath);
    }
    
    ProjectConfig config;
    config.name = name;
    config.lastScene = "Assets/Scenes/Default.bin";
    if (!Ditto::JsonConfig::WriteProjectConfig(fs::path(projectPath) / "project.json", config))
    {
        DITTO_LOG_ERROR_STREAM("Failed to create project file: " << (fs::path(projectPath) / "project.json").string());
        return false;
    }
    
    DITTO_LOG_VERBOSE_STREAM("Project created: " << projectPath );
    return true;
}

bool ProjectManager::OpenProject(const std::string& projectPath)
{
    if (!fs::exists(projectPath))
    {
        DITTO_LOG_ERROR_STREAM("Project does not exist: " << projectPath );
        return false;
    }

    // Close current project
    CloseProject();

    // Allocate new project
    currentProject = std::make_unique<Project>();
    currentProject->path = projectPath;
    currentProject->name = fs::path(projectPath).filename().string();
    
    ProjectConfig config;
    if (Ditto::JsonConfig::ReadProjectConfig(fs::path(projectPath) / "project.json", config))
    {
        if (!config.name.empty())
            currentProject->name = config.name;
        currentProject->lastScene = config.lastScene;
    }
    
    DITTO_LOG_VERBOSE_STREAM("Project opened: " << currentProject->name );

    fs::path projectAssetsPath = fs::path(projectPath) / "Assets";
    EnsureDefaultAsset("Shaders/Lit_Toon.shader", projectAssetsPath);
    EnsureDefaultAsset("Shaders/Lit_Sprite.shader", projectAssetsPath);
    EnsureDefaultAsset("Materials/Lit_Toon.mat", projectAssetsPath);
    EnsureDefaultAsset("Materials/Lit_Sprite.mat", projectAssetsPath);
    EnsureDefaultAsset("Sprites/Square.png", projectAssetsPath);
    EnsureDefaultAsset("Sprites/Circle.png", projectAssetsPath);
    EnsureDefaultAsset("PhysicsMaterials2D/Default.physmat2d", projectAssetsPath);
    Ditto::AssetDatabase::Get().ScanProjectAssets(projectPath, true);
    return true;
}

void ProjectManager::CloseProject()
{
    Ditto::AssetDatabase::Get().Clear();
    currentProject.reset();
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

