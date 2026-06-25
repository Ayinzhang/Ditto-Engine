#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
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
void CanvasComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Canvas");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    const char* renderModeNames[] = { "Screen Space - Overlay", "Screen Space - Camera", "World Space" };
    int mode = static_cast<int>(renderMode);
    if (UnityCombo("Render Mode", &mode, renderModeNames, 3, "##CanvasRenderMode"))
        renderMode = static_cast<RenderMode>(mode);
    UnityCheckbox("Pixel Perfect", &pixelPerfect, "##CanvasPixelPerfect");
    UnityDragFloat("Plane Distance", &planeDistance, "##CanvasPlaneDistance", 1.0f, 0.0f, 100000.0f);
    UnityLabel("Sorting Order");
    ImGui::DragInt("##CanvasSortingOrder", &sortingOrder, 1.0f, -32768, 32767);
    TrackUndoableEdit();
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void RectTransformComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Rect Transform");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);
    DrawUIAnchorCombo(anchor); TrackUndoableEdit();
    UnityDragFloat2("Pos", &anchoredPosition, "##RectTransformPos", 1.0f);
    UnityDragFloat2("Width Height", &sizeDelta, "##RectTransformSize", 1.0f, 0.0f, 8192.0f);
    UnityDragFloat2("Pivot", &pivot, "##RectTransformPivot", 0.01f, 0.0f, 1.0f);
    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UIImageComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Image");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    DrawAssetObjectField("Source Image", texturePath, "UIImageTextureObjectPopup",
        "Select Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" }, "None (Texture2D)");
    UnityColor4("Color", &color, "##UIImgColor");
    const char* imageTypeNames[] = { "Simple", "Sliced", "Tiled", "Filled" };
    int imageTypeIndex = static_cast<int>(type);
    if (UnityCombo("Image Type", &imageTypeIndex, imageTypeNames, 4, "##UIImageType"))
        type = static_cast<Type>(imageTypeIndex);
    UnityCheckbox("Raycast Target", &raycastTarget, "##UIImageRaycastTarget");
    UnityCheckbox("Maskable", &maskable, "##UIImageMaskable");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UIImgOffset", 1.0f);
        UnityDragFloat2("Size", &size, "##UIImgSize", 1.0f, 0.0f, 8192.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UITextComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Text");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    static char textBuf[1024];
    strcpy_s(textBuf, sizeof(textBuf), text.c_str());
    UnityLabel("Text");
    if (ImGui::InputTextMultiline("##UIText", textBuf, sizeof(textBuf),
            ImVec2(-1, ImGui::GetTextLineHeight() * 3)))
        text = textBuf;
    TrackUndoableEdit();

    DrawAssetObjectField("Font", fontPath, "UITextFontObjectPopup",
        "Select Font", { ".ttf", ".otf" }, "Default Font");
    const char* fontStyleNames[] = { "Normal", "Bold", "Italic", "Bold And Italic" };
    UnityCombo("Font Style", &fontStyle, fontStyleNames, 4, "##UITextFontStyle");
    UnityDragFloat("Font Size", &fontSize, "##UITextSize", 0.5f, 4.0f, 256.0f);
    const char* alignmentNames[] = { "Left", "Center", "Right" };
    UnityCombo("Alignment", &alignment, alignmentNames, 3, "##UITextAlignment");
    UnityColor4("Color", &color, "##UITextColor");
    UnityCheckbox("Raycast Target", &raycastTarget, "##UITextRaycastTarget");
    UnityCheckbox("Maskable", &maskable, "##UITextMaskable");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UITextOffset", 1.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

void UIButtonComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Button");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    ImGui::Indent(20.0f);

    static char labelBuf[256];
    strcpy_s(labelBuf, sizeof(labelBuf), label.c_str());
    UnityCheckbox("Interactable", &interactable, "##UIButtonInteractable");
    const char* transitionNames[] = { "None", "Color Tint" };
    UnityCombo("Transition", &transition, transitionNames, 2, "##UIButtonTransition");
    UnityColor4("Normal Color", &color, "##UIBtnColor");
    UnityColor4("Highlighted Color", &hoverColor, "##UIBtnHover");
    UnityColor4("Pressed Color", &pressedColor, "##UIBtnPressed");
    UnityColor4("Disabled Color", &disabledColor, "##UIBtnDisabled");
    UnityDragFloat("Color Multiplier", &colorMultiplier, "##UIBtnColorMultiplier", 0.01f, 0.0f, 10.0f);
    UnityDragFloat("Fade Duration", &fadeDuration, "##UIBtnFadeDuration", 0.01f, 0.0f, 10.0f);

    UnityLabel("Text");
    if (ImGui::InputText("##UIBtnLabel", labelBuf, sizeof(labelBuf)))
        label = labelBuf;
    TrackUndoableEdit();
    UnityDragFloat("Font Size", &fontSize, "##UIBtnFontSize", 0.5f, 4.0f, 256.0f);
    UnityColor4("Text Color", &labelColor, "##UIBtnLabelColor");
    if (!gameObject || !gameObject->GetComponent<RectTransformComponent>())
    {
        DrawUIAnchorCombo(anchor); TrackUndoableEdit();
        UnityDragFloat2("Offset", &offset, "##UIBtnOffset", 1.0f);
        UnityDragFloat2("Size", &size, "##UIBtnSize", 1.0f, 0.0f, 8192.0f);
    }

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

#endif



