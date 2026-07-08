#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ProjectWindow.h"
#include "AssetPreviewUtils.h"
#include "Editor.h"
#include "InspectorWindow.h"
#include "../Engine/Core/Engine.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/PrefabAsset.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/Logger.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Physics/PhysicsMaterial2DAsset.h"
#include "../Engine/Resources/AssetDatabase.h"
#include "../3rdParty/stb_image.h"
#include <filesystem>
#include <shlobj.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <windows.h>
#include <objbase.h>
#include <comdef.h>

namespace fs = std::filesystem;
using Ditto::EditorUtils::GenerateMaterialPreviewPixels;
using Ditto::EditorUtils::IsImageExtension;
using Ditto::EditorUtils::IsMaterialExtension;
using Ditto::EditorUtils::LoadImageRGBA;
using Ditto::EditorUtils::MakeUniquePath;

namespace
{
    std::string LowerExtension(const fs::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    bool IsProjectVisibleFile(const fs::path& path)
    {
        std::string ext = LowerExtension(path);
        return ext != ".meta" && ext != ".dll";
    }

    void GoToVisualStudioLine(IDispatch* dte, int line, int column)
    {
        if (!dte || line <= 0) return;

        DISPID activeDocumentId = 0;
        LPOLESTR activeDocumentName = const_cast<LPOLESTR>(L"ActiveDocument");
        if (FAILED(dte->GetIDsOfNames(IID_NULL, &activeDocumentName, 1, LOCALE_USER_DEFAULT, &activeDocumentId)))
            return;

        DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
        VARIANT activeDocument;
        VariantInit(&activeDocument);
        if (FAILED(dte->Invoke(activeDocumentId, IID_NULL, LOCALE_USER_DEFAULT,
            DISPATCH_PROPERTYGET, &noArgs, &activeDocument, nullptr, nullptr))
            || activeDocument.vt != VT_DISPATCH || !activeDocument.pdispVal)
        {
            VariantClear(&activeDocument);
            return;
        }

        IDispatch* document = activeDocument.pdispVal;
        DISPID selectionId = 0;
        LPOLESTR selectionName = const_cast<LPOLESTR>(L"Selection");
        if (FAILED(document->GetIDsOfNames(IID_NULL, &selectionName, 1, LOCALE_USER_DEFAULT, &selectionId)))
        {
            VariantClear(&activeDocument);
            return;
        }

        VARIANT selectionValue;
        VariantInit(&selectionValue);
        if (FAILED(document->Invoke(selectionId, IID_NULL, LOCALE_USER_DEFAULT,
            DISPATCH_PROPERTYGET, &noArgs, &selectionValue, nullptr, nullptr))
            || selectionValue.vt != VT_DISPATCH || !selectionValue.pdispVal)
        {
            VariantClear(&selectionValue);
            VariantClear(&activeDocument);
            return;
        }

        IDispatch* selection = selectionValue.pdispVal;
        DISPID gotoLineId = 0;
        LPOLESTR gotoLineName = const_cast<LPOLESTR>(L"GotoLine");
        if (SUCCEEDED(selection->GetIDsOfNames(IID_NULL, &gotoLineName, 1, LOCALE_USER_DEFAULT, &gotoLineId)))
        {
            VARIANT args[2];
            VariantInit(&args[0]);
            VariantInit(&args[1]);
            args[0].vt = VT_BOOL;
            args[0].boolVal = VARIANT_TRUE;
            args[1].vt = VT_I4;
            args[1].lVal = line;
            DISPPARAMS params = { args, nullptr, 2, 0 };
            VARIANT result;
            VariantInit(&result);
            selection->Invoke(gotoLineId, IID_NULL, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD, &params, &result, nullptr, nullptr);
            VariantClear(&result);
        }

        (void)column;
        VariantClear(&selectionValue);
        VariantClear(&activeDocument);
    }
}

ProjectWindow::ProjectWindow(Editor* editor)
    : m_editor(editor)
{
}

ProjectWindow::~ProjectWindow()
{
    if (!m_editor || !m_editor->engine || !m_editor->engine->renderer) return;
    for (auto& pair : m_thumbnailCache)
        m_editor->engine->renderer->DestroyTexture(pair.second);
}

Ditto::TextureHandle ProjectWindow::GetOrCreateThumbnail(const std::string& filePath, const std::string& ext)
{
    if ((!IsImageExtension(ext) && !IsMaterialExtension(ext)) || !m_editor || !m_editor->engine || !m_editor->engine->renderer)
        return {};

    auto it = m_thumbnailCache.find(filePath);
    if (it != m_thumbnailCache.end())
        return it->second;

    if (IsMaterialExtension(ext))
    {
        Ditto::MaterialAsset material = Ditto::LoadMaterialAsset(filePath);
        if (!material.ok) material = Ditto::MakeDefaultMaterial(fs::path(filePath).stem().string());
        std::vector<unsigned char> pixels = GenerateMaterialPreviewPixels(material, 64);
        Ditto::TextureHandle texture = m_editor->engine->renderer->CreateTexture(pixels.data(), 64, 64, 4);
        m_thumbnailCache[filePath] = texture;
        return texture;
    }

    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* pixels = LoadImageRGBA(fs::path(filePath), &width, &height, &channels);
    if (!pixels || width <= 0 || height <= 0)
    {
        if (pixels) stbi_image_free(pixels);
        m_thumbnailCache[filePath] = {};
        return {};
    }

    Ditto::TextureHandle texture = m_editor->engine->renderer->CreateTexture(pixels, width, height, 4);
    stbi_image_free(pixels);
    m_thumbnailCache[filePath] = texture;
    return texture;
}

void ProjectWindow::ImportExternalFiles(const std::vector<std::string>& paths)
{
    Project* project = ProjectManager::GetInstance().GetCurrentProject();
    if (!project || paths.empty()) return;

    std::string relativeFolder = m_currentFolder;
    if (relativeFolder.rfind("Assets", 0) == 0)
        relativeFolder = relativeFolder.size() > 6 ? relativeFolder.substr(7) : "";

    fs::path targetFolder = fs::path(project->path) / "Assets" / fs::path(relativeFolder);
    std::error_code ec;
    fs::create_directories(targetFolder, ec);

    for (const std::string& pathString : paths)
    {
        fs::path source(pathString);
        if (!fs::exists(source, ec)) continue;

        fs::path target = MakeUniquePath(targetFolder / source.filename());
        try
        {
            if (fs::is_directory(source))
                fs::copy(source, target, fs::copy_options::recursive);
            else
                fs::copy_file(source, target);
            if (fs::is_directory(target, ec))
                Ditto::AssetDatabase::Get().ScanProjectAssets(project->path, true);
            else if (!Ditto::AssetDatabase::IsMetaFile(target))
                Ditto::AssetDatabase::Get().EnsureMetaForAsset(target);
            m_thumbnailCache.erase(target.string());
            DITTO_LOG_INFO_STREAM("[ProjectWindow] Imported external file: " << target.string());
        }
        catch (const std::exception& e)
        {
            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Import failed: " << e.what());
        }
    }
}

void ProjectWindow::OnLoadScene(const std::string& scenePath)
{
    
    if (m_editor) {
        m_editor->LoadSceneFromProject(scenePath);
    }
}

void ProjectWindow::OnFileSelected(const std::string& path, const std::string& name,
                                   const std::string& ext, const std::string& folder)
{
    if (m_editor) {
        
        if (m_editor->lockingSelection) return;

        m_editor->selectedFile.path = path;
        m_editor->selectedFile.name = name;
        m_editor->selectedFile.extension = ext;
        m_editor->selectedFile.folder = folder;
    }
}

void ProjectWindow::NavigateToFile(const std::string& filePath)
{
    namespace fs = std::filesystem;

    if (!m_editor) return;

    Project* project = ProjectManager::GetInstance().GetCurrentProject();
    if (!project) return;

    
    fs::path absPath;
    fs::path assetsPath = fs::path(project->path) / "Assets";

    
    fs::path inputPath(filePath);
    if (inputPath.is_absolute())
    {
        absPath = inputPath;
    }
    else
    {
        
        fs::path candidate = assetsPath / filePath;
        if (fs::exists(candidate))
        {
            absPath = candidate;
        }
        else
        {
            
            absPath = fs::absolute(filePath);
        }
    }

    
    fs::path relativePath = absPath.lexically_relative(fs::absolute(assetsPath));
    if (relativePath.empty() || relativePath.string().find("..") != std::string::npos)
    {
        
        DITTO_LOG_WARN_STREAM("[ProjectWindow] Cannot navigate to file outside Assets: " << filePath);
        return;
    }

    
    fs::path parentPath = relativePath.parent_path();
    if (parentPath.empty() || parentPath == ".")
    {
        m_currentFolder = "Assets";
    }
    else
    {
        m_currentFolder = "Assets/" + parentPath.generic_string();
    }

    DITTO_LOG_INFO_STREAM("[ProjectWindow] Navigated to: " << m_currentFolder);
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

    
    m_projectDockId = ImGui::GetWindowDockID();

    
    ImGui::Text("Project");
    ImGui::SameLine();

    
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

    const auto& assetDiagnostics = Ditto::AssetDatabase::Get().Diagnostics();
    std::vector<Ditto::AssetRecord> assetsNeedingImport = Ditto::AssetDatabase::Get().AssetsNeedingImport();
    const int diagnosticCount =
        static_cast<int>(assetDiagnostics.missingMeta.size()
            + assetDiagnostics.invalidMeta.size()
            + assetDiagnostics.duplicateGuid.size()
            + assetDiagnostics.missingGuidReference.size());
    ImGui::TextDisabled("Assets: %d diagnostics, %d need import",
        diagnosticCount, static_cast<int>(assetsNeedingImport.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Scan"))
        Ditto::AssetDatabase::Get().ScanProjectAssets(proj->path, true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Import Needed"))
    {
        int imported = 0;
        for (const Ditto::AssetRecord& record : assetsNeedingImport)
        {
            if (Ditto::AssetDatabase::Get().ImportAsset(record.guid))
                ++imported;
        }
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Imported " << imported << " changed assets");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Repair Meta"))
    {
        Ditto::AssetDatabase::Get().ScanProjectAssets(proj->path, true);
        DITTO_LOG_INFO("[ProjectWindow] Repaired missing metadata where possible");
    }
    if (diagnosticCount > 0)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Details"))
            ImGui::OpenPopup("AssetDiagnosticsPopup");
    }
    if (ImGui::BeginPopup("AssetDiagnosticsPopup"))
    {
        auto drawList = [](const char* label, const std::vector<std::string>& values) {
            if (values.empty()) return;
            ImGui::TextUnformatted(label);
            for (const std::string& value : values)
                ImGui::BulletText("%s", value.c_str());
        };
        drawList("Missing Meta", assetDiagnostics.missingMeta);
        drawList("Invalid Meta", assetDiagnostics.invalidMeta);
        drawList("Duplicate GUID", assetDiagnostics.duplicateGuid);
        drawList("Missing GUID References", assetDiagnostics.missingGuidReference);
        if (!assetsNeedingImport.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Needs Import");
            for (const Ditto::AssetRecord& record : assetsNeedingImport)
            {
                ImGui::BulletText("%s [%s]%s%s",
                    record.relativePath.c_str(),
                    record.importerType.empty() ? "Generic" : record.importerType.c_str(),
                    record.importError.empty() ? "" : " - ",
                    record.importError.c_str());
            }
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    float panelWidth = ImGui::GetContentRegionAvail().x;
    float panelHeight = ImGui::GetContentRegionAvail().y;

    if (m_splitterPos < 100) m_splitterPos = 100;
    if (m_splitterPos > panelWidth - 100) m_splitterPos = panelWidth - 100;

    
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
                            } else if (sub.is_regular_file() && IsProjectVisibleFile(sub.path())) {
                                hasFiles = true;
                            }
                        }

                        
                        float indent = depth * 18.0f;

                        if (hasSubfolders) {
                            ImGui::PushID(fullPath.c_str());
                            
                            bool isOpen = IsFolderExpanded(fullPath);
                            
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                            if (ImGui::ArrowButton(("##arrow_" + fullPath).c_str(), isOpen ? ImGuiDir_Down : ImGuiDir_Right)) {
                                ToggleFolderExpanded(fullPath);
                            }
                            ImGui::PopStyleVar();
                            
                            
                            void* folderIcon = nullptr;
                            if (m_editor) {
                                folderIcon = isOpen ? m_editor->GetFolderOpenedIcon() : m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::SameLine();
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                            }
                            
                            
                            ImGui::SameLine();
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                DITTO_LOG_INFO_STREAM("[ProjectWindow] Folder clicked: " << folderName );
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open in Explorer"))
                                {
                                    std::wstring fullPathW = std::filesystem::absolute(fullFsPath).wstring();
                                    ShellExecuteW(NULL, L"open", L"explorer.exe", (L"/select,\"" + fullPathW + L"\"").c_str(), NULL, SW_SHOW);
                                }
                                ImGui::EndPopup();
                            }
                            
                            
                            if (isOpen) {
                                DrawFolderTree(fullFsPath, fullPath, depth + 1);
                            }
                            
                            ImGui::PopID();
                        } else if (hasFiles) {
                            ImGui::PushID(fullPath.c_str());
                            
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            void* folderIcon = nullptr;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                DITTO_LOG_INFO_STREAM("[ProjectWindow] Folder clicked: " << folderName );
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            
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
                            
                            
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent + 20.0f);
                            
                            void* folderIcon = nullptr;
                            if (m_editor) {
                                folderIcon = m_editor->GetFolderEmptyIcon();
                            }
                            
                            if (folderIcon) {
                                ImGui::Image((void*)(intptr_t)folderIcon, ImVec2(16, 16), ImVec2(0, 1), ImVec2(1, 0));
                                ImGui::SameLine();
                            }
                            
                            
                            bool isSelected = (m_currentFolder == fullPath);
                            if (ImGui::Selectable(folderName.c_str(), isSelected))
                            {
                                DITTO_LOG_INFO_STREAM("[ProjectWindow] Folder clicked: " << folderName );
                                m_currentFolder = fullPath;
                                if (m_editor) m_editor->selectedFile.Clear();
                            }
                            
                            
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

    
    ImGui::BeginChild("View", ImVec2(0, panelHeight), true);

    std::string folderPath = assetsPath;
    size_t pos = m_currentFolder.find('/');
    if (pos != std::string::npos)
    {
        folderPath = assetsPath + "/" + m_currentFolder.substr(pos + 1);
    }

    
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
            if (ImGui::MenuItem("Create Material..."))
            {
                m_showCreateMaterialPopup = true;
                strcpy_s(m_newMaterialNameBuffer, "New Material");
            }
            if (ImGui::MenuItem("Create Physics Material 2D..."))
            {
                m_showCreatePhysicsMaterial2DPopup = true;
                strcpy_s(m_newPhysicsMaterial2DNameBuffer, "New Physics Material 2D");
            }
            if (ImGui::MenuItem("Create Shader..."))
            {
                m_showCreateShaderPopup = true;
                strcpy_s(m_newShaderNameBuffer, "New Shader");
            }
            if (ImGui::MenuItem("Create Empty Prefab..."))
            {
                m_showCreatePrefabPopup = true;
                strcpy_s(m_newPrefabNameBuffer, "New Prefab");
            }
            if (m_editor && m_editor->selectedObject && ImGui::MenuItem("Save Selected As Prefab..."))
            {
                m_showCreatePrefabPopup = true;
                strcpy_s(m_newPrefabNameBuffer, m_editor->selectedObject->name.c_str());
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

                    void* folderIcon = m_editor ? m_editor->GetFolderIcon() : nullptr;
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
                        
                        std::string newFolder = m_currentFolder + "/" + folderName;
                        m_currentFolder = newFolder;
                    }
                    else if (ImGui::IsItemClicked(0))
                    {
                        
                        if (m_editor) m_editor->selectedFile.Clear();
                    }

                    ImGui::EndGroup();
                    ImGui::SameLine();
                    currentX += itemWidth;
                }
            }

            
            for (const auto& entry : fs::directory_iterator(folderPath))
            {
                if (entry.is_regular_file() && IsProjectVisibleFile(entry.path()))
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

                    Ditto::TextureHandle thumbnail = GetOrCreateThumbnail(entry.path().string(), ext);
                    void* thumbnailTexture = (thumbnail && m_editor && m_editor->engine && m_editor->engine->renderer)
                        ? m_editor->engine->renderer->GetImGuiTextureID(thumbnail) : nullptr;
                    void* iconTexture = thumbnailTexture ? thumbnailTexture : (m_editor ? m_editor->GetIconByExtension(ext) : nullptr);
                    if (iconTexture) {
                        ImGui::Image((void*)(intptr_t)iconTexture, ImVec2(40, 40), ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        if (isSelected)
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[]");
                        else if (ext == ".bin")
                            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "[]");
                        else if (ext == ".obj" || ext == ".fbx")
                            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "[]");
                        else if (ext == ".mat" || ext == ".physmat2d")
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
                        
                        DITTO_LOG_INFO_STREAM("[ProjectWindow] Double click on: " << filename );
                        
                        if (ext == ".bin")
                        {
                            OnLoadScene(filePath);
                        }
                        else if (ext == ".cs")
                        {
                            OpenCSharpFile(filePath);
                        }
                        else if (ext == ".prefab" && m_editor)
                        {
                            m_editor->InstantiatePrefabToScene(filePath);
                        }
                        else if (ext == ".shader" || ext == ".hlsl" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
                        {
                            ShellExecuteA(NULL, "open", filePath.c_str(), NULL, NULL, SW_SHOW);
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

                    
                    if (ImGui::BeginPopupContextItem())
                    {
                        
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
                                fs::path metaPath = entry.path();
                                metaPath += ".meta";
                                fs::remove(entry.path());
                                std::error_code ec;
                                fs::remove(metaPath, ec);
                                Ditto::AssetDatabase::Get().ForgetAsset(entry.path());
                                if (m_editor && m_editor->selectedFile.path == entry.path().string())
                                    m_editor->selectedFile.Clear();
                            }
                            catch (const std::exception& e) {
                                DITTO_LOG_ERROR_STREAM("Delete failed: " << e.what() );
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

                    
                    
                    if (ImGui::BeginDragDropSource())
                    {
                        std::string fullPath = entry.path().string();
                        ImGui::SetDragDropPayload("PROJECT_FILE", fullPath.c_str(), fullPath.length() + 1);
                        if (ext == ".cs")
                            ImGui::SetDragDropPayload("CS_SCRIPT", fullPath.c_str(), fullPath.length() + 1);
                        ImGui::TextUnformatted(entry.path().filename().string().c_str());
                        ImGui::EndDragDropSource();
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

    
    DrawPopups();

    ImGui::End();
}





void ProjectWindow::DrawConsoleWindow()
{
    
    
    
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
    
    
    DITTO_LOG_INFO_STREAM("[ProjectWindow] Script dropped: " << scriptPath );
    
    if (m_editor && m_editor->selectedObject)
    {
        
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
        Ditto::AssetDatabase::Get().EnsureMetaForAsset(filePath);
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Created script: " << filePath );
    }
}

void ProjectWindow::CreateNewMaterial(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();

    std::string targetFolder = assetsPath + "/Materials";
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }

    fs::create_directories(targetFolder);
    fs::path filePath = MakeUniquePath(fs::path(targetFolder) / (name + ".mat"));
    Ditto::MaterialAsset material = Ditto::MakeDefaultMaterial(filePath.stem().string());
    if (Ditto::SaveMaterialAsset(material, filePath))
    {
        Ditto::AssetDatabase::Get().EnsureMetaForAsset(filePath);
        m_thumbnailCache.erase(filePath.string());
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Created material: " << filePath.string());
    }
}

void ProjectWindow::CreateNewPhysicsMaterial2D(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();

    std::string targetFolder = assetsPath + "/PhysicsMaterials2D";
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }

    fs::create_directories(targetFolder);
    fs::path filePath = MakeUniquePath(fs::path(targetFolder) / (name + ".physmat2d"));
    Ditto::PhysicsMaterial2DAsset material = Ditto::MakeDefaultPhysicsMaterial2D(filePath.stem().string());
    if (Ditto::SavePhysicsMaterial2DAsset(material, filePath))
    {
        Ditto::AssetDatabase::Get().EnsureMetaForAsset(filePath);
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Created physics material 2D: " << filePath.string());
    }
}

void ProjectWindow::CreateNewShader(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();

    std::string targetFolder = assetsPath + "/Shaders";
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }

    fs::create_directories(targetFolder);
    fs::path filePath = MakeUniquePath(fs::path(targetFolder) / (name + ".shader"));

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create shader: " << filePath.string());
        return;
    }

    file << "Shader \"" << filePath.stem().string() << "\"\n";
    file << "{\n";
    file << "    Properties\n";
    file << "    {\n";
    file << "        _Color (\"Color\", Color) = (1,1,1,1)\n";
    file << "        _MainTex (\"Main Texture\", 2D) = \"white\" {}\n";
    file << "    }\n";
    file << "    SubShader\n";
    file << "    {\n";
    file << "        Tags { \"RenderType\"=\"Opaque\" \"Queue\"=\"Geometry\" }\n";
    file << "        Pass\n";
    file << "        {\n";
    file << "            HLSLPROGRAM\n";
    file << "            #pragma vertex vert\n";
    file << "            #pragma fragment frag\n\n";
    file << "            struct appdata\n";
    file << "            {\n";
    file << "                float4 vertex : POSITION;\n";
    file << "                float3 normal : NORMAL;\n";
    file << "                float4 texcoord : TEXCOORD0;\n";
    file << "            };\n\n";
    file << "            struct v2f\n";
    file << "            {\n";
    file << "                float4 position : SV_Position;\n";
    file << "                float2 uv : TEXCOORD0;\n";
    file << "                float4 color : COLOR;\n";
    file << "            };\n\n";
    file << "            v2f vert(appdata v)\n";
    file << "            {\n";
    file << "                v2f o;\n";
    file << "                o.position = ObjectToClipPos(v.vertex);\n";
    file << "                o.uv = v.texcoord.xy;\n";
    file << "                o.color = _Color;\n";
    file << "                return o;\n";
    file << "            }\n\n";
    file << "            float4 frag(v2f i) : SV_Target\n";
    file << "            {\n";
    file << "                return tex2D(_MainTex, i.uv) * i.color;\n";
    file << "            }\n";
    file << "            ENDHLSL\n";
    file << "        }\n";
    file << "    }\n";
    file << "}\n";
    file.close();
    Ditto::AssetDatabase::Get().EnsureMetaForAsset(filePath);
    DITTO_LOG_INFO_STREAM("[ProjectWindow] Created shader: " << filePath.string());
}

