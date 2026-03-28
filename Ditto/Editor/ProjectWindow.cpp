#include "ProjectWindow.h"
#include "Editor.h"
#include "InspectorWindow.h"
#include "../Engine/Core/ProjectManager.h"
#include <filesystem>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>
#include <windows.h>
#include <objbase.h>

namespace fs = std::filesystem;

ProjectWindow::ProjectWindow(Editor* editor)
    : m_editor(editor)
{
}

void ProjectWindow::OnLoadScene(const std::string& scenePath)
{
    // 转发给 Editor
    if (m_editor) {
        m_editor->LoadSceneFromProject(scenePath);
    }
}

void ProjectWindow::OnFileSelected(const std::string& path, const std::string& name,
                                   const std::string& ext, const std::string& folder)
{
    if (m_editor) {
        // 如果 Inspector 锁定选择，不切换文件
        if (m_editor->lockingSelection) return;
        
        m_editor->selectedFile.path = path;
        m_editor->selectedFile.name = name;
        m_editor->selectedFile.extension = ext;
        m_editor->selectedFile.folder = folder;
    }
}

void ProjectWindow::Draw()
{
    auto& pm = ProjectManager::GetInstance();
    Project* proj = pm.GetCurrentProject();

    if (!proj)
    {
        ImGui::Begin("Project");
        ImGui::TextDisabled("No project loaded");
        ImGui::End();
        return;
    }

    std::string assetsPath = pm.GetProjectAssetsPath();

    ImGui::Begin("Project");

    // 顶部路径栏
    ImGui::Text("Project");
    ImGui::SameLine();

    // 后退按钮
    if (m_currentFolder != "Assets") {
        if (ImGui::Button("<")) {
            size_t lastSlash = m_currentFolder.find_last_of('/');
            if (lastSlash != std::string::npos) {
                m_currentFolder = m_currentFolder.substr(0, lastSlash);
                if (m_editor) m_editor->selectedFile.Clear();
            }
        }
        ImGui::SameLine();
    }

    ImGui::Text(" > ");
    ImGui::SameLine();
    ImGui::Text(m_currentFolder.c_str());
    ImGui::Separator();

    float panelWidth = ImGui::GetContentRegionAvail().x;
    float panelHeight = ImGui::GetContentRegionAvail().y;

    if (m_splitterPos < 100) m_splitterPos = 100;
    if (m_splitterPos > panelWidth - 100) m_splitterPos = panelWidth - 100;

    // 左侧 - 文件夹树
    ImGui::BeginChild("Folders", ImVec2(m_splitterPos, panelHeight), true);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

    std::function<void(const std::string&, const std::string&, int)> DrawFolderTree =
        [&](const std::string& folderPath, const std::string& displayPath, int depth) {
            try {
                if (!fs::exists(folderPath)) return;

                for (const auto& entry : fs::directory_iterator(folderPath)) {
                    if (entry.is_directory()) {
                        std::string folderName = entry.path().filename().string();
                        std::string fullPath = displayPath + "/" + folderName;
                        std::string fullFsPath = entry.path().string();

                        bool isSelected = (m_currentFolder == fullPath);

                        bool hasSubfolders = false;
                        bool hasFiles = false;
                        for (const auto& sub : fs::directory_iterator(fullFsPath)) {
                            if (sub.is_directory()) {
                                hasSubfolders = true;
                            } else {
                                hasFiles = true;
                            }
                        }

                        // 计算缩进：每层 18px
                        float indent = depth * 18.0f;

                        if (hasSubfolders) {
                            ImGui::PushID(fullPath.c_str());
                            
                            bool isOpen = IsFolderExpanded(fullPath);
                            
                            // 箭头按钮（12px）
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                            if (ImGui::ArrowButton(("##arrow_" + fullPath).c_str(), isOpen ? ImGuiDir_Down : ImGuiDir_Right)) {
                                ToggleFolderExpanded(fullPath);
                            }
                            ImGui::PopStyleVar();
                            
                            // 图标
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = isOpen ? m_editor->GetFolderOpenedIcon() : m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::SameLine();
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                            }
                            
                            // 名称
                            ImGui::SameLine();
                            ImVec2 textSize = ImGui::CalcTextSize(folderName.c_str());
                            ImGui::Text(folderName.c_str());
                            
                            // 检测点击（使用 InvisibleButton 覆盖文本区域）
                            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - textSize.x - 8, 
                                                        ImGui::GetCursorPosY() - textSize.y - 4));
                            ImGui::InvisibleButton(("folder_" + fullPath).c_str(), 
                                                   ImVec2(textSize.x + 8, textSize.y + 8));
                            
                            if (ImGui::IsItemClicked(0))
                            {
                                double currentTime = ImGui::GetTime();
                                bool isDoubleClick = (fullPath == m_lastClickedFolderPath) && 
                                                     (currentTime - m_lastClickTime < 1.0);
                                
                                if (isDoubleClick)
                                {
                                    // 双击：在资源管理器中打开文件夹
                                    std::cout << "[ProjectWindow] Double click on folder: " << folderName << std::endl;
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                    m_lastClickedFolderPath = "";
                                    m_lastClickTime = 0.0;
                                }
                                else
                                {
                                    // 单击：选中文件夹
                                    m_currentFolder = fullPath;
                                    if (m_editor) m_editor->selectedFile.Clear();
                                    m_lastClickedFolderPath = fullPath;
                                    m_lastClickTime = currentTime;
                                }
                            }
                            
                            // 子文件夹
                            if (isOpen) {
                                DrawFolderTree(fullFsPath, fullPath, depth + 1);
                            }
                            
                            ImGui::PopID();
                        } else if (hasFiles) {
                            ImGui::PushID(fullPath.c_str());
                            
                            // 无子文件夹但有文件：留出箭头位置（20px = 12px箭头 + 8px间距）+ 深度缩进
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            // 文件夹名称和点击处理
                            ImVec2 textSize = ImGui::CalcTextSize(folderName.c_str());
                            ImGui::Text(folderName.c_str());
                            
                            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - textSize.x - 8, 
                                                        ImGui::GetCursorPosY() - textSize.y - 4));
                            ImGui::InvisibleButton(("folder_" + fullPath).c_str(), 
                                                   ImVec2(textSize.x + 8, textSize.y + 8));
                            
                            if (ImGui::IsItemClicked(0))
                            {
                                double currentTime = ImGui::GetTime();
                                bool isDoubleClick = (fullPath == m_lastClickedFolderPath) && 
                                                     (currentTime - m_lastClickTime < 1.0);
                                
                                if (isDoubleClick)
                                {
                                    // 双击：在资源管理器中打开文件夹
                                    std::cout << "[ProjectWindow] Double click on folder: " << folderName << std::endl;
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                    m_lastClickedFolderPath = "";
                                    m_lastClickTime = 0.0;
                                }
                                else
                                {
                                    m_currentFolder = fullPath;
                                    if (m_editor) m_editor->selectedFile.Clear();
                                    m_lastClickedFolderPath = fullPath;
                                    m_lastClickTime = currentTime;
                                }
                            }
                            
                            ImGui::PopID();
                        } else {
                            ImGui::PushID(fullPath.c_str());
                            
                            // 空文件夹：留出箭头位置（20px = 12px箭头 + 8px间距）+ 深度缩进
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderEmptyIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            // 文件夹名称和点击处理
                            ImVec2 textSize = ImGui::CalcTextSize(folderName.c_str());
                            ImGui::Text(folderName.c_str());
                            
                            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() - textSize.x - 8, 
                                                        ImGui::GetCursorPosY() - textSize.y - 4));
                            ImGui::InvisibleButton(("folder_" + fullPath).c_str(), 
                                                   ImVec2(textSize.x + 8, textSize.y + 8));
                            
                            if (ImGui::IsItemClicked(0))
                            {
                                double currentTime = ImGui::GetTime();
                                bool isDoubleClick = (fullPath == m_lastClickedFolderPath) && 
                                                     (currentTime - m_lastClickTime < 1.0);
                                
                                if (isDoubleClick)
                                {
                                    // 双击：在资源管理器中打开文件夹
                                    std::cout << "[ProjectWindow] Double click on folder: " << folderName << std::endl;
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                    m_lastClickedFolderPath = "";
                                    m_lastClickTime = 0.0;
                                }
                                else
                                {
                                    m_currentFolder = fullPath;
                                    if (m_editor) m_editor->selectedFile.Clear();
                                    m_lastClickedFolderPath = fullPath;
                                    m_lastClickTime = currentTime;
                                }
                            }
                            
                            ImGui::PopID();
                        }
                    }
                }
            } catch (const std::exception&) {}
        };

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.0f));
    
    DrawFolderTree(assetsPath, "Assets", 0);
    ImGui::Dummy(ImVec2(0, 0));
    
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::EndChild();

    // 分隔条
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.5f));
    ImGui::Button("##splitter", ImVec2(1, panelHeight));
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (ImGui::IsItemActive())
    {
        m_splitterPos += ImGui::GetIO().MouseDelta.x;
        if (m_splitterPos < 100) m_splitterPos = 100;
        if (m_splitterPos > panelWidth - 100) m_splitterPos = panelWidth - 100;
    }

    ImGui::SameLine();

    // 右侧 - 文件视图
    ImGui::BeginChild("View", ImVec2(0, panelHeight), true);

    std::string folderPath = assetsPath;
    size_t pos = m_currentFolder.find('/');
    if (pos != std::string::npos)
    {
        folderPath = assetsPath + "/" + m_currentFolder.substr(pos + 1);
    }

    // 右键菜单
    if (ImGui::BeginPopupContextWindow("ProjectContext"))
    {
        if (!m_editor || !m_editor->selectedFile.IsValid())
        {
            if (ImGui::MenuItem("Create Folder..."))
            {
                m_showCreateFolderPopup = true;
                strcpy_s(m_newFolderNameBuffer, "NewFolder");
            }
            if (ImGui::MenuItem("Create Scene..."))
            {
                m_showCreateScenePopup = true;
                strcpy_s(m_newSceneNameBuffer, "NewScene");
            }
            if (ImGui::MenuItem("Create C# Script..."))
            {
                m_showCreateScriptPopup = true;
                strcpy_s(m_newScriptNameBuffer, "NewScript");
            }
        }
        ImGui::EndPopup();
    }

    float itemWidth = 80;
    float itemHeight = 80;
    float currentX = 0;
    float availWidth = ImGui::GetContentRegionAvail().x;

    try
    {
        if (fs::exists(folderPath))
        {
            // 先显示文件夹
            for (const auto& entry : fs::directory_iterator(folderPath))
            {
                if (entry.is_directory())
                {
                    std::string folderName = entry.path().filename().string();

                    if (currentX + itemWidth > availWidth)
                    {
                        ImGui::NewLine();
                        currentX = 0;
                    }

                    ImGui::BeginGroup();

                    bool isSelected = false;

                    ImVec2 cursorPos = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - 40) / 2, cursorPos.y));

                    unsigned int folderIcon = m_editor ? m_editor->GetFolderIcon() : 0;
                    if (folderIcon) {
                        ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(40, 40), ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "[]");
                    }

                    std::string displayName = folderName;
                    if (displayName.size() > 10) displayName = displayName.substr(0, 8) + "..";

                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - ImGui::CalcTextSize(displayName.c_str()).x) / 2, cursorPos.y + 50));
                    ImGui::Text(displayName.c_str());

                    ImGui::SetCursorPos(cursorPos);
                    ImGui::InvisibleButton(("folder_" + folderName).c_str(), ImVec2(itemWidth, itemHeight));

                    if (ImGui::IsItemClicked(0))
                    {
                        m_currentFolder = m_currentFolder + "/" + folderName;
                        if (m_editor) m_editor->selectedFile.Clear();
                    }

                    ImGui::EndGroup();
                    ImGui::SameLine();
                    currentX += itemWidth;
                }
            }

            // 再显示文件
            for (const auto& entry : fs::directory_iterator(folderPath))
            {
                if (entry.is_regular_file())
                {
                    std::string filename = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();

                    if (currentX + itemWidth > availWidth)
                    {
                        ImGui::NewLine();
                        currentX = 0;
                    }

                    ImGui::BeginGroup();

                    bool isSelected = m_editor && m_editor->selectedFile.IsValid() && 
                                      (m_editor->selectedFile.path == entry.path().string());

                    ImVec2 cursorPos = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - 40) / 2, cursorPos.y));

                    unsigned int iconTexture = m_editor ? m_editor->GetIconByExtension(ext) : 0;
                    if (iconTexture) {
                        ImGui::Image((void*)(intptr_t)iconTexture, ImVec2(40, 40), ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        if (isSelected)
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[]");
                        else if (ext == ".bin")
                            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "[]");
                        else if (ext == ".obj" || ext == ".fbx")
                            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[]");
                        else if (ext == ".mat")
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "[]");
                        else
                            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[]");
                    }

                    std::string displayName = filename;
                    if (ext == ".bin") displayName = filename.substr(0, filename.size() - 4);
                    if (displayName.size() > 10) displayName = displayName.substr(0, 8) + "..";

                    ImGui::SetCursorPos(ImVec2(cursorPos.x + (itemWidth - ImGui::CalcTextSize(displayName.c_str()).x) / 2, cursorPos.y + 50));

                    if (isSelected)
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), displayName.c_str());
                    else
                        ImGui::Text(displayName.c_str());

                    ImGui::SetCursorPos(cursorPos);
                    ImGui::InvisibleButton(("file_" + filename).c_str(), ImVec2(itemWidth, itemHeight));

                    if (ImGui::IsItemClicked(0))
                    {
                        std::string filePath = entry.path().string();
                        std::string nameOnly = filename.substr(0, filename.size() - ext.size());
                        double currentTime = ImGui::GetTime();
                        
                        // 检查是否是双击（1秒内点击同一文件）
                        bool isDoubleClick = (filePath == m_lastClickedFilePath) && 
                                             (currentTime - m_lastClickTime < 1.0);
                        
                        if (isDoubleClick)
                        {
                            // 双击：打开文件
                            std::cout << "[ProjectWindow] Double click on: " << filename << std::endl;
                            
                            if (ext == ".bin")
                            {
                                OnLoadScene(filePath);
                            }
                            else if (ext == ".cs")
                            {
                                OpenCSharpFile(filePath);
                            }
                            // 重置点击状态
                            m_lastClickedFilePath = "";
                            m_lastClickTime = 0.0;
                        }
                        else
                        {
                            // 单击：选中文件
                            OnFileSelected(filePath, nameOnly, ext, m_currentFolder);
                            
                            // 加载模型预览
                            if (m_editor && (ext == ".obj" || ext == ".fbx")) {
                                m_editor->LoadPreviewModel(filePath);
                            }
                            
                            // 记录点击状态
                            m_lastClickedFilePath = filePath;
                            m_lastClickTime = currentTime;
                        }
                    }

                    // 右键菜单
                    if (ImGui::BeginPopupContextItem())
                    {
                        // 所有文件都可以 Rename
                        if (ImGui::MenuItem("Rename"))
                        {
                            m_renameTargetPath = entry.path().string();
                            m_renameTargetOldName = filename.substr(0, filename.size() - ext.size());
                            strcpy_s(m_renameBuffer, sizeof(m_renameBuffer), m_renameTargetOldName.c_str());
                            m_showRenamePopup = true;
                        }
                        ImGui::Separator();
                        
                        if (ImGui::MenuItem("Delete"))
                        {
                            try {
                                fs::remove(entry.path());
                                if (m_editor && m_editor->selectedFile.path == entry.path().string())
                                    m_editor->selectedFile.Clear();
                            }
                            catch (const std::exception& e) {
                                std::cerr << "Delete failed: " << e.what() << std::endl;
                            }
                        }
                        if (ImGui::MenuItem("Show in Explorer"))
                        {
                            std::wstring fullPathW = std::filesystem::absolute(entry.path()).wstring();

                            HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
                            PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(fullPathW.c_str());
                            SHOpenFolderAndSelectItems(pidl, 0, NULL, 0); ILFree(pidl);
                            CoUninitialize();
                        }
                        ImGui::EndPopup();
                    }

                    // 设置拖拽源（.cs 文件可拖到 Inspector）
                    if (ext == ".cs" && ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("CS_SCRIPT", entry.path().string().c_str(), 
                            entry.path().string().length() + 1);
                        ImGui::EndDragDropSource();
                    }

                    // 双击检测已在上面的 IsItemClicked 中处理

                    currentX += itemWidth;
                    ImGui::EndGroup();
                    ImGui::SameLine();
                }
            }
        }
    }
    catch (const std::exception&) {}

    ImGui::EndChild();

    // 绘制弹窗
    DrawPopups();

    ImGui::End();
}

