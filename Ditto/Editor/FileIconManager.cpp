#define GLFW_INCLUDE_NONE
#include "FileIconManager.h"
#include <iostream>
#include <filesystem>
// stb_image 实现在 Editor.cpp 中，这里只声明
#include "../3rdParty/stb_image.h"
#include "../3rdParty/GLAD/glad.h"
#include "../3rdParty/GLFW/glfw3.h"

// 扩展名到图标索引: 0:Default, 1:Cpp, 2:Prefab(模型), 3:Text(材质), 4:Shader, 5:Scene, 6:Folder(纹理)
const char* FileIconManager::s_iconFiles[] = {
    "Default.png", "Cpp.png", "Prefab.png", "Text.png", "Shader.png", "Scene.png", "Folder.png"
};

FileIconManager& FileIconManager::GetInstance()
{
    static FileIconManager instance;
    return instance;
}

void FileIconManager::Initialize(const std::string& assetsPath)
{
    if (m_initialized) return;
    
    m_assetsPath = assetsPath;
    std::cout << "[FileIcon] Initializing from: " << assetsPath << std::endl;
    
    // 加载文件图标
    for (int i = 0; i < 6; i++) {
        std::string path = assetsPath + "/" + s_iconFiles[i];
        m_icons[i] = LoadIcon(path);
    }
    
    // 加载文件夹图标
    m_folderIcon = LoadIcon(assetsPath + "/Folder.png");
    m_folderEmptyIcon = LoadIcon(assetsPath + "/FolderEmpty.png");
    
    m_initialized = true;
    std::cout << "[FileIcon] Initialized successfully" << std::endl;
}

unsigned int FileIconManager::LoadIcon(const std::string& iconPath)
{
    namespace fs = std::filesystem;
    
    if (!fs::exists(iconPath)) {
        std::cerr << "[FileIcon] File not found: " << iconPath << std::endl;
        return 0;
    }
    
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        std::cerr << "[FileIcon] Failed to load: " << iconPath << std::endl;
        return 0;
    }
    
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    stbi_image_free(data);
    
    std::cout << "[FileIcon] Loaded: " << iconPath << " (" << width << "x" << height << ")" << std::endl;
    return textureID;
}

int FileIconManager::GetIconIndex(const std::string& ext)
{
    if (ext == ".cpp" || ext == ".h" || ext == ".c" || ext == ".hpp") return 1;  // Cpp.png
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return 2;  // Prefab.png (模型)
    if (ext == ".mat") return 3;  // Text.png (材质)
    if (ext == ".shader") return 4;  // Shader.png
    if (ext == ".bin") return 5;  // Scene.png
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") return 6;  // Folder.png (纹理)
    return 0;  // 默认 Default.png
}

unsigned int FileIconManager::GetIconByExtension(const std::string& extension)
{
    if (!m_initialized) {
        std::cerr << "[FileIcon] Not initialized!" << std::endl;
        return 0;
    }
    
    int idx = GetIconIndex(extension);
    return m_icons[idx];  // GetIconIndex 已经返回 0-6 范围
}

void FileIconManager::Cleanup()
{
    for (int i = 0; i < 7; i++) {
        if (m_icons[i]) {
            glDeleteTextures(1, &m_icons[i]);
            m_icons[i] = 0;
        }
    }
    if (m_folderIcon) {
        glDeleteTextures(1, &m_folderIcon);
        m_folderIcon = 0;
    }
    if (m_folderEmptyIcon) {
        glDeleteTextures(1, &m_folderEmptyIcon);
        m_folderEmptyIcon = 0;
    }
    m_initialized = false;
}