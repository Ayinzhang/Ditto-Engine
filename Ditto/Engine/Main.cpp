#include "Core/Engine.h"
#include "Core/Logger.h"
#include "Core/JsonConfig.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace std;
namespace fs = filesystem;

struct Args
{
    bool gameMode = false;
    bool showHelp = false;
    string projectPath;
    string startupScene;
};

class ArgsParser
{
public:
    static optional<Args> Parse(int argc, char* argv[])
    {
        Args args;

        for (int i = 1; i < argc; ++i)
        {
            const string a = argv[i];

            if (a == "-h" || a == "--help" || a == "/?")
            {
                args.showHelp = true;
            }
            else if (a == "-game" || a == "/game")
            {
                args.gameMode = true;
            }
            else if ((a == "-project" || a == "/project") && i + 1 < argc)
            {
                args.projectPath = argv[++i];
                args.gameMode    = true;
            }
            else if ((a == "-scene" || a == "/scene") && i + 1 < argc)
            {
                args.startupScene = argv[++i];
            }
            else
            {
                DITTO_LOG_ERROR_STREAM("[Ditto] Unknown argument: " << a);
                return nullopt;
            }
        }

        return args;
    }

    static void PrintUsage(const char* exeName)
    {
        DITTO_LOG_INFO_STREAM("Ditto Engine\n"
            << "Usage: " << exeName << " [options]\n"
            << "\n"
            << "Options:\n"
            << "  -game                 Force Game mode (no editor window).\n"
            << "  -project <path>       Path to a Ditto project. Implies -game.\n"
            << "  -scene <scene>        Scene to load on startup. Overrides game.config.\n"
            << "  -h, --help, /?        Show this help and exit.\n"
            << "\n"
            << "If a game.config file exists next to the executable, Game mode is\n"
            << "entered automatically. CLI flags override the file.");
    }
};

static string GetExeDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        wchar_t wpath[32768] = {};
        len = GetModuleFileNameW(nullptr, wpath, size(wpath));
        if (len == 0) return fs::current_path().string();

        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wpath, (int)len, nullptr, 0, nullptr, nullptr);
        string utf8(utf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath, (int)len, utf8.data(), utf8Len, nullptr, nullptr);

        string fullPath = move(utf8);
        size_t lastSlash = fullPath.find_last_of("\\/");
        return (lastSlash == string::npos) ? fullPath : fullPath.substr(0, lastSlash);
    }

    string fullPath(path, len);
    size_t lastSlash = fullPath.find_last_of("\\/");
    return (lastSlash == string::npos) ? fullPath : fullPath.substr(0, lastSlash);
#else
    return fs::current_path().string();
#endif
}

static bool IsValidProjectPath(const string& path, string* outError = nullptr)
{
    auto fail = [&](const char* msg) {
        if (outError) *outError = msg;
        return false;
    };

    if (path.empty())                   return fail("project path is empty");
    if (!fs::exists(path))              return fail("project path does not exist");
    if (!fs::is_directory(path))        return fail("project path is not a directory");

    fs::path assetsDir = fs::path(path) / "Assets";
    if (!fs::exists(assetsDir))
        return fail("project path has no 'Assets/' subfolder (not a Ditto project?)");

    return true;
}

static void ReportFatal(const string& message)
{
    DITTO_LOG_ERROR_STREAM("[Ditto] FATAL: " << message);

#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), "Ditto Engine - Fatal Error", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
#endif
}

int main(int argc, char* argv[])
{
    auto parsedArgs = ArgsParser::Parse(argc, argv);
    if (!parsedArgs)
    {
        ArgsParser::PrintUsage(argv[0]);
        return 1;
    }
    Args args = *parsedArgs;

    if (args.showHelp)
    {
        ArgsParser::PrintUsage(argv[0]);
        return 0;
    }

    const string exeDir = GetExeDirectory();
    DITTO_LOG_INFO_STREAM("[Ditto] EXE directory: " << exeDir);

    const fs::path gameConfigPath = fs::path(exeDir) / "game.config";
    const bool hasConfigFile = fs::exists(gameConfigPath);

    string projectPath;
    string startupScene;

    if (hasConfigFile)
    {
        args.gameMode = true;

        GameConfig cfg;
        if (!Ditto::JsonConfig::ReadGameConfig(gameConfigPath, cfg))
        {
            DITTO_LOG_WARN_STREAM("[Ditto] could not read " << gameConfigPath);
        }
        else
        {
            if (cfg.startupScene.empty())
            {
                DITTO_LOG_WARN("[Ditto] game.config has no 'startupScene' key");
            }
            projectPath = exeDir;
            if (!cfg.startupScene.empty())
                startupScene = cfg.startupScene;

            DITTO_LOG_INFO("[Ditto] Game mode (game.config detected)");
            DITTO_LOG_INFO_STREAM("[Ditto] Project path: " << projectPath);
            DITTO_LOG_INFO_STREAM("[Ditto] Startup scene: " << startupScene);
        }
    }

    if (!args.projectPath.empty())   projectPath  = args.projectPath;
    if (!args.startupScene.empty())  startupScene = args.startupScene;

    if (args.gameMode)
    {
        string err;
        if (!IsValidProjectPath(projectPath, &err))
        {
            ReportFatal("Invalid project path '" + projectPath + "': " + err);
            return 1;
        }
    }

    unique_ptr<Engine> engine;
    try
    {
        if (args.gameMode && !projectPath.empty())
            engine = make_unique<Engine>(true, projectPath, startupScene);
        else
            engine = make_unique<Engine>();
    }
    catch (const exception& e)
    {
        ReportFatal(string("Failed to construct engine: ") + e.what());
        return 1;
    }
    catch (...)
    {
        ReportFatal("Failed to construct engine: unknown exception");
        return 1;
    }

    try
    {
        engine->Run();
    }
    catch (const exception& e)
    {
        ReportFatal(string("Engine::Run() threw: ") + e.what());
        return 1;
    }
    catch (...)
    {
        ReportFatal("Engine::Run() threw an unknown exception");
        return 1;
    }

    return 0;
}