void ProjectWindow::OnScriptDropped(const std::string& scriptPath)
{
    // TODO: 将脚本附加到当前选中的 GameObject
    // 需要和 Inspector/Editor 协调
    std::cout << "[ProjectWindow] Script dropped: " << scriptPath << std::endl;
    
    if (m_editor && m_editor->selectedObject)
    {
        // 通知 Editor 添加脚本组件
        m_editor->OnScriptComponentDropped(scriptPath);
    }
}

void ProjectWindow::CreateNewScript(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();
    
    std::string targetFolder = assetsPath;
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    } else {
        targetFolder = assetsPath + "/Scripts";
    }
    
    if (!fs::exists(targetFolder))
    {
        fs::create_directories(targetFolder);
    }
    
    std::string filePath = targetFolder + "/" + name + ".cs";
    
    std::ofstream file(filePath);
    if (file.is_open())
    {
        file << "using DittoEngine;\n";
        file << "\n";
        file << "public class " << name << " : MonoBehaviour\n";
        file << "{\n";
        file << "    public float speed = 5.0f;\n";
        file << "    public int health = 100;\n";
        file << "\n";
        file << "    void Start()\n";
        file << "    {\n";
        file << "        Debug.Log(\"" << name << ": Start\");\n";
        file << "    }\n";
        file << "\n";
        file << "    void Update()\n";
        file << "    {\n";
        file << "    }\n";
        file << "\n";
        file << "    void OnDestroy()\n";
        file << "    {\n";
        file << "        Debug.Log(\"" << name << ": OnDestroy\");\n";
        file << "    }\n";
        file << "}\n";
        file.close();
        std::cout << "[ProjectWindow] Created script: " << filePath << std::endl;
    }
}

