#include "LayoutManager.h"
#include "../3rdParty/ImGui/imgui.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

void LayoutManager::Initialize(const std::string& settingsPath)
{
    settingsDirectory = settingsPath;
    EnsureDirectoryExists();
}

std::string LayoutManager::GetLayoutFilePath(const std::string& layoutName)
{
    return settingsDirectory + "/" + layoutName + ".ini";
}

void LayoutManager::EnsureDirectoryExists()
{
    if (!fs::exists(settingsDirectory))
    {
        fs::create_directories(settingsDirectory);
    }
}

bool LayoutManager::SaveLayout(const std::string& layoutName)
{
    if (layoutName.empty()) return false;
    
    std::string filePath = GetLayoutFilePath(layoutName);
    
    // 使用ImGui内置函数获取当前INI配置
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    
    if (!iniData || iniSize == 0)
    {
        std::cerr << "Failed to get ImGui settings" << std::endl;
        return false;
    }
    
    // 写入文件
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to save layout: " << filePath << std::endl;
        return false;
    }
    
    file.write(iniData, iniSize);
    file.close();
    
    std::cout << "Layout saved: " << filePath << " (" << iniSize << " bytes)" << std::endl;
    return true;
}

bool LayoutManager::LoadLayout(const std::string& layoutName)
{
    std::string filePath = GetLayoutFilePath(layoutName);
    
    // 读取文件
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Failed to load layout: " << filePath << std::endl;
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
    {
        std::cerr << "Failed to read layout file: " << filePath << std::endl;
        return false;
    }
    file.close();
    
    // 使用ImGui内置函数加载INI配置
    ImGui::LoadIniSettingsFromMemory(buffer.data(), size);
    
    // 标记需要重新初始化Dock
    needsReloadDock = true;
    
    std::cout << "Layout loaded: " << filePath << " (" << size << " bytes)" << std::endl;
    return true;
}

bool LayoutManager::DeleteLayout(const std::string& layoutName)
{
    std::string filePath = GetLayoutFilePath(layoutName);
    
    try
    {
        if (fs::exists(filePath))
        {
            fs::remove(filePath);
            std::cout << "Layout deleted: " << filePath << std::endl;
            return true;
        }
        else
        {
            std::cerr << "Layout file does not exist: " << filePath << std::endl;
            return false;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error deleting layout: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> LayoutManager::GetAllLayoutNames()
{
    std::vector<std::string> layoutNames;
    
    try
    {
        if (fs::exists(settingsDirectory))
        {
            for (const auto& entry : fs::directory_iterator(settingsDirectory))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".ini")
                {
                    layoutNames.push_back(entry.path().stem().string());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading layout directory: " << e.what() << std::endl;
    }
    
    return layoutNames;
}