void ProjectWindow::CreateNewPrefab(const std::string& name)
{
    SaveSelectedObjectAsPrefab(name);
}

void ProjectWindow::SaveSelectedObjectAsPrefab(const std::string& name)
{
    auto& pm = ProjectManager::GetInstance();
    std::string assetsPath = pm.GetProjectAssetsPath();

    std::string targetFolder = assetsPath + "/Prefabs";
    if (!m_currentFolder.empty() && m_currentFolder != "Assets") {
        targetFolder = assetsPath + "/" + m_currentFolder.substr(7);
    }

    fs::create_directories(targetFolder);
    fs::path filePath = MakeUniquePath(fs::path(targetFolder) / (name + ".prefab"));

    bool saved = false;
    if (m_editor && m_editor->selectedObject)
        saved = m_editor->SaveSelectedObjectAsPrefab(filePath.string());
    else
    {
        GameObject emptyPrefab(name);
        saved = Ditto::PrefabAsset::Save(emptyPrefab, filePath);
        if (saved)
            Ditto::AssetDatabase::Get().EnsureMetaForAsset(filePath);
    }

    if (saved)
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Created prefab: " << filePath.string());
    else
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create prefab: " << filePath.string());
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
            DITTO_LOG_INFO_STREAM("[ProjectWindow] Created folder: " << newFolderPath );
        }
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create folder: " << e.what() );
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
            Ditto::AssetDatabase::Get().EnsureMetaForAsset(scenePath);
            DITTO_LOG_INFO_STREAM("[ProjectWindow] Created scene: " << scenePath );
        }
    }
    catch (const std::exception& e)
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create scene: " << e.what() );
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
        fs::path oldMeta = fs::path(oldPath);
        oldMeta += ".meta";
        fs::path newMeta = fs::path(newPath);
        newMeta += ".meta";
        std::error_code ec;
        Ditto::AssetDatabase::Get().ForgetAsset(oldPath);
        if (fs::exists(oldMeta, ec))
            fs::rename(oldMeta, newMeta, ec);
        Ditto::AssetDatabase::Get().EnsureMetaForAsset(newPath);
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Renamed: " << oldPath << " -> " << newPath );
    }
    catch (const std::exception& e) {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Rename failed: " << e.what() );
    }
}

