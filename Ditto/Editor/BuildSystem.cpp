#include "BuildSystem.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <windows.h>
#include <shlobj.h>

namespace fs = std::filesystem;

const char* BuildSettings::GetPlatformName(BuildPlatform platform)
{
    switch (platform)
    {
        case BuildPlatform::Windows: return "Windows";
        default: return "Unknown";
    }
}

const char* BuildSettings::GetConfigurationName(BuildConfiguration config)
{
    switch (config)
    {
        case BuildConfiguration::Debug: return "Debug";
        case BuildConfiguration::Release: return "Release";
        default: return "Unknown";
    }
}

bool BuildSystem::ValidateSettings(const BuildSettings& settings, std::string& error)
{
    if (settings.scenes.empty())
    {
        error = "No scenes selected for build";
        return false;
    }
    
    if (settings.startupScene.empty())
    {
        error = "No startup scene specified";
        return false;
    }
    
    if (settings.productName.empty())
    {
        error = "Product name is empty";
        return false;
    }
    
    if (settings.outputPath.empty())
    {
        error = "Output path is empty";
        return false;
    }
    
    return true;
}

std::string BuildSystem::GetDefaultOutputPath(const std::string& projectPath)
{
    std::string path = projectPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    return path + "/Build/Windows";
}

std::vector<std::string> BuildSystem::GetProjectScenes(const std::string& projectPath)
{
    std::vector<std::string> scenes;
    std::string scenesPath = projectPath + "/Assets/Scenes";
    
    if (fs::exists(scenesPath))
    {
        for (const auto& entry : fs::directory_iterator(scenesPath))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".bin")
            {
                scenes.push_back(entry.path().string());
            }
        }
    }
    
    return scenes;
}

static std::string FindEngineRootDir()
{
    auto current = fs::current_path();
    while (current.has_parent_path() && current != current.parent_path())
    {
        if (fs::exists(current / "Ditto" / "Assets" / "Shaders" / "Vertex.glsl") ||
            fs::exists(current / "ditto" / "Assets" / "Shaders" / "Vertex.glsl"))
        {
            return current.string();
        }
        current = current.parent_path();
    }
    
    std::vector<std::string> candidates = {
        "../../",
        "../../../",
        "../Ditto/",
        "../../Ditto/",
    };
    
    for (const auto& c : candidates)
    {
        if (fs::exists(c + "Ditto/Assets/Shaders/Vertex.glsl") || 
            fs::exists(c + "ditto/Assets/Shaders/Vertex.glsl"))
        {
            return fs::absolute(c).string();
        }
    }
    
    return "";
}

static std::string ExtractSceneName(const std::string& scenePath)
{
    return fs::path(scenePath).stem().string();
}

bool BuildSystem::Build(const BuildSettings& settings, BuildProgressCallback callback)
{
    auto report = [&](const std::string& stage, float progress)
    {
        std::cout << "[Build] " << stage << " (" << (int)(progress * 100) << "%)" << std::endl;
        if (callback) callback(stage, progress);
    };
    
    report("Starting build...", 0.0f);
    
    std::string projectPath = settings.outputPath;
    {
        std::replace(projectPath.begin(), projectPath.end(), '\\', '/');
        size_t pos = projectPath.find("/Build/");
        if (pos != std::string::npos)
        {
            projectPath = projectPath.substr(0, pos);
        }
    }
    
    std::string engineRoot = FindEngineRootDir();
    std::cout << "[Build] Project path: " << projectPath << std::endl;
    std::cout << "[Build] Engine root: " << engineRoot << std::endl;
    std::cout << "[Build] Startup scene: " << settings.startupScene << std::endl;
    std::cout << "[Build] Output path: " << settings.outputPath << std::endl;
    
    report("Preparing output directory...", 0.05f);
    if (!PrepareOutputDirectory(settings.outputPath))
    {
        std::cerr << "[Build] Failed to prepare output directory" << std::endl;
        return false;
    }
    
    report("Copying assets...", 0.1f);
    if (!CopyAssets(projectPath, settings.outputPath, callback))
    {
        std::cerr << "[Build] Failed to copy assets" << std::endl;
        return false;
    }
    
    report("Copying scenes...", 0.3f);
    if (!CopyScenes(settings.scenes, projectPath, settings.outputPath))
    {
        std::cerr << "[Build] Failed to copy scenes" << std::endl;
        return false;
    }
    
    report("Copying shaders...", 0.4f);
    if (!CopyShaders(engineRoot, settings.outputPath))
    {
        std::cerr << "[Build] Warning: Failed to copy shaders" << std::endl;
    }
    
    report("Compiling scripts...", 0.45f);
    if (!CompileScripts(projectPath, settings.outputPath, engineRoot))
    {
        std::cerr << "[Build] Warning: Failed to compile scripts" << std::endl;
    }
    
    report("Generating game config...", 0.5f);
    if (!GenerateGameConfig(settings, settings.outputPath))
    {
        std::cerr << "[Build] Failed to generate game config" << std::endl;
        return false;
    }
    
    report("Copying executable...", 0.6f);
    if (!CopyExecutable(settings.outputPath, settings.productName, settings.configuration, engineRoot))
    {
        std::cerr << "[Build] Failed to copy executable" << std::endl;
        return false;
    }
    
    report("Copying dependencies...", 0.8f);
    if (!CopyDependencies(settings.outputPath, engineRoot))
    {
        std::cerr << "[Build] Warning: Some dependencies may be missing" << std::endl;
    }
    
    report("Creating launcher...", 0.9f);
    CreateLauncher(settings.outputPath, settings.productName);
    
    report("Build completed!", 1.0f);
    return true;
}

