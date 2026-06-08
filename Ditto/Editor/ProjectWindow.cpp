#include "ProjectWindow.h"
#include "Editor.h"
#include "InspectorWindow.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/Logger.h"
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
#include <comdef.h>

namespace fs = std::filesystem;

ProjectWindow::ProjectWindow(Editor* editor)
    : m_editor(editor)
{
}

void ProjectWindow::OnLoadScene(const std::string& scenePath)
{
    // Forward to Editor
    if (m_editor) {
        m_editor->LoadSceneFromProject(scenePath);
    }
}

void ProjectWindow::OnFileSelected(const std::string& path, const std::string& name,
                                   const std::string& ext, const std::string& folder)
{
    if (m_editor) {
        // If Inspector locks selection, don't switch files
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

    // Remember which dock node Project lives in, so Console can dock beside it.
    m_projectDockId = ImGui::GetWindowDockID();

    // Top path bar
    ImGui::Text("Project");
    ImGui::SameLine();

    // Back button
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

    // Left side - folder tree
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

                        // Calculate indent: 18px per level
                        float indent = depth * 18.0f;

                        if (hasSubfolders) {
                            ImGui::PushID(fullPath.c_str());
                            
                            bool isOpen = IsFolderExpanded(fullPath);
                            
                            // Arrow button (12px)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                            if (ImGui::ArrowButton(("##arrow_" + fullPath).c_str(), isOpen ? ImGuiDir_Down : ImGuiDir_Right)) {
                                ToggleFolderExpanded(fullPath);
                            }
                            ImGui::PopStyleVar();
                            
                            // Icon
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = isOpen ? m_editor->GetFolderOpenedIcon() : m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::SameLine();
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                            }
                            
                            // Name - use Selectable to detect clicks (same as Hierarchy)
                            ImGui::SameLine();
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                std::cout << "[ProjectWindow] Folder clicked: " << folderName << std::endl;
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            // Right-click menu
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open in Explorer"))
                                {
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                }
                                ImGui::EndPopup();
                            }
                            
                            // Sub-folders
                            if (isOpen) {
                                DrawFolderTree(fullFsPath, fullPath, depth + 1);
                            }
                            
                            ImGui::PopID();
                        } else if (hasFiles) {
                            ImGui::PushID(fullPath.c_str());
                            
                            // No sub-folders but has files: leave space for arrow (20px = 12px arrow + 8px spacing) + depth indent
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            // Folder name - use Selectable
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                std::cout << "[ProjectWindow] Folder clicked: " << folderName << std::endl;
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            // Right-click menu
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open in Explorer"))
                                {
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                }
                                ImGui::EndPopup();
                            }
                            
                            ImGui::PopID();
                        } else {
                            ImGui::PushID(fullPath.c_str());
                            
                            // Empty folder: leave space for arrow (20px = 12px arrow + 8px spacing) + depth indent
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            unsigned int folderIcon = 0;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderEmptyIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            // Folder name - use Selectable
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                std::cout << "[ProjectWindow] Folder clicked: " << folderName << std::endl;
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            // Right-click menu
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open in Explorer"))
                                {
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                }
                                ImGui::EndPopup();
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

    // Splitter
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

    // Right side - file view
    ImGui::BeginChild("View", ImVec2(0, panelHeight), true);

    std::string folderPath = assetsPath;
    size_t pos = m_currentFolder.find('/');
    if (pos != std::string::npos)
    {
        folderPath = assetsPath + "/" + m_currentFolder.substr(pos + 1);
    }

    // Right-click menu
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
            // Show folders first
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

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        // Double-click to enter folder
                        std::string newFolder = m_currentFolder + "/" + folderName;
                        m_currentFolder = newFolder;
                    }
                    else if (ImGui::IsItemClicked(0))
                    {
                        // Single-click to select folder
                        if (m_editor) m_editor->selectedFile.Clear();
                    }

                    ImGui::EndGroup();
                    ImGui::SameLine();
                    currentX += itemWidth;
                }
            }

            // Then show files
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

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        std::string filePath = entry.path().string();
                        std::string ext = entry.path().extension().string();
                        
                        std::cout << "[ProjectWindow] Double click on: " << filename << std::endl;
                        
                        if (ext == ".bin")
                        {
                            OnLoadScene(filePath);
                        }
                        else if (ext == ".cs")
                        {
                            OpenCSharpFile(filePath);
                        }
                    }
                    else if (ImGui::IsItemClicked(0))
                    {
                        std::string filePath = entry.path().string();
                        std::string nameOnly = filename.substr(0, filename.size() - ext.size());
                        
                        OnFileSelected(filePath, nameOnly, ext, m_currentFolder);
                        
                        if (m_editor && (ext == ".obj" || ext == ".fbx")) {
                            m_editor->LoadPreviewModel(filePath);
                        }
                    }

                    // Right-click menu
                    if (ImGui::BeginPopupContextItem())
                    {
                        // All files can be renamed
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

                    // Set drag source (.cs files can be dragged to Inspector)
                    if (ext == ".cs" && ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload("CS_SCRIPT", entry.path().string().c_str(), 
                            entry.path().string().length() + 1);
                        ImGui::EndDragDropSource();
                    }

                    // Double-click detection is already handled in IsItemClicked above

                    currentX += itemWidth;
                    ImGui::EndGroup();
                    ImGui::SameLine();
                }
            }
        }
    }
    catch (const std::exception&) {}

    ImGui::EndChild();

    // Draw popups
    DrawPopups();

    ImGui::End();
}

