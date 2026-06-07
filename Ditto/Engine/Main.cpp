#include "Core/Engine.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace std;

static string GetExeDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != string::npos) return fullPath.substr(0, lastSlash);
    }
#endif
    return filesystem::current_path().string();
}

static string ReadGameConfig(const string& configPath)
{
    ifstream file(configPath);
    if (!file.is_open()) return "";

    string startupScene;
    string line;
    while (getline(file, line))
    {
        size_t pos = line.find("\"startupScene\"");
        if (pos == string::npos) continue;

        size_t colonPos = line.find(':', pos);
        if (colonPos == string::npos) continue;

        size_t start = line.find('"', colonPos);
        if (start == string::npos) continue;
        size_t end = line.find('"', start + 1);
        if (end == string::npos) continue;

        startupScene = line.substr(start + 1, end - start - 1);
        break;
    }
    file.close();
    return startupScene;
}

int main(int argc, char* argv[])
{
    string exeDir = GetExeDirectory();
    cout << "[Ditto] EXE directory: " << exeDir << endl;

    string gameConfigPath = exeDir + "/game.config";
    bool gameMode = filesystem::exists(gameConfigPath);

    string startupScene;
    string projectPath;

    if (gameMode)
    {
        projectPath = exeDir;
        startupScene = ReadGameConfig(gameConfigPath);
        cout << "[Ditto] Game mode detected" << endl;
        cout << "[Ditto] Project path: " << projectPath << endl;
        cout << "[Ditto] Startup scene: " << startupScene << endl;
    }

    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];
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
