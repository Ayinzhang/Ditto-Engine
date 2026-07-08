#include "LayoutManager.h"
#include "../Engine/Core/Logger.h"
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
    
    
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    
    if (!iniData || iniSize == 0)
    {
        DITTO_LOG_ERROR_STREAM("Failed to get ImGui settings" );
        return false;
    }
    
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("Failed to save layout: " << filePath );
        return false;
    }
    
    file.write(iniData, iniSize);
    file.close();
    
    DITTO_LOG_INFO_STREAM("Layout saved: " << filePath << " (" << iniSize << " bytes)" );
    return true;
}

bool LayoutManager::LoadLayout(const std::string& layoutName)
{
    std::string filePath = GetLayoutFilePath(layoutName);
    
    
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("Failed to load layout: " << filePath );
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
    {
        DITTO_LOG_ERROR_STREAM("Failed to read layout file: " << filePath );
        return false;
    }
    file.close();
    
    
    ImGui::LoadIniSettingsFromMemory(buffer.data(), size);
    
    
    needsReloadDock = true;
    
    DITTO_LOG_INFO_STREAM("Layout loaded: " << filePath << " (" << size << " bytes)" );
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
            DITTO_LOG_INFO_STREAM("Layout deleted: " << filePath );
            return true;
        }
        else
        {
            DITTO_LOG_ERROR_STREAM("Layout file does not exist: " << filePath );
            return false;
        }
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("Error deleting layout: " << e.what() );
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
        DITTO_LOG_ERROR_STREAM("Error reading layout directory: " << e.what() );
    }
    
    return layoutNames;
}