// Console is a separate, independently-dockable window (drawn by Editor::Draw so
// it shows even when no project is loaded). By default it is docked next to
// Project (see Editor::SetupDocking), so the two appear as tabs but can be
// dragged apart like any other panel.
void ProjectWindow::DrawConsoleWindow()
{
    // On first appearance, dock Console into Project's node (so it shows up as a
    // tab beside Project even with a pre-existing saved layout). Applied once;
    // afterwards the user can drag/dock it freely and the .ini remembers it.
    if (!m_consoleDockInitialized && m_projectDockId != 0)
    {
        ImGui::SetNextWindowDockID(m_projectDockId, ImGuiCond_Once);
        m_consoleDockInitialized = true;
    }

    ImGui::Begin("Console");
    DrawConsole();
    ImGui::End();
}

void ProjectWindow::OnScriptDropped(const std::string& scriptPath)
{
    // TODO: Attach script to currently selected GameObject
    // Need to coordinate with Inspector/Editor
    std::cout << "[ProjectWindow] Script dropped: " << scriptPath << std::endl;
    
    if (m_editor && m_editor->selectedObject)
    {
        // Notify Editor to add script component
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
    // Create folder popup
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
    
    // Create scene popup
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
    
    // Create script popup
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
    
    // Rename popup
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
    // Backward-compatible entry point: route any legacy console message into the
    // shared Logger so it shows up in the Console tab alongside everything else.
    Ditto::Logger::Get().Info(message);
}

void ProjectWindow::DrawConsole()
{
    auto& logger = Ditto::Logger::Get();

    int infoCount = 0, warnCount = 0, errCount = 0;
    logger.GetCounts(infoCount, warnCount, errCount);

    // ---- Toolbar ----
    if (ImGui::Button("Clear")) logger.Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Collapse", &m_consoleCollapse);
    ImGui::SameLine();
    ImGui::Checkbox("Autoscroll", &m_consoleAutoScroll);

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12, 0));
    ImGui::SameLine();

    // Level filter toggles with live counts. `###id` keeps a stable widget id
    // even though the visible count text changes every frame.
    std::string infoLabel  = "Info "  + std::to_string(infoCount)  + "###consoleInfo";
    std::string warnLabel  = "Warn "  + std::to_string(warnCount)  + "###consoleWarn";
    std::string errLabel   = "Error " + std::to_string(errCount)   + "###consoleError";

    ImGui::PushStyleColor(ImGuiCol_Text, m_consoleShowInfo
        ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    if (ImGui::SmallButton(infoLabel.c_str())) m_consoleShowInfo = !m_consoleShowInfo;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, m_consoleShowWarning
        ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(0.5f, 0.45f, 0.25f, 1.0f));
    if (ImGui::SmallButton(warnLabel.c_str())) m_consoleShowWarning = !m_consoleShowWarning;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, m_consoleShowError
        ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.5f, 0.3f, 0.3f, 1.0f));
    if (ImGui::SmallButton(errLabel.c_str())) m_consoleShowError = !m_consoleShowError;
    ImGui::PopStyleColor();

    ImGui::Separator();

    // ---- Log list ----
    ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::vector<Ditto::LogEntry> entries = logger.Snapshot();
    for (const auto& e : entries)
    {
        if (e.level == Ditto::LogLevel::Info && !m_consoleShowInfo) continue;
        if (e.level == Ditto::LogLevel::Warning && !m_consoleShowWarning) continue;
        if (e.level == Ditto::LogLevel::Error && !m_consoleShowError) continue;

        ImVec4 color;
        switch (e.level)
        {
            case Ditto::LogLevel::Warning: color = ImVec4(1.0f, 0.85f, 0.3f, 1.0f); break;
            case Ditto::LogLevel::Error:   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  break;
            default:                       color = ImVec4(0.85f, 0.85f, 0.85f, 1.0f); break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (m_consoleCollapse)
        {
            if (e.count > 1)
            {
                std::string line = e.message + "   (" + std::to_string(e.count) + ")";
                ImGui::TextUnformatted(line.c_str());
            }
            else
            {
                ImGui::TextUnformatted(e.message.c_str());
            }
        }
        else
        {
            // Expanded view: one line per occurrence.
            for (int i = 0; i < e.count; ++i)
                ImGui::TextUnformatted(e.message.c_str());
        }
        ImGui::PopStyleColor();
    }

    // Auto-scroll only when the user is already pinned to the bottom.
    if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
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
    
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        std::cerr << "[ProjectWindow] No project loaded" << std::endl;
        return;
    }
    
    std::filesystem::path absPath = std::filesystem::absolute(proj->path);
    std::string projectPath = absPath.string();
    std::replace(projectPath.begin(), projectPath.end(), '/', '\\');
    
    std::string solutionName = proj->name;
    std::string solutionPath = projectPath + "\\" + solutionName + ".sln";
    
    if (!fs::exists(solutionPath))
    {
        std::cout << "[ProjectWindow] Creating Visual Studio solution: " << solutionPath << std::endl;
        if (!CreateVisualStudioSolution(projectPath, solutionName))
        {
            std::cerr << "[ProjectWindow] Failed to create solution" << std::endl;
            return;
        }
    }
    
    std::string absFilePath = std::filesystem::absolute(filePath).string();
    std::replace(absFilePath.begin(), absFilePath.end(), '/', '\\');
    
    std::cout << "[ProjectWindow] Solution: " << solutionPath << std::endl;
    std::cout << "[ProjectWindow] File: " << absFilePath << std::endl;
    
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE)
    {
        comInitialized = false;
    }
    
    IDispatch* pDte = nullptr;
    bool foundExisting = false;
    
    IRunningObjectTable* pROT = nullptr;
    if (SUCCEEDED(GetRunningObjectTable(0, &pROT)) && pROT)
    {
        IEnumMoniker* pEnum = nullptr;
        if (SUCCEEDED(pROT->EnumRunning(&pEnum)) && pEnum)
        {
            IMoniker* pMoniker = nullptr;
            while (pEnum->Next(1, &pMoniker, nullptr) == S_OK)
            {
                IBindCtx* pCtx = nullptr;
                if (SUCCEEDED(CreateBindCtx(0, &pCtx)) && pCtx)
                {
                    LPOLESTR displayName = nullptr;
                    if (SUCCEEDED(pMoniker->GetDisplayName(pCtx, nullptr, &displayName)) && displayName)
                    {
                        std::wstring name(displayName);
                        CoTaskMemFree(displayName);
                        
                        if (name.find(L"VisualStudio.DTE") != std::wstring::npos)
                        {
                            IUnknown* pUnk = nullptr;
                            if (SUCCEEDED(pROT->GetObject(pMoniker, &pUnk)) && pUnk)
                            {
                                IDispatch* pCandidate = nullptr;
                                if (SUCCEEDED(pUnk->QueryInterface(IID_IDispatch, (void**)&pCandidate)) && pCandidate)
                                {
                                    DISPID dispidSolution = 0;
                                    LPOLESTR propName = const_cast<LPOLESTR>(L"Solution");
                                    if (SUCCEEDED(pCandidate->GetIDsOfNames(IID_NULL, &propName, 1, LOCALE_USER_DEFAULT, &dispidSolution)))
                                    {
                                        DISPPARAMS dp = {nullptr, nullptr, 0, 0};
                                        VARIANT varSolution;
                                        VariantInit(&varSolution);
                                        if (SUCCEEDED(pCandidate->Invoke(dispidSolution, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, &varSolution, nullptr, nullptr)))
                                        {
                                            if (varSolution.vt == VT_DISPATCH && varSolution.pdispVal)
                                            {
                                                IDispatch* pSolution = varSolution.pdispVal;
                                                DISPID dispidFullName = 0;
                                                LPOLESTR propFullName = const_cast<LPOLESTR>(L"FullName");
                                                if (SUCCEEDED(pSolution->GetIDsOfNames(IID_NULL, &propFullName, 1, LOCALE_USER_DEFAULT, &dispidFullName)))
                                                {
                                                    DISPPARAMS dp2 = {nullptr, nullptr, 0, 0};
                                                    VARIANT varPath;
                                                    VariantInit(&varPath);
                                                    if (SUCCEEDED(pSolution->Invoke(dispidFullName, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp2, &varPath, nullptr, nullptr)))
                                                    {
                                                        if (varPath.vt == VT_BSTR && varPath.bstrVal)
                                                        {
                                                            std::wstring openSlnPath(varPath.bstrVal);
                                                            std::wstring wSolutionPath(solutionPath.begin(), solutionPath.end());
                                                            if (_wcsicmp(openSlnPath.c_str(), wSolutionPath.c_str()) == 0)
                                                            {
                                                                pDte = pCandidate;
                                                                pCandidate = nullptr;
                                                                foundExisting = true;
                                                                std::cout << "[ProjectWindow] Found existing VS instance with solution open" << std::endl;
                                                            }
                                                        }
                                                        VariantClear(&varPath);
                                                    }
                                                }
                                            }
                                            VariantClear(&varSolution);
                                        }
                                    }
                                    if (pCandidate) pCandidate->Release();
                                }
                                pUnk->Release();
                            }
                        }
                    }
                    pCtx->Release();
                }
                pMoniker->Release();
                
                if (foundExisting) break;
            }
            pEnum->Release();
        }
        pROT->Release();
    }
    
    if (foundExisting && pDte)
    {
        DISPID dispidItemOps = 0;
        LPOLESTR propName = const_cast<LPOLESTR>(L"ItemOperations");
        if (SUCCEEDED(pDte->GetIDsOfNames(IID_NULL, &propName, 1, LOCALE_USER_DEFAULT, &dispidItemOps)))
        {
            DISPPARAMS dp = {nullptr, nullptr, 0, 0};
            VARIANT varItemOps;
            VariantInit(&varItemOps);
            if (SUCCEEDED(pDte->Invoke(dispidItemOps, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, &varItemOps, nullptr, nullptr)))
            {
                if (varItemOps.vt == VT_DISPATCH && varItemOps.pdispVal)
                {
                    IDispatch* pItemOps = varItemOps.pdispVal;
                    DISPID dispidOpenFile = 0;
                    LPOLESTR methodName = const_cast<LPOLESTR>(L"OpenFile");
                    if (SUCCEEDED(pItemOps->GetIDsOfNames(IID_NULL, &methodName, 1, LOCALE_USER_DEFAULT, &dispidOpenFile)))
                    {
                        std::wstring wFilePath(absFilePath.begin(), absFilePath.end());
                        VARIANT varPath;
                        varPath.vt = VT_BSTR;
                        varPath.bstrVal = SysAllocString(wFilePath.c_str());
                        
                        VARIANT varViewKind;
                        varViewKind.vt = VT_BSTR;
                        varViewKind.bstrVal = SysAllocString(L"{7651A701-06E5-11D1-8EBD-00A0C90F26EA}");
                        
                        VARIANT args[2] = { varViewKind, varPath };
                        DISPPARAMS dpOpen = { args, nullptr, 2, 0 };
                        
                        VARIANT varResult;
                        VariantInit(&varResult);
                        HRESULT openHR = pItemOps->Invoke(dispidOpenFile, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dpOpen, &varResult, nullptr, nullptr);
                        
                        if (SUCCEEDED(openHR))
                        {
                            std::cout << "[ProjectWindow] File opened in existing VS instance" << std::endl;
                        }
                        else
                        {
                            std::cerr << "[ProjectWindow] Failed to open file via DTE, hr=0x" << std::hex << openHR << std::dec << std::endl;
                        }
                        
                        VariantClear(&varResult);
                        SysFreeString(varPath.bstrVal);
                        SysFreeString(varViewKind.bstrVal);
                    }
                    pItemOps->Release();
                }
                VariantClear(&varItemOps);
            }
        }
        
        DISPID dispidMainWnd = 0;
        LPOLESTR propMainWnd = const_cast<LPOLESTR>(L"MainWindow");
        if (SUCCEEDED(pDte->GetIDsOfNames(IID_NULL, &propMainWnd, 1, LOCALE_USER_DEFAULT, &dispidMainWnd)))
        {
            DISPPARAMS dp = {nullptr, nullptr, 0, 0};
            VARIANT varWnd;
            VariantInit(&varWnd);
            if (SUCCEEDED(pDte->Invoke(dispidMainWnd, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, &varWnd, nullptr, nullptr)))
            {
                if (varWnd.vt == VT_DISPATCH && varWnd.pdispVal)
                {
                    IDispatch* pWnd = varWnd.pdispVal;
                    DISPID dispidActivate = 0;
                    LPOLESTR methodActivate = const_cast<LPOLESTR>(L"Activate");
                    if (SUCCEEDED(pWnd->GetIDsOfNames(IID_NULL, &methodActivate, 1, LOCALE_USER_DEFAULT, &dispidActivate)))
                    {
                        DISPPARAMS dpAct = {nullptr, nullptr, 0, 0};
                        VARIANT varRes;
                        VariantInit(&varRes);
                        pWnd->Invoke(dispidActivate, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dpAct, &varRes, nullptr, nullptr);
                        VariantClear(&varRes);
                    }
                    pWnd->Release();
                }
                VariantClear(&varWnd);
            }
        }
        
        pDte->Release();
    }
    else
    {
        std::string vsPath = GetVisualStudioPath();
        if (vsPath.empty())
        {
            std::cerr << "[ProjectWindow] Visual Studio not found" << std::endl;
            if (comInitialized) CoUninitialize();
            return;
        }
        
        std::string cmdLine = "\"" + vsPath + "\" \"" + solutionPath + "\"";
        
        std::cout << "[ProjectWindow] Starting new VS instance: " << cmdLine << std::endl;
        
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        
        std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
        cmdBuffer.push_back('\0');
        
        BOOL success = CreateProcessA(
            NULL,
            cmdBuffer.data(),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        );
        
        if (!success)
        {
            DWORD error = GetLastError();
            std::cerr << "[ProjectWindow] Failed to start Visual Studio, error code: " << error << std::endl;
            if (comInitialized) CoUninitialize();
            return;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        std::cout << "[ProjectWindow] VS started, waiting for DTE to be available..." << std::endl;
        
        IDispatch* pNewDte = nullptr;
        bool dteFound = false;
        
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            IRunningObjectTable* pROTRetry = nullptr;
            if (FAILED(GetRunningObjectTable(0, &pROTRetry)) || !pROTRetry) continue;
            
            IEnumMoniker* pEnumRetry = nullptr;
            if (FAILED(pROTRetry->EnumRunning(&pEnumRetry)) || !pEnumRetry)
            {
                pROTRetry->Release();
                continue;
            }
            
            IMoniker* pMoniker = nullptr;
            while (pEnumRetry->Next(1, &pMoniker, nullptr) == S_OK)
            {
                IBindCtx* pCtx = nullptr;
                if (SUCCEEDED(CreateBindCtx(0, &pCtx)) && pCtx)
                {
                    LPOLESTR displayName = nullptr;
                    if (SUCCEEDED(pMoniker->GetDisplayName(pCtx, nullptr, &displayName)) && displayName)
                    {
                        std::wstring name(displayName);
                        CoTaskMemFree(displayName);
                        
                        if (name.find(L"VisualStudio.DTE") != std::wstring::npos)
                        {
                            IUnknown* pUnk = nullptr;
                            if (SUCCEEDED(pROTRetry->GetObject(pMoniker, &pUnk)) && pUnk)
                            {
                                IDispatch* pCandidate = nullptr;
                                if (SUCCEEDED(pUnk->QueryInterface(IID_IDispatch, (void**)&pCandidate)) && pCandidate)
                                {
                                    DISPID dispidSolution = 0;
                                    LPOLESTR propName = const_cast<LPOLESTR>(L"Solution");
                                    if (SUCCEEDED(pCandidate->GetIDsOfNames(IID_NULL, &propName, 1, LOCALE_USER_DEFAULT, &dispidSolution)))
                                    {
                                        DISPPARAMS dp = {nullptr, nullptr, 0, 0};
                                        VARIANT varSolution;
                                        VariantInit(&varSolution);
                                        if (SUCCEEDED(pCandidate->Invoke(dispidSolution, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, &varSolution, nullptr, nullptr)))
                                        {
                                            if (varSolution.vt == VT_DISPATCH && varSolution.pdispVal)
                                            {
                                                IDispatch* pSolution = varSolution.pdispVal;
                                                DISPID dispidFullName = 0;
                                                LPOLESTR propFullName = const_cast<LPOLESTR>(L"FullName");
                                                if (SUCCEEDED(pSolution->GetIDsOfNames(IID_NULL, &propFullName, 1, LOCALE_USER_DEFAULT, &dispidFullName)))
                                                {
                                                    DISPPARAMS dp2 = {nullptr, nullptr, 0, 0};
                                                    VARIANT varPath;
                                                    VariantInit(&varPath);
                                                    if (SUCCEEDED(pSolution->Invoke(dispidFullName, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp2, &varPath, nullptr, nullptr)))
                                                    {
                                                        if (varPath.vt == VT_BSTR && varPath.bstrVal)
                                                        {
                                                            std::wstring openSlnPath(varPath.bstrVal);
                                                            std::wstring wSolutionPath(solutionPath.begin(), solutionPath.end());
                                                            if (_wcsicmp(openSlnPath.c_str(), wSolutionPath.c_str()) == 0)
                                                            {
                                                                pNewDte = pCandidate;
                                                                pCandidate = nullptr;
                                                                dteFound = true;
                                                            }
                                                        }
                                                        VariantClear(&varPath);
                                                    }
                                                }
                                            }
                                            VariantClear(&varSolution);
                                        }
                                    }
                                    if (pCandidate) pCandidate->Release();
                                }
                                pUnk->Release();
                            }
                        }
                    }
                    pCtx->Release();
                }
                pMoniker->Release();
                
                if (dteFound) break;
            }
            pEnumRetry->Release();
            pROTRetry->Release();
            
            if (dteFound) break;
        }
        
        if (dteFound && pNewDte)
        {
            DISPID dispidItemOps = 0;
            LPOLESTR propName = const_cast<LPOLESTR>(L"ItemOperations");
            if (SUCCEEDED(pNewDte->GetIDsOfNames(IID_NULL, &propName, 1, LOCALE_USER_DEFAULT, &dispidItemOps)))
            {
                DISPPARAMS dp = {nullptr, nullptr, 0, 0};
                VARIANT varItemOps;
                VariantInit(&varItemOps);
                if (SUCCEEDED(pNewDte->Invoke(dispidItemOps, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, &varItemOps, nullptr, nullptr)))
                {
                    if (varItemOps.vt == VT_DISPATCH && varItemOps.pdispVal)
                    {
                        IDispatch* pItemOps = varItemOps.pdispVal;
                        DISPID dispidOpenFile = 0;
                        LPOLESTR methodName = const_cast<LPOLESTR>(L"OpenFile");
                        if (SUCCEEDED(pItemOps->GetIDsOfNames(IID_NULL, &methodName, 1, LOCALE_USER_DEFAULT, &dispidOpenFile)))
                        {
                            std::wstring wFilePath(absFilePath.begin(), absFilePath.end());
                            VARIANT varPath;
                            varPath.vt = VT_BSTR;
                            varPath.bstrVal = SysAllocString(wFilePath.c_str());
                            
                            VARIANT varViewKind;
                            varViewKind.vt = VT_BSTR;
                            varViewKind.bstrVal = SysAllocString(L"{7651A701-06E5-11D1-8EBD-00A0C90F26EA}");
                            
                            VARIANT args[2] = { varViewKind, varPath };
                            DISPPARAMS dpOpen = { args, nullptr, 2, 0 };
                            
                            VARIANT varResult;
                            VariantInit(&varResult);
                            HRESULT openHR = pItemOps->Invoke(dispidOpenFile, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dpOpen, &varResult, nullptr, nullptr);
                            
                            if (SUCCEEDED(openHR))
                            {
                                std::cout << "[ProjectWindow] File opened in new VS instance" << std::endl;
                            }
                            else
                            {
                                std::cerr << "[ProjectWindow] Failed to open file via DTE, hr=0x" << std::hex << openHR << std::dec << std::endl;
                            }
                            
                            VariantClear(&varResult);
                            SysFreeString(varPath.bstrVal);
                            SysFreeString(varViewKind.bstrVal);
                        }
                        pItemOps->Release();
                    }
                    VariantClear(&varItemOps);
                }
            }
            pNewDte->Release();
        }
        else
        {
            std::cerr << "[ProjectWindow] Could not find VS DTE after starting, file not opened automatically" << std::endl;
        }
    }
    
    if (comInitialized) CoUninitialize();
}

