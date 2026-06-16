#include "AnimatorComponent.h"
#ifndef DITTO_HEADLESS_TESTS
#include "../../3rdParty/ImGui/imgui.h"
#endif
#include "../../3rdParty/GLM/common.hpp"
#include <ostream>
#include <istream>
#include <cmath>

// ----- AnimationClip -----

void AnimationClip::Evaluate(float time, glm::vec3& outPos, glm::vec3& outRot, glm::vec3& outScale) const
{
    if (keyframes.empty())
    {
        outPos = glm::vec3(0.0f);
        outRot = glm::vec3(0.0f);
        outScale = glm::vec3(1.0f);
        return;
    }

    // Before first / single keyframe.
    if (keyframes.size() == 1 || time <= keyframes.front().time)
    {
        outPos = keyframes.front().position;
        outRot = keyframes.front().rotation;
        outScale = keyframes.front().scale;
        return;
    }
    // After last keyframe.
    if (time >= keyframes.back().time)
    {
        outPos = keyframes.back().position;
        outRot = keyframes.back().rotation;
        outScale = keyframes.back().scale;
        return;
    }

    // Find the bracketing pair and lerp.
    for (size_t i = 1; i < keyframes.size(); ++i)
    {
        const AnimationKeyframe& b = keyframes[i];
        if (time <= b.time)
        {
            const AnimationKeyframe& a = keyframes[i - 1];
            float span = b.time - a.time;
            float t = span > 1e-6f ? (time - a.time) / span : 0.0f;
            outPos = glm::mix(a.position, b.position, t);
            outRot = glm::mix(a.rotation, b.rotation, t);
            outScale = glm::mix(a.scale, b.scale, t);
            return;
        }
    }
}

// ----- AnimatorComponent -----

AnimatorComponent::AnimatorComponent()
{
    index = ComponentIndex::Animator;
}

AnimatorComponent::AnimatorComponent(AnimatorComponent* other)
    : clips(other->clips), defaultClip(other->defaultClip), currentClip(other->currentClip),
      currentTime(other->currentTime), playbackSpeed(other->playbackSpeed),
      playOnAwake(other->playOnAwake), playing(other->playing), paused(other->paused)
{
    index = ComponentIndex::Animator;
    enabled = other->enabled;
}

void AnimatorComponent::Play(const std::string& clipName)
{
    std::string target = clipName.empty() ? (currentClip.empty() ? defaultClip : currentClip) : clipName;
    if (target.empty() && !clips.empty())
        target = clips.begin()->first;
    if (clips.find(target) == clips.end())
        return;
    currentClip = target;
    currentTime = 0.0f;
    playing = true;
    paused = false;
}

void AnimatorComponent::Stop()
{
    playing = false;
    paused = false;
    currentTime = 0.0f;
}

void AnimatorComponent::Pause() { paused = true; }
void AnimatorComponent::Resume() { paused = false; }

void AnimatorComponent::Update(float deltaTime)
{
    if (!enabled || !playing || paused || currentClip.empty())
        return;

    auto it = clips.find(currentClip);
    if (it == clips.end())
        return;
    const AnimationClip& clip = it->second;

    currentTime += deltaTime * playbackSpeed;

    float evalTime = currentTime;
    if (clip.loop && clip.length > 1e-6f)
    {
        evalTime = std::fmod(currentTime, clip.length);
        if (evalTime < 0.0f) evalTime += clip.length;
    }
    else if (currentTime >= clip.length)
    {
        evalTime = clip.length;
        playing = false; // one-shot finished
    }

    glm::vec3 pos, rot, scale;
    clip.Evaluate(evalTime, pos, rot, scale);
    ApplyPose(pos, rot, scale);
}

void AnimatorComponent::ApplyPose(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale)
{
    if (!gameObject) return;
    TransformComponent* t = gameObject->GetComponent<TransformComponent>();
    if (!t) return;
    t->position = pos;
    t->rotation = rot;
    t->scale = scale;
    t->localDirty = true;
    t->UpdateTransform();
}

