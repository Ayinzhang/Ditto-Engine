#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/PrefabAsset.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../3rdParty/ImGui/imgui.h"

#include <algorithm>
#include <cstring>

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
void GameObject::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    ProcessRemovals();
#else
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine();
    char nameBuffer[256];
    strcpy_s(nameBuffer, sizeof(nameBuffer), name.c_str());
    ImGui::Text("Name"); ImGui::SameLine();
    ImGui::PushID("NameInput");
    if (ImGui::InputText("", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot();   // pre-rename state
        name = nameBuffer;
        if (CurrentScene()) CurrentScene()->MarkDirty();
    }
    ImGui::PopID();
    
    // Lock button (after name)
    ImGui::SameLine();
    if (CurrentEditor())
    {
        void* lockIcon = locked ? CurrentEditor()->GetLockIcon() : CurrentEditor()->GetUnlockIcon();
        if (lockIcon)
        {
            float btnSize = 16.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - btnSize - 20);
            if (ImGui::ImageButton("##lock", (void*)(intptr_t)lockIcon, ImVec2(btnSize, btnSize), 
                ImVec2(0, 1), ImVec2(1, 0)))
            {
                locked = !locked;
                CurrentEditor()->lockingSelection = locked;
                if (locked) CurrentEditor()->activeSelection = this;
            }
        }
    }
    
    ImGui::Separator();

    if (!prefabSourcePath.empty())
    {
        ImGui::TextDisabled("Prefab: %s", prefabSourcePath.c_str());
        if (CurrentEditor())
        {
            if (ImGui::SmallButton("Apply"))
                CurrentEditor()->ApplySelectedPrefabInstance();
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert"))
                CurrentEditor()->RevertSelectedPrefabInstance();
        }

        std::vector<Ditto::PrefabAsset::Override> overrides =
            Ditto::PrefabAsset::CollectOverrides(*this);
        ImGui::TextDisabled("%d overrides", static_cast<int>(overrides.size()));
        if (!overrides.empty() && ImGui::TreeNode("Overrides"))
        {
            for (const auto& overrideEntry : overrides)
                ImGui::BulletText("%s %s = %s",
                    overrideEntry.kind.c_str(),
                    overrideEntry.path.c_str(),
                    overrideEntry.value.c_str());
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    for (auto& comp : components)
    {
        ImGui::PushID(comp.get());
        ImVec2 componentStart = ImGui::GetCursorScreenPos();
        comp->OnInspectorGUI();
        SelectComponentArea(comp.get(), componentStart);
        ImGui::PopID();
        ImGui::Separator();
    }
    ProcessRemovals();
#endif
}

void TransformComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    UpdateTransform();
#else
    if (!DrawComponentHeader(this, "Transform", false)) return;

    ImGui::Indent(20.0f);
    UnityLabel("Position");
    if (ImGui::DragFloat3("##TransformPosition", &position.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    UnityLabel("Rotation");
    if (ImGui::DragFloat3("##TransformRotation", &rotation.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    UnityLabel("Scale");
    if (ImGui::DragFloat3("##TransformScale", &scale.x, 0.1f)) localDirty = true;
    TrackUndoableEdit();
    ImGui::Unindent(20.0f);

    if (!enabled) ImGui::PopStyleVar();

    if (localDirty)
    {
        lastPosition = position;
        lastRotation = rotation;
        lastScale = scale;
        UpdateTransform();
        
        // Mark scene as modified
        if (CurrentScene()) CurrentScene()->MarkDirty();
    }
#endif
}

void LightComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    if (!DrawComponentHeader(this, "Light")) return;
    ImGui::Indent(20.0f);
    const char* typeNames[] = { "Directional", "Point", "Spot", "Area" };
    int typeIndex = static_cast<int>(type);
    if (UnityCombo("Type", &typeIndex, typeNames, 4, "##LightType"))
        type = static_cast<Type>(typeIndex);
    UnityColor3("Color", &color, "##LightColor");
    const char* modeNames[] = { "Realtime" };
    int mode = 0;
    UnityCombo("Mode", &mode, modeNames, 1, "##LightMode");
    UnityDragFloat("Intensity", &intensity, "##LightIntensity", 0.1f, 0.0f, 100.0f);
    if (type == Point || type == Spot)
        UnityDragFloat("Range", &range, "##LightRange", 0.1f, 0.0f, 10000.0f);
    if (type == Spot)
        UnityDragFloat("Spot Angle", &spotAngle, "##LightSpotAngle", 0.1f, 1.0f, 179.0f);
    UnityDragFloat("Indirect Multiplier", &indirectMultiplier, "##LightIndirectMultiplier", 0.1f, 0.0f, 100.0f);
    const char* shadowNames[] = { "No Shadows", "Hard Shadows" };
    int shadow = castShadows ? 1 : 0;
    if (UnityCombo("Shadow Type", &shadow, shadowNames, 2, "##LightShadowType"))
        castShadows = shadow != 0;
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void CameraComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    if (!DrawComponentHeader(this, "Camera")) return;

    ImGui::Indent(20.0f);
    UnityCheckbox("Main Camera", &mainCamera, "##CameraMain");
    const char* clearNames[] = { "Skybox", "Solid Color", "Depth Only", "Don't Clear" };
    int clearIndex = static_cast<int>(clearFlags);
    if (UnityCombo("Clear Flags", &clearIndex, clearNames, 4, "##CameraClearFlags"))
        clearFlags = static_cast<ClearFlags>(clearIndex);
    if (clearFlags == SolidColor)
        UnityColor4("Background", &backgroundColor, "##CameraBackground");

    const char* projectionNames[] = { "Perspective", "Orthographic" };
    int projection = projectionType == Camera::ProjectionType::Orthographic ? 1 : 0;
    if (UnityCombo("Projection", &projection, projectionNames, 2, "##CameraProjection"))
        projectionType = projection == 1 ? Camera::ProjectionType::Orthographic : Camera::ProjectionType::Perspective;

    if (projectionType == Camera::ProjectionType::Perspective)
    {
        UnityLabel("Field of View");
        if (ImGui::DragFloat("##CameraFOV", &fieldOfView, 0.1f, 1.0f, 179.0f))
            fieldOfView = std::clamp(fieldOfView, 1.0f, 179.0f);
        TrackUndoableEdit();
    }
    else
    {
        UnityLabel("Size");
        if (ImGui::DragFloat("##CameraOrthoSize", &orthographicSize, 0.1f, 0.0001f, 10000.0f))
            orthographicSize = std::max(0.0001f, orthographicSize);
        TrackUndoableEdit();
    }

    ImGui::TextUnformatted("Clipping Planes");
    ImGui::Indent(12.0f);
    UnityLabel("Near");
    if (ImGui::DragFloat("##CameraNear", &nearClipPlane, 0.01f, 0.0001f, farClipPlane - 0.0001f))
        nearClipPlane = std::max(0.0001f, nearClipPlane);
    TrackUndoableEdit();
    UnityLabel("Far");
    if (ImGui::DragFloat("##CameraFar", &farClipPlane, 0.1f, nearClipPlane + 0.0001f, 100000.0f))
        farClipPlane = std::max(nearClipPlane + 0.0001f, farClipPlane);
    TrackUndoableEdit();
    ImGui::Unindent(12.0f);

    UnityLabel("Viewport Rect");
    ImGui::DragFloat4("##CameraViewportRect", &viewportRect.x, 0.01f, 0.0f, 1.0f);
    TrackUndoableEdit();
    UnityDragFloat("Depth", &depth, "##CameraDepth", 1.0f, -100.0f, 100.0f);
    const char* renderingPath[] = { "Use Graphics Settings" };
    int renderingPathIndex = 0;
    UnityCombo("Rendering Path", &renderingPathIndex, renderingPath, 1, "##CameraRenderingPath");
    const char* targetTexture[] = { "None (Render Texture)" };
    int targetTextureIndex = 0;
    UnityCombo("Target Texture", &targetTextureIndex, targetTexture, 1, "##CameraTargetTexture");
    UnityCheckbox("Occlusion Culling", &occlusionCulling, "##CameraOcclusionCulling");
    UnityCheckbox("HDR", &allowHDR, "##CameraHDR");
    UnityCheckbox("MSAA", &allowMSAA, "##CameraMSAA");

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

#endif