bool ProjectWindow::CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName)
{
    // Find DittoEngine.dll path - search upward from project directory to engine root
    std::string dittoEnginePath;
    fs::path currentPath = fs::absolute(projectPath);
    
    // Traverse directory tree upward to find Ditto/ditto directory
    while (!currentPath.empty() && currentPath.has_parent_path())
    {
        std::vector<std::string> searchPaths = {
            (currentPath / "Ditto" / "3rdParty" / "Mono" / "DittoEngine.dll").string(),
            (currentPath / "ditto" / "3rdParty" / "Mono" / "DittoEngine.dll").string(),
        };
        
        for (const auto& p : searchPaths)
        {
            if (fs::exists(p))
            {
                dittoEnginePath = fs::absolute(p).string();
                std::cout << "[ProjectWindow] Found DittoEngine.dll at: " << dittoEnginePath << std::endl;
                break;
            }
        }
        
        if (!dittoEnginePath.empty()) break;
        currentPath = currentPath.parent_path();
    }
    
    if (dittoEnginePath.empty())
    {
        // Fallback to relative path search (from project directory)
        std::vector<std::string> searchPaths = {
            projectPath + "\\..\\..\\Ditto\\3rdParty\\Mono\\DittoEngine.dll",
            projectPath + "\\..\\..\\ditto\\3rdParty\\Mono\\DittoEngine.dll",
            projectPath + "\\..\\Ditto\\3rdParty\\Mono\\DittoEngine.dll",
            projectPath + "\\..\\ditto\\3rdParty\\Mono\\DittoEngine.dll",
        };
        for (const auto& p : searchPaths)
        {
            if (fs::exists(p))
            {
                dittoEnginePath = fs::absolute(p).string();
                break;
            }
        }
    }
    
    // Create .csproj file
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
    csprojFile << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
    csprojFile << "  </PropertyGroup>\n";
    csprojFile << "  <ItemGroup>\n";
    csprojFile << "    <Compile Include=\"Assets\\Scripts\\**\\*.cs\" />\n";
    csprojFile << "  </ItemGroup>\n";
    
    if (!dittoEnginePath.empty())
    {
        fs::path csprojPath = fs::path(dittoEnginePath).parent_path() / "DittoEngine.csproj";
        if (fs::exists(csprojPath))
        {
            std::string projRefPath = fs::relative(csprojPath.string(), projectPath).string();
            std::replace(projRefPath.begin(), projRefPath.end(), '/', '\\');
            csprojFile << "  <ItemGroup>\n";
            csprojFile << "    <ProjectReference Include=\"" << projRefPath << "\" />\n";
            csprojFile << "  </ItemGroup>\n";
            std::cout << "[ProjectWindow] DittoEngine project reference: " << projRefPath << std::endl;
        }
        else
        {
            std::string hintPath = fs::relative(dittoEnginePath, projectPath).string();
            std::replace(hintPath.begin(), hintPath.end(), '/', '\\');
            csprojFile << "  <ItemGroup>\n";
            csprojFile << "    <Reference Include=\"DittoEngine\">\n";
            csprojFile << "      <HintPath>" << hintPath << "</HintPath>\n";
            csprojFile << "    </Reference>\n";
            csprojFile << "  </ItemGroup>\n";
            std::cout << "[ProjectWindow] DittoEngine.dll reference: " << hintPath << std::endl;
        }
    }
    else
    {
        std::cerr << "[ProjectWindow] Warning: DittoEngine.dll not found for project reference" << std::endl;
    }
    
    csprojFile << "</Project>\n";
    csprojFile.close();
    
    // Create .sln file
    std::string slnPath = projectPath + "/" + projectName + ".sln";
    std::ofstream slnFile(slnPath);
    if (!slnFile.is_open())
    {
        std::cerr << "[ProjectWindow] Failed to create .sln file" << std::endl;
        return false;
    }
    
    // Generate GUID
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

std::string FindDevEnvViaVsWhere()
{
    std::string result;
    const char* regKey = "SOFTWARE\\Microsoft\\VisualStudio\\Setup\\Instances";
    HKEY hKey = nullptr;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char name[MAX_PATH];
        DWORD index = 0;
        DWORD nameSize = MAX_PATH;

        while (RegEnumKeyExA(hKey, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            std::string instanceKey = std::string(regKey) + "\\" + name;
            HKEY hInstKey = nullptr;

            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instanceKey.c_str(), 0, KEY_READ, &hInstKey) == ERROR_SUCCESS)
            {
                char installPath[MAX_PATH];
                DWORD size = MAX_PATH;
                DWORD type = REG_SZ;

                if (RegQueryValueExA(hInstKey, "InstallLocation", nullptr, &type, (LPBYTE)installPath, &size) == ERROR_SUCCESS)
                {
                    std::string devenvPath = std::string(installPath) + "Common7\\IDE\\devenv.exe";
                    if (fs::exists(devenvPath))
                    {
                        result = devenvPath;
                        RegCloseKey(hInstKey);
                        break;
                    }
                }
                RegCloseKey(hInstKey);
            }
            nameSize = MAX_PATH;
        }
        RegCloseKey(hKey);
    }
    return result;
}

