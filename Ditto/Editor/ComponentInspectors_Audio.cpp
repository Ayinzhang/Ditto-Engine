#ifndef DITTO_HEADLESS_TESTS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ComponentInspectorWidgets.h"
#include "Editor.h"
#include "../Engine/Core/GameObject.h"
#include "../Engine/Core/Scene.h"
#include "../Engine/Core/RuntimeContext.h"
#include "../Engine/Audio/AudioEngine.h"
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
void AudioSourceComponent::OnInspectorGUI()
{
#ifdef DITTO_HEADLESS_TESTS
    return;
#else
    DrawComponentSelectionBackground(this);
    ImGui::Checkbox("##Enabled", &enabled);
    ImGui::SameLine(); ImGui::TextUnformatted("Audio Source");
    SelectComponentOnLastItem(this);
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::SmallButton("X")) { if (CurrentEditor()) CurrentEditor()->PushUndoSnapshot(); gameObject->RemoveComponent(this); return; }
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

    ImGui::Indent(20.0f);

    DrawAssetObjectField("Audio Clip", clipPath, "AudioClipObjectPopup",
        "Select Audio Clip", { ".wav", ".mp3", ".ogg", ".flac" }, "None (Audio Clip)");

    UnityLabel("Output");
    ImGui::TextDisabled("None (Audio Mixer Group)");
    UnityCheckbox("Mute", &mute, "##AudioMute");
    UnityCheckbox("Bypass Effects", &bypassEffects, "##AudioBypassEffects");
    UnityCheckbox("Bypass Listener Effects", &bypassListenerEffects, "##AudioBypassListenerEffects");
    UnityCheckbox("Bypass Reverb Zones", &bypassReverbZones, "##AudioBypassReverbZones");
    UnityCheckbox("Play On Awake", &playOnAwake, "##AudioPlayOnAwake");
    UnityCheckbox("Loop", &loop, "##AudioLoop");
    UnityLabel("Priority");
    ImGui::DragInt("##AudioPriority", &priority, 1.0f, 0, 256);
    priority = std::clamp(priority, 0, 256);
    TrackUndoableEdit();

    UnityLabel("Volume");
    if (ImGui::SliderFloat("##AudioVolume", &volume, 0.0f, 1.0f))
    {
        if (soundHandle != 0) AudioEngine::SetVolume(soundHandle, volume);
    }
    TrackUndoableEdit();
    UnityDragFloat("Pitch", &pitch, "##AudioPitch", 0.01f, -3.0f, 3.0f);
    UnityDragFloat("Stereo Pan", &stereoPan, "##AudioStereoPan", 0.01f, -1.0f, 1.0f);
    UnityDragFloat("Spatial Blend", &spatialBlend, "##AudioSpatialBlend", 0.01f, 0.0f, 1.0f);
    UnityDragFloat("Reverb Zone Mix", &reverbZoneMix, "##AudioReverbZoneMix", 0.01f, 0.0f, 1.1f);

    ImGui::Unindent(20.0f);
    if (!enabled) ImGui::PopStyleVar();
#endif
}

#endif



