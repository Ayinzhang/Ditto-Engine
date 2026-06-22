#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ProjectConfig
{
    std::string name;
    std::string version = "1.0";
    std::string engineVersion = "1.0";
    std::string lastScene;
};

struct GameConfig
{
    std::string productName;
    std::string companyName;
    std::string version;
    std::string startupScene;
    std::vector<std::string> scenes;
    bool developmentBuild = false;
    bool enableScriptDebugging = false;
};

namespace Ditto::JsonConfig
{
    bool ReadProjectConfig(const std::filesystem::path& path, ProjectConfig& outConfig);
    bool WriteProjectConfig(const std::filesystem::path& path, const ProjectConfig& config);
    bool UpdateProjectLastScene(const std::filesystem::path& path, const std::string& lastScene);

    bool ReadGameConfig(const std::filesystem::path& path, GameConfig& outConfig);
    bool WriteGameConfig(const std::filesystem::path& path, const GameConfig& config);
}