std::string FindDevEnvInPATH()
{
    char* pathEnv = nullptr;
    size_t len = 0;
    if (_dupenv_s(&pathEnv, &len, "PATH") != 0 || !pathEnv)
        return "";

    std::string pathStr(pathEnv);
    free(pathEnv);

    size_t pos = 0;
    while ((pos = pathStr.find(';')) != std::string::npos)
    {
        std::string dir = pathStr.substr(0, pos);
        std::string devenvPath = dir + "\\devenv.exe";
        if (fs::exists(devenvPath))
            return devenvPath;
        pathStr.erase(0, pos + 1);
    }
    return "";
}

static bool CheckDevEnvInDirectory(const std::string& baseDir)
{
    std::vector<std::string> subDirs = {
        "Common7\\IDE",
        "Enterprise\\Common7\\IDE",
        "Professional\\Common7\\IDE",
        "Community\\Common7\\IDE",
    };
    for (const auto& sub : subDirs)
    {
        std::string path = baseDir + "\\" + sub + "\\devenv.exe";
        if (fs::exists(path))
            return true;
    }
    return false;
}

std::string SearchDevEnvInCommonLocations()
{
    const char* drives[] = { "C:", "D:", "E:", "F:" };
    const char* progFiles[] = { "Program Files", "Program Files (x86)" };

    for (const auto& drive : drives)
    {
        for (const auto& pf : progFiles)
        {
            std::string vsBase = std::string(drive) + "\\" + pf + "\\Microsoft Visual Studio";
            if (!fs::exists(vsBase))
                continue;

            try
            {
                for (const auto& entry : fs::directory_iterator(vsBase))
                {
                    if (!entry.is_directory())
                        continue;
                    std::string yearDir = entry.path().string();
                    if (CheckDevEnvInDirectory(yearDir))
                    {
                        for (const auto& subEntry : fs::directory_iterator(entry.path()))
                        {
                            if (!subEntry.is_directory())
                                continue;
                            std::string candidate = subEntry.path().string() + "\\Common7\\IDE\\devenv.exe";
                            if (fs::exists(candidate))
                                return candidate;
                        }
                    }
                }
            }
            catch (const fs::filesystem_error&)
            {
            }
        }

        std::string customPath = std::string(drive) + "\\Visual Studio 2022\\Common7\\IDE\\devenv.exe";
        if (fs::exists(customPath))
            return customPath;

        std::string vs2022Base = std::string(drive) + "\\Visual Studio 2022";
        if (fs::exists(vs2022Base) && CheckDevEnvInDirectory(vs2022Base))
        {
            for (const auto& entry : fs::directory_iterator(vs2022Base))
            {
                if (!entry.is_directory())
                    continue;
                std::string candidate = entry.path().string() + "\\Common7\\IDE\\devenv.exe";
                if (fs::exists(candidate))
                    return candidate;
            }
        }
    }
    return "";
}

std::string ProjectWindow::GetVisualStudioPath()
{
    HKEY hKey = nullptr;
    const char* subKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\devenv.exe";

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char path[MAX_PATH];
        DWORD pathSize = MAX_PATH;
        if (RegQueryValueExA(hKey, NULL, NULL, NULL, (LPBYTE)path, &pathSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            if (fs::exists(path))
                return std::string(path);
        }
        RegCloseKey(hKey);
    }

    std::string vswhereResult = FindDevEnvViaVsWhere();
    if (!vswhereResult.empty())
        return vswhereResult;

    std::string pathResult = FindDevEnvInPATH();
    if (!pathResult.empty())
        return pathResult;

    std::string commonResult = SearchDevEnvInCommonLocations();
    if (!commonResult.empty())
        return commonResult;

    return "";
}
