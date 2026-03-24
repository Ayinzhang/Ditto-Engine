#include "ProjectWindow.h"
#include "Editor.h"
#include "InspectorWindow.h"
#include "../Engine/Core/ProjectManager.h"
#include <filesystem>
#include <shlobj.h>
#include <iostream>
#include <fstream>

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
                            if (ImGui::Selectable(folderName.c_str(), isSelected)) {
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
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
                            
                            if (ImGui::Selectable(folderName.c_str(), isSelected)) {
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
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
                            
                            if (ImGui::Selectable(folderName.c_str(), isSelected)) {
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
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
                        std::string nameOnly = filename.substr(0, filename.size() - ext.size());
                        OnFileSelected(entry.path().string(), nameOnly, ext, m_currentFolder);

                        // 加载模型预览
                        if (m_editor && (ext == ".obj" || ext == ".fbx")) {
                            m_editor->LoadPreviewModel(entry.path().string());
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

                    // 双击加载场景
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        if (ext == ".bin")
                        {
                            OnLoadScene(entry.path().string());
                        }
                    }

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
