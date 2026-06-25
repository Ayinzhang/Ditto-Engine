#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/ProjectManager.h"
#include "../Engine/Core/Logger.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../Engine/Audio/AudioEngine.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
#include "../Engine/Physics/PhysicsMaterial2DAsset.h"
#include "../Engine/Resources/AssetPath.h"
#include "../3rdParty/ImGui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace
{
    Editor* CurrentEditor()
    {
        return Ditto::RuntimeContext::CurrentEditor();
    }

    Scene* CurrentScene()
    {
        return Ditto::RuntimeContext::CurrentScene();
    }
}

void TrackUndoableEdit()
{
    Editor* editor = CurrentEditor();
    if (!editor) return;
    if (ImGui::IsItemActivated())            editor->BeginInspectorEdit();
    if (ImGui::IsItemDeactivatedAfterEdit()) editor->EndInspectorEdit();
}

void DrawComponentSelectionBackground(Component* component)
{
    Editor* editor = CurrentEditor();
    if (!editor || editor->selectedComponent != component) return;
    ImVec2 min = ImGui::GetCursorScreenPos();
    ImVec2 max(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f, min.y + ImGui::GetFrameHeight());
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(45, 105, 175, 95), 3.0f);
}

void SelectComponentOnLastItem(Component* component)
{
    if (Editor* editor = CurrentEditor(); editor && ImGui::IsItemClicked())
        editor->selectedComponent = component;
}

void SelectComponentArea(Component* component, const ImVec2& start)
{
    Editor* editor = CurrentEditor();
    if (!editor) return;

    ImVec2 end(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f, ImGui::GetCursorScreenPos().y);
    if (end.y < start.y + ImGui::GetFrameHeight())
        end.y = start.y + ImGui::GetFrameHeight();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(start, end, true))
        editor->selectedComponent = component;

    if (editor->selectedComponent == component)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(start, end, IM_COL32(45, 105, 175, 24), 3.0f);
        drawList->AddRect(start, end, IM_COL32(75, 145, 220, 150), 3.0f);
    }
}

bool DrawComponentHeader(Component* component, const char* title, bool removable)
{
    if (!component) return false;

    DrawComponentSelectionBackground(component);
    ImGui::Checkbox(("##Enabled" + std::string(title)).c_str(), &component->enabled);
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
    SelectComponentOnLastItem(component);

    if (removable)
    {
        ImGui::SameLine(ImGui::GetWindowWidth() - 30);
        if (ImGui::SmallButton(("X##Remove" + std::string(title)).c_str()))
        {
            if (Editor* editor = CurrentEditor()) editor->PushUndoSnapshot();
            if (component->gameObject) component->gameObject->RemoveComponent(component);
            return false;
        }
    }

    if (!component->enabled)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    return true;
}

void UnityLabel(const char* label)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(160.0f);
}

void UnityCheckbox(const char* label, bool* value, const char* id)
{
    UnityLabel(label);
    ImGui::Checkbox(id, value);
    TrackUndoableEdit();
}

void UnityDragFloat(const char* label, float* value, const char* id,
    float speed, float min, float max)
{
    UnityLabel(label);
    ImGui::DragFloat(id, value, speed, min, max);
    TrackUndoableEdit();
}

void UnityDragFloat2(const char* label, glm::vec2* value, const char* id,
    float speed, float min, float max)
{
    UnityLabel(label);
    ImGui::DragFloat2(id, &value->x, speed, min, max);
    TrackUndoableEdit();
}

void UnityDragFloat3(const char* label, glm::vec3* value, const char* id,
    float speed, float min, float max)
{
    UnityLabel(label);
    ImGui::DragFloat3(id, &value->x, speed, min, max);
    TrackUndoableEdit();
}

void UnityColor4(const char* label, glm::vec4* value, const char* id)
{
    UnityLabel(label);
    ImGui::ColorEdit4(id, &value->x, ImGuiColorEditFlags_AlphaBar);
    TrackUndoableEdit();
}

void UnityColor3(const char* label, glm::vec3* value, const char* id)
{
    UnityLabel(label);
    ImGui::ColorEdit3(id, &value->x);
    TrackUndoableEdit();
}

bool UnityCombo(const char* label, int* value, const char* const* names, int count, const char* id)
{
    UnityLabel(label);
    bool changed = ImGui::Combo(id, value, names, count);
    TrackUndoableEdit();
    return changed;
}

std::string FileNameFromPath(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string ToFullAssetPath(const std::string& relativePath)
{
    if (relativePath.empty()) return "";
    return Ditto::AssetPath::ResolveAssetPath(relativePath).string();
}

std::string FileNameWithoutExtension(const std::string& path)
{
    std::string name = FileNameFromPath(path);
    size_t dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos)
        return name.substr(0, dotPos);
    return name;
}