void ProjectWindow::CreateNewFolder(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();
    
    std::string targetFolder = assetsPath;
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }
    
    std::string newFolderPath = targetFolder + "/" + name;
    try
    {
        if (!fs::exists(newFolderPath))
        {
            fs::create_directories(newFolderPath);
            std::cout << "[ProjectWindow] Created folder: " << newFolderPath << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ProjectWindow] Failed to create folder: " << e.what() << std::endl;
    }
}

void ProjectWindow::CreateNewScene(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();
    
    std::string targetFolder = assetsPath + "/Scenes";
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }
    
    if (!fs::exists(targetFolder)) {
        fs::create_directories(targetFolder);
    }
    
    std::string scenePath = targetFolder + "/" + name + ".bin";
    
    try
    {
        fs::create_directories(assetsPath + "/Scenes");
        
        std::ofstream file(scenePath, std::ios::binary);
        if (file.is_open())
        {
            const char SCENE_MAGIC[4] = { 'S', 'C', 'N', '\0' };
            file.write(SCENE_MAGIC, 4);
            uint32_t version = 1;
            file.write(reinterpret_cast<const char*>(&version), sizeof(version));
            uint32_t gameObjectCount = 0;
            file.write(reinterpret_cast<const char*>(&gameObjectCount), sizeof(gameObjectCount));
            uint64_t fileSize = 0;
            file.write(reinterpret_cast<const char*>(&fileSize), sizeof(fileSize));
            file.close();
            std::cout << "[ProjectWindow] Created scene: " << scenePath << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ProjectWindow] Failed to create scene: " << e.what() << std::endl;
    }
}