void ProjectWindow::DrawPopups()
{
    
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
    
    
    if (m_showCreateScriptPopup)
    {
        ImGui::OpenPopup("Create Script");
        m_showCreateScriptPopup = false;
    }

    if (m_showCreateMaterialPopup)
    {
        ImGui::OpenPopup("Create Material");
        m_showCreateMaterialPopup = false;
    }

    if (m_showCreatePhysicsMaterial2DPopup)
    {
        ImGui::OpenPopup("Create Physics Material 2D");
        m_showCreatePhysicsMaterial2DPopup = false;
    }

    if (m_showCreateShaderPopup)
    {
        ImGui::OpenPopup("Create Shader");
        m_showCreateShaderPopup = false;
    }

    if (m_showCreatePrefabPopup)
    {
        ImGui::OpenPopup("Create Prefab");
        m_showCreatePrefabPopup = false;
    }

    if (ImGui::BeginPopupModal("Create Material", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Material Name:"); ImGui::SameLine();
        ImGui::InputText("##MaterialName", m_newMaterialNameBuffer, sizeof(m_newMaterialNameBuffer));

        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newMaterialNameBuffer) > 0)
            {
                CreateNewMaterial(m_newMaterialNameBuffer);
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

    if (ImGui::BeginPopupModal("Create Physics Material 2D", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Physics Material 2D Name:"); ImGui::SameLine();
        ImGui::InputText("##PhysicsMaterial2DName", m_newPhysicsMaterial2DNameBuffer, sizeof(m_newPhysicsMaterial2DNameBuffer));

        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newPhysicsMaterial2DNameBuffer) > 0)
            {
                CreateNewPhysicsMaterial2D(m_newPhysicsMaterial2DNameBuffer);
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

    if (ImGui::BeginPopupModal("Create Shader", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Shader Name:"); ImGui::SameLine();
        ImGui::InputText("##ShaderName", m_newShaderNameBuffer, sizeof(m_newShaderNameBuffer));

        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newShaderNameBuffer) > 0)
            {
                CreateNewShader(m_newShaderNameBuffer);
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

    if (ImGui::BeginPopupModal("Create Prefab", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Prefab Name:"); ImGui::SameLine();
        ImGui::InputText("##PrefabName", m_newPrefabNameBuffer, sizeof(m_newPrefabNameBuffer));

        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(m_newPrefabNameBuffer) > 0)
            {
                CreateNewPrefab(m_newPrefabNameBuffer);
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
    
    
    Ditto::Logger::Get().Info(message);
}

void ProjectWindow::DrawConsole()
{
    auto& logger = Ditto::Logger::Get();

    int infoCount = 0, warnCount = 0, errCount = 0;
    logger.GetCounts(infoCount, warnCount, errCount);

    
    if (ImGui::Button("Clear")) logger.Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Collapse", &m_consoleCollapse);
    ImGui::SameLine();
    ImGui::Checkbox("Autoscroll", &m_consoleAutoScroll);

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12, 0));
    ImGui::SameLine();

    
    
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
            
            for (int i = 0; i < e.count; ++i)
                ImGui::TextUnformatted(e.message.c_str());
        }
        ImGui::PopStyleColor();
    }

    
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

void ProjectWindow::OpenCSharpFile(const std::string& filePath, int line, int column)
{
    DITTO_LOG_INFO_STREAM("[ProjectWindow] Opening C# file: " << filePath );
    
    Project* proj = ProjectManager::GetInstance().GetCurrentProject();
    if (!proj)
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] No project loaded" );
        return;
    }
    
    std::filesystem::path absPath = std::filesystem::absolute(proj->path);
    std::string projectPath = absPath.string();
    std::replace(projectPath.begin(), projectPath.end(), '/', '\\');
    
    std::string solutionName = proj->name;
    std::string solutionPath = projectPath + "\\" + solutionName + ".sln";
    
    if (!fs::exists(solutionPath))
    {
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Creating Visual Studio solution: " << solutionPath );
        if (!CreateVisualStudioSolution(projectPath, solutionName))
        {
            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create solution" );
            return;
        }
    }
    
    std::string absFilePath = std::filesystem::absolute(filePath).string();
    std::replace(absFilePath.begin(), absFilePath.end(), '/', '\\');
    
    DITTO_LOG_INFO_STREAM("[ProjectWindow] Solution: " << solutionPath );
    DITTO_LOG_INFO_STREAM("[ProjectWindow] File: " << absFilePath );
    
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
                                                                DITTO_LOG_INFO_STREAM("[ProjectWindow] Found existing VS instance with solution open" );
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
                            DITTO_LOG_INFO_STREAM("[ProjectWindow] File opened in existing VS instance" );
                            GoToVisualStudioLine(pDte, line, column);
                        }
                        else
                        {
                            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to open file via DTE, hr=0x" << std::hex << openHR << std::dec );
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
            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Visual Studio not found" );
            if (comInitialized) CoUninitialize();
            return;
        }
        
        std::string cmdLine = "\"" + vsPath + "\" \"" + solutionPath + "\"";
        
        DITTO_LOG_INFO_STREAM("[ProjectWindow] Starting new VS instance: " << cmdLine );
        
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
            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to start Visual Studio, error code: " << error );
            if (comInitialized) CoUninitialize();
            return;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        DITTO_LOG_INFO_STREAM("[ProjectWindow] VS started, waiting for DTE to be available..." );
        
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
                                DITTO_LOG_INFO_STREAM("[ProjectWindow] File opened in new VS instance" );
                                GoToVisualStudioLine(pNewDte, line, column);
                            }
                            else
                            {
                                DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to open file via DTE, hr=0x" << std::hex << openHR << std::dec );
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
            DITTO_LOG_ERROR_STREAM("[ProjectWindow] Could not find VS DTE after starting, file not opened automatically" );
        }
    }
    
    if (comInitialized) CoUninitialize();
}

