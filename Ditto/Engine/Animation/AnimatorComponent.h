#pragma once
#include "../Core/GameObject.h"
#include <string>
#include <vector>
#include <map>

// AnimatorComponent: Unity-style keyframe animation on a GameObject's Transform.
// A clip is a sorted list of keyframes (time -> position/rotation/scale); the
// component interpolates between them each frame while playing. Clips are stored
// inline on the component and serialized with the scene, so no external asset
// files are required for a simple demo.
struct AnimationKeyframe
{
    float time = 0.0f;          // seconds from clip start
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f }; // euler degrees
    glm::vec3 scale{ 1.0f };
};

struct AnimationClip
{
    std::string name = "Clip";
    float length = 1.0f;        // seconds
    bool loop = true;
    std::vector<AnimationKeyframe> keyframes;

    // Interpolate the clip at `time` (already wrapped/clamped by the caller).
    void Evaluate(float time, glm::vec3& outPos, glm::vec3& outRot, glm::vec3& outScale) const;
};

struct AnimatorComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Animator;

    // Clip library keyed by clip name.
    std::map<std::string, AnimationClip> clips;

    // Authoring/runtime state.
    std::string defaultClip;     // played automatically on Play-mode enter
    std::string currentClip;     // currently playing clip name
    float currentTime = 0.0f;    // playback cursor (seconds)
    float playbackSpeed = 1.0f;
    bool playOnAwake = true;
    bool playing = false;
    bool paused = false;

    AnimatorComponent();
    AnimatorComponent(AnimatorComponent* other);

    // Playback control (Unity-like API).
    void Play(const std::string& clipName = "");
    void Stop();
    void Pause();
    void Resume();
    void SetSpeed(float speed) { playbackSpeed = speed; }
    bool IsPlaying() const { return playing && !paused; }

    // Advance the active clip and write the pose onto the Transform. Called by
    // the engine each frame in Play mode.
    void Update(float deltaTime);

    void AddClip(const AnimationClip& clip) { clips[clip.name] = clip; }

    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    void ApplyPose(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
};
