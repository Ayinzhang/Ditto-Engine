#include "BuildSystem.h"
#include "../Engine/Core/Logger.h"
#include "../Engine/Core/PathUtils.h"
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
        DITTO_LOG_INFO_STREAM("[Build] " << stage << " (" << (int)(progress * 100) << "%)");
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
    DITTO_LOG_INFO_STREAM("[Build] Project path: " << projectPath);
    DITTO_LOG_INFO_STREAM("[Build] Engine root: " << engineRoot);
    DITTO_LOG_INFO_STREAM("[Build] Startup scene: " << settings.startupScene);
    DITTO_LOG_INFO_STREAM("[Build] Output path: " << settings.outputPath);
    
    report("Preparing output directory...", 0.05f);
    if (!PrepareOutputDirectory(settings.outputPath))
    {
        DITTO_LOG_ERROR("[Build] Failed to prepare output directory");
        return false;
    }
    
    report("Copying assets...", 0.1f);
    if (!CopyAssets(projectPath, settings.outputPath, callback))
    {
        DITTO_LOG_ERROR("[Build] Failed to copy assets");
        return false;
    }
    
    report("Copying scenes...", 0.3f);
    if (!CopyScenes(settings.scenes, projectPath, settings.outputPath))
    {
        DITTO_LOG_ERROR("[Build] Failed to copy scenes");
        return false;
    }
    
    report("Copying shaders...", 0.4f);
    if (!CopyShaders(engineRoot, settings.outputPath))
    {
        DITTO_LOG_WARN("[Build] Failed to copy shaders");
    }

    report("Copying engine models...", 0.42f);
    if (!CopyEngineModels(settings.outputPath))
    {
        DITTO_LOG_WARN("[Build] engine models missing; built-in Cube/Sphere will not render");
    }
    
    report("Compiling scripts...", 0.45f);
    if (!CompileScripts(projectPath, settings.outputPath, engineRoot))
    {
        DITTO_LOG_WARN("[Build] Failed to compile scripts");
    }
    
    report("Generating game config...", 0.5f);
    if (!GenerateGameConfig(settings, settings.outputPath))
    {
        DITTO_LOG_ERROR("[Build] Failed to generate game config");
        return false;
    }
    
    report("Copying executable...", 0.6f);
    if (!CopyExecutable(settings.outputPath, settings.productName, settings.configuration, engineRoot))
    {
        DITTO_LOG_ERROR("[Build] Failed to copy executable");
        return false;
    }
    
    report("Copying shader cache...", 0.7f);
    if (!CopyShaderCache(settings.outputPath))
    {
        DITTO_LOG_WARN("[Build] Shader cache not copied; the game will need the Vulkan SDK shader tools to compile shaders at runtime");
    }

    report("Copying dependencies...", 0.8f);
    if (!CopyDependencies(settings.outputPath, engineRoot))
    {
        DITTO_LOG_WARN("[Build] Some dependencies may be missing");
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
        DITTO_LOG_ERROR_STREAM("[Build] Error preparing output directory: " << e.what() );
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
            // A project without an Assets directory cannot produce a runnable
            // game (no scenes) -- fail the build loudly instead of packaging
            // an empty shell.
            DITTO_LOG_ERROR_STREAM("[Build] Assets directory not found: " << assetsSrc );
            return false;
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
        
        DITTO_LOG_INFO_STREAM("[Build] Copied Assets to " << assetsDst );
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying assets: " << e.what() );
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
                DITTO_LOG_INFO_STREAM("[Build] Copied scene: " << filename );
            }
            else
            {
                DITTO_LOG_ERROR_STREAM("[Build] Scene not found: " << scenePath );
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying scenes: " << e.what() );
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
            DITTO_LOG_ERROR_STREAM("[Build] Shaders not found" );
            return false;
        }
        
        std::string shaderDst = outputPath + "/Assets/Shaders";
        fs::create_directories(shaderDst);
        
        for (const auto& entry : fs::directory_iterator(shaderSrc))
        {
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".shader" || ext == ".hlsl")
                {
                    fs::copy(entry.path(), shaderDst + "/" + entry.path().filename().string(), 
                             fs::copy_options::overwrite_existing);
                    DITTO_LOG_INFO_STREAM("[Build] Copied shader: " << entry.path().filename().string() );
                }
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying shaders: " << e.what() );
        return false;
    }
}