bool ProjectWindow::CreateVisualStudioSolution(const std::string& projectPath, const std::string& projectName)
{
    
    std::string dittoEnginePath;
    fs::path currentPath = fs::absolute(projectPath);
    
    
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
                DITTO_LOG_INFO_STREAM("[ProjectWindow] Found DittoEngine.dll at: " << dittoEnginePath );
                break;
            }
        }
        
        if (!dittoEnginePath.empty()) break;
        currentPath = currentPath.parent_path();
    }
    
    if (dittoEnginePath.empty())
    {
        
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
    
    
    std::string csprojPath = projectPath + "/" + projectName + ".csproj";
    std::ofstream csprojFile(csprojPath);
    if (!csprojFile.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create .csproj file" );
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
            DITTO_LOG_INFO_STREAM("[ProjectWindow] DittoEngine project reference: " << projRefPath );
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
            DITTO_LOG_INFO_STREAM("[ProjectWindow] DittoEngine.dll reference: " << hintPath );
        }
    }
    else
    {
        DITTO_LOG_WARN("[ProjectWindow] DittoEngine.dll not found for project reference");
    }
    
    csprojFile << "</Project>\n";
    csprojFile.close();
    
    
    std::string slnPath = projectPath + "/" + projectName + ".sln";
    std::ofstream slnFile(slnPath);
    if (!slnFile.is_open())
    {
        DITTO_LOG_ERROR_STREAM("[ProjectWindow] Failed to create .sln file" );
        return false;
    }
    
    
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
    
    DITTO_LOG_INFO_STREAM("[ProjectWindow] Created solution: " << slnPath );
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
