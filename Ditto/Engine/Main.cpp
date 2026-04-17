#include "Core/Engine.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

static std::string GetExeDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != std::string::npos)
            return fullPath.substr(0, lastSlash);
    }
#endif
    return std::filesystem::current_path().string();
}

static std::string ReadGameConfig(const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open()) return "";

    std::string startupScene;
    std::string line;
    while (std::getline(file, line))
    {
        size_t pos = line.find("\"startupScene\"");
        if (pos == std::string::npos) continue;

        size_t colonPos = line.find(':', pos);
        if (colonPos == std::string::npos) continue;

        size_t start = line.find('"', colonPos);
        if (start == std::string::npos) continue;
        size_t end = line.find('"', start + 1);
        if (end == std::string::npos) continue;

        startupScene = line.substr(start + 1, end - start - 1);
        break;
    }
    file.close();
    return startupScene;
}

int main(int argc, char* argv[])
{
    std::string exeDir = GetExeDirectory();
    std::cout << "[Ditto] EXE directory: " << exeDir << std::endl;

    std::string gameConfigPath = exeDir + "/game.config";
    bool gameMode = std::filesystem::exists(gameConfigPath);

    std::string startupScene;
    std::string projectPath;

    if (gameMode)
    {
        projectPath = exeDir;
        startupScene = ReadGameConfig(gameConfigPath);
        std::cout << "[Ditto] Game mode detected" << std::endl;
        std::cout << "[Ditto] Project path: " << projectPath << std::endl;
        std::cout << "[Ditto] Startup scene: " << startupScene << std::endl;
    }

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "-game" || arg == "/game")
        {
            gameMode = true;
        }
        else if (arg == "-project" && i + 1 < argc)
        {
            projectPath = argv[++i];
            gameMode = true;
        }
        else if (arg == "-scene" && i + 1 < argc)
        {
            startupScene = argv[++i];
        }
    }

    Engine* engine = nullptr;

    if (gameMode && !projectPath.empty())
    {
        engine = new Engine(true, projectPath, startupScene);
    }
    else
    {
        engine = new Engine();
    }

    engine->Run();
    delete engine;
    return 0;
}