// Copy the engine's built-in models (Cube.obj/Sphere.obj) into the package.
// Resource::Initialize resolves "Models/Cube.obj" anchored to the game
// executable, so without this the shipped game has no base geometry and
// renders nothing.
bool BuildSystem::CopyEngineModels(const std::string& outputPath)
{
    try
    {
        fs::path cubeSrc = PathUtils::ResolveAsset("Models/Cube.obj");
        fs::path modelsSrc = cubeSrc.parent_path();
        if (!fs::exists(cubeSrc))
        {
            DITTO_LOG_ERROR_STREAM("[Build] Engine models not found at " << modelsSrc.string() );
            return false;
        }

        fs::path modelsDst = fs::path(outputPath) / "Assets" / "Models";
        fs::create_directories(modelsDst);
        int copied = 0;
        for (const auto& entry : fs::directory_iterator(modelsSrc))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".obj") continue;
            fs::copy(entry.path(), modelsDst / entry.path().filename(), fs::copy_options::overwrite_existing);
            ++copied;
        }
        DITTO_LOG_INFO_STREAM("[Build] Copied " << copied << " engine models" );
        return copied > 0;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying engine models: " << e.what() );
        return false;
    }
}

// Copy the editor's compiled-shader cache next to the game executable so the
// shipped game loads shaders from cache instead of needing dxc/spirv-cross
// (i.e. a Vulkan SDK install) on the player's machine. The editor process IS
// the engine executable, so its ShaderCache dir holds every built-in shader
// compiled during this session.
bool BuildSystem::CopyShaderCache(const std::string& outputPath)
{
    try
    {
        fs::path cacheSrc = PathUtils::GetExecutableDir() / "ShaderCache";
        if (!fs::exists(cacheSrc) || fs::is_empty(cacheSrc))
        {
            DITTO_LOG_ERROR_STREAM("[Build] No shader cache at " << cacheSrc.string() );
            return false;
        }

        fs::path cacheDst = fs::path(outputPath) / "ShaderCache";
        fs::create_directories(cacheDst);
        int copied = 0;
        for (const auto& entry : fs::directory_iterator(cacheSrc))
        {
            if (!entry.is_regular_file()) continue;
            fs::copy(entry.path(), cacheDst / entry.path().filename(), fs::copy_options::overwrite_existing);
            ++copied;
        }
        DITTO_LOG_INFO_STREAM("[Build] Copied " << copied << " shader cache entries" );
        return copied > 0;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying shader cache: " << e.what() );
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
                DITTO_LOG_INFO_STREAM("[Build] Copied executable from: " << fs::absolute(path).string() );
                return true;
            }
        }
        
        DITTO_LOG_ERROR_STREAM("[Build] Could not find Ditto.exe in any known location" );
        DITTO_LOG_ERROR_STREAM("[Build] Searched:" );
        for (const auto& path : possiblePaths)
        {
            DITTO_LOG_ERROR_STREAM("[Build]   " << fs::absolute(path).string() );
        }
        return false;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying executable: " << e.what() );
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
                DITTO_LOG_INFO_STREAM("[Build] Copied glfw3.dll" );
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
                DITTO_LOG_INFO_STREAM("[Build] Copied mono-2.0-sgen.dll" );
                
                std::string monoDst = outputPath + "/Mono";
                if (fs::exists(p) && !fs::exists(monoDst))
                {
                    fs::copy(p, monoDst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                    DITTO_LOG_INFO_STREAM("[Build] Copied Mono directory" );
                }
                break;
            }
        }

        std::vector<std::string> assimpSearchPaths = {
            engineRoot + "/Ditto/3rdParty/Assimp/bin",
            engineRoot + "/ditto/3rdParty/Assimp/bin",
            "3rdParty/Assimp/bin",
            "Ditto/3rdParty/Assimp/bin",
            "../../Ditto/Ditto/3rdParty/Assimp/bin",
            "../Ditto/3rdParty/Assimp/bin",
        };

        for (const auto& p : assimpSearchPaths)
        {
            if (fs::exists(p + "/assimp-vc143-mt.dll"))
            {
                fs::copy(p + "/assimp-vc143-mt.dll", outputPath + "/assimp-vc143-mt.dll",
                         fs::copy_options::overwrite_existing);
                DITTO_LOG_INFO_STREAM("[Build] Copied assimp-vc143-mt.dll" );
                break;
            }
        }
        
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error copying dependencies: " << e.what() );
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
            DITTO_LOG_INFO_STREAM("[Build] No Scripts directory found, skipping script compilation" );
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
            DITTO_LOG_INFO_STREAM("[Build] No C# scripts found, skipping compilation" );
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
            DITTO_LOG_ERROR_STREAM("[Build] DittoEngine.dll not found, cannot compile scripts" );
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
            const char* regKey = "SOFTWARE\\Microsoft\\VisualStudio\\Setup\\Instances";
            HKEY hKey = nullptr;

            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                char name[MAX_PATH];
                DWORD index = 0;
                DWORD nameSize = MAX_PATH;

                while (RegEnumKeyExA(hKey, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
                {
                    std::string instanceKey = std::string(regKey) + "\\" + name;
                    HKEY hInstKey = nullptr;

                    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, KEY_READ, &hInstKey) == ERROR_SUCCESS)
                    {
                        char installPath[MAX_PATH];
                        DWORD size = MAX_PATH;
                        DWORD type = REG_SZ;

                        if (RegQueryValueExA(hInstKey, "InstallLocation", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS)
                        {
                            std::string roslynPath = std::string(installPath) + "MSBuild\\Current\\Bin\\Roslyn";
                            if (fs::exists(roslynPath))
                            {
                                msbuildPath = roslynPath;
                                RegCloseKey(hInstKey);
                                RegCloseKey(hKey);
                                break;
                            }
                        }
                        RegCloseKey(hInstKey);
                    }
                    nameSize = MAX_PATH;
                }
                RegCloseKey(hKey);
            }
        }

        if (msbuildPath.empty())
        {
            char* pathEnv = nullptr;
            size_t pathLen = 0;
            if (_dupenv_s(&pathEnv, &pathLen, "PATH") == 0 && pathEnv)
            {
                std::string pathStr(pathEnv);
                free(pathEnv);

                size_t pos = 0;
                while ((pos = pathStr.find(';')) != std::string::npos)
                {
                    std::string dir = pathStr.substr(0, pos);
                    std::string cscPath = dir + "\\csc.exe";
                    if (fs::exists(cscPath))
                    {
                        size_t lastSlash = dir.find_last_of("\\/");
                        if (lastSlash != std::string::npos)
                        {
                            std::string roslynDir = dir.substr(0, lastSlash);
                            if (roslynDir.find("Roslyn") != std::string::npos)
                            {
                                msbuildPath = roslynDir;
                                break;
                            }
                        }
                    }
                    pathStr.erase(0, pos + 1);
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

        DITTO_LOG_INFO_STREAM("[Build] Compiling scripts: " << csFiles.size() << " files" );
        DITTO_LOG_INFO_STREAM("[Build] Output: " << outputDll );

        int result = system(cmd.c_str());
        if (result != 0)
        {
            DITTO_LOG_ERROR_STREAM("[Build] Script compilation failed" );
            return false;
        }

        fs::copy(dittoEngineDll, outputPath + "/DittoEngine.dll", fs::copy_options::overwrite_existing);
        DITTO_LOG_INFO_STREAM("[Build] Copied DittoEngine.dll to output" );

        DITTO_LOG_INFO_STREAM("[Build] Scripts compiled successfully" );
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error compiling scripts: " << e.what() );
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
            DITTO_LOG_ERROR_STREAM("[Build] Failed to create game.config" );
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
        DITTO_LOG_INFO_STREAM("[Build] Generated game.config with startupScene: " << startupSceneName );
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error generating game config: " << e.what() );
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
        
        DITTO_LOG_INFO_STREAM("[Build] Created launcher: " << batPath );
        return true;
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[Build] Error creating launcher: " << e.what() );
        return false;
    }
}

