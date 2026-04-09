#include "Core/Engine.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[])
{
    bool gameMode = false;
    std::string projectPath;
    
    // 检测是否在打包目录运行
    if (argc == 1)
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path projectFile = currentPath / "project.json";
        
        if (std::filesystem::exists(projectFile))
        {
            gameMode = true;
            projectPath = currentPath.string();
            std::cout << "[Ditto] Auto-detected project: " << projectPath << std::endl;
        }
    }
    else
    {
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if (arg == "-game" || arg == "/game")
            {
                gameMode = true;
            }
            else if (arg == "-project" && i + 1 < argc)
            {
                projectPath = argv[i + 1];
                i++;
            }
        }
    }
    
    Engine* engine = new Engine();
    
    if (gameMode && !projectPath.empty())
    {
        engine->SetProjectPath(projectPath);
    }
    
    engine->Run();
}