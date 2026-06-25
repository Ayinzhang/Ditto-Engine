#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../Engine/Graphics/Materials/MaterialAsset.h"
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
void RendererComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::TextUnformatted("Mesh Filter");
    SelectComponentOnLastItem(this);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Mesh", meshPath, "RendererMeshObjectPopup",
        "Select Mesh", { ".obj", ".fbx", ".mesh" }, "None (Mesh)");

    ImGui::Unindent(20.0f);
    ImGui::Separator();

    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Mesh Renderer");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    ImGui::TextUnformatted("Materials");
    ImGui::Indent(12.0f);

    DrawAssetObjectField("Element 0", materialPath, "RendererMaterialObjectPopup",
        "Select Material", { ".mat" }, "Default-Material");

    if (!materialPath.empty() && !Ditto::LoadMaterialAsset(materialPath).ok)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Material load failed");

    ImGui::Unindent(12.0f);
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* shadowNames[] = { "Off", "On", "Two Sided", "Shadows Only" };
        int shadowIndex = static_cast<int>(shadowCastingMode);
        if (UnityCombo("Cast Shadows", &shadowIndex, shadowNames, 4, "##RendererCastShadows"))
            shadowCastingMode = static_cast<ShadowCastingMode>(shadowIndex);
        UnityCheckbox("Receive Shadows", &receiveShadows, "##RendererReceiveShadows");
        UnityCheckbox("Static Shadow Caster", &staticShadowCaster, "##RendererStaticShadowCaster");
        UnityCheckbox("Contribute Global Illumination", &contributeGI, "##RendererContributeGI");
    }
    if (ImGui::CollapsingHeader("Probes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* lightProbeNames[] = { "Off", "Blend Probes", "Use Proxy Volume", "Custom Provided" };
        UnityCombo("Light Probes", &lightProbeUsage, lightProbeNames, 4, "##RendererLightProbes");
        const char* reflectionProbeNames[] = { "Off", "Blend Probes", "Blend Probes And Skybox", "Simple" };
        UnityCombo("Reflection Probes", &reflectionProbeUsage, reflectionProbeNames, 4, "##RendererReflectionProbes");
        const char* anchorNames[] = { "None (Transform)" };
        int anchor = 0;
        UnityCombo("Anchor Override", &anchor, anchorNames, 1, "##RendererAnchorOverride");
    }
    if (ImGui::CollapsingHeader("Additional Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const char* motionNames[] = { "Camera Motion Only", "Per Object Motion", "Force No Motion" };
        UnityCombo("Motion Vectors", &motionVectors, motionNames, 3, "##RendererMotionVectors");
        UnityCheckbox("Dynamic Occlusion", &dynamicOcclusion, "##RendererDynamicOcclusion");
        int layerMask = static_cast<int>(renderingLayerMask);
        UnityLabel("Rendering Layer Mask");
        if (ImGui::InputInt("##RendererLayerMask", &layerMask))
            renderingLayerMask = static_cast<uint32_t>(std::max(0, layerMask));
        TrackUndoableEdit();
    }
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void SpriteRendererComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Sprite Renderer");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Sprite", spritePath, "SpriteRendererSpriteObjectPopup",
        "Select Sprite", { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" }, "None (Sprite)");

    UnityColor4("Color", &color, "##SpriteRendererColor");
    UnityLabel("Flip");
    ImGui::Checkbox("X##SpriteRendererFlipX", &flipX);
    ImGui::SameLine();
    ImGui::Checkbox("Y##SpriteRendererFlipY", &flipY);
    TrackUndoableEdit();
    const char* drawModeNames[] = { "Simple", "Sliced", "Tiled" };
    int drawModeIndex = static_cast<int>(drawMode);
    if (UnityCombo("Draw Mode", &drawModeIndex, drawModeNames, 3, "##SpriteRendererDrawMode"))
        drawMode = static_cast<DrawMode>(drawModeIndex);
    if (drawMode != Simple)
        UnityDragFloat2("Size", &size, "##SpriteRendererSize", 0.05f, 0.0f, 10000.0f);

    ImGui::TextUnformatted("Materials");
    ImGui::Indent(12.0f);

    DrawAssetObjectField("Element 0", materialPath, "SpriteRendererMaterialObjectPopup",
        "Select Material", { ".mat" }, "Default-Sprite-Material");

    if (!materialPath.empty() && !Ditto::LoadMaterialAsset(materialPath).ok)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Material load failed");

    ImGui::Unindent(12.0f);
    const char* maskNames[] = { "None", "Visible Inside Mask", "Visible Outside Mask" };
    int maskIndex = static_cast<int>(maskInteraction);
    if (UnityCombo("Mask Interaction", &maskIndex, maskNames, 3, "##SpriteRendererMaskInteraction"))
        maskInteraction = static_cast<MaskInteraction>(maskIndex);
    const char* sortingLayerNames[] = { "Default" };
    UnityCombo("Sorting Layer", &sortingLayer, sortingLayerNames, 1, "##SpriteRendererSortingLayer");
    UnityLabel("Order in Layer");
    ImGui::DragInt("##SpriteRendererSortingOrder", &sortingOrder, 1.0f, -32768, 32767);
    TrackUndoableEdit();
    const char* sortPointNames[] = { "Center", "Pivot" };
    int sortPointIndex = static_cast<int>(spriteSortPoint);
    if (UnityCombo("Sprite Sort Point", &sortPointIndex, sortPointNames, 2, "##SpriteRendererSortPoint"))
        spriteSortPoint = static_cast<SpriteSortPoint>(sortPointIndex);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

#endif



