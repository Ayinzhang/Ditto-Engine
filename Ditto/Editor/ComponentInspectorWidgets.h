#pragma once

#ifndef DITTO_HEADLESS_TESTS

#include <initializer_list>
#include <string>
#include <vector>
#include "../3rdParty/ImGui/imgui.h"
#include "../3rdParty/GLM/glm.hpp"

struct Component;
enum class UIAnchor : int;

void TrackUndoableEdit();
void DrawComponentSelectionBackground(Component* component);
void SelectComponentOnLastItem(Component* component);
void SelectComponentArea(Component* component, const ImVec2& start);
bool DrawComponentHeader(Component* component, const char* title, bool removable = true);

void UnityLabel(const char* label);
void UnityCheckbox(const char* label, bool* value, const char* id);
void UnityDragFloat(const char* label, float* value, const char* id,
    float speed = 0.1f, float min = 0.0f, float max = 0.0f);
void UnityDragFloat2(const char* label, glm::vec2* value, const char* id,
    float speed = 0.1f, float min = 0.0f, float max = 0.0f);
void UnityDragFloat3(const char* label, glm::vec3* value, const char* id,
    float speed = 0.1f, float min = 0.0f, float max = 0.0f);
void UnityColor4(const char* label, glm::vec4* value, const char* id);
void UnityColor3(const char* label, glm::vec3* value, const char* id);
bool UnityCombo(const char* label, int* value, const char* const* names, int count, const char* id);

std::string FileNameFromPath(const std::string& path);
std::string ToFullAssetPath(const std::string& relativePath);
std::string FileNameWithoutExtension(const std::string& path);
bool DrawObjectFieldButton(const char* label, void* iconTexture, const std::string& value, const char* popupId,
    std::string* droppedPath = nullptr, const std::string& fullPath = "");
bool DrawUnitySelectorHeader(const char* title, const char* searchId, char* searchBuffer, size_t searchBufferSize);
std::string ToAssetRelativePath(const std::string& path);
std::string LowerExtension(const std::string& path);
bool ExtensionMatches(const std::string& path, std::initializer_list<const char*> extensions);
std::vector<std::string> FindProjectAssets(std::initializer_list<const char*> extensions);
bool DrawAssetMenuItems(const std::vector<std::string>& assets, const std::string& currentPath, std::string& selectedPath);
bool SearchMatches(const std::string& text, const std::string& search);
bool DrawFilteredAssetMenuItems(const std::vector<std::string>& assets,
    const std::string& currentPath, const std::string& search, std::string& selectedPath);
bool DrawAssetObjectField(const char* label, std::string& assetPath, const char* popupId,
    const char* selectorTitle, std::initializer_list<const char*> extensions, const char* emptyDisplay);
void DrawUnitySelectorGroupLabel(const char* label);
void DrawUIAnchorCombo(UIAnchor& anchor);

#endif