bool DrawObjectFieldButton(const char* label, void* iconTexture, const std::string& value, const char* popupId,
    std::string* droppedPath, const std::string& fullPath)
{
    UnityLabel(label);

    float availWidth = ImGui::GetContentRegionAvail().x;
    float buttonHeight = ImGui::GetFrameHeight();
    float circleButtonWidth = buttonHeight;
    float mainButtonWidth = availWidth - circleButtonWidth - 2.0f;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    ImVec2 buttonPos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Button(("##main" + std::string(popupId)).c_str(), ImVec2(mainButtonWidth, buttonHeight));
    bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textPos = buttonPos;
    textPos.x += 4.0f;
    textPos.y += (buttonHeight - ImGui::GetTextLineHeight()) * 0.5f;

    if (iconTexture)
    {
        ImVec2 iconPos = buttonPos;
        iconPos.x += 2.0f;
        iconPos.y += (buttonHeight - 16.0f) * 0.5f;
        drawList->AddImage(iconTexture, iconPos, ImVec2(iconPos.x + 16, iconPos.y + 16));
        textPos.x += 18.0f;
    }

    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), value.c_str());

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (doubleClicked && !fullPath.empty() && CurrentEditor())
    {
        DITTO_LOG_INFO_STREAM("[DrawObjectFieldButton] Double clicked: " << fullPath);
        CurrentEditor()->selectedFile.path = fullPath;
        CurrentEditor()->selectedFile.name = value;
        std::string ext;
        size_t dotPos = fullPath.find_last_of('.');
        if (dotPos != std::string::npos)
            ext = fullPath.substr(dotPos);
        CurrentEditor()->selectedFile.extension = ext;
        CurrentEditor()->selectedObject = nullptr;
    }
    else if (clicked && !fullPath.empty() && CurrentEditor())
    {
        DITTO_LOG_INFO_STREAM("[DrawObjectFieldButton] Single clicked: " << fullPath);
        DITTO_LOG_INFO_STREAM("[DrawObjectFieldButton] Before: selectedObject=" << (CurrentEditor()->selectedObject ? "valid" : "null"));

        if (auto* projectWindow = CurrentEditor()->GetProjectWindow())
            projectWindow->NavigateToFile(fullPath);

        CurrentEditor()->selectedObject = nullptr;

        CurrentEditor()->selectedFile.path = fullPath;
        CurrentEditor()->selectedFile.name = value;
        std::string ext;
        size_t dotPos = fullPath.find_last_of('.');
        if (dotPos != std::string::npos)
            ext = fullPath.substr(dotPos);
        CurrentEditor()->selectedFile.extension = ext;

        DITTO_LOG_INFO_STREAM("[DrawObjectFieldButton] After: selectedFile.path=" << CurrentEditor()->selectedFile.path << " selectedObject=" << (CurrentEditor()->selectedObject ? "valid" : "null"));
    }

    if (droppedPath && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_FILE"))
            *droppedPath = static_cast<const char*>(payload->Data);
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine(0, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

    ImVec2 pickerButtonPos = ImGui::GetCursorScreenPos();
    bool pickerClicked = ImGui::Button(("##picker" + std::string(popupId)).c_str(), ImVec2(circleButtonWidth, buttonHeight));

    void* pickerIcon = CurrentEditor() ? CurrentEditor()->GetIconByExtension(".objectpicker") : nullptr;

    if (pickerIcon)
    {
        float iconSize = 16.0f;
        ImVec2 iconPos = ImVec2(
            pickerButtonPos.x + (circleButtonWidth - iconSize) * 0.5f,
            pickerButtonPos.y + (buttonHeight - iconSize) * 0.5f
        );
        drawList->AddImage(pickerIcon, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize));
    }
    else
    {
        ImVec2 center = ImVec2(pickerButtonPos.x + circleButtonWidth * 0.5f, pickerButtonPos.y + buttonHeight * 0.5f);
        float innerRadius = 5.0f;
        float outerRadius = 7.5f;
        drawList->AddCircle(center, innerRadius, ImGui::GetColorU32(ImGuiCol_Text), 16, 1.5f);
        drawList->AddCircle(center, outerRadius, ImGui::GetColorU32(ImGuiCol_Text), 16, 1.5f);
    }

    ImGui::PopStyleColor(3);

    if (pickerClicked)
        ImGui::OpenPopup(popupId);

    return clicked || pickerClicked;
}

bool DrawUnitySelectorHeader(const char* title, const char* searchId, char* searchBuffer, size_t searchBufferSize)
{
    ImGui::TextUnformatted(title);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 18.0f);
    bool close = ImGui::SmallButton("x");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(searchId, searchBuffer, searchBufferSize);
    ImGui::Separator();
    return close;
}

std::string ToAssetRelativePath(const std::string& path)
{
    return Ditto::AssetPath::ToProjectRelativeAssetPath(path);
}

