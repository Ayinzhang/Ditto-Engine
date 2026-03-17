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
    return settingsDirectory + "/" + layoutName + ".layout";
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
    std::ofstream file(filePath, std::ios::binary);
    
    if (!file.is_open())
    {
        std::cerr << "Failed to save layout: " << filePath << std::endl;
        return false;
    }
    
    // 写入布局名称
    size_t nameLen = layoutName.size();
    file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    file.write(layoutName.c_str(), nameLen);
    
    // 写入窗口数量
    size_t windowCount = currentLayout.windows.size();
    file.write(reinterpret_cast<const char*>(&windowCount), sizeof(windowCount));
    
    // 写入每个窗口的布局信息
    for (const auto& [windowName, windowLayout] : currentLayout.windows)
    {
        // 窗口名称
        size_t winNameLen = windowName.size();
        file.write(reinterpret_cast<const char*>(&winNameLen), sizeof(winNameLen));
        file.write(windowName.c_str(), winNameLen);
        
        // 位置和大小
        file.write(reinterpret_cast<const char*>(&windowLayout.pos.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&windowLayout.pos.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&windowLayout.size.x), sizeof(float));
        file.write(reinterpret_cast<const char*>(&windowLayout.size.y), sizeof(float));
        file.write(reinterpret_cast<const char*>(&windowLayout.visible), sizeof(bool));
        file.write(reinterpret_cast<const char*>(&windowLayout.collapsed), sizeof(bool));
    }
    
    file.close();
    std::cout << "Layout saved: " << filePath << std::endl;
    return true;
}

bool LayoutManager::LoadLayout(const std::string& layoutName)
{
    std::string filePath = GetLayoutFilePath(layoutName);
    std::ifstream file(filePath, std::ios::binary);
    
    if (!file.is_open())
    {
        std::cerr << "Failed to load layout: " << filePath << std::endl;
        return false;
    }
    
    // 清空当前布局
    currentLayout.windows.clear();
    
    // 读取布局名称
    size_t nameLen;
    file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    std::string layoutNameStr(nameLen, '\0');
    file.read(&layoutNameStr[0], nameLen);
    currentLayout.name = layoutNameStr;
    
    // 读取窗口数量
    size_t windowCount;
    file.read(reinterpret_cast<char*>(&windowCount), sizeof(windowCount));
    
    // 读取每个窗口的布局信息
    for (size_t i = 0; i < windowCount; i++)
    {
        // 窗口名称
        size_t winNameLen;
        file.read(reinterpret_cast<char*>(&winNameLen), sizeof(winNameLen));
        std::string windowName(winNameLen, '\0');
        file.read(&windowName[0], winNameLen);
        
        WindowLayout windowLayout;
        windowLayout.name = windowName;
        
        // 位置和大小
        float posX, posY, sizeX, sizeY;
        file.read(reinterpret_cast<char*>(&posX), sizeof(float));
        file.read(reinterpret_cast<char*>(&posY), sizeof(float));
        file.read(reinterpret_cast<char*>(&sizeX), sizeof(float));
        file.read(reinterpret_cast<char*>(&sizeY), sizeof(float));
        windowLayout.pos = glm::vec2(posX, posY);
        windowLayout.size = glm::vec2(sizeX, sizeY);
        
        file.read(reinterpret_cast<char*>(&windowLayout.visible), sizeof(bool));
        file.read(reinterpret_cast<char*>(&windowLayout.collapsed), sizeof(bool));
        
        currentLayout.windows[windowName] = windowLayout;
    }
    
    file.close();
    std::cout << "Layout loaded: " << filePath << std::endl;
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
                if (entry.is_regular_file() && entry.path().extension() == ".layout")
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

void LayoutManager::SaveCurrentWindowState(const std::string& windowName, const ImVec2& pos, const ImVec2& size, bool visible, bool collapsed)
{
    WindowLayout layout;
    layout.name = windowName;
    layout.pos = glm::vec2(pos.x, pos.y);
    layout.size = glm::vec2(size.x, size.y);
    layout.visible = visible;
    layout.collapsed = collapsed;
    
    currentLayout.windows[windowName] = layout;
}