bool BuildSystem::PrepareOutputDirectory(const std::string& outputPath)
{
    try
    {
        if (fs::exists(outputPath))
        {
            fs::remove_all(outputPath);
        }
        fs::create_directories(outputPath);
        fs::create_directories(outputPath + "/Assets");
        fs::create_directories(outputPath + "/Assets/Scenes");
        fs::create_directories(outputPath + "/Assets/Shaders");
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error preparing output directory: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CopyAssets(const std::string& projectPath, const std::string& outputPath, BuildProgressCallback callback)
{
    try
    {
        std::string assetsSrc = projectPath + "/Assets";
        std::string assetsDst = outputPath + "/Assets";
        
        if (!fs::exists(assetsSrc))
        {
            std::cerr << "[Build] Assets directory not found: " << assetsSrc << std::endl;
            return true;
        }
        
        for (const auto& entry : fs::recursive_directory_iterator(assetsSrc))
        {
            std::string relativePath = fs::relative(entry.path(), assetsSrc).string();
            std::string dstPath = assetsDst + "/" + relativePath;
            
            if (entry.is_directory())
            {
                fs::create_directories(dstPath);
                continue;
            }
            
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                if (ext == ".cs" || ext == ".cpp" || ext == ".h" || ext == ".obj" || 
                    ext == ".pdb" || ext == ".ilk" || ext == ".lib")
                {
                    continue;
                }
                
                fs::create_directories(fs::path(dstPath).parent_path());
                fs::copy(entry.path(), dstPath, fs::copy_options::overwrite_existing);
            }
        }
        
        std::cout << "[Build] Copied Assets to " << assetsDst << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error copying assets: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CopyScenes(const std::vector<std::string>& scenes, const std::string& projectPath, const std::string& outputPath)
{
    try
    {
        std::string scenesDst = outputPath + "/Assets/Scenes";
        fs::create_directories(scenesDst);
        
        for (const auto& scenePath : scenes)
        {
            if (fs::exists(scenePath))
            {
                std::string filename = fs::path(scenePath).filename().string();
                std::string dstPath = scenesDst + "/" + filename;
                fs::copy(scenePath, dstPath, fs::copy_options::overwrite_existing);
                std::cout << "[Build] Copied scene: " << filename << std::endl;
            }
            else
            {
                std::cerr << "[Build] Scene not found: " << scenePath << std::endl;
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error copying scenes: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CopyShaders(const std::string& engineRoot, const std::string& outputPath)
{
    try
    {
        std::vector<std::string> shaderSearchPaths = {
            engineRoot + "/Ditto/Assets/Shaders",
            engineRoot + "/ditto/Assets/Shaders",
            "Assets/Shaders",
            "Ditto/Assets/Shaders",
            "../../Ditto/Ditto/Assets/Shaders",
            "../Ditto/Assets/Shaders",
        };
        
        std::string shaderSrc;
        for (const auto& p : shaderSearchPaths)
        {
            if (fs::exists(p + "/Vertex.glsl"))
            {
                shaderSrc = p;
                break;
            }
        }
        
        if (shaderSrc.empty())
        {
            std::cerr << "[Build] Shaders not found" << std::endl;
            return false;
        }
        
        std::string shaderDst = outputPath + "/Assets/Shaders";
        fs::create_directories(shaderDst);
        
        for (const auto& entry : fs::directory_iterator(shaderSrc))
        {
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".shader")
                {
                    fs::copy(entry.path(), shaderDst + "/" + entry.path().filename().string(), 
                             fs::copy_options::overwrite_existing);
                    std::cout << "[Build] Copied shader: " << entry.path().filename().string() << std::endl;
                }
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error copying shaders: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CopyExecutable(const std::string& outputPath, const std::string& productName, 
                                  BuildConfiguration config, const std::string& engineRoot)
{
    try
    {
        std::string exeDst = outputPath + "/" + productName + ".exe";
        std::string configName = BuildSettings::GetConfigurationName(config);
        
        std::vector<std::string> possiblePaths = {
            engineRoot + "/x64/" + configName + "/Ditto.exe",
            engineRoot + "/x64/Debug/Ditto.exe",
            engineRoot + "/x64/Release/Ditto.exe",
            "x64/" + configName + "/Ditto.exe",
            "x64/Debug/Ditto.exe",
            "x64/Release/Ditto.exe",
            "../../x64/" + configName + "/Ditto.exe",
            "../../x64/Debug/Ditto.exe",
            "../../x64/Release/Ditto.exe",
            "../../../x64/" + configName + "/Ditto.exe",
            "../../../x64/Debug/Ditto.exe",
            "../../../x64/Release/Ditto.exe",
        };
        
        for (const auto& path : possiblePaths)
        {
            if (fs::exists(path))
            {
                fs::copy(path, exeDst, fs::copy_options::overwrite_existing);
                std::cout << "[Build] Copied executable from: " << fs::absolute(path).string() << std::endl;
                return true;
            }
        }
        
        std::cerr << "[Build] Could not find Ditto.exe in any known location" << std::endl;
        std::cerr << "[Build] Searched:" << std::endl;
        for (const auto& path : possiblePaths)
        {
            std::cerr << "[Build]   " << fs::absolute(path).string() << std::endl;
        }
        return false;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error copying executable: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CopyDependencies(const std::string& outputPath, const std::string& engineRoot)
{
    try
    {
        std::vector<std::string> glfwSearchPaths = {
            engineRoot + "/Ditto/3rdParty/GLFW",
            engineRoot + "/ditto/3rdParty/GLFW",
            "3rdParty/GLFW",
            "Ditto/3rdParty/GLFW",
            "../../Ditto/Ditto/3rdParty/GLFW",
            "../Ditto/3rdParty/GLFW",
        };
        
        for (const auto& p : glfwSearchPaths)
        {
            if (fs::exists(p + "/glfw3.dll"))
            {
                fs::copy(p + "/glfw3.dll", outputPath + "/glfw3.dll", fs::copy_options::overwrite_existing);
                std::cout << "[Build] Copied glfw3.dll" << std::endl;
                break;
            }
        }
        
        std::vector<std::string> monoSearchPaths = {
            engineRoot + "/Ditto/3rdParty/Mono",
            engineRoot + "/ditto/3rdParty/Mono",
            "3rdParty/Mono",
            "Ditto/3rdParty/Mono",
            "../../Ditto/Ditto/3rdParty/Mono",
            "../Ditto/3rdParty/Mono",
        };
        
        for (const auto& p : monoSearchPaths)
        {
            if (fs::exists(p + "/mono-2.0-sgen.dll"))
            {
                fs::copy(p + "/mono-2.0-sgen.dll", outputPath + "/mono-2.0-sgen.dll", 
                         fs::copy_options::overwrite_existing);
                std::cout << "[Build] Copied mono-2.0-sgen.dll" << std::endl;
                
                std::string monoDst = outputPath + "/Mono";
                if (fs::exists(p) && !fs::exists(monoDst))
                {
                    fs::copy(p, monoDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                    std::cout << "[Build] Copied Mono directory" << std::endl;
                }
                break;
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error copying dependencies: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CompileScripts(const std::string& projectPath, const std::string& outputPath, const std::string& engineRoot)
{
    try
    {
        std::string scriptsDir = projectPath + "/Assets/Scripts";
        if (!fs::exists(scriptsDir))
        {
            std::cout << "[Build] No Scripts directory found, skipping script compilation" << std::endl;
            return true;
        }

        std::vector<std::string> csFiles;
        for (const auto& entry : fs::recursive_directory_iterator(scriptsDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".cs")
            {
                csFiles.push_back(entry.path().string());
            }
        }

        if (csFiles.empty())
        {
            std::cout << "[Build] No C# scripts found, skipping compilation" << std::endl;
            return true;
        }

        std::string dittoEngineDll;
        std::vector<std::string> dllSearchPaths = {
            engineRoot + "/Ditto/3rdParty/Mono/DittoEngine.dll",
            engineRoot + "/ditto/3rdParty/Mono/DittoEngine.dll",
            "3rdParty/Mono/DittoEngine.dll",
            "Ditto/3rdParty/Mono/DittoEngine.dll",
            "../../Ditto/Ditto/3rdParty/Mono/DittoEngine.dll",
            "../Ditto/3rdParty/Mono/DittoEngine.dll",
        };

        for (const auto& p : dllSearchPaths)
        {
            if (fs::exists(p))
            {
                dittoEngineDll = fs::absolute(p).string();
                break;
            }
        }

        if (dittoEngineDll.empty())
        {
            std::cerr << "[Build] DittoEngine.dll not found, cannot compile scripts" << std::endl;
            return false;
        }

        std::string outputDll = fs::absolute(outputPath + "/GameScripts.dll").string();

        std::string msbuildPath;
        char* vsPath = nullptr;
        size_t vsPathLen = 0;
        _dupenv_s(&vsPath, &vsPathLen, "VSINSTALLDIR");
        if (vsPath)
        {
            std::string roslynPath = std::string(vsPath) + "MSBuild\\Current\\Bin\\Roslyn";
            free(vsPath);
            if (fs::exists(roslynPath))
                msbuildPath = roslynPath;
        }

        if (msbuildPath.empty())
        {
            std::vector<std::string> roslynPaths = {
                "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/Roslyn",
                "C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/Roslyn",
                "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/Roslyn",
                "D:/Visual Studio 2022/MSBuild/Current/Bin/Roslyn",
            };
            for (const auto& p : roslynPaths)
            {
                if (fs::exists(p))
                {
                    msbuildPath = p;
                    break;
                }
            }
        }

        std::string pathCmd;
        if (!msbuildPath.empty())
            pathCmd = "set PATH=" + msbuildPath + ";%PATH%&";

        std::string csFileList;
        for (const auto& f : csFiles)
        {
            csFileList += " \"" + f + "\"";
        }

        std::string cmd = pathCmd + "csc /target:library /reference:\"" + dittoEngineDll + "\" /out:\"" + outputDll + "\"" + csFileList;

        std::cout << "[Build] Compiling scripts: " << csFiles.size() << " files" << std::endl;
        std::cout << "[Build] Output: " << outputDll << std::endl;

        int result = system(cmd.c_str());
        if (result != 0)
        {
            std::cerr << "[Build] Script compilation failed" << std::endl;
            return false;
        }

        fs::copy(dittoEngineDll, outputPath + "/DittoEngine.dll", fs::copy_options::overwrite_existing);
        std::cout << "[Build] Copied DittoEngine.dll to output" << std::endl;

        std::cout << "[Build] Scripts compiled successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error compiling scripts: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::GenerateGameConfig(const BuildSettings& settings, const std::string& outputPath)
{
    try
    {
        std::string configPath = outputPath + "/game.config";
        std::ofstream configFile(configPath);
        
        if (!configFile.is_open())
        {
            std::cerr << "[Build] Failed to create game.config" << std::endl;
            return false;
        }
        
        std::string startupSceneName = ExtractSceneName(settings.startupScene);
        
        configFile << "{\n";
        configFile << "  \"productName\": \"" << settings.productName << "\",\n";
        configFile << "  \"companyName\": \"" << settings.companyName << "\",\n";
        configFile << "  \"version\": \"" << settings.version << "\",\n";
        configFile << "  \"startupScene\": \"" << startupSceneName << "\",\n";
        configFile << "  \"scenes\": [\n";
        
        for (size_t i = 0; i < settings.scenes.size(); i++)
        {
            std::string sceneName = ExtractSceneName(settings.scenes[i]);
            configFile << "    \"" << sceneName << "\"";
            if (i < settings.scenes.size() - 1) configFile << ",";
            configFile << "\n";
        }
        
        configFile << "  ],\n";
        configFile << "  \"developmentBuild\": " << (settings.developmentBuild ? "true" : "false") << ",\n";
        configFile << "  \"enableScriptDebugging\": " << (settings.enableScriptDebugging ? "true" : "false") << "\n";
        configFile << "}\n";
        
        configFile.close();
        std::cout << "[Build] Generated game.config with startupScene: " << startupSceneName << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error generating game config: " << e.what() << std::endl;
        return false;
    }
}

bool BuildSystem::CreateLauncher(const std::string& outputPath, const std::string& productName)
{
    try
    {
        std::string batPath = outputPath + "/Run.bat";
        std::ofstream batFile(batPath);
        batFile << "@echo off\n";
        batFile << "cd /d \"%~dp0\"\n";
        batFile << "\"" << productName << ".exe\"\n";
        batFile << "pause\n";
        batFile.close();
        
        std::cout << "[Build] Created launcher: " << batPath << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Build] Error creating launcher: " << e.what() << std::endl;
        return false;
    }
}