std::string LowerExtension(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool ExtensionMatches(const std::string& path, std::initializer_list<const char*> extensions)
{
    std::string ext = LowerExtension(path);
    for (const char* allowed : extensions)
        if (ext == allowed)
            return true;
    return false;
}

std::vector<std::string> FindProjectAssets(std::initializer_list<const char*> extensions)
{
    std::vector<std::string> results;
    Project* project = ProjectManager::GetInstance().GetCurrentProject();
    if (!project) return results;

    std::filesystem::path assetsRoot = std::filesystem::path(project->path) / "Assets";
    std::error_code ec;
    if (!std::filesystem::exists(assetsRoot, ec)) return results;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot, ec))
    {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string fullPath = entry.path().string();
        if (ExtensionMatches(fullPath, extensions))
            results.push_back(ToAssetRelativePath(fullPath));
    }
    std::sort(results.begin(), results.end(), [](const std::string& a, const std::string& b)
    {
        return FileNameFromPath(a) < FileNameFromPath(b);
    });
    return results;
}

bool DrawAssetMenuItems(const std::vector<std::string>& assets, const std::string& currentPath, std::string& selectedPath)
{
    bool changed = false;
    if (assets.empty())
    {
        ImGui::TextDisabled("No matching assets");
        return false;
    }

    for (const std::string& asset : assets)
    {
        const std::string label = FileNameFromPath(asset) + "##" + asset;
        if (ImGui::MenuItem(label.c_str(), nullptr, asset == currentPath))
        {
            selectedPath = asset;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", asset.c_str());
    }
    return changed;
}

bool SearchMatches(const std::string& text, const std::string& search)
{
    if (search.empty()) return true;
    std::string lowerText = text;
    std::string lowerSearch = search;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowerText.find(lowerSearch) != std::string::npos;
}

bool DrawFilteredAssetMenuItems(const std::vector<std::string>& assets,
    const std::string& currentPath, const std::string& search, std::string& selectedPath)
{
    bool any = false;
    bool changed = false;
    for (const std::string& asset : assets)
    {
        std::string fileName = FileNameFromPath(asset);
        if (!SearchMatches(fileName, search) && !SearchMatches(asset, search))
            continue;
        any = true;
        const std::string label = fileName + "##" + asset;
        if (ImGui::MenuItem(label.c_str(), nullptr, asset == currentPath))
        {
            selectedPath = asset;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", asset.c_str());
    }
    if (!any)
        ImGui::TextDisabled("No matching assets");
    return changed;
}

bool DrawAssetObjectField(const char* label, std::string& assetPath, const char* popupId,
    const char* selectorTitle, std::initializer_list<const char*> extensions, const char* emptyDisplay)
{
    std::string display = assetPath.empty() ? emptyDisplay : FileNameWithoutExtension(assetPath);
    std::string droppedPath;
    DrawObjectFieldButton(label, nullptr, display, popupId, &droppedPath, ToFullAssetPath(assetPath));

    bool changed = false;
    if (!droppedPath.empty() && ExtensionMatches(droppedPath, extensions))
    {
        if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot();
        assetPath = ToAssetRelativePath(droppedPath);
        changed = true;
    }

    if (ImGui::BeginPopup(popupId))
    {
        static std::unordered_map<std::string, std::array<char, 128>> searchBuffers;
        auto& searchBuffer = searchBuffers[popupId ? popupId : ""];
        if (DrawUnitySelectorHeader(selectorTitle, ("##Search" + std::string(popupId)).c_str(),
            searchBuffer.data(), searchBuffer.size()))
        {
            ImGui::CloseCurrentPopup();
        }

        std::string search(searchBuffer.data());
        if (SearchMatches("None", search) && ImGui::MenuItem("None", nullptr, assetPath.empty()))
        {
            if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot();
            assetPath.clear();
            changed = true;
        }
        ImGui::Separator();

        std::string selectedPath;
        if (DrawFilteredAssetMenuItems(FindProjectAssets(extensions), assetPath, search, selectedPath))
        {
            if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot();
            assetPath = selectedPath;
            changed = true;
        }
        ImGui::EndPopup();
    }

    return changed;
}

void DrawUnitySelectorGroupLabel(const char* label)
{
    ImGui::TextDisabled("%s", label);
    ImGui::Separator();
}

void DrawUIAnchorCombo(UIAnchor& anchor)
{
    static const char* kAnchorNames[] = {
        "Top Left", "Top", "Top Right",
        "Left", "Center", "Right",
        "Bottom Left", "Bottom", "Bottom Right",
    };
    int idx = static_cast<int>(anchor);
    ImGui::Text("Anchor  "); ImGui::SameLine();
    if (ImGui::Combo("##UIAnchor", &idx, kAnchorNames, 9))
        anchor = static_cast<UIAnchor>(idx);
}

#endif
