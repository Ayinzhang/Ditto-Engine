#pragma once
#include "../Core/GameObject.h"
#include <vector>





struct Particle
{
    glm::vec3 position{ 0.0f };
    glm::vec3 velocity{ 0.0f };
    glm::vec4 color{ 1.0f };
    float size = 1.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    bool alive = false;
};

struct ParticleSystemComponent : Component
{
    static constexpr int TypeBit = ComponentIndex::ParticleSystem;

    enum EmissionShape { Cone = 0, Sphere = 1, Box = 2 };

    
    int maxParticles = 1000;
    float emissionRate = 20.0f;   
    bool looping = true;
    float duration = 5.0f;        
    bool playOnAwake = true;

    
    float startLifetime = 3.0f;
    float startSpeed = 3.0f;
    glm::vec4 startColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 endColor{ 1.0f, 1.0f, 1.0f, 0.0f };
    float startSize = 0.2f;
    float endSize = 0.0f;
    std::string materialPath;

    
    glm::vec3 gravity{ 0.0f, -2.0f, 0.0f };

    
    EmissionShape shape = Cone;
    float coneAngle = 25.0f;      
    float sphereRadius = 1.0f;

    
    std::vector<Particle> particles;
    float emissionAccumulator = 0.0f;
    float systemTime = 0.0f;
    bool playing = false;
    bool paused = false;

    ParticleSystemComponent();
    ParticleSystemComponent(ParticleSystemComponent* other);

    
    void Play();
    void Stop();
    void Pause() { paused = true; }
    void Resume() { paused = false; }
    bool IsPlaying() const { return playing && !paused; }

    
    void Update(float deltaTime);
    
    void Emit(int count);

    void OnInspectorGUI() override;
    void Serialize(std::ostream& file) const override;
    void Deserialize(std::istream& file) override;

private:
    void SpawnOne(Particle& p);
    glm::vec3 RandomEmissionDir();
};
