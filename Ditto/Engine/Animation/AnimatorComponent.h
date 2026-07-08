#pragma once
#include "../Core/GameObject.h"
#include <string>
#include <vector>
#include <map>






struct AnimationKeyframe
{
    float time = 0.0f;          
    glm::vec3 position{ 0.0f };
    glm::vec3 rotation{ 0.0f }; 
    glm::vec3 scale{ 1.0f };
};

struct AnimationClip
{
    std::string name = "Clip";
    float length = 1.0f;        
    bool loop = true;
    std::vector<AnimationKeyframe> keyframes;

    
    void Evaluate(float time, glm::vec3& outPos, glm::vec3& outRot, glm::vec3& outScale) const;
};

struct AnimatorComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::Animator;

    
    std::map<std::string, AnimationClip> clips;

    
    std::string defaultClip;     
    std::string currentClip;     
    float currentTime = 0.0f;    
    float playbackSpeed = 1.0f;
    bool playOnAwake = true;
    bool playing = false;
    bool paused = false;

    AnimatorComponent();
    AnimatorComponent(AnimatorComponent* other);

    
    void Play(const std::string& clipName = "");
    void Stop();
    void Pause();
    void Resume();
    void SetSpeed(float speed) { playbackSpeed = speed; }
    bool IsPlaying() const { return playing && !paused; }

    
    
    void Update(float deltaTime);

    void AddClip(const AnimationClip& clip) { clips[clip.name] = clip; }

    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    void ApplyPose(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
};