void AnimatorComponent::OnInspectorGUI()
{
#ifndef DITTO_HEADLESS_TESTS
    ImGui::Checkbox("Enabled", &enabled);
    ImGui::Checkbox("Play On Awake", &playOnAwake);

    ImGui::Separator();
    ImGui::Text("Clips (%d)", static_cast<int>(clips.size()));

    for (auto& [name, clip] : clips)
    {
        ImGui::PushID(name.c_str());
        if (ImGui::Button("Play")) Play(name);
        ImGui::SameLine();
        bool isDefault = (defaultClip == name);
        if (ImGui::Checkbox("Default", &isDefault))
            defaultClip = isDefault ? name : std::string();
        ImGui::SameLine();
        ImGui::Text("%s  (%.2fs, %d keys%s)", name.c_str(), clip.length,
            static_cast<int>(clip.keyframes.size()), clip.loop ? ", loop" : "");
        ImGui::PopID();
    }

    ImGui::Separator();

    // Add a new clip from the object's current transform as a 2-keyframe demo.
    static char newClipName[64] = "NewClip";
    ImGui::InputText("##NewClipName", newClipName, sizeof(newClipName));
    ImGui::SameLine();
    if (ImGui::Button("Add Clip"))
    {
        AnimationClip clip;
        clip.name = newClipName[0] ? newClipName : "Clip";
        clip.length = 1.0f;
        clip.loop = true;
        // Seed two keyframes from the current transform so the clip is valid.
        AnimationKeyframe k0, k1;
        if (gameObject)
        {
            if (TransformComponent* t = gameObject->GetComponent<TransformComponent>())
            {
                k0.position = k1.position = t->position;
                k0.rotation = k1.rotation = t->rotation;
                k0.scale = k1.scale = t->scale;
            }
        }
        k0.time = 0.0f;
        k1.time = 1.0f;
        clip.keyframes = { k0, k1 };
        AddClip(clip);
        if (defaultClip.empty()) defaultClip = clip.name;
    }

    ImGui::Separator();

    // Playback control + keyframe editing for the current clip.
    if (!currentClip.empty() && clips.count(currentClip))
    {
        AnimationClip& clip = clips[currentClip];
        ImGui::Text("Editing: %s", currentClip.c_str());
        ImGui::DragFloat("Length", &clip.length, 0.05f, 0.05f, 600.0f);
        ImGui::Checkbox("Loop", &clip.loop);
        ImGui::DragFloat("Speed", &playbackSpeed, 0.05f, 0.0f, 10.0f);

        if (playing)
        {
            if (ImGui::Button(paused ? "Resume" : "Pause")) { paused = !paused; }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) Stop();
            ImGui::SameLine();
            ImGui::Text("t = %.2f / %.2f", currentTime, clip.length);
        }
        else
        {
            if (ImGui::Button("Play")) Play(currentClip);
        }

        ImGui::Separator();
        ImGui::Text("Keyframes");
        int removeIdx = -1;
        for (size_t i = 0; i < clip.keyframes.size(); ++i)
        {
            AnimationKeyframe& k = clip.keyframes[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::DragFloat("Time", &k.time, 0.02f, 0.0f, clip.length);
            ImGui::DragFloat3("Position", &k.position.x, 0.05f);
            ImGui::DragFloat3("Rotation", &k.rotation.x, 0.5f);
            ImGui::DragFloat3("Scale", &k.scale.x, 0.05f);
            if (ImGui::Button("Set From Current") && gameObject)
            {
                if (TransformComponent* t = gameObject->GetComponent<TransformComponent>())
                {
                    k.position = t->position;
                    k.rotation = t->rotation;
                    k.scale = t->scale;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) removeIdx = static_cast<int>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeIdx >= 0)
            clip.keyframes.erase(clip.keyframes.begin() + removeIdx);
        if (ImGui::Button("Add Keyframe"))
        {
            AnimationKeyframe k;
            if (gameObject)
            {
                if (TransformComponent* t = gameObject->GetComponent<TransformComponent>())
                {
                    k.position = t->position;
                    k.rotation = t->rotation;
                    k.scale = t->scale;
                }
            }
            k.time = clip.keyframes.empty() ? 0.0f : clip.keyframes.back().time + 0.25f;
            clip.keyframes.push_back(k);
        }
    }
#endif
}

void AnimatorComponent::Serialize(std::ostream& file) const
{
    auto writeStr = [&](const std::string& s)
    {
        uint32_t len = static_cast<uint32_t>(s.size());
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(s.data(), len);
    };

    writeStr(defaultClip);
    file.write(reinterpret_cast<const char*>(&playOnAwake), sizeof(playOnAwake));
    file.write(reinterpret_cast<const char*>(&playbackSpeed), sizeof(playbackSpeed));

    uint32_t clipCount = static_cast<uint32_t>(clips.size());
    file.write(reinterpret_cast<const char*>(&clipCount), sizeof(clipCount));
    for (const auto& [name, clip] : clips)
    {
        writeStr(clip.name);
        file.write(reinterpret_cast<const char*>(&clip.length), sizeof(clip.length));
        file.write(reinterpret_cast<const char*>(&clip.loop), sizeof(clip.loop));
        uint32_t kfCount = static_cast<uint32_t>(clip.keyframes.size());
        file.write(reinterpret_cast<const char*>(&kfCount), sizeof(kfCount));
        for (const auto& k : clip.keyframes)
        {
            file.write(reinterpret_cast<const char*>(&k.time), sizeof(k.time));
            file.write(reinterpret_cast<const char*>(&k.position), sizeof(k.position));
            file.write(reinterpret_cast<const char*>(&k.rotation), sizeof(k.rotation));
            file.write(reinterpret_cast<const char*>(&k.scale), sizeof(k.scale));
        }
    }
}

void AnimatorComponent::Deserialize(std::istream& file)
{
    auto readStr = [&]() -> std::string
    {
        uint32_t len = 0;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string s(len, '\0');
        if (len) file.read(&s[0], len);
        return s;
    };

    defaultClip = readStr();
    file.read(reinterpret_cast<char*>(&playOnAwake), sizeof(playOnAwake));
    file.read(reinterpret_cast<char*>(&playbackSpeed), sizeof(playbackSpeed));

    clips.clear();
    uint32_t clipCount = 0;
    file.read(reinterpret_cast<char*>(&clipCount), sizeof(clipCount));
    for (uint32_t i = 0; i < clipCount; ++i)
    {
        AnimationClip clip;
        clip.name = readStr();
        file.read(reinterpret_cast<char*>(&clip.length), sizeof(clip.length));
        file.read(reinterpret_cast<char*>(&clip.loop), sizeof(clip.loop));
        uint32_t kfCount = 0;
        file.read(reinterpret_cast<char*>(&kfCount), sizeof(kfCount));
        clip.keyframes.resize(kfCount);
        for (auto& k : clip.keyframes)
        {
            file.read(reinterpret_cast<char*>(&k.time), sizeof(k.time));
            file.read(reinterpret_cast<char*>(&k.position), sizeof(k.position));
            file.read(reinterpret_cast<char*>(&k.rotation), sizeof(k.rotation));
            file.read(reinterpret_cast<char*>(&k.scale), sizeof(k.scale));
        }
        clips[clip.name] = clip;
    }
}