void ProjectWindow::RenameFile(const std::string& oldPath, const std::string& newName)
{
    try {
        fs::path p(oldPath);
        std::string ext = p.extension().string();
        std::string parent = p.parent_path().string();
        std::string newPath = parent + "/" + newName + ext;
        
        fs::rename(oldPath, newPath);
        std::cout << "[ProjectWindow] Renamed: " << oldPath << " -> " << newPath << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[ProjectWindow] Rename failed: " << e.what() << std::endl;
    }
}

void ProjectWindow::DrawPopups()
{
    // 创建文件夹弹窗
    if (m_showCreateFolderPopup)
    {
        ImGui::OpenPopup("Create Folder");
        m_showCreateFolderPopup = false;
    }
    
    if (ImGui::BeginPopupModal("Create Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Folder Name:"); ImGui::SameLine();
        ImGui::InputText("##FolderName", m_newFolderNameBuffer, sizeof(m_newFolderNameBuffer));
        
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newFolderNameBuffer) > 0)
            {
                CreateNewFolder(m_newFolderNameBuffer);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // 创建场景弹窗
    if (m_showCreateScenePopup)
    {
        ImGui::OpenPopup("Create Scene");
        m_showCreateScenePopup = false;
    }
    
    if (ImGui::BeginPopupModal("Create Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Scene Name:"); ImGui::SameLine();
        ImGui::InputText("##SceneName", m_newSceneNameBuffer, sizeof(m_newSceneNameBuffer));
        
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newSceneNameBuffer) > 0)
            {
                CreateNewScene(m_newSceneNameBuffer);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // 创建脚本弹窗
    if (m_showCreateScriptPopup)
    {
        ImGui::OpenPopup("Create Script");
        m_showCreateScriptPopup = false;
    }
    
    if (ImGui::BeginPopupModal("Create Script", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Script Name:"); ImGui::SameLine();
        ImGui::InputText("##ScriptName", m_newScriptNameBuffer, sizeof(m_newScriptNameBuffer));
        
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newScriptNameBuffer) > 0)
            {
                CreateNewScript(m_newScriptNameBuffer);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // 重命名弹窗
    if (m_showRenamePopup)
    {
        ImGui::OpenPopup("Rename");
        m_showRenamePopup = false;
    }
    
    if (ImGui::BeginPopupModal("Rename", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("New Name:"); ImGui::SameLine();
        ImGui::InputText("##Rename", m_renameBuffer, sizeof(m_renameBuffer));
        
        if (ImGui::Button("Confirm", ImVec2(120, 0)))
        {
            if (strlen(m_renameBuffer) > 0 && m_renameBuffer != m_renameTargetOldName)
            {
                RenameFile(m_renameTargetPath, m_renameBuffer);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ProjectWindow::AddConsoleMessage(const std::string& message)
{
    m_consoleMessages.push_back(message);
    if (m_consoleMessages.size() > 100)
    {
        m_consoleMessages.erase(m_consoleMessages.begin());
    }
}

bool ProjectWindow::IsFolderExpanded(const std::string& path) const
{
    return m_expandedFolders.find(path) != m_expandedFolders.end();
}

void ProjectWindow::ToggleFolderExpanded(const std::string& path)
{
    if (IsFolderExpanded(path)) {
        m_expandedFolders.erase(path);
    } else {
        m_expandedFolders.insert(path);
    }
}

void ProjectWindow::OpenCSharpFile(const std::string& filePath)
{
    std::cout << "[ProjectWindow] Opening C# file: " << filePath << std::endl;
    
    // 获取项目路径
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        std::cerr << "[ProjectWindow] No project loaded" << std::endl;
        return;
    }
    
    std::cout << "[ProjectWindow] Project name: " << proj->name << std::endl;
    std::cout << "[ProjectWindow] Project path: " << proj->path << std::endl;
    
    // 转换为绝对路径并规范化（统一使用反斜杠）
    std::filesystem::path absPath = std::filesystem::absolute(proj->path);
    std::string projectPath = absPath.string();
    std::replace(projectPath.begin(), projectPath.end(), '/', '\\');
    
    std::cout << "[ProjectWindow] Absolute project path: " << projectPath << std::endl;
    
    std::string projectName = proj->name;
    std::string solutionPath = projectPath + "\\" + projectName + ".sln";
    
    std::cout << "[ProjectWindow] Solution path: " << solutionPath << std::endl;
    
    // 检查解决方案是否存在，不存在则创建
    if (!fs::exists(solutionPath))
    {
        std::cout << "[ProjectWindow] Creating Visual Studio solution: " << solutionPath << std::endl;
        if (!CreateVisualStudioSolution(projectPath, projectName))
        {
            std::cerr << "[ProjectWindow] Failed to create solution" << std::endl;
            return;
        }
    }
    
    // 使用 Visual Studio 打开解决方案并定位到文件
    std::string vsPath = GetVisualStudioPath();
    if (vsPath.empty())
    {
        std::cerr << "[ProjectWindow] Visual Studio not found" << std::endl;
        return;
    }
    
    // 规范化文件路径
    std::string normalizedFilePath = filePath;
    std::replace(normalizedFilePath.begin(), normalizedFilePath.end(), '/', '\\');
    
    std::cout << "[ProjectWindow] Opening solution: " << solutionPath << std::endl;
    std::cout << "[ProjectWindow] Target file: " << normalizedFilePath << std::endl;
    
    // 使用 devenv 打开解决方案和文件
    // 格式: devenv "solution.sln" /Edit "filepath"
    std::string args = "\"" + solutionPath + "\" /Edit \"" + normalizedFilePath + "\"";
    
    HINSTANCE result = ShellExecuteA(NULL, "open", vsPath.c_str(), args.c_str(), NULL, SW_SHOW);
    
    if ((intptr_t)result <= 32)
    {
        std::cerr << "[ProjectWindow] Failed to open Visual Studio, error: " << (intptr_t)result << std::endl;
        return;
    }
    
    std::cout << "[ProjectWindow] Visual Studio opened successfully" << std::endl;
}

bool ProjectWindow::CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName)
{
    // 创建 .csproj 文件
    std::string csprojPath = projectPath + "/" + projectName + ".csproj";
    std::ofstream csprojFile(csprojPath);
    if (!csprojFile.is_open())
    {
        std::cerr << "[ProjectWindow] Failed to create .csproj file" << std::endl;
        return false;
    }
    
    csprojFile << "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
    csprojFile << "  <PropertyGroup>\n";
    csprojFile << "    <TargetFramework>net8.0</TargetFramework>\n";
    csprojFile << "    <ImplicitUsings>enable</ImplicitUsings>\n";
    csprojFile << "    <Nullable>enable</Nullable>\n";
    csprojFile << "  </PropertyGroup>\n";
    csprojFile << "  <ItemGroup>\n";
    
    // 添加所有 .cs 文件
    for (const auto& entry : fs::recursive_directory_iterator(projectPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cs")
        {
            std::string relativePath = fs::relative(entry.path(), projectPath).string();
            csprojFile << "    <Compile Include=\"" << relativePath << "\" />\n";
        }
    }
    
    csprojFile << "  </ItemGroup>\n";
    csprojFile << "</Project>\n";
    csprojFile.close();
    
    // 创建 .sln 文件
    std::string slnPath = projectPath + "/" + projectName + ".sln";
    std::ofstream slnFile(slnPath);
    if (!slnFile.is_open())
    {
        std::cerr << "[ProjectWindow] Failed to create .sln file" << std::endl;
        return false;
    }
    
    // 生成 GUID
    GUID guid;
    CoCreateGuid(&guid);
    char guidStr[40];
    sprintf_s(guidStr, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              guid.Data1, guid.Data2, guid.Data3,
              guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    
    slnFile << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
    slnFile << "# Visual Studio Version 17\n";
    slnFile << "VisualStudioVersion = 17.0.31903.59\n";
    slnFile << "MinimumVisualStudioVersion = 10.0.40219.1\n";
    slnFile << "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"" << projectName << "\", \"" << projectName << ".csproj\", \"" << guidStr << "\"\n";
    slnFile << "EndProject\n";
    slnFile << "Global\n";
    slnFile << "  GlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
    slnFile << "    Debug|Any CPU = Debug|Any CPU\n";
    slnFile << "    Release|Any CPU = Release|Any CPU\n";
    slnFile << "  EndGlobalSection\n";
    slnFile << "  GlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
    slnFile << "    {" << guidStr << "}.Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
    slnFile << "    {" << guidStr << "}.Debug|Any CPU.Build.0 = Debug|Any CPU\n";
    slnFile << "    {" << guidStr << "}.Release|Any CPU.ActiveCfg = Release|Any CPU\n";
    slnFile << "    {" << guidStr << "}.Release|Any CPU.Build.0 = Release|Any CPU\n";
    slnFile << "  EndGlobalSection\n";
    slnFile << "EndGlobal\n";
    slnFile.close();
    
    std::cout << "[ProjectWindow] Created solution: " << slnPath << std::endl;
    return true;
}

std::string ProjectWindow::GetVisualStudioPath()
{
    // 尝试从注册表获取 Visual Studio 路径
    HKEY hKey;
    const char* subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\devenv.exe";
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char path[MAX_PATH];
        DWORD pathSize = MAX_PATH;
        if (RegQueryValueExA(hKey, NULL, NULL, NULL, (LPBYTE)path, &pathSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return std::string(path);
        }
        RegCloseKey(hKey);
    }
    
    // 尝试常见路径
    std::vector<std::string> possiblePaths = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\Common7\\IDE\\devenv.exe",
        "D:\\Visual Studio 2022\\Common7\\IDE\\devenv.exe"
    };
    
    for (const auto& path : possiblePaths)
    {
        if (fs::exists(path))
        {
            return path;
        }
    }
    
    // 最后尝试在 PATH 中查找
    char pathEnv[MAX_PATH];
    if (GetEnvironmentVariableA("PATH", pathEnv, MAX_PATH) > 0)
    {
        std::string pathStr(pathEnv);
        size_t pos = 0;
        while ((pos = pathStr.find(';')) != std::string::npos)
        {
            std::string dir = pathStr.substr(0, pos);
            std::string devenvPath = dir + "\\devenv.exe";
            if (fs::exists(devenvPath))
            {
                return devenvPath;
            }
            pathStr.erase(0, pos + 1);
        }
    }
    
    return "";
}
