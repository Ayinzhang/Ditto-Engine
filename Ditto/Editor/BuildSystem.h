#pragma once
#include <string>
#include <vector>
#include <functional>

enum class BuildPlatform
{
    Windows,
    Android,
    Count
};

enum class BuildConfiguration
{
    Debug,
    Release,
    Count
};

struct BuildSettings
{
    BuildPlatform platform = BuildPlatform::Windows;
    BuildConfiguration configuration = BuildConfiguration::Release;
    std::string outputPath;
    std::vector<std::string> scenes;
    std::string startupScene;
    std::string companyName = "MyCompany";
    std::string productName;
    std::string version = "1.0.0";
    bool developmentBuild = false;
    bool enableScriptDebugging = false;
    
    static const char* GetPlatformName(BuildPlatform platform);
    static const char* GetConfigurationName(BuildConfiguration config);
};

using BuildProgressCallback = std::function<void(const std::string& stage, float progress)>;

class BuildSystem
{
public:
    static bool Build(const BuildSettings& settings, BuildProgressCallback callback = nullptr);
    static bool ValidateSettings(const BuildSettings& settings, std::string& error);
    static std::string GetDefaultOutputPath(const std::string& projectPath, BuildPlatform platform = BuildPlatform::Windows);
    static std::vector<std::string> GetProjectScenes(const std::string& projectPath);
    
private:
    static bool PrepareOutputDirectory(const std::string& outputPath);
    static bool CopyAssets(const std::string& projectPath, const std::string& outputPath, BuildProgressCallback callback);
    static bool CopyScenes(const std::vector<std::string>& scenes, const std::string& projectPath, const std::string& outputPath);
    static bool CopyShaders(const std::string& engineRoot, const std::string& outputPath);
    static bool CopyShaderCache(const std::string& outputPath);
    static bool CopyEngineModels(const std::string& outputPath);
    static bool CopyExecutable(const std::string& outputPath, const std::string& productName, BuildConfiguration config, const std::string& engineRoot);
    static bool CopyDependencies(const std::string& outputPath, const std::string& engineRoot);
    static bool CompileScripts(const std::string& projectPath, const std::string& outputPath, const std::string& engineRoot);
    static bool GenerateGameConfig(const BuildSettings& settings, const std::string& outputPath);
    static bool CreateLauncher(const std::string& outputPath, const std::string& productName);
};